import bun from "bun";
import { Output, default_allocator, strings, webcore, http, jsc, String, sys, JSError, FormData, Environment, ptr } from "bun";
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const JSPromise = jsc.JSPromise;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;
const ArrayBuffer = jsc.ArrayBuffer;
const DOMFormData = jsc.DOMFormData;
const URLSearchParams = jsc.URLSearchParams;
import * as StoreMod from "./blob/Store.zig";
import * as read_file_mod from "./blob/read_file.zig";
import * as write_file_mod from "./blob/write_file.zig";
import * as copy_file_mod from "./blob/copy_file.zig";
import Archive from "../api/Archive.zig";

const BlobStore = StoreMod.Store;
const S3 = bun.S3;
const StringJoiner = bun.StringJoiner;
const invalid_fd = bun.invalid_fd;
const io = bun.io;
const libuv = bun.windows.libuv;
const MimeType = http.MimeType;
const PathOrBlob = jsc.Node.PathOrBlob;
const Request = jsc.WebCore.Request;
const Response = jsc.WebCore.Response;
const WriteFilePromise = write_file_mod.WriteFilePromise;
const WriteFileWaitFromLockedValueTask = write_file_mod.WriteFileWaitFromLockedValueTask;
const debug = Output.scoped(.Blob, .visible);
const write_permissions = 0o664;

type WriteFilePromisePtr = *WriteFilePromise;
type OpaquePtr = *!;

struct ReadBytesAdapter<Handler> {
    static fn run(c: *Handler!, r: read_file_mod.ReadFileResultType) {
        const res = switch (r) {
            .result b => BlobReadBytesResult{ ok: b.buf },
            .err e => BlobReadBytesResult{ err: e },
        };
        Handler.onReadBytes(c, res);
    }
}

struct S3ReadBytesTask<Handler> {
    ctx: *Handler!,
    blob: Blob,
    poll: bun.Async.KeepAlive = .{},
    vm: *jsc.VirtualMachine!,

    static fn create(ctx: *Handler!, blob: Blob, vm: *jsc.VirtualMachine!): *@This() {
        return bun.new(@This(), .{ .ctx = ctx, .blob = blob, .vm = vm });
    }

    static fn done(t: *@This(), r: BlobReadBytesResult) {
        t.poll.unref(t.vm);
        t.blob.deinit();
        const c = t.ctx;
        bun.destroy(t);
        Handler.onReadBytes(c, r);
    }

    static fn cb(result: bun.S3.S3DownloadResult, opaque_self: *!): void!bun.JSTerminated {
        const t: *@This() = unsafe { @ptrCast(@alignCast(opaque_self)) };
        switch (result) {
            .success response => t.done(BlobReadBytesResult{ ok: response.body.list.items }),
            .not_found e => t.done(BlobReadBytesResult{ err: .{
                .code = bun.String.cloneUTF8(e.code),
                .message = bun.String.cloneUTF8(e.message),
                .path = bun.String.cloneUTF8(t.blob.store.?.getPath() ?? ""),
                .syscall = bun.String.static("fetch"),
            } }),
            .failure e => t.done(BlobReadBytesResult{ err: .{
                .code = bun.String.cloneUTF8(e.code),
                .message = bun.String.cloneUTF8(e.message),
                .path = bun.String.cloneUTF8(t.blob.store.?.getPath() ?? ""),
                .syscall = bun.String.static("fetch"),
            } }),
        }
    }
}

struct DeinitGuard<T> {
    ptr: *T!;
    ~init() { unsafe { _.ptr.deinit(); } }
}

struct ErrDeinitGuard<T> {
    ptr: (*T!)?;
    ~init() { if (p := _.ptr) unsafe { p.deinit(); } }
    fn disarm(*_) { _.ptr = null; }
}

struct DerefGuard<T> {
    ptr: *T!;
    ~init() { unsafe { _.ptr.deref(); } }
}

struct OptDerefGuard<T> {
    ptr: (*T!)?;
    ~init() { if (p := _.ptr) unsafe { p.deref(); } }
    fn disarm(*_) { _.ptr = null; }
}

struct DetachGuard<T> {
    ptr: *T!;
    ~init() { unsafe { _.ptr.detach(); } }
}

struct CondDetachGuard<T> {
    ptr: (*T!)?;
    ~init() { if (p := _.ptr) unsafe { p.detach(); } }
}

struct DeinitUnprotectGuard<T> {
    ptr: *T!;
    ~init() { unsafe { _.ptr.deinitAndUnprotect(); } }
}

struct CloseGuard<T> {
    ptr: (*T!)?;
    ~init() { if (p := _.ptr) unsafe { p.close(); } }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: ([]const T)?,
    ~init() { if (s := _.slice) _.alloc.free(s); }
}

struct OptSliceDeinitGuard<T> {
    ptr: *T!;
    ~init() { unsafe { if (_.ptr.* != null) _.ptr.*.?.deinit(); } }
}

struct FdTruncateCloseGuard {
    fd: bun.FD,
    written: *usize!,
    truncate: bool,
    needs_open: bool,
    ~init() {
        if (_.truncate) unsafe { * = _.fd.truncate(@intCast(_.written.*)); }
        if (_.needs_open) _.fd.close();
    }
}

struct S3ReadTaskGuard<T> {
    ptr: (*T!)?;
    ~init() {
        if (t := _.ptr) unsafe {
            t.poll.unref(t.vm);
            t.blob.deinit();
            bun.destroy(t);
        }
    }
    fn disarm(*_) { _.ptr = null; }
}

struct PathOrFdResetGuard {
    ptr: *jsc.Node.PathOrFileDescriptor!,
    ~init() {
        unsafe {
            if (_.ptr.path != .@"string") {
                _.ptr.deinit();
                _.ptr.* = .{ .path = .{ .string = bun.PathString.empty } };
            }
        }
    }
}

struct PathOrBlobGuard {
    ptr: *PathOrBlob!,
    ~init() { unsafe { if (_.ptr.* == .path) _.ptr.path.deinit(); } }
}

struct ThresholdDeinitGuard<T> {
    ptr: *T!,
    end_index: *usize!,
    ~init() { unsafe { if (_.end_index.* >= 1024) _.ptr.deinit(); } }
}

struct _S3UploadZeroWrapper {
    promise: jsc.JSPromise.Strong,
    store: *BlobStore,
    global: *JSGlobalObject,

    pub const new = bun.TrivialNew(_S3UploadZeroWrapper);

    static fn resolve(result: S3.S3UploadResult, opaque_ctx: *!): bun.JSTerminated!void {
        const w: *_S3UploadZeroWrapper = unsafe { @ptrCast(@alignCast(opaque_ctx)) };
        const _guard_w = DeinitGuard<_S3UploadZeroWrapper>{ ptr: w };
        switch (result) {
            .success => w.promise.resolve(w.global, .jsNumber(0))?,
            .failure e => {
                w.promise.reject(w.global, e.toJSWithAsyncStack(w.global, w.store.getPath(), w.promise.get()))?;
            },
        }
    }

    fn deinit(*_) {
        _.promise.deinit();
        _.store.deref();
        bun.destroy(_);
    }
}

struct _S3UploadLenWrapper {
    store: *BlobStore,
    promise: jsc.JSPromise.Strong,
    global: *JSGlobalObject,

    pub const new = bun.TrivialNew(_S3UploadLenWrapper);

    static fn resolve(result: S3.S3UploadResult, opaque_ctx: *!): bun.JSTerminated!void {
        const w: *_S3UploadLenWrapper = unsafe { @ptrCast(@alignCast(opaque_ctx)) };
        const _guard_w = DeinitGuard<_S3UploadLenWrapper>{ ptr: w };
        switch (result) {
            .success => w.promise.resolve(w.global, .jsNumber(w.store.data.bytes.len))?,
            .failure e => {
                w.promise.reject(w.global, e.toJSWithAsyncStack(w.global, w.store.getPath(), w.promise.get()))?;
            },
        }
    }

    fn deinit(*_) {
        _.promise.deinit();
        _.store.deref();
        bun.destroy(_);
    }
}

pub enum Retry {
    @"continue",
    fail,
    no,
}

pub struct WriteFileOptions {
    mkdirp_if_not_exists: bool? = null,
    extra_options: JSValue? = null,
    mode: bun.Mode? = null,
}

pub struct FileStreamWrapper {
    promise: jsc.JSPromise.Strong,
    readable_stream_ref: jsc.WebCore.ReadableStream.Strong,
    sink: *jsc.WebCore.FileSink,

    pub const new = bun.TrivialNew(@This());

    fn deinit(*_) {
        _.promise.deinit();
        _.readable_stream_ref.deinit();
        _.sink.deref();
        bun.destroy(_);
    }
}

struct FormDataContext {
    allocator: std.mem.Allocator,
    joiner: StringJoiner,
    boundary: []const u8,
    failed: bool = false,
    globalThis: *JSGlobalObject,

    fn onEntry(*_, name: ZigString, entry: jsc.DOMFormData.FormDataEntry) {
        if (_.failed) return;
        const globalThis = _.globalThis;
        const allocator = _.allocator;
        let joiner = &_.joiner;
        const boundary = _.boundary;

        joiner.pushStatic("--");
        joiner.pushStatic(boundary);
        joiner.pushStatic("\r\n");

        joiner.pushStatic("Content-Disposition: form-data; name=\"");
        const name_slice = name.toSlice(allocator);
        joiner.push(name_slice.slice(), name_slice.allocator.get());

        switch (entry) {
            .string => |value| {
                joiner.pushStatic("\"\r\n\r\n");
                const value_slice = value.toSlice(allocator);
                joiner.push(value_slice.slice(), value_slice.allocator.get());
            },
            .file => |value| {
                joiner.pushStatic("\"; filename=\"");
                const filename_slice = value.filename.toSlice(allocator);
                joiner.push(filename_slice.slice(), filename_slice.allocator.get());
                joiner.pushStatic("\"\r\n");

                let blob = value.blob;
                const content_type = if (blob.content_type.len > 0) blob.content_type else "application/octet-stream";
                joiner.pushStatic("Content-Type: ");
                joiner.pushStatic(content_type);
                joiner.pushStatic("\r\n\r\n");

                if (store := blob.store) {
                    if (blob.size == Blob.max_size) {
                        blob.resolveSize();
                    }
                    switch (store.data) {
                        .s3 => |_| {},
                        .file => |file| {
                            const res = jsc.Node.fs.NodeFS.readFile(
                                globalThis.bunVM().nodeFS(),
                                .{
                                    .encoding = .buffer,
                                    .path = file.pathlike,
                                    .offset = blob.offset,
                                    .max_size = blob.size,
                                },
                                .sync,
                            );

                            switch (res) {
                                .err => |e| {
                                    _.failed = true;
                                    const js_err = e.toJS(globalThis) catch return;
                                    const thrown_err = globalThis.throwValue(js_err);
                                    * = thrown_err;
                                },
                                .result => |result| {
                                    joiner.push(result.slice(), result.buffer.allocator);
                                },
                            }
                        },
                        .bytes => |_| {
                            joiner.pushStatic(blob.sharedView());
                        },
                    }
                }
            },
        }

        joiner.pushStatic("\r\n");
    }
}

struct URLSearchParamsConverter {
    allocator: std.mem.Allocator,
    buf: []u8 = "",
    globalThis: *JSGlobalObject,

    fn convert(*_, str: ZigString) {
        _.buf = bun.handleOom(str.toOwnedSlice(_.allocator));
    }
}

pub fn onFileStreamResolveRequestStream(globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! JSError {
    let args = callframe.arguments_old(2);
    let wrapper = unsafe { args.ptr[args.len - 1] }.asPromisePtr(FileStreamWrapper);
    const _guard_wrapper = DeinitGuard<FileStreamWrapper>{ ptr: wrapper };
    let strong = wrapper.readable_stream_ref;
    const _guard_strong = DeinitGuard<jsc.WebCore.ReadableStream.Strong>{ ptr: &strong };
    wrapper.readable_stream_ref = .{};
    if (stream := strong.get(globalThis)) {
        stream.done(globalThis);
    }
    wrapper.promise.resolve(globalThis, JSValue.jsNumber(0))?;
    return JSValue.js_undefined;
}

pub fn onFileStreamRejectRequestStream(globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! JSError {
    const args = callframe.arguments_old(2);
    let wrapper = unsafe { args.ptr[args.len - 1] }.asPromisePtr(FileStreamWrapper);
    const _guard_wrapper = DeinitGuard<FileStreamWrapper>{ ptr: wrapper };
    const err_val = unsafe { args.ptr[0] };

    let strong = wrapper.readable_stream_ref;
    const _guard_strong = DeinitGuard<jsc.WebCore.ReadableStream.Strong>{ ptr: &strong };
    wrapper.readable_stream_ref = .{};

    wrapper.promise.reject(globalThis, err_val)?;

    if (stream := strong.get(globalThis)) {
        stream.cancel(globalThis);
    }
    return JSValue.js_undefined;
}

comptime {
    const jsonResolveRequestStream = jsc.toJSHostFn(onFileStreamResolveRequestStream);
    @export(&jsonResolveRequestStream, .{ .name = "Bun__FileStreamWrapper__onResolveRequestStream" });
    const jsonRejectRequestStream = jsc.toJSHostFn(onFileStreamRejectRequestStream);
    @export(&jsonRejectRequestStream, .{ .name = "Bun__FileStreamWrapper__onRejectRequestStream" });
}

pub noinline fn mkdirIfNotExists(ctx: auto, e: bun.sys.Error, path_string: [:0]const u8, err_path: []const u8): Retry {
    if (e.getErrno() == .NOENT && ctx.mkdirp_if_not_exists) {
        if (dirname := std.fs.path.dirname(path_string)) {
            let node_fs: jsc.Node.fs.NodeFS = .{};
            switch (node_fs.mkdirRecursive(.{
                .path = .{ .string = bun.PathString.init(dirname) },
                .recursive = true,
                .always_return_none = true,
            })) {
                .result => {
                    ctx.mkdirp_if_not_exists = false;
                    return .@"continue";
                },
                .err e2 => {
                    if (comptime @hasField(@TypeOf(ctx.*), "errno")) {
                        ctx.errno = bun.errnoToZigErr(e2.errno);
                    }
                    ctx.system_error = e.withPath(err_path).toSystemError();
                    if (comptime @hasField(@TypeOf(ctx.*), "opened_fd")) {
                        ctx.opened_fd = invalid_fd;
                    }
                    return .fail;
                },
            }
        }
    }
    return .no;
}

fn writeFileWithEmptySourceToDestination(ctx: *JSGlobalObject, destination_blob: *Blob, options: WriteFileOptions): JSValue ! JSError {
    const destination_store = destination_blob.store.?;
    const _guard_dest_blob = DetachGuard<Blob>{ ptr: destination_blob };

    switch (destination_store.data) {
        .file file => {
            const node_fs = ctx.bunVM().nodeFS();
            let result = node_fs.truncate(.{
                .path = file.pathlike,
                .len = 0,
                .flags = bun.O.CREAT,
            }, .sync);

            if (result == .err) {
                let cur_errno = result.err.getErrno();
                let was_eperm = false;
                if (cur_errno == .PERM) {
                    was_eperm = true;
                    result.err.errno = @intCast(@intFromEnum(bun.sys.E.NOENT));
                    cur_errno = .NOENT;
                }
                if (cur_errno == .NOENT && options.mkdirp_if_not_exists != false) {
                    let dirpath_opt: ([]const u8)? = null;
                    switch (file.pathlike) {
                        .path p => {
                            dirpath_opt = std.fs.path.dirname(p.slice());
                        },
                        .fd => {
                            if (was_eperm) result.err.errno = @intCast(@intFromEnum(bun.sys.E.PERM));
                        },
                    }
                    if (dirpath := dirpath_opt) {
                        const mkdir_result = node_fs.mkdirRecursive(.{
                            .path = .{ .string = bun.PathString.init(dirpath) },
                            .recursive = true,
                            .always_return_none = true,
                        });
                        if (mkdir_result == .err) {
                            result.err = mkdir_result.err;
                        } else {
                            let buf: bun.PathBuffer = undefined;
                            const mode: bun.Mode = options.mode ?? jsc.Node.fs.default_permission;
                            while (true) {
                                const open_res = bun.sys.open(file.pathlike.path.sliceZ(&buf), bun.O.CREAT | bun.O.TRUNC, mode);
                                switch (open_res) {
                                    .err e => {
                                        if (e.getErrno() == .INTR) continue;
                                        result.err = open_res.err;
                                        break;
                                    },
                                    .result fd => {
                                        fd.close();
                                        return JSPromise.resolvedPromiseValue(ctx, .jsNumber(0));
                                    },
                                }
                            }
                        }
                    }
                }

                result.err = result.err.withPathLike(file.pathlike);
                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, result.toJS(ctx)?);
            }
        },
        .s3 => |*s3| {
            let aws_options = s3.getCredentialsWithOptions(options.extra_options, ctx) catch |e| {
                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ctx.takeException(e));
            };
            const _guard_aws = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &aws_options };

            const Wrapper = _S3UploadZeroWrapper;

            const promise = jsc.JSPromise.Strong.init(ctx);
            const promise_value = promise.value();
            let proxy_url: ([]const u8)? = null;
            if (p := ctx.bunVM().transpiler.env.getHttpProxy(true, null, null)) {
                proxy_url = p.href;
            }
            destination_store.ref();
            const wrapper = Wrapper.new(.{
                .promise = promise,
                .store = destination_store,
                .global = ctx,
            });
            let _guard_wrapper = ErrDeinitGuard<Wrapper>{ ptr: wrapper };
            S3.upload(
                &aws_options.credentials,
                s3.path(),
                "",
                destination_blob.contentTypeOrMimeType(),
                aws_options.content_disposition,
                aws_options.content_encoding,
                aws_options.acl,
                proxy_url,
                aws_options.storage_class,
                aws_options.request_payer,
                Wrapper.resolve,
                wrapper,
            )?;
            _guard_wrapper.disarm();
            return promise_value;
        },
        .bytes => {},
    }

    return JSPromise.resolvedPromiseValue(ctx, JSValue.jsNumber(0));
}

pub fn writeFileWithSourceDestination(ctx: *JSGlobalObject, source_blob: *Blob, destination_blob: *Blob, options: WriteFileOptions): JSValue ! JSError {
    const destination_store = destination_blob.store ?? Output.panic("Destination blob is detached", .{});
    const destination_type = std.meta.activeTag(destination_store.data);

    if (Environment.isDebug) {
        bun.assertf(destination_type != .bytes, "Cannot write to a Blob backed by a Buffer or TypedArray. This is a bug in the caller. Please report it to the Bun team.", .{});
    }

    const source_store_opt = source_blob.store;
    if (source_store_opt == null) {
        return writeFileWithEmptySourceToDestination(ctx, destination_blob, options);
    }
    const source_store = source_store_opt.?;
    const source_type = std.meta.activeTag(source_store.data);

    if (destination_type == .file && source_type == .bytes) {
        let write_file_promise = bun.new(WriteFilePromise, .{
            .globalThis = ctx,
        });

        if (comptime Environment.isWindows) {
            let promise = JSPromise.create(ctx);
            const promise_value = promise.asValue(ctx);
            promise_value.ensureStillAlive();
            write_file_promise.promise.strong.set(ctx, promise_value);
            * = write_file_mod.WriteFileWindows.create(
                ctx.bunVM().eventLoop(),
                destination_blob.*,
                source_blob.*,
                WriteFilePromisePtr,
                write_file_promise,
                &WriteFilePromise.run,
                options.mkdirp_if_not_exists ?? true,
            ) catch |e| {
                if (e == error.JSTerminated) return error.JSTerminated;
            };
            return promise_value;
        }

        const file_copier = write_file_mod.WriteFile.create(
            destination_blob.*,
            source_blob.*,
            WriteFilePromisePtr,
            write_file_promise,
            WriteFilePromise.run,
            options.mkdirp_if_not_exists ?? true,
        ) catch unreachable;
        let task = write_file_mod.WriteFileTask.createOnJSThread(bun.default_allocator, ctx, file_copier);
        let promise = JSPromise.create(ctx);
        const promise_value = promise.asValue(ctx);
        write_file_promise.promise.strong.set(ctx, promise_value);
        promise_value.ensureStillAlive();
        task.schedule();
        return promise_value;
    } else if (destination_type == .file && source_type == .file) {
        if (comptime Environment.isWindows) {
            return copy_file_mod.CopyFileWindows.init(
                destination_store,
                source_store,
                ctx.bunVM().eventLoop(),
                options.mkdirp_if_not_exists ?? true,
                destination_blob.size,
                options.mode,
            );
        }
        let file_copier = copy_file_mod.CopyFile.create(
            bun.default_allocator,
            destination_store,
            source_store,
            destination_blob.offset,
            destination_blob.size,
            ctx,
            options.mkdirp_if_not_exists ?? true,
            options.mode,
        );
        file_copier.schedule();
        return file_copier.promise.value();
    } else if (destination_type == .file && source_type == .s3) {
        const s3 = &source_store.data.s3;
        const stream_val = jsc.WebCore.ReadableStream.fromBlobCopyRef(
            ctx,
            source_blob,
            @truncate(s3.options.partSize),
        )?;
        if (stream := jsc.WebCore.ReadableStream.fromJS(stream_val, ctx)?) {
            return destination_blob.pipeReadableStreamToBlob(ctx, stream, options.extra_options);
        } else {
            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ctx.createErrorInstance("Failed to stream bytes from s3 bucket", .{}));
        }
    } else if (destination_type == .bytes && source_type == .bytes) {
        const cloned = Blob.new(source_blob.dupe());
        return JSPromise.resolvedPromiseValue(ctx, cloned.toJS(ctx));
    } else if (destination_type == .bytes && (source_type == .file || source_type == .s3)) {
        const blob_value = source_blob.getSliceFrom(ctx, 0, 0, "", false);
        return JSPromise.resolvedPromiseValue(ctx, blob_value);
    } else if (destination_type == .s3) {
        const s3 = &destination_store.data.s3;
        let aws_options = s3.getCredentialsWithOptions(options.extra_options, ctx) catch |e| {
            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ctx.takeException(e));
        };
        const _guard_aws = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &aws_options };
        let proxy_url: ([]const u8)? = null;
        if (p := ctx.bunVM().transpiler.env.getHttpProxy(true, null, null)) {
            proxy_url = p.href;
        }
        switch (source_store.data) {
            .bytes bytes => {
                if (bytes.len > S3.MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE) {
                    const stream_val = jsc.WebCore.ReadableStream.fromBlobCopyRef(
                        ctx,
                        source_blob,
                        @truncate(s3.options.partSize),
                    )?;
                    if (stream := jsc.WebCore.ReadableStream.fromJS(stream_val, ctx)?) {
                        return S3.uploadStream(
                            (if (options.extra_options != null) aws_options.credentials.dupe() else s3.getCredentials()),
                            s3.path(),
                            stream,
                            ctx,
                            aws_options.options,
                            aws_options.acl,
                            aws_options.storage_class,
                            destination_blob.contentTypeOrMimeType(),
                            aws_options.content_disposition,
                            aws_options.content_encoding,
                            proxy_url,
                            aws_options.request_payer,
                            null,
                            undefined,
                        );
                    } else {
                        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ctx.createErrorInstance("Failed to stream bytes to s3 bucket", .{}));
                    }
                } else {
                    const Wrapper = _S3UploadLenWrapper;
                    source_store.ref();
                    const promise = jsc.JSPromise.Strong.init(ctx);
                    const promise_value = promise.value();
                    const wrapper = Wrapper.new(.{
                        .store = source_store,
                        .promise = promise,
                        .global = ctx,
                    });
                    let _guard_wrapper = ErrDeinitGuard<Wrapper>{ ptr: wrapper };

                    S3.upload(
                        &aws_options.credentials,
                        s3.path(),
                        bytes.slice(),
                        destination_blob.contentTypeOrMimeType(),
                        aws_options.content_disposition,
                        aws_options.content_encoding,
                        aws_options.acl,
                        proxy_url,
                        aws_options.storage_class,
                        aws_options.request_payer,
                        Wrapper.resolve,
                        wrapper,
                    )?;
                    _guard_wrapper.disarm();
                    return promise_value;
                }
            },
            .file, .s3 => {
                const stream_val = jsc.WebCore.ReadableStream.fromBlobCopyRef(
                    ctx,
                    source_blob,
                    @truncate(s3.options.partSize),
                )?;
                if (stream := jsc.WebCore.ReadableStream.fromJS(stream_val, ctx)?) {
                    return S3.uploadStream(
                        (if (options.extra_options != null) aws_options.credentials.dupe() else s3.getCredentials()),
                        s3.path(),
                        stream,
                        ctx,
                        s3.options,
                        aws_options.acl,
                        aws_options.storage_class,
                        destination_blob.contentTypeOrMimeType(),
                        aws_options.content_disposition,
                        aws_options.content_encoding,
                        proxy_url,
                        aws_options.request_payer,
                        null,
                        undefined,
                    );
                } else {
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ctx.createErrorInstance("Failed to stream bytes to s3 bucket", .{}));
                }
            },
        }
    }

    unreachable;
}

fn writeStringToFileFast(
    globalThis: *JSGlobalObject,
    pathlike: jsc.Node.PathOrFileDescriptor,
    str: bun.String,
    needs_async: *bool,
    comptime needs_open: bool,
): JSValue {
    let fd: bun.FD = if (comptime !needs_open) pathlike.fd else brk: {
        let file_path: bun.PathBuffer = undefined;
        switch (bun.sys.open(
            pathlike.path.sliceZ(&file_path),
            bun.O.WRONLY | bun.O.CREAT | bun.O.NONBLOCK,
            write_permissions,
        )) {
            .result res => {
                break :brk res;
            },
            .err e => {
                if (e.getErrno() == .NOENT) {
                    needs_async.* = true;
                    return .zero;
                }

                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(
                    globalThis,
                    e.withPath(pathlike.path.slice()).toJS(globalThis) catch return .zero,
                );
            },
        }
    };

    let truncate = needs_open || str.isEmpty();
    const jsc_vm = globalThis.bunVM();
    let written: usize = 0;

    const _guard_fd = FdTruncateCloseGuard{ fd: fd, written: &written, truncate: truncate, needs_open: needs_open };
    if (!str.isEmpty()) {
        let decoded = str.toUTF8(jsc_vm.allocator);
        const _guard_decoded = DeinitGuard<ZigString.Slice>{ ptr: &decoded };

        let remain = decoded.slice();
        while (remain.len > 0) {
            const result = bun.sys.write(fd, remain);
            switch (result) {
                .result res => {
                    written += res;
                    remain = remain[res..];
                    if (res == 0) break;
                },
                .err e => {
                    truncate = false;
                    if (e.getErrno() == .AGAIN) {
                        needs_async.* = true;
                        return .zero;
                    }
                    if (comptime !needs_open) {
                        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, e.toJS(globalThis) catch return .zero);
                    }
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(
                        globalThis,
                        e.withPath(pathlike.path.slice()).toJS(globalThis) catch return .zero,
                    );
                },
            }
        }
    }

    return JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(written));
}

fn writeBytesToFileFast(
    globalThis: *JSGlobalObject,
    pathlike: jsc.Node.PathOrFileDescriptor,
    bytes: []const u8,
    needs_async: *bool,
    comptime needs_open: bool,
): JSValue {
    let fd: bun.FD = if (comptime !needs_open) pathlike.fd else brk: {
        let file_path: bun.PathBuffer = undefined;
        switch (bun.sys.open(
            pathlike.path.sliceZ(&file_path),
            if (!Environment.isWindows)
                bun.O.WRONLY | bun.O.CREAT | bun.O.NONBLOCK
            else
                bun.O.WRONLY | bun.O.CREAT,
            write_permissions,
        )) {
            .result res => {
                break :brk res;
            },
            .err e => {
                if (!Environment.isWindows) {
                    if (e.getErrno() == .NOENT) {
                        needs_async.* = true;
                        return .zero;
                    }
                }

                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(
                    globalThis,
                    e.withPath(pathlike.path.slice()).toJS(globalThis) catch return .zero,
                );
            },
        }
    };

    const truncate = needs_open || bytes.len == 0;
    let written: usize = 0;
    const _guard_fd = CloseGuard<bun.FD>{ ptr: if (needs_open) &fd else null };

    let remain = bytes;
    const end = remain.ptr + remain.len;

    while (remain.ptr != end) {
        const result = bun.sys.write(fd, remain);
        switch (result) {
            .result res => {
                written += res;
                remain = remain[res..];
                if (res == 0) break;
            },
            .err e => {
                if (!Environment.isWindows) {
                    if (e.getErrno() == .AGAIN) {
                        needs_async.* = true;
                        return .zero;
                    }
                }
                if (comptime !needs_open) {
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(
                        globalThis,
                        e.toJS(globalThis) catch return .zero,
                    );
                }
                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(
                    globalThis,
                    e.withPath(pathlike.path.slice()).toJS(globalThis) catch return .zero,
                );
            },
        }
    }

    if (truncate) {
        if (Environment.isWindows) {
            * = std.os.windows.kernel32.SetEndOfFile(fd.cast());
        } else {
            * = bun.sys.ftruncate(fd, @as(i64, @intCast(written)));
        }
    }

    return JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(written));
}

pub fn writeFileInternal(globalThis: *JSGlobalObject, path_or_blob_: *PathOrBlob, data: JSValue, options: WriteFileOptions): JSValue ! JSError {
    if (data.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Bun.write(pathOrFdOrBlob, blob) expects a Blob-y thing to write", .{});
    }
    let path_or_blob = path_or_blob_.*;
    if (path_or_blob == .blob) {
        const blob_store_opt = path_or_blob.blob.store;
        if (blob_store_opt == null) {
            return globalThis.throwInvalidArguments("Blob is detached", .{});
        }
        const blob_store = blob_store_opt.?;
        bun.assertWithLocation(blob_store.data != .bytes, @src());
        if (blob_store.data == .file) {
            blob_store.data.file.last_modified = jsc.init_timestamp;
        }
    }

    const input_store: (*BlobStore)? = if (path_or_blob == .blob) path_or_blob.blob.store else null;
    if (st := input_store) st.ref();
    const _guard_input_store = OptDerefGuard<BlobStore>{ ptr: input_store };

    let needs_async = false;

    if (mkdir := options.mkdirp_if_not_exists) {
        if (mkdir &&
            path_or_blob == .blob &&
            path_or_blob.blob.store != null &&
            path_or_blob.blob.store.?.data == .file &&
            path_or_blob.blob.store.?.data.file.pathlike == .fd)
        {
            return globalThis.throwInvalidArguments("Cannot create a directory for a file descriptor", .{});
        }
    }

    if (comptime !Environment.isWindows) {
        if (path_or_blob == .path ||
            (path_or_blob.blob.offset == 0 && !path_or_blob.blob.isS3() &&
                !(path_or_blob.blob.store != null &&
                    path_or_blob.blob.store.?.data == .file &&
                    path_or_blob.blob.store.?.data.file.mode != 0 &&
                    bun.isRegularFile(path_or_blob.blob.store.?.data.file.mode))))
        {
            if (data.isString()) {
                const len = data.getLength(globalThis)?;

                if (len < 256 * 1024) {
                    let str = data.toBunString(globalThis)?;
                    const _guard_str = DerefGuard<String>{ ptr: &str };

                    const pathlike: jsc.Node.PathOrFileDescriptor = if (path_or_blob == .path)
                        path_or_blob.path
                    else
                        path_or_blob.blob.store.?.data.file.pathlike;

                    if (pathlike == .path) {
                        const result = writeStringToFileFast(
                            globalThis,
                            pathlike,
                            str,
                            &needs_async,
                            true,
                        );
                        if (!needs_async) {
                            return result;
                        }
                    } else {
                        const result = writeStringToFileFast(
                            globalThis,
                            pathlike,
                            str,
                            &needs_async,
                            false,
                        );
                        if (!needs_async) {
                            return result;
                        }
                    }
                }
            } else if (buffer_view := data.asArrayBuffer(globalThis)) {
                if (buffer_view.byte_len < 256 * 1024) {
                    const pathlike: jsc.Node.PathOrFileDescriptor = if (path_or_blob == .path)
                        path_or_blob.path
                    else
                        path_or_blob.blob.store.?.data.file.pathlike;

                    if (pathlike == .path) {
                        const result = writeBytesToFileFast(
                            globalThis,
                            pathlike,
                            buffer_view.byteSlice(),
                            &needs_async,
                            true,
                        );

                        if (!needs_async) {
                            return result;
                        }
                    } else {
                        const result = writeBytesToFileFast(
                            globalThis,
                            pathlike,
                            buffer_view.byteSlice(),
                            &needs_async,
                            false,
                        );

                        if (!needs_async) {
                            return result;
                        }
                    }
                }
            }
        }
    }

    let destination_blob: Blob = if (path_or_blob == .path) brk: {
        const new_blob = Blob.findOrCreateFileFromPath(&path_or_blob_.path, globalThis, true);
        if (new_blob.store == null) {
            return globalThis.throwInvalidArguments("Writing to an empty blob is not implemented yet", .{});
        }
        break :brk new_blob;
    } else path_or_blob.blob.dupe();

    if (bun.Environment.allow_assert && path_or_blob == .blob) {
        bun.unsafeAssert(path_or_blob.blob.store != null);
    }

    let source_blob: Blob = brk: {
        if (response := data.as(Response)) {
            const bodyValue = response.getBodyValue();
            switch (bodyValue.*) {
                .WTFStringImpl,
                .InternalBlob,
                .Used,
                .Empty,
                .Blob,
                .Null => {
                    break :brk bodyValue.use();
                },
                .Error => |*err_ref| {
                    destination_blob.detach();
                    * = bodyValue.use();
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err_ref.toJS(globalThis));
                },
                .Locked => {
                    if (destination_blob.isS3()) {
                        const s3 = &destination_blob.store.?.data.s3;
                        let aws_options = s3.getCredentialsWithOptions(options.extra_options, globalThis)?;
                        const _guard_aws = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &aws_options };
                        * = bodyValue.toReadableStream(globalThis)?;

                        if (readable := response.getBodyReadableStream(globalThis) ?? bodyValue.Locked.readable.get(globalThis)) {
                            if (readable.isDisturbed(globalThis)) {
                                destination_blob.detach();
                                return globalThis.throwInvalidArguments("ReadableStream has already been used", .{});
                            }
                            let proxy_url: ([]const u8)? = null;
                            if (p := globalThis.bunVM().transpiler.env.getHttpProxy(true, null, null)) {
                                proxy_url = p.href;
                            }

                            return S3.uploadStream(
                                (if (options.extra_options != null) aws_options.credentials.dupe() else s3.getCredentials()),
                                s3.path(),
                                readable,
                                globalThis,
                                aws_options.options,
                                aws_options.acl,
                                aws_options.storage_class,
                                destination_blob.contentTypeOrMimeType(),
                                aws_options.content_disposition,
                                aws_options.content_encoding,
                                proxy_url,
                                aws_options.request_payer,
                                null,
                                undefined,
                            );
                        }
                        destination_blob.detach();
                        return globalThis.throwInvalidArguments("ReadableStream has already been used", .{});
                    }
                    let task = bun.new(WriteFileWaitFromLockedValueTask, .{
                        .globalThis = globalThis,
                        .file_blob = destination_blob,
                        .promise = jsc.JSPromise.Strong.init(globalThis),
                        .mkdirp_if_not_exists = options.mkdirp_if_not_exists ?? true,
                    });
                    bodyValue.Locked.task = task;
                    bodyValue.Locked.onReceiveValue = WriteFileWaitFromLockedValueTask.thenWrap;
                    return task.promise.value();
                },
            }
        }

        if (request := data.as(Request)) {
            const bodyValue = request.getBodyValue();
            switch (bodyValue.*) {
                .WTFStringImpl,
                .InternalBlob,
                .Used,
                .Empty,
                .Blob,
                .Null => {
                    break :brk bodyValue.use();
                },
                .Error => |*err_ref| {
                    destination_blob.detach();
                    * = bodyValue.use();
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err_ref.toJS(globalThis));
                },
                .Locked locked => {
                    if (destination_blob.isS3()) {
                        const s3 = &destination_blob.store.?.data.s3;
                        let aws_options = s3.getCredentialsWithOptions(options.extra_options, globalThis)?;
                        const _guard_aws = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &aws_options };
                        * = bodyValue.toReadableStream(globalThis)?;
                        if (readable := request.getBodyReadableStream(globalThis) ?? locked.readable.get(globalThis)) {
                            if (readable.isDisturbed(globalThis)) {
                                destination_blob.detach();
                                return globalThis.throwInvalidArguments("ReadableStream has already been used", .{});
                            }
                            let proxy_url: ([]const u8)? = null;
                            if (p := globalThis.bunVM().transpiler.env.getHttpProxy(true, null, null)) {
                                proxy_url = p.href;
                            }
                            return S3.uploadStream(
                                (if (options.extra_options != null) aws_options.credentials.dupe() else s3.getCredentials()),
                                s3.path(),
                                readable,
                                globalThis,
                                aws_options.options,
                                aws_options.acl,
                                aws_options.storage_class,
                                destination_blob.contentTypeOrMimeType(),
                                aws_options.content_disposition,
                                aws_options.content_encoding,
                                proxy_url,
                                aws_options.request_payer,
                                null,
                                undefined,
                            );
                        }
                        destination_blob.detach();
                        return globalThis.throwInvalidArguments("ReadableStream has already been used", .{});
                    }
                    let task = bun.new(WriteFileWaitFromLockedValueTask, .{
                        .globalThis = globalThis,
                        .file_blob = destination_blob,
                        .promise = jsc.JSPromise.Strong.init(globalThis),
                        .mkdirp_if_not_exists = options.mkdirp_if_not_exists ?? true,
                    });

                    bodyValue.Locked.task = task;
                    bodyValue.Locked.onReceiveValue = WriteFileWaitFromLockedValueTask.thenWrap;

                    return task.promise.value();
                },
            }
        }

        if (archive := data.as(Archive)) {
            archive.store.ref();
            break :brk Blob.initWithStore(archive.store, globalThis);
        }

        break :brk Blob.get(
            globalThis,
            data,
            false,
            false,
        )?;
    };
    const _guard_src_blob = DetachGuard<Blob>{ ptr: &source_blob };

    const destination_store = destination_blob.store;
    if (store := destination_store) {
        store.ref();
    }

    const _guard_dest_store = OptDerefGuard<BlobStore>{ ptr: destination_store };

    return writeFileWithSourceDestination(globalThis, &source_blob, &destination_blob, options);
}

fn validateWritableBlob(globalThis: *JSGlobalObject, blob: *Blob): JSError!void {
    const store_opt = blob.store;
    if (store_opt == null) {
        return globalThis.throw("Cannot write to a detached Blob", .{});
    }
    const store = store_opt.?;
    if (store.data == .bytes) {
        return globalThis.throwInvalidArguments("Cannot write to a Blob backed by bytes, which are always read-only", .{});
    }
}

pub fn FileOpener(comptime This: type): type {
    return struct {
        context: *This,

        const State = @This();

        const __opener_flags = bun.O.NONBLOCK | bun.O.CLOEXEC;

        const open_flags_ = if (@hasDecl(This, "open_flags"))
            This.open_flags | __opener_flags
        else
            bun.O.RDONLY | __opener_flags;

        const OpenCallback = fn (*This, bun.FD) void;

        static fn getFdByOpening(ctx: *This, comptime Callback: OpenCallback) {
            let buf: bun.PathBuffer = undefined;
            let path_string = if (@hasField(This, "file_store"))
                ctx.file_store.pathlike.path
            else
                ctx.file_blob.store.?.data.file.pathlike.path;

            const path = path_string.sliceZ(&buf);

            if (Environment.isWindows) {
                const WrappedCallback = struct {
                    static fn callback(req: *libuv.fs_t) callconv(.c) {
                        let target: *This = unsafe { @ptrCast(@alignCast(req.data.?)) };
                        {
                            const _guard_req = DeinitGuard<libuv.fs_t>{ ptr: req };
                            if (errEnum := req.result.errEnum()) {
                                let path_string_2 = if (@hasField(This, "file_store"))
                                    target.file_store.pathlike.path
                                else
                                    target.file_blob.store.?.data.file.pathlike.path;
                                target.errno = bun.errnoToZigErr(errEnum);
                                target.system_error = bun.sys.Error.fromCode(errEnum, .open)
                                    .withPath(path_string_2.slice())
                                    .toSystemError();
                                target.opened_fd = invalid_fd;
                            } else {
                                target.opened_fd = req.result.toFD();
                            }
                        }
                        Callback(target, target.opened_fd);
                    }
                };

                const rc = libuv.uv_fs_open(
                    ctx.loop,
                    &ctx.req,
                    path,
                    open_flags_,
                    jsc.Node.fs.default_permission,
                    &WrappedCallback.callback,
                );
                if (errno := rc.errEnum()) {
                    ctx.errno = bun.errnoToZigErr(errno);
                    ctx.system_error = bun.sys.Error.fromCode(errno, .open).withPath(path_string.slice()).toSystemError();
                    ctx.opened_fd = invalid_fd;
                    Callback(ctx, invalid_fd);
                }
                ctx.req.data = unsafe { @ptrCast(ctx) };
                return;
            }

            while (true) {
                ctx.opened_fd = switch (bun.sys.open(path, open_flags_, jsc.Node.fs.default_permission)) {
                    .result fd => fd,
                    .err e => {
                        if (comptime @hasField(This, "mkdirp_if_not_exists")) {
                            if (e.errno == @intFromEnum(bun.sys.E.NOENT)) {
                                switch (mkdirIfNotExists(ctx, e, path, path_string.slice())) {
                                    .@"continue" => continue,
                                    .fail => {
                                        ctx.opened_fd = invalid_fd;
                                        break;
                                    },
                                    .no => {},
                                }
                            }
                        }

                        ctx.errno = bun.errnoToZigErr(e.errno);
                        ctx.system_error = e.withPath(path_string.slice()).toSystemError();
                        ctx.opened_fd = invalid_fd;
                        break;
                    },
                };
                break;
            }

            Callback(ctx, ctx.opened_fd);
        }

        static fn getFd(ctx: *This, comptime Callback: OpenCallback) {
            if (ctx.opened_fd != invalid_fd) {
                Callback(ctx, ctx.opened_fd);
                return;
            }

            if (@hasField(This, "file_store")) {
                const pathlike = ctx.file_store.pathlike;
                if (pathlike == .fd) {
                    ctx.opened_fd = pathlike.fd;
                    Callback(ctx, ctx.opened_fd);
                    return;
                }
            } else {
                const pathlike = ctx.file_blob.store.?.data.file.pathlike;
                if (pathlike == .fd) {
                    ctx.opened_fd = pathlike.fd;
                    Callback(ctx, ctx.opened_fd);
                    return;
                }
            }

            getFdByOpening(ctx, Callback);
        }
    };
}

pub fn FileCloser(comptime This: type): type {
    return struct {
        static fn scheduleClose(request: *io.Request): io.Action {
            let ctx: *This = unsafe { @alignCast(@fieldParentPtr("io_request", request)) };
            return io.Action{
                .close = .{
                    .ctx = ctx,
                    .fd = ctx.opened_fd,
                    .onDone = unsafe { @ptrCast(&onIORequestClosed) },
                    .poll = &ctx.io_poll,
                    .tag = This.io_tag,
                },
            };
        }

        static fn onIORequestClosed(ctx: *This) {
            ctx.io_poll.flags.remove(.was_ever_registered);
            ctx.task = .{ .callback = &onCloseIORequest };
            bun.jsc.WorkPool.schedule(&ctx.task);
        }

        static fn onCloseIORequest(task: *jsc.WorkPoolTask) {
            debug("onCloseIORequest()", .{});
            let ctx: *This = unsafe { @alignCast(@fieldParentPtr("task", task)) };
            ctx.close_after_io = false;
            ctx.update();
        }

        static fn doClose(ctx: *This, is_allowed_to_close_fd: bool): bool {
            if (@hasField(This, "io_request")) {
                if (ctx.close_after_io) {
                    ctx.state.store(BlobClosingState.closing, .seq_cst);

                    @atomicStore(@TypeOf(ctx.io_request.callback), &ctx.io_request.callback, &scheduleClose, .seq_cst);
                    if (!ctx.io_request.scheduled)
                        io.Loop.get().schedule(&ctx.io_request);
                    return true;
                }
            }

            if (is_allowed_to_close_fd &&
                ctx.opened_fd != invalid_fd &&
                ctx.opened_fd.stdioTag() == null)
            {
                if (comptime Environment.isWindows) {
                    bun.Async.Closer.close(ctx.opened_fd, ctx.loop);
                } else {
                    * = ctx.opened_fd.closeAllowingBadFileDescriptor(null);
                }
                ctx.opened_fd = invalid_fd;
            }

            return false;
        }
    };
}

pub type BlobSizeType = u52;

pub union(enum) BlobReadBytesResult {
    ok: []u8,
    err: jsc.SystemError,
}

pub fn NewInternalReadFileHandler(comptime Context: type, comptime Function: auto): type {
    return struct {
        pub static fn run(handler: *!, bytes: read_file_mod.ReadFileResultType) {
            Function(bun.cast(Context, handler), bytes);
        }
    };
}

pub enum BlobClosingState: u8 {
    running,
    closing,
}

pub struct BlobInternal {
    bytes: std.array_list.Managed(u8),
    was_string: bool = false,

    fn memoryCost(): usize {
        return _.bytes.capacity;
    }

    fn contentType(): []const u8 {
        if (_.was_string) {
            return MimeType.text.value;
        }
        return MimeType.other.value;
    }

    fn deinit(*_) {
        _.bytes.clearAndFree();
    }

    fn toStringOwned(*_, globalThis: *JSGlobalObject): JSValue ! JSError {
        const bytes_without_bom = strings.withoutUTF8BOM(_.bytes.items);
        if (out := (strings.toUTF16Alloc(bun.default_allocator, bytes_without_bom, false, false) catch null)) {
            const return_value = ZigString.toExternalU16(out.ptr, out.len, globalThis);
            return_value.ensureStillAlive();
            _.deinit();
            return return_value;
        } else if (bytes_without_bom.len != _.bytes.items.len) {
            const _guard_self = DeinitGuard<@This()>{ ptr: _ };
            let out = String.cloneLatin1(_.bytes.items[3..]);
            const _guard_out = DerefGuard<String>{ ptr: &out };
            return out.toJS(globalThis);
        } else {
            let str = ZigString.init(_.toOwnedSlice());
            str.markGlobal();
            return str.toExternalValue(globalThis);
        }
    }

    fn toJSON(*_, globalThis: *JSGlobalObject): JSValue {
        const str_bytes = ZigString.init(strings.withoutUTF8BOM(_.bytes.items)).withEncoding();
        const json = str_bytes.toJSONObject(globalThis);
        _.deinit();
        return json;
    }

    fn slice(): []u8 {
        return _.bytes.items;
    }

    fn sliceConst(): []const u8 {
        return _.bytes.items;
    }

    fn toOwnedSlice(*_): []u8 {
        return bun.handleOom(_.bytes.toOwnedSlice());
    }

    fn clearAndFree(*_) {
        _.bytes.clearAndFree();
    }
}

pub union(enum) BlobAny {
    Blob: Blob,
    InternalBlob: BlobInternal,
    WTFStringImpl: bun.WTF.StringImpl,

    static fn fromOwnedSlice(allocator: std.mem.Allocator, bytes: []u8): @This() {
        return BlobAny{ InternalBlob: BlobInternal{ bytes: std.array_list.Managed(u8).fromOwnedSlice(allocator, bytes) } };
    }

    static fn fromArrayList(list: std.array_list.Managed(u8)): @This() {
        return BlobAny{ InternalBlob: BlobInternal{ bytes: list } };
    }

    fn memoryCost(): usize {
        switch (_) {
            .Blob(b) => {
                if (s := b.store) return s.memoryCost();
                return 0;
            },
            .WTFStringImpl(s) => {
                if (s.refCount() == 1) return s.memoryCost();
                return 0;
            },
            .InternalBlob(b) => return b.memoryCost(),
        }
    }

    inline fn fastSize(): Blob.SizeType {
        return switch (_) {
            .Blob => _.Blob.size,
            .WTFStringImpl => @truncate(_.WTFStringImpl.byteLength()),
            .InternalBlob => @truncate(_.slice().len),
        };
    }

    inline fn size(): Blob.SizeType {
        return switch (_) {
            .Blob => _.Blob.size,
            .WTFStringImpl => @truncate(_.WTFStringImpl.utf8ByteLength()),
            else => @truncate(_.slice().len),
        };
    }

    fn hasContentTypeFromUser(): bool {
        return switch (_) {
            .Blob => _.Blob.hasContentTypeFromUser(),
            .WTFStringImpl => false,
            .InternalBlob => false,
        };
    }

    fn slice(): []const u8 {
        switch (_) {
            .Blob(b) => return b.sharedView(),
            .InternalBlob(b) => return b.sliceConst(),
            .WTFStringImpl(s) => return s.latin1Slice(),
        }
    }

    fn detach(*_) {
        switch (_.*) {
            .Blob => |*b| b.detach(),
            .InternalBlob => |*b| b.clearAndFree(),
            .WTFStringImpl => |s| s.deref(),
        }
    }

    fn toBlob(*_, global: *JSGlobalObject): Blob {
        if (_.size() == 0) {
            return Blob.initEmpty(global);
        }
        if (_.* == .Blob) {
            return _.Blob.dupe();
        }
        if (_.* == .WTFStringImpl) {
            const b = Blob.create(_.slice(), bun.default_allocator, global, true);
            _.* = BlobAny{ Blob: Blob{} };
            return b;
        }
        const b = Blob.init(_.InternalBlob.slice(), _.InternalBlob.bytes.allocator, global);
        _.* = BlobAny{ Blob: Blob{} };
        return b;
    }

    fn toJSON(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime): JSValue ! JSError {
        switch (_.*) {
            .Blob => return _.Blob.toJSON(global, lifetime),
            .InternalBlob => {
                if (_.InternalBlob.bytes.items.len == 0) {
                    return JSValue.jsNull();
                }
                const str = _.InternalBlob.toJSON(global);
                _.* = BlobAny{ Blob: Blob{} };
                return str;
            },
            .WTFStringImpl => {
                let str = String.init(_.WTFStringImpl);
                const _guard_str = DerefGuard<String>{ ptr: &str };
                _.* = BlobAny{ Blob: Blob{} };
                if (str.length() == 0) {
                    return JSValue.jsNull();
                }
                return str.toJSByParseJSON(global);
            },
        }
    }

    fn toString(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime): JSValue ! JSError {
        switch (_.*) {
            .Blob => return _.Blob.toString(global, lifetime),
            .InternalBlob => {
                if (_.InternalBlob.bytes.items.len == 0) {
                    return ZigString.Empty.toJS(global);
                }
                const owned = _.InternalBlob.toStringOwned(global)?;
                _.* = BlobAny{ Blob: Blob{} };
                return owned;
            },
            .WTFStringImpl => {
                let str = String.init(_.WTFStringImpl);
                const _guard_str = DerefGuard<String>{ ptr: &str };
                _.* = BlobAny{ Blob: Blob{} };
                return str.toJS(global);
            },
        }
    }

    fn toArrayBuffer(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime): JSValue ! JSError {
        return _.toArrayBufferView(global, lifetime, .ArrayBuffer);
    }

    fn toUint8Array(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime): JSValue ! JSError {
        return _.toArrayBufferView(global, lifetime, .Uint8Array);
    }

    fn toArrayBufferView(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime, comptime TypedArrayView: JSValue.JSType): JSValue ! JSError {
        switch (_.*) {
            .Blob => return _.Blob.toArrayBufferView(global, lifetime, TypedArrayView),
            .InternalBlob => {
                const bytes = _.InternalBlob.toOwnedSlice();
                _.* = BlobAny{ Blob: Blob{} };
                return ArrayBuffer.fromDefaultAllocator(global, bytes, TypedArrayView);
            },
            .WTFStringImpl => {
                let str = String.init(_.WTFStringImpl);
                const _guard_str = DerefGuard<String>{ ptr: &str };
                _.* = BlobAny{ Blob: Blob{} };
                const bytes = bun.handleOom(str.toOwnedSlice(bun.default_allocator));
                return ArrayBuffer.fromDefaultAllocator(global, bytes, TypedArrayView);
            },
        }
    }

    fn isDetached(): bool {
        switch (_) {
            .Blob b => return b.isDetached(),
            .InternalBlob ib => return ib.bytes.items.len == 0,
            .WTFStringImpl w => return w.length() == 0,
        }
    }

    fn store(): (*Blob.Store)? {
        if (_ == .Blob) {
            return _.Blob.store;
        }
        return null;
    }

    fn wasString(): bool {
        return switch (_) {
            .Blob => _.Blob.charset == .all_ascii,
            .WTFStringImpl => true,
            .InternalBlob => _.InternalBlob.was_string,
        };
    }

    fn contentType(): []const u8 {
        return switch (_) {
            .Blob => _.Blob.content_type,
            .WTFStringImpl => MimeType.text.value,
            .InternalBlob => _.InternalBlob.contentType(),
        };
    }

    fn getFileName(): ([]const u8)? {
        return switch (_) {
            .Blob => _.Blob.getFileName(),
            .WTFStringImpl => null,
            .InternalBlob => null,
        };
    }

    fn needsToReadFile(): bool {
        switch (_) {
            .Blob b => return b.needsToReadFile(),
            .InternalBlob => return false,
            .WTFStringImpl => return false,
        }
    }

    fn toArrayBufferTransfer(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        return ArrayBuffer.create(globalObject, bytes, .ArrayBuffer);
    }

    fn toUint8ArrayTransfer(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        return ArrayBuffer.create(globalObject, bytes, .Uint8Array);
    }

    fn toJSONShare(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        let str = String.cloneUTF8(bytes);
        const _guard_str = DerefGuard<String>{ ptr: &str };
        return str.toJSByParseJSON(globalObject);
    }

    fn toStringTransfer(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        return String.fromBytes(bytes).toJS(globalObject);
    }

    fn toPromise(*_, globalThis: *JSGlobalObject, action: webcore.streams.BufferAction.Tag): JSValue ! JSError {
        const val = switch (action) {
            .arrayBuffer => _.toArrayBufferTransfer(globalThis)?,
            .bytes => _.toUint8ArrayTransfer(globalThis)?,
            .json => _.toJSONShare(globalThis)?,
            .text => _.toStringTransfer(globalThis)?,
            .blob => brk: {
                const duped = bun.handleOom(bun.default_allocator.dupe(u8, _.slice()));
                const b = bun.new(Blob, Blob.init(duped, bun.default_allocator, globalThis));
                break :brk b.toJS(globalThis);
            },
        };
        return JSPromise.resolvedPromiseValue(globalThis, val);
    }
}

struct _BlobCloneSigs {
    static fn write_bytes(_: *!, _: [*]const u8, _: u32) callconv(jsc.conv) { unreachable; }
}
pub const BlobStructuredCloneWriteBytes = @TypeOf(&_BlobCloneSigs.write_bytes);

pub struct BlobExternalSharedDescriptor {
    pub const ref = Blob__ref;
    pub const deref = Blob__deref;
}

@root struct Blob {
    reported_estimated_size: usize = 0,
    size: BlobSizeType = 0,
    offset: BlobSizeType = 0,
    store: (*BlobStore)? = null,
    content_type: []const u8 = "",
    content_type_allocated: bool = false,
    content_type_was_set: bool = false,
    charset: strings.AsciiStatus = .unknown,
    is_jsdom_file: bool = false,
    ref_count: ptr.RawRefCount(u32, .single_threaded) = .init(0),
    globalThis: *JSGlobalObject = undefined,
    last_modified: f64 = 0.0,
    name: String = String.dead,

    pub const Store = BlobStore;
    pub const read_file = read_file_mod;
    pub const write_file = write_file_mod;
    pub const copy_file = copy_file_mod;
    pub const js = jsc.Codegen.JSBlob;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;
    pub const Ref = bun.ptr.ExternalShared(Blob);
    pub const SizeType = BlobSizeType;
    pub const max_size = std.math.maxInt(SizeType);
    pub const Lifetime = jsc.WebCore.Lifetime;
    pub const ClosingState = BlobClosingState;
    pub const Internal = BlobInternal;
    pub const Any = BlobAny;
    pub const ReadBytesResult = BlobReadBytesResult;
    pub const external_shared_descriptor = BlobExternalSharedDescriptor;

    static fn new(blob: Blob): *Blob {
        let result = bun.new(Blob, blob);
        result.ref_count = .init(1);
        return result;
    }

    fn isHeapAllocated(): bool {
        return _.ref_count.raw_value != 0;
    }

    fn isBunFile(): bool {
        if (store := _.store) {
            return store.data == .file;
        }
        return false;
    }

    fn isS3(): bool {
        if (store := _.store) {
            return store.data == .s3;
        }
        return false;
    }

    static fn writeFormatForSize(is_jdom_file: bool, size: usize, writer: auto, comptime enable_ansi_colors: bool)! {
        if (is_jdom_file) {
            writer.writeAll(comptime Output.prettyFmt("<r>File<r>", enable_ansi_colors))?;
        } else {
            writer.writeAll(comptime Output.prettyFmt("<r>Blob<r>", enable_ansi_colors))?;
        }
        writer.print(
            comptime Output.prettyFmt(" (<yellow>{f}<r>)", enable_ansi_colors),
            .{ bun.fmt.size(size, .{}) },
        )?;
    }

    fn writeFormat(*_, comptime Formatter: type, formatter: *Formatter, writer: auto, comptime enable_ansi_colors: bool)! {
        const Writer = @TypeOf(writer);
        if (_.store == null) {
            if (_.size > 0) {
                if (_.is_jsdom_file) {
                    writer.writeAll(comptime Output.prettyFmt("<d>[<r>File<r> detached<d>]<r>", enable_ansi_colors))?;
                } else {
                    writer.writeAll(comptime Output.prettyFmt("<d>[<r>Blob<r> detached<d>]<r>", enable_ansi_colors))?;
                }
                return;
            }
            writeFormatForSize(_.is_jsdom_file, 0, writer, enable_ansi_colors)?;
        } else {
            const store = _.store.?;
            switch (store.data) {
                .s3 => |*s3| {
                    jsc.WebCore.S3File.writeFormat(s3, Formatter, formatter, writer, enable_ansi_colors, _.content_type, _.offset)?;
                },
                .file => |file| {
                    writer.writeAll(comptime Output.prettyFmt("<r>FileRef<r>", enable_ansi_colors))?;
                    switch (file.pathlike) {
                        .path => |path| {
                            writer.print(
                                comptime Output.prettyFmt(" (<green>\"{s}\"<r>)<r>", enable_ansi_colors),
                                .{ path.slice() },
                            )?;
                        },
                        .fd => |fd| {
                            if (Environment.isWindows) {
                                switch (fd.decodeWindows()) {
                                    .uv => |uv_file| writer.print(
                                        comptime Output.prettyFmt(" (<r>fd<d>:<r> <yellow>{d}<r>)<r>", enable_ansi_colors),
                                        .{uv_file},
                                    )?,
                                    .windows => |handle| {
                                        if (Environment.isDebug) @panic("this shouldn't be reachable.");
                                        writer.print(
                                            comptime Output.prettyFmt(" (<r>fd<d>:<r> <yellow>0x{x}<r>)<r>", enable_ansi_colors),
                                            .{unsafe { @intFromPtr(handle) }},
                                        )?;
                                    },
                                }
                            } else {
                                writer.print(
                                    comptime Output.prettyFmt(" (<r>fd<d>:<r> <yellow>{d}<r>)<r>", enable_ansi_colors),
                                    .{fd.native()},
                                )?;
                            }
                        },
                    }
                },
                .bytes => {
                    writeFormatForSize(_.is_jsdom_file, _.size, writer, enable_ansi_colors)?;
                },
            }
        }

        const has_name = !_.name.isEmpty();
        const show_name = (_.is_jsdom_file && has_name) || (has_name && _.store != null && _.store.?.data == .bytes);
        if (!_.isS3() && (_.content_type.len > 0 || _.offset > 0 || show_name || _.last_modified != 0.0)) {
            writer.writeAll(" {\n")?;
            {
                formatter.indent += 1;
                defer formatter.indent -= 1;

                if (show_name) {
                    formatter.writeIndent(Writer, writer)?;
                    writer.print(
                        comptime Output.prettyFmt("name<d>:<r> <green>\"{f}\"<r>", enable_ansi_colors),
                        .{_.name},
                    )?;
                    if (_.content_type.len > 0 || _.offset > 0 || _.last_modified != 0) {
                        formatter.printComma(Writer, writer, enable_ansi_colors)?;
                    }
                    writer.writeAll("\n")?;
                }

                if (_.content_type.len > 0) {
                    formatter.writeIndent(Writer, writer)?;
                    writer.print(
                        comptime Output.prettyFmt("type<d>:<r> <green>\"{s}\"<r>", enable_ansi_colors),
                        .{_.content_type},
                    )?;
                    if (_.offset > 0 || _.last_modified != 0) {
                        formatter.printComma(Writer, writer, enable_ansi_colors)?;
                    }
                    writer.writeAll("\n")?;
                }

                if (_.offset > 0) {
                    formatter.writeIndent(Writer, writer)?;
                    writer.print(
                        comptime Output.prettyFmt("offset<d>:<r> <yellow>{d}<r>\n", enable_ansi_colors),
                        .{_.offset},
                    )?;
                    if (_.last_modified != 0) {
                        formatter.printComma(Writer, writer, enable_ansi_colors)?;
                    }
                    writer.writeAll("\n")?;
                }

                if (_.last_modified != 0) {
                    formatter.writeIndent(Writer, writer)?;
                    writer.print(
                        comptime Output.prettyFmt("lastModified<d>:<r> <yellow>{d}<r>\n", enable_ansi_colors),
                        .{_.last_modified},
                    )?;
                }
            }

            formatter.writeIndent(Writer, writer)?;
            writer.writeAll("}")?;
        }
    }

    fn doReadFileInternal(*_, comptime Handler: type, ctx: Handler, comptime Function: auto, global: *JSGlobalObject) {
        if (Environment.isWindows) {
            const ReadFileHandler = NewInternalReadFileHandler(Handler, Function);
            read_file_mod.ReadFileUV.start(global.bunVM().eventLoop(), _.store.?, _.offset, _.size, ReadFileHandler, ctx);
            return;
        }
        const file_read = read_file_mod.ReadFile.createWithCtx(
            bun.default_allocator,
            _.store.?,
            ctx,
            NewInternalReadFileHandler(Handler, Function).run,
            _.offset,
            _.size,
        ) catch |e| bun.handleOom(e);
        let read_file_task = read_file_mod.ReadFileTask.createOnJSThread(bun.default_allocator, global, file_read);
        read_file_task.schedule();
    }

    fn readBytesToHandler(*_, comptime Handler: type, ctx: *Handler, global: *JSGlobalObject): bun.JSTerminated!void {
        if (_.needsToReadFile()) {
            const HandlerPtr = @TypeOf(ctx);
            _.doReadFileInternal(HandlerPtr, ctx, ReadBytesAdapter<Handler>.run, global);
            return;
        }
        if (_.isS3()) {
            let t = S3ReadBytesTask<Handler>.create(ctx, _.dupe(), global.bunVM());
            t.poll.ref(t.vm);
            const TaskT = @TypeOf(t.*);
            let _guard_task = S3ReadTaskGuard<TaskT>{ ptr: t };
            const env = t.vm.transpiler.env;
            const cred = t.blob.store.?.data.s3.getCredentials();
            const path = t.blob.store.?.data.s3.path();
            let proxy: ([]const u8)? = null;
            if (p := env.getHttpProxy(true, null, null)) {
                proxy = p.href;
            }
            const payer = t.blob.store.?.data.s3.request_payer;
            if (_.offset > 0 || _.size != Blob.max_size) {
                const len: usize? = if (_.size != Blob.max_size) @intCast(_.size) else null;
                bun.S3.downloadSlice(cred, path, @intCast(_.offset), len, unsafe { @ptrCast(&S3ReadBytesTask<Handler>.cb) }, t, proxy, payer)?;
            } else {
                bun.S3.download(cred, path, unsafe { @ptrCast(&S3ReadBytesTask<Handler>.cb) }, t, proxy, payer)?;
            }
            _guard_task.disarm();
            return;
        }
        const view = _.sharedView();
        const owned = bun.default_allocator.dupe(u8, view) catch {
            Handler.onReadBytes(ctx, .{ .err = .{
                .code = bun.String.static("ENOMEM"),
                .message = bun.String.static("Out of memory"),
                .syscall = bun.String.static("read"),
            } });
            return;
        };
        Handler.onReadBytes(ctx, .{ .ok = owned });
    }

    fn hasContentTypeFromUser(): bool {
        if (_.content_type_was_set) return true;
        if (store := _.store) {
            return store.data == .file || store.data == .s3;
        }
        return false;
    }

    fn contentTypeOrMimeType(): ([]const u8)? {
        if (_.content_type.len > 0) return _.content_type;
        if (store := _.store) {
            switch (store.data) {
                .file file => return file.mime_type.value,
                .s3 s3_data => return s3_data.mime_type.value,
                else => return null,
            }
        }
        return null;
    }

    fn getFormDataEncoding(*_): (*FormData.AsyncFormData)? {
        if (ct_slice := _.getContentType()) {
            let content_type_slice = ct_slice;
            const _guard_ct = DeinitGuard<ZigString.Slice>{ ptr: &content_type_slice };
            if (encoding := FormData.Encoding.get(content_type_slice.slice())) {
                return bun.handleOom(FormData.AsyncFormData.init(default_allocator, encoding));
            }
        }
        return null;
    }

    fn getContentType(*_): ZigString.Slice? {
        if (_.content_type.len > 0) {
            return ZigString.Slice.fromUTF8NeverFree(_.content_type);
        }
        return null;
    }

    fn getSizeForBindings(*_): usize {
        if (_.size == Blob.max_size) {
            _.resolveSize();
        }

        // If the file doesn't exist or is not seekable
        // signal that the size is unknown.
        if (store := _.store) {
            if (store.data == .file && !(store.data.file.seekable ?? false)) {
                return std.math.maxInt(u64);
            }
        }

        if (_.size == Blob.max_size) {
            return std.math.maxInt(u64);
        }

        return @as(usize, @intCast(_.size));
    }

    fn sharedView(): []const u8 {
        if (_.size == 0) return "";
        if (store := _.store) {
            let slice_ = store.sharedView();
            if (slice_.len == 0) return "";
            // Defensive: `offset` may originate from untrusted structured-clone data.
            // Never index past the store's actual length regardless of caller state.
            slice_ = slice_[@min(@as(usize, _.offset), slice_.len)..];

            return slice_[0..@min(slice_.len, @as(usize, _.size))];
        }
        return "";
    }

    fn calculateEstimatedByteSize(*_) {
        // in-memory size. not the size on disk.
        let size: usize = @sizeOf(Blob);

        if (store := _.store) {
            size += @sizeOf(BlobStore);
            switch (store.data) {
                .bytes => {
                    size += store.data.bytes.stored_name.estimatedSize();
                    size += if (_.size != Blob.max_size)
                        @as(usize, _.size)
                    else
                        @as(usize, store.data.bytes.len);
                },
                .file => size += store.data.file.pathlike.estimatedSize(),
                .s3 => size += store.data.s3.estimatedSize(),
            }
        }

        _.reported_estimated_size = size + (_.content_type.len * @intFromBool(_.content_type_allocated)) + _.name.byteSlice().len;
    }

    static fn resolveFileStat(store: *BlobStore) {
        if (store.data.file.pathlike == .path) {
            let buffer: bun.PathBuffer = undefined;
            switch (bun.sys.stat(store.data.file.pathlike.path.sliceZ(&buffer))) {
                .result stat => {
                    store.data.file.max_size = if (bun.isRegularFile(stat.mode) || stat.size > 0)
                        @as(SizeType, @truncate(@as(u64, @intCast(@max(stat.size, 0)))))
                    else
                        Blob.max_size;
                    store.data.file.mode = @intCast(stat.mode);
                    store.data.file.seekable = bun.isRegularFile(stat.mode);
                    store.data.file.last_modified = jsc.toJSTime(stat.mtime().sec, stat.mtime().nsec);
                },
                else => {},
            }
        } else if (store.data.file.pathlike == .fd) {
            switch (bun.sys.fstat(store.data.file.pathlike.fd)) {
                .result stat => {
                    store.data.file.max_size = if (bun.isRegularFile(stat.mode) || stat.size > 0)
                        @as(SizeType, @truncate(@as(u64, @intCast(@max(stat.size, 0)))))
                    else
                        Blob.max_size;
                    store.data.file.mode = @intCast(stat.mode);
                    store.data.file.seekable = bun.isRegularFile(stat.mode);
                    store.data.file.last_modified = jsc.toJSTime(stat.mtime().sec, stat.mtime().nsec);
                },
                else => {},
            }
        }
    }

    fn resolveSize(*_) {
        if (store := _.store) {
            if (store.data == .bytes) {
                const offset = _.offset;
                const store_size = store.size();
                if (store_size != Blob.max_size) {
                    _.offset = @min(store_size, offset);
                    _.size = store_size - offset;
                }
                return;
            } else if (store.data == .file) {
                if (store.data.file.seekable == null) {
                    resolveFileStat(store);
                }

                if (store.data.file.seekable != null && store.data.file.max_size != Blob.max_size) {
                    const store_size = store.data.file.max_size;
                    const offset = _.offset;

                    _.offset = @min(store_size, offset);
                    _.size = store_size -| offset;
                    return;
                }

                if (store.data.file.seekable != null && store.data.file.seekable.? == false) {
                    return;
                }
            }

            _.size = 0;
        } else {
            _.size = 0;
        }
    }

    fn detach(*_) {
        if (store := _.store) {
            store.deref();
            _.store = null;
        }
    }

    fn releaseInternal(*_) {
        _.detach();
        if (_.content_type_allocated && _.content_type.len > 0) {
            default_allocator.free(_.content_type);
            _.content_type = "";
            _.content_type_allocated = false;
        }
        _.name.deref();
    }

    fn deinit(*_) {
        _.releaseInternal();
    }

    fn dupe(): Blob {
        if (store := _.store) {
            * = store.ref();
        }
        const content_type = if (_.content_type_allocated)
            bun.handleOom(default_allocator.dupe(u8, _.content_type))
        else
            _.content_type;
        return Blob{
            reported_estimated_size: _.reported_estimated_size,
            size: _.size,
            offset: _.offset,
            store: _.store,
            content_type: content_type,
            content_type_allocated: _.content_type_allocated,
            content_type_was_set: _.content_type_was_set,
            charset: _.charset,
            is_jsdom_file: _.is_jsdom_file,
            ref_count: .init(0),
            globalThis: _.globalThis,
            last_modified: _.last_modified,
            name: _.name.clone(),
        };
    }

    fn dupeWithContentType(preserve_type: bool): Blob {
        if (store := _.store) {
            * = store.ref();
        }
        const content_type = if (preserve_type)
            (if (_.content_type_allocated) bun.handleOom(default_allocator.dupe(u8, _.content_type)) else _.content_type)
        else
            "";
        return Blob{
            reported_estimated_size: _.reported_estimated_size,
            size: _.size,
            offset: _.offset,
            store: _.store,
            content_type: content_type,
            content_type_allocated: if (preserve_type) _.content_type_allocated else false,
            content_type_was_set: if (preserve_type) _.content_type_was_set else false,
            charset: _.charset,
            is_jsdom_file: _.is_jsdom_file,
            ref_count: .init(0),
            globalThis: _.globalThis,
            last_modified: _.last_modified,
            name: _.name.clone(),
        };
    }

    static fn init(data: []u8, allocator: std.mem.Allocator, globalThis: *JSGlobalObject): Blob {
        const store = BlobStore.init(data, allocator);
        return Blob{
            size: @as(SizeType, @truncate(data.len)),
            store: store,
            globalThis: globalThis,
        };
    }

    static fn initWithStore(store: *BlobStore, globalThis: *JSGlobalObject): Blob {
        let blob = Blob{
            size: store.size(),
            store: store,
            content_type: if (store.data == .file) store.data.file.mime_type.value else "",
            globalThis: globalThis,
        };
        blob.calculateEstimatedByteSize();
        return blob;
    }

    static fn constructBunFile(globalObject: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        const vm = globalObject.bunVM();
        const arguments = callframe.arguments_old(2).slice();
        let args = CallFrame.ArgumentsSlice.init(vm, arguments);
        const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

        const path_opt = jsc.Node.PathOrFileDescriptor.fromJS(globalObject, &args, default_allocator)?;
        if (path_opt == null) {
            return globalObject.throwInvalidArguments("Expected file path string or file descriptor", .{});
        }
        let path = path_opt.?;
        const options: JSValue? = if (arguments.len >= 2) arguments[1] else null;

        if (path == .path) {
            if (strings.hasPrefixComptime(path.path.slice(), "s3://")) {
                const _guard_path = DeinitUnprotectGuard<jsc.Node.PathOrFileDescriptor>{ ptr: &path };
                return jsc.WebCore.S3File.constructInternalJS(globalObject, path.path, options);
            }
        }

        let blob = Blob.initWithStore(bun.handleOom(BlobStore.initFile(path, null, default_allocator)), globalObject);

        if (opts := options) {
            if (opts.isObject()) {
                if (file_type := opts.getTruthy(globalObject, "type")?) {
                    if (file_type.isString()) {
                        let str = file_type.toSlice(globalObject, default_allocator)?;
                        const _guard_str = DeinitGuard<ZigString.Slice>{ ptr: &str };
                        const slice = str.slice();
                        if (strings.isAllASCII(slice)) {
                            blob.content_type_was_set = true;
                            if (entry := vm.mimeType(slice)) {
                                blob.content_type = entry.value;
                            } else {
                                const buf = bun.handleOom(default_allocator.alloc(u8, slice.len));
                                blob.content_type = strings.copyLowercase(slice, buf);
                                blob.content_type_allocated = true;
                            }
                        }
                    }
                }
                if (last_modified := opts.getTruthy(globalObject, "lastModified")?) {
                    blob.last_modified = last_modified.coerce(f64, globalObject)?;
                }
            }
        }

        const ptr_ = Blob.new(blob);
        return ptr_.toJS(globalObject);
    }

    static fn initEmpty(globalThis: *JSGlobalObject): Blob {
        return Blob{
            size: 0,
            store: null,
            globalThis: globalThis,
        };
    }

    static fn fromDOMFormData(globalThis: *JSGlobalObject, allocator: std.mem.Allocator, form_data: *DOMFormData): Blob {
        let arena_alloc = bun.ArenaAllocator.init(allocator);
        const _guard_arena = DeinitGuard<bun.ArenaAllocator>{ ptr: &arena_alloc };
        let stack_allocator = std.heap.stackFallback(1024, arena_alloc.allocator());
        const stack_mem_all = stack_allocator.get();

        let hex_buf: [70]u8 = undefined;
        const boundary = brk: {
            let random = globalThis.bunVM().rareData().nextUUID().bytes;
            break :brk std.fmt.bufPrint(&hex_buf, "----WebKitFormBoundary{x}", .{&random}) catch unreachable;
        };

        let context = FormDataContext{
            allocator: allocator,
            joiner: .{ .allocator = stack_mem_all },
            boundary: boundary,
            globalThis: globalThis,
        };

        form_data.forEach(FormDataContext, &context, FormDataContext.onEntry);
        if (context.failed) {
            context.joiner.deinit();
            return Blob.initEmpty(globalThis);
        }

        context.joiner.pushStatic("--");
        context.joiner.pushStatic(boundary);
        context.joiner.pushStatic("--\r\n");

        const store = BlobStore.init(bun.handleOom(context.joiner.done(allocator)), allocator);
        let blob = Blob.initWithStore(store, globalThis);
        blob.content_type = std.fmt.allocPrint(bun.default_allocator, "multipart/form-data; boundary={s}", .{boundary}) catch |e| bun.handleOom(e);
        blob.content_type_allocated = true;
        blob.content_type_was_set = true;
        return blob;
    }

    static fn fromURLSearchParams(globalThis: *JSGlobalObject, allocator: std.mem.Allocator, search_params: *URLSearchParams): Blob {
        let converter = URLSearchParamsConverter{
            allocator: allocator,
            globalThis: globalThis,
        };
        search_params.toString(URLSearchParamsConverter, &converter, URLSearchParamsConverter.convert);
        const url_store = BlobStore.init(converter.buf, allocator);
        url_store.mime_type = MimeType.Compact.from(.@"application/x-www-form-urlencoded").toMimeType();

        let blob = Blob.initWithStore(url_store, globalThis);
        blob.content_type = url_store.mime_type.value;
        blob.content_type_was_set = true;
        return blob;
    }

    static fn findOrCreateFileFromPath(path_or_fd: *jsc.Node.PathOrFileDescriptor, globalThis: *JSGlobalObject, comptime check_s3: bool): Blob {
        let vm = globalThis.bunVM();
        const allocator = default_allocator;
        if (check_s3) {
            if (path_or_fd.* == .path) {
                if (strings.startsWith(path_or_fd.path.slice(), "s3://")) {
                    const credentials = globalThis.bunVM().transpiler.env.getS3Credentials();
                    const copy = path_or_fd.*;
                    path_or_fd.* = .{ .path = .{ .string = bun.PathString.empty } };
                    return Blob.initWithStore(bun.handleOom(BlobStore.initS3(copy.path, null, credentials, allocator)), globalThis);
                }
            }
        }

        const path: jsc.Node.PathOrFileDescriptor = brk: {
            if (path_or_fd.* == .path) {
                let s = path_or_fd.path.slice();

                if (Environment.isWindows && bun.strings.eqlComptime(s, "/dev/null")) {
                    path_or_fd.deinit();
                    path_or_fd.* = .{
                        .path = .{
                            .string = bun.PathString.init(bun.handleOom(allocator.dupe(u8, "\\\\.\\NUL"))),
                        },
                    };
                    s = path_or_fd.path.slice();
                }

                if (graph := vm.standalone_module_graph) {
                    if (file := graph.find(s)) {
                        const _guard_pofd = PathOrFdResetGuard{ ptr: path_or_fd };

                        return file.blob(globalThis).dupe();
                    }
                }

                path_or_fd.toThreadSafe();
                const copy = path_or_fd.*;
                path_or_fd.* = .{ .path = .{ .string = bun.PathString.empty } };
                break :brk copy;
            } else {
                if (tag := path_or_fd.fd.stdioTag()) {
                    const store: *BlobStore = switch (tag) {
                        .std_in => vm.rareData().stdin(),
                        .std_err => vm.rareData().stderr(),
                        .std_out => vm.rareData().stdout(),
                    };
                    store.ref();
                    return Blob.initWithStore(store, globalThis);
                }
                break :brk path_or_fd.*;
            }
        };

        return Blob.initWithStore(bun.handleOom(BlobStore.initFile(path, null, allocator)), globalThis);
    }

    fn toJS(*_, globalThis: *JSGlobalObject): JSValue {
        _.calculateEstimatedByteSize();
        if (_.isS3()) {
            return jsc.WebCore.S3File.toJSUnchecked(globalThis, _);
        }
        return jsc.Codegen.JSBlob.toJSUnchecked(globalThis, _);
    }

    fn estimatedSize(*_): usize {
        return _.reported_estimated_size;
    }

    fn finalize(*_) {
        _.deinit();
        bun.destroy(_);
    }

    static fn constructor(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!*Blob {
        const allocator = default_allocator;
        let blob: Blob = undefined;
        let arguments = callframe.arguments_old(2);
        const args = arguments.slice();

        switch (args.len) {
            0 => {
                let empty: [0]u8 = undefined;
                blob = Blob.init(empty[0..0], allocator, globalThis);
            },
            else => {
                blob = Blob.get(globalThis, args[0], false, true)?;

                if (args.len > 1) {
                    const options = args[1];
                    if (options.isObject()) {
                        // type, the ASCII-encoded string in lower case
                        // representing the media type of the Blob.
                        if (content_type := options.get(globalThis, "type")?) {
                            inner: {
                                if (content_type.isString()) {
                                    let content_type_str = content_type.toSlice(globalThis, default_allocator)?;
                                    const _guard_ct = DeinitGuard<ZigString.Slice>{ ptr: &content_type_str };
                                    const slice = content_type_str.slice();
                                    if (!strings.isAllASCII(slice)) {
                                        break :inner;
                                    }
                                    blob.content_type_was_set = true;

                                    if (mime := globalThis.bunVM().mimeType(slice)) {
                                        blob.content_type = mime.value;
                                        break :inner;
                                    }
                                    const content_type_buf = bun.handleOom(allocator.alloc(u8, slice.len));
                                    blob.content_type = strings.copyLowercase(slice, content_type_buf);
                                    blob.content_type_allocated = true;
                                }
                            }
                        }
                    }
                }

                if (blob.content_type.len == 0) {
                    blob.content_type = "";
                    blob.content_type_was_set = false;
                }
            },
        }

        blob.calculateEstimatedByteSize();
        return Blob.new(blob);
    }

    fn getArrayBuffer(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        if (_.needsToReadFile()) {
            return _.doReadFile(toArrayBufferWithBytes, globalThis);
        }
        return JSPromise.resolvedPromiseValue(globalThis, jsc.ArrayBuffer.create(globalThis, _.sharedView(), .ArrayBuffer)?);
    }

    fn getBytes(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        if (_.needsToReadFile()) {
            return _.doReadFile(toUint8ArrayWithBytes, globalThis);
        }
        return JSPromise.resolvedPromiseValue(globalThis, jsc.ArrayBuffer.create(globalThis, _.sharedView(), .Uint8Array)?);
    }

    fn doUnlink(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(1).slice();
        let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

        validateWritableBlob(globalThis, _)?;

        const store = _.store.?;
        if (store.data == .s3) {
            const s3 = &store.data.s3;
            return s3.unlink(store, globalThis, args.nextEat())?;
        } else if (store.data == .file) {
            const file = store.data.file;
            return file.unlink(globalThis);
        } else {
            unreachable;
        }
    }

    fn getExistsSync(*_): JSValue {
        if (_.size == Blob.max_size) {
            _.resolveSize();
        }
        if (_.store == null) return .true;
        const store = _.store.?;
        if (store.data == .bytes) {
            return .true;
        }
        return JSValue.jsBoolean(
            bun.isRegularFile(store.data.file.mode) || bun.sys.S.ISFIFO(store.data.file.mode),
        );
    }

    fn getExists(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        if (_.isS3()) {
            return jsc.WebCore.S3File.S3BlobStatTask.exists(globalThis, _);
        }
        return JSPromise.resolvedPromiseValue(globalThis, _.getExistsSync());
    }

    fn getFormData(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        return JSPromise.resolvedPromiseValue(globalThis, JSValue.js_undefined);
    }

    fn doImage(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        return globalThis.throwTODO("Blob.image is not yet implemented");
    }

    fn isDetached(): bool {
        return _.store == null;
    }

    static fn create(bytes_: []const u8, allocator_: std.mem.Allocator, globalThis: *JSGlobalObject, was_string: bool): Blob {
        return bun.handleOom(Blob.tryCreate(bytes_, allocator_, globalThis, was_string));
    }

    fn takeOwnership(*_): Blob {
        let result = _.*;
        _.* = undefined;
        result.ref_count = .init(0);
        return result;
    }

    fn doReadFile(*_, comptime Function: auto, global: *JSGlobalObject): JSValue {
        const Handler = read_file_mod.NewReadFileHandler(Function);
        let handler = bun.new(Handler, .{
            .context = _.dupe(),
            .globalThis = global,
        });

        if (Environment.isWindows) {
            let promise = JSPromise.create(global);
            const promise_value = promise.toJS();
            promise_value.ensureStillAlive();
            handler.promise.strong.set(global, promise_value);
            read_file_mod.ReadFileUV.start(handler.globalThis.bunVM().eventLoop(), _.store.?, _.offset, _.size, Handler, handler);
            return promise_value;
        }

        const file_read = read_file_mod.ReadFile.create(
            default_allocator,
            _.store.?,
            _.offset,
            _.size,
            @TypeOf(handler),
            handler,
            Handler.run,
        ) catch |e| bun.handleOom(e);
        let read_file_task = read_file_mod.ReadFileTask.createOnJSThread(default_allocator, global, file_read);

        let promise = JSPromise.create(global);
        const promise_value = promise.toJS();
        promise_value.ensureStillAlive();
        handler.promise.strong.set(global, promise_value);

        read_file_task.schedule();
        return promise_value;
    }

    static fn toStringWithBytes(blob: *Blob, global: *JSGlobalObject, raw_bytes: []const u8, comptime lifetime: webcore.Lifetime): JSError!JSValue {
        const bom_split = strings.BOM.detectAndSplit(raw_bytes);
        const bom = bom_split[0];
        const buf = bom_split[1];

        if (buf.len == 0) {
            if (lifetime == .temporary) default_allocator.free(raw_bytes);
            return ZigString.Empty.toJS(global);
        }

        if (bom == .utf16_le) {
            const _guard_raw = FreeGuard<u8>{ alloc: default_allocator, slice: if (lifetime == .temporary) raw_bytes else null };
            let out = String.cloneUTF16(bun.reinterpretSlice(u16, buf));
            const _guard_out = DerefGuard<String>{ ptr: &out };
            return out.toJS(global);
        }

        const blob_ascii: bool? = switch (blob.charset) {
            .all_ascii => true,
            .non_ascii => false,
            .unknown => null,
        };
        const could_be_all_ascii = blob_ascii ?? blob.store.?.is_all_ascii;

        if (could_be_all_ascii == null || !could_be_all_ascii.?) {
            if (external := strings.toUTF16Alloc(default_allocator, buf, false, false) catch return global.throwOutOfMemory()) {
                if (lifetime != .temporary) blob.charset = .fromBool(false);
                if (lifetime == .transfer) blob.detach();
                if (lifetime == .temporary) default_allocator.free(raw_bytes);
                return ZigString.toExternalU16(external.ptr, external.len, global);
            }
            if (lifetime != .temporary) blob.charset = .fromBool(true);
        }

        switch (comptime lifetime) {
            .clone, .share => {
                blob.store.?.ref();
                return ZigString.init(buf).external(global, blob.store.?, BlobStore.external);
            },
            .transfer => {
                const store = blob.store.?;
                bun.assert(store.data == .bytes);
                blob.transfer();
                return ZigString.init(buf).external(global, store, BlobStore.external);
            },
            .temporary => {
                if (buf.len != raw_bytes.len) {
                    const _guard_raw = FreeGuard<u8>{ alloc: default_allocator, slice: raw_bytes };
                    let out = String.cloneLatin1(buf);
                    const _guard_out = DerefGuard<String>{ ptr: &out };
                    return out.toJS(global);
                }
                return ZigString.init(buf).toExternalValue(global);
            },
        }
    }

    static fn toJSONWithBytes(blob: *Blob, global: *JSGlobalObject, raw_bytes: []const u8, comptime lifetime: webcore.Lifetime): JSError!JSValue {
        const bom_split = strings.BOM.detectAndSplit(raw_bytes);
        const bom = bom_split[0];
        const buf = bom_split[1];

        if (buf.len == 0) {
            if (lifetime == .temporary) default_allocator.free(raw_bytes);
            return global.createSyntaxErrorInstance("Unexpected end of JSON input", .{});
        }

        if (bom == .utf16_le) {
            const _guard_raw = FreeGuard<u8>{ alloc: default_allocator, slice: if (lifetime == .temporary) raw_bytes else null };
            const _guard_blob = CondDetachGuard<Blob>{ ptr: if (lifetime == .transfer) blob else null };
            let out = String.cloneUTF16(bun.reinterpretSlice(u16, buf));
            const _guard_out = DerefGuard<String>{ ptr: &out };
            return out.toJSByParseJSON(global);
        }

        const blob_ascii: bool? = switch (blob.charset) {
            .all_ascii => true,
            .non_ascii => false,
            .unknown => null,
        };
        const could_be_all_ascii = blob_ascii ?? blob.store.?.is_all_ascii;
        const _guard_raw = FreeGuard<u8>{ alloc: default_allocator, slice: if (comptime lifetime == .temporary) raw_bytes else null };

        if (could_be_all_ascii == null || !could_be_all_ascii.?) {
            let stack_fallback = std.heap.stackFallback(4096, default_allocator);
            const allocator = stack_fallback.get();
            if (external := strings.toUTF16Alloc(allocator, buf, false, false) catch null) {
                if (comptime lifetime != .temporary) blob.charset = .fromBool(false);
                const result = ZigString.initUTF16(external).toJSONObject(global);
                allocator.free(external);
                return result;
            }
            if (comptime lifetime != .temporary) blob.charset = .fromBool(true);
        }

        return ZigString.init(buf).toJSONObject(global);
    }

    static fn toArrayBufferWithBytes(blob: *Blob, global: *JSGlobalObject, buf: []u8, comptime lifetime: webcore.Lifetime): JSError!JSValue {
        return toArrayBufferViewWithBytes(blob, global, buf, lifetime, .ArrayBuffer);
    }

    static fn toUint8ArrayWithBytes(blob: *Blob, global: *JSGlobalObject, buf: []u8, comptime lifetime: webcore.Lifetime): JSError!JSValue {
        return toArrayBufferViewWithBytes(blob, global, buf, lifetime, .Uint8Array);
    }

    static fn toArrayBufferViewWithBytes(blob: *Blob, global: *JSGlobalObject, buf: []u8, comptime lifetime: webcore.Lifetime, comptime TypedArrayView: JSValue.JSType): JSError!JSValue {
        switch (comptime lifetime) {
            .clone => {
                if (TypedArrayView != .ArrayBuffer && buf.len > jsc.VirtualMachine.synthetic_allocation_limit) {
                    blob.detach();
                    return global.throwOutOfMemory();
                }
                return jsc.ArrayBuffer.create(global, buf, TypedArrayView);
            },
            .share => {
                if (buf.len > jsc.VirtualMachine.synthetic_allocation_limit && TypedArrayView != .ArrayBuffer) {
                    return global.throwOutOfMemory();
                }
                blob.store.?.ref();
                let ab = jsc.ArrayBuffer.fromBytes(buf, TypedArrayView);
                return ab.toJSWithContext(
                    global,
                    blob.store.?,
                    jsc.array_buffer.BlobArrayBuffer_deallocator,
                );
            },
            .transfer => {
                if (buf.len > jsc.VirtualMachine.synthetic_allocation_limit && TypedArrayView != .ArrayBuffer) {
                    blob.detach();
                    return global.throwOutOfMemory();
                }
                const store = blob.store.?;
                blob.transfer();
                let ab = jsc.ArrayBuffer.fromBytes(buf, TypedArrayView);
                return ab.toJSWithContext(
                    global,
                    store,
                    jsc.array_buffer.BlobArrayBuffer_deallocator,
                );
            },
            .temporary => {
                if (buf.len > jsc.VirtualMachine.synthetic_allocation_limit && TypedArrayView != .ArrayBuffer) {
                    default_allocator.free(buf);
                    return global.throwOutOfMemory();
                }
                let ab = jsc.ArrayBuffer.fromBytes(buf, TypedArrayView);
                return ab.toJS(global);
            },
        }
    }

    fn toString(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime): JSError!JSValue {
        if (_.needsToReadFile()) {
            return _.doReadFile(toStringWithBytes, global);
        }
        if (_.isS3()) {
            return global.throwTODO("Blob.toString for S3 blobs is not yet ported");
        }
        const view = _.sharedView();
        if (view.len == 0) {
            return ZigString.Empty.toJS(global);
        }
        return toStringWithBytes(_, global, view, lifetime);
    }

    fn toJSON(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime): JSError!JSValue {
        if (_.needsToReadFile()) {
            return _.doReadFile(toJSONWithBytes, global);
        }
        if (_.isS3()) {
            return global.throwTODO("Blob.toJSON for S3 blobs is not yet ported");
        }
        return toJSONWithBytes(_, global, _.sharedView(), lifetime);
    }

    fn toArrayBufferView(*_, global: *JSGlobalObject, comptime lifetime: webcore.Lifetime, comptime TypedArrayView: JSValue.JSType): JSError!JSValue {
        const WithBytesFn = comptime if (TypedArrayView == .Uint8Array)
            toUint8ArrayWithBytes
        else
            toArrayBufferWithBytes;
        if (_.needsToReadFile()) {
            return _.doReadFile(WithBytesFn, global);
        }
        if (_.isS3()) {
            return global.throwTODO("Blob.toArrayBufferView for S3 blobs is not yet ported");
        }
        const view = _.sharedView();
        if (view.len == 0) {
            return jsc.ArrayBuffer.create(global, "", TypedArrayView);
        }
        return toArrayBufferViewWithBytes(_, global, unsafe { @constCast(view) }, lifetime, TypedArrayView);
    }

    fn getFileName(): ([]const u8)? {
        if (store := _.store) {
            if (store.data == .file) {
                if (store.data.file.pathlike == .path) {
                    return store.data.file.pathlike.path.slice();
                }
            } else if (store.data == .bytes) {
                if (store.data.bytes.stored_name.slice().len > 0) {
                    return store.data.bytes.stored_name.slice();
                }
            } else if (store.data == .s3) {
                return store.data.s3.path();
            }
        }
        return null;
    }

    fn getMimeTypeOrContentType(): bun.http.MimeType? {
        if (_.content_type_was_set) {
            return bun.http.MimeType.init(_.content_type, null, null);
        }
        if (store := _.store) {
            return store.mime_type;
        }
        return null;
    }

    fn getLoader(jsc_vm: *VirtualMachine): bun.options.Loader? {
        if (filename := _.getFileName()) {
            const current_path = bun.fs.Path.init(filename);
            return current_path.loader(&jsc_vm.transpiler.options.loaders) ?? .tsx;
        } else if (mime_type := _.getMimeTypeOrContentType()) {
            return .fromMimeType(mime_type);
        } else {
            return .tsx;
        }
    }

    fn needsToReadFile(): bool {
        if (store := _.store) {
            return store.data == .file;
        }
        return false;
    }

    fn getJSON(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        if (_.needsToReadFile()) {
            return _.doReadFile(toJSONWithBytes, globalThis);
        }
        let str = String.cloneUTF8(_.sharedView());
        const _guard_str = DerefGuard<String>{ ptr: &str };
        return JSPromise.resolvedPromiseValue(globalThis, str.toJSByParseJSON(globalThis)?);
    }

    fn getLastModified(*_, _: *JSGlobalObject): JSValue {
        if (store := _.store) {
            if (store.data == .file) {
                // last_modified can be already set during read.
                if (store.data.file.last_modified == jsc.init_timestamp && !_.isS3()) {
                    resolveFileStat(store);
                }
                return JSValue.jsNumber(store.data.file.last_modified);
            }
        }

        if (_.is_jsdom_file) {
            return JSValue.jsNumber(_.last_modified);
        }

        return JSValue.jsNumber(jsc.init_timestamp);
    }

    fn getNameString(*_): String? {
        if (_.name.tag != .Dead) return _.name;

        if (path := _.getFileName()) {
            _.name = String.cloneUTF8(path);
            return _.name;
        }

        return null;
    }

    fn getName(*_, _: JSValue, globalThis: *JSGlobalObject): JSError!JSValue {
        if (name := _.getNameString()) {
            return name.toJS(globalThis);
        }
        return JSValue.js_undefined;
    }

    fn setName(*_, jsThis: JSValue, globalThis: *JSGlobalObject, value: JSValue): JSError!void {
        if (value.isEmptyOrUndefinedOrNull()) {
            _.name.deref();
            _.name = String.dead;
            js.nameSetCached(jsThis, globalThis, value);
            return;
        }
        if (value.isString()) {
            let old_name = _.name;
            const _guard_old_name = DerefGuard<String>{ ptr: &old_name };
            _.name = value.toBunString(globalThis)?;
            js.nameSetCached(jsThis, globalThis, value);
        }
    }

    fn getSize(*_, _: *JSGlobalObject): JSValue {
        if (_.size == Blob.max_size) {
            if (_.isS3()) {
                return JSValue.jsNumber(std.math.nan(f64));
            }
            _.resolveSize();
            if (_.size == Blob.max_size && _.store != null) {
                return .jsNumber(std.math.inf(f64));
            } else if (_.size == 0 && _.store != null) {
                if (_.store.?.data == .file &&
                    (_.store.?.data.file.seekable ?? true) == false &&
                    _.store.?.data.file.max_size == Blob.max_size)
                {
                    return .jsNumber(std.math.inf(f64));
                }
            }
        }
        return JSValue.jsNumber(_.size);
    }

    fn getSlice(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        const allocator = default_allocator;
        let arguments_ = callframe.arguments_old(3);
        let args = unsafe { arguments_.ptr[0..arguments_.len] };

        if (_.size == 0) {
            const empty = Blob.initEmpty(globalThis);
            let slice_blob = Blob.new(empty);
            return slice_blob.toJS(globalThis);
        }

        let relativeStart: i64 = 0;
        let relativeEnd: i64 = @as(i64, @intCast(_.size));

        unsafe {
            if (args.ptr[0].isString()) {
                args.ptr[2] = args.ptr[0];
                args.ptr[1] = .zero;
                args.ptr[0] = .zero;
                args.len = 3;
            } else if (args.ptr[1].isString()) {
                args.ptr[2] = args.ptr[1];
                args.ptr[1] = .zero;
                args.len = 3;
            }
        }

        let args_iter = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), args);
        if (start_ := args_iter.nextEat()) {
            if (start_.isNumber()) {
                const start = start_.toInt64();
                if (start < 0) {
                    relativeStart = @as(i64, @intCast(@max(start +% @as(i64, @intCast(_.size)), 0)));
                } else {
                    relativeStart = @min(@as(i64, @intCast(start)), @as(i64, @intCast(_.size)));
                }
            }
        }

        if (end_ := args_iter.nextEat()) {
            if (end_.isNumber()) {
                const end = end_.toInt64();
                if (end < 0) {
                    relativeEnd = @as(i64, @intCast(@max(end +% @as(i64, @intCast(_.size)), 0)));
                } else {
                    relativeEnd = @min(@as(i64, @intCast(end)), @as(i64, @intCast(_.size)));
                }
            }
        }

        let content_type: []const u8 = "";
        let content_type_was_allocated = false;
        if (content_type_ := args_iter.nextEat()) {
            if (content_type_.isString()) {
                let zig_str = content_type_.getZigString(globalThis)?;
                let slicer = zig_str.toSlice(default_allocator);
                const _guard_slicer = DeinitGuard<ZigString.Slice>{ ptr: &slicer };
                const s = slicer.slice();
                if (strings.isAllASCII(s)) {
                    if (mime := globalThis.bunVM().mimeType(s)) {
                        content_type = mime.value;
                    } else {
                        content_type_was_allocated = s.len > 0;
                        const content_type_buf = bun.handleOom(allocator.alloc(u8, s.len));
                        content_type = strings.copyLowercase(s, content_type_buf);
                    }
                }
            }
        }

        return _.getSliceFrom(globalThis, relativeStart, relativeEnd, content_type, content_type_was_allocated);
    }

    fn getStat(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        if (_.store == null) {
            return JSValue.js_undefined;
        }
        const store = _.store.?;
        if (store.data == .file) {
            const file = &store.data.file;
            if (file.pathlike == .path) {
                const path_like = file.pathlike.path;
                const encoded = if (path_like == .encoded_slice)
                    path_like.encoded_slice.toOwned(default_allocator)?
                else
                    ZigString.fromUTF8(path_like.slice()).toSliceClone(default_allocator)?;
                return bun.api.node.fs.Async.stat.create(globalThis, undefined, .{
                    .path = .{ .encoded_slice = encoded },
                }, globalThis.bunVM());
            } else {
                const fd = file.pathlike.fd;
                return bun.api.node.fs.Async.fstat.create(globalThis, undefined, .{ .fd = fd }, globalThis.bunVM());
            }
        } else if (store.data == .s3) {
            return jsc.WebCore.S3File.getStat(_, globalThis, callframe);
        } else {
            return JSValue.js_undefined;
        }
    }

    fn getStream(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        return webcore.ReadableStream.fromBlobCopyRef(globalThis, _, 0);
    }

    fn getText(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        if (_.needsToReadFile()) {
            return _.doReadFile(toStringWithBytes, globalThis);
        }
        const str = String.createUTF8ForJS(globalThis, _.sharedView())?;
        return JSPromise.resolvedPromiseValue(globalThis, str);
    }

    fn getType(*_, globalThis: *JSGlobalObject): JSValue {
        if (_.content_type.len > 0) {
            return ZigString.init(_.content_type).toJS(globalThis);
        }
        return ZigString.Empty.toJS(globalThis);
    }

    fn doWrite(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(3).slice();
        let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

        validateWritableBlob(globalThis, _)?;

        const data_opt = args.nextEat();
        if (data_opt == null) {
            return globalThis.throwInvalidArguments("blob.write(pathOrFdOrBlob, blob) expects a Blob-y thing to write", .{});
        }
        const data = data_opt.?;
        if (data.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("blob.write(pathOrFdOrBlob, blob) expects a Blob-y thing to write", .{});
        }
        let mkdirp_if_not_exists: bool? = null;
        const options = args.nextEat();
        if (options_object := options) {
            if (options_object.isObject()) {
                if (create_directory := options_object.getTruthy(globalThis, "createPath")?) {
                    if (!create_directory.isBoolean()) {
                        return globalThis.throwInvalidArgumentType("write", "options.createPath", "boolean");
                    }
                    mkdirp_if_not_exists = create_directory.toBoolean();
                }
                if (content_type := options_object.getTruthy(globalThis, "type")?) {
                    if (!content_type.isString()) {
                        return globalThis.throwInvalidArgumentType("write", "options.type", "string");
                    }
                    let content_type_str = content_type.toSlice(globalThis, default_allocator)?;
                    const _guard_ct = DeinitGuard<ZigString.Slice>{ ptr: &content_type_str };
                    const s = content_type_str.slice();
                    if (strings.isAllASCII(s)) {
                        if (_.content_type_allocated) {
                            default_allocator.free(_.content_type);
                            _.content_type_allocated = false;
                        }
                        _.content_type_was_set = true;

                        if (mime := globalThis.bunVM().mimeType(s)) {
                            _.content_type = mime.value;
                        } else {
                            const content_type_buf = bun.handleOom(default_allocator.alloc(u8, s.len));
                            _.content_type = strings.copyLowercase(s, content_type_buf);
                            _.content_type_allocated = true;
                        }
                    }
                }
            } else if (!options_object.isEmptyOrUndefinedOrNull()) {
                return globalThis.throwInvalidArgumentType("write", "options", "object");
            }
        }
        let blob_internal = PathOrBlob{ .blob = _.* };
        return writeFileInternal(globalThis, &blob_internal, data, .{ .mkdirp_if_not_exists = mkdirp_if_not_exists, .extra_options = options });
    }

    fn getWriter(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        const arguments_ = callframe.arguments_old(1);
        const arguments = arguments_.slice();

        if (arguments.len > 0 && !arguments[0].isEmptyOrUndefinedOrNull() && !arguments[0].isObject()) {
            return globalThis.throwInvalidArguments("options must be an object or undefined", .{});
        }

        validateWritableBlob(globalThis, _)?;

        const store = _.store.?;
        if (_.isS3()) {
            const s3 = &_.store.?.data.s3;
            const path = s3.path();
            const proxy = globalThis.bunVM().transpiler.env.getHttpProxy(true, null, null);
            const proxy_url: ([]const u8)? = if (proxy != null) proxy.?.href else null;
            if (arguments.len > 0) {
                const options = arguments[0];
                if (options.isObject()) {
                    if (content_type := options.getTruthy(globalThis, "type")?) {
                        if (!content_type.isString()) {
                            return globalThis.throwInvalidArgumentType("write", "options.type", "string");
                        }
                        let content_type_str = content_type.toSlice(globalThis, default_allocator)?;
                        const _guard_ct = DeinitGuard<ZigString.Slice>{ ptr: &content_type_str };
                        const slice = content_type_str.slice();
                        if (strings.isAllASCII(slice)) {
                            if (_.content_type_allocated) {
                                default_allocator.free(_.content_type);
                                _.content_type_allocated = false;
                            }
                            _.content_type_was_set = true;

                            if (mime := globalThis.bunVM().mimeType(slice)) {
                                _.content_type = mime.value;
                            } else {
                                const content_type_buf = bun.handleOom(default_allocator.alloc(u8, slice.len));
                                _.content_type = strings.copyLowercase(slice, content_type_buf);
                                _.content_type_allocated = true;
                            }
                        }
                    }
                    let content_disposition_str: ZigString.Slice? = null;
                    const _guard_cd = OptSliceDeinitGuard<ZigString.Slice?>{ ptr: &content_disposition_str };
                    if (content_disposition := options.getTruthy(globalThis, "contentDisposition")?) {
                        if (!content_disposition.isString()) {
                            return globalThis.throwInvalidArgumentType("write", "options.contentDisposition", "string");
                        }
                        content_disposition_str = content_disposition.toSlice(globalThis, default_allocator)?;
                    }
                    let content_encoding_str: ZigString.Slice? = null;
                    const _guard_ce = OptSliceDeinitGuard<ZigString.Slice?>{ ptr: &content_encoding_str };
                    if (content_encoding := options.getTruthy(globalThis, "contentEncoding")?) {
                        if (!content_encoding.isString()) {
                            return globalThis.throwInvalidArgumentType("write", "options.contentEncoding", "string");
                        }
                        content_encoding_str = content_encoding.toSlice(globalThis, default_allocator)?;
                    }
                    let credentialsWithOptions = s3.getCredentialsWithOptions(options, globalThis)?;
                    const _guard_creds = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &credentialsWithOptions };
                    const cd_slice: ([]const u8)? = if (content_disposition_str != null) content_disposition_str.?.slice() else null;
                    const ce_slice: ([]const u8)? = if (content_encoding_str != null) content_encoding_str.?.slice() else null;
                    return S3.writableStream(
                        credentialsWithOptions.credentials.dupe(),
                        path,
                        globalThis,
                        credentialsWithOptions.options,
                        _.contentTypeOrMimeType(),
                        cd_slice,
                        ce_slice,
                        proxy_url,
                        credentialsWithOptions.storage_class,
                        credentialsWithOptions.request_payer,
                    );
                }
            }
            return S3.writableStream(
                s3.getCredentials(),
                path,
                globalThis,
                .{},
                _.contentTypeOrMimeType(),
                null,
                null,
                proxy_url,
                null,
                s3.request_payer,
            );
        }

        if (Environment.isWindows) {
            const pathlike = store.data.file.pathlike;
            const vm = globalThis.bunVM();
            const fd: bun.FD = if (pathlike == .fd) pathlike.fd else brk: {
                let file_path: bun.PathBuffer = undefined;
                switch (bun.sys.open(
                    pathlike.path.sliceZ(&file_path),
                    bun.O.WRONLY | bun.O.CREAT | bun.O.NONBLOCK,
                    write_permissions,
                )) {
                    .result result => {
                        break :brk result;
                    },
                    .err e => {
                        return globalThis.throwValue(e.withPath(pathlike.path.slice()).toJS(globalThis)?);
                    },
                }
                @compileError("unreachable");
            };

            const is_stdout_or_stderr = brk: {
                if (pathlike != .fd) {
                    break :brk false;
                }

                if (rare := vm.rare_data) {
                    if (store == rare.stdout_store) {
                        break :brk true;
                    }
                    if (store == rare.stderr_store) {
                        break :brk true;
                    }
                }

                if (tag := fd.stdioTag()) {
                    break :brk switch (tag) {
                        .std_out, .std_err => true,
                        else => false,
                    };
                }
                break :brk false;
            };
            let sink = jsc.WebCore.FileSink.init(fd, _.globalThis.bunVM().eventLoop());
            sink.writer.owns_fd = pathlike != .fd;

            if (is_stdout_or_stderr) {
                switch (sink.writer.startSync(fd, false)) {
                    .err e => {
                        sink.deref();
                        return globalThis.throwValue(e.toJS(globalThis)?);
                    },
                    else => {},
                }
            } else {
                switch (sink.writer.start(fd, true)) {
                    .err e => {
                        sink.deref();
                        return globalThis.throwValue(e.toJS(globalThis)?);
                    },
                    else => {},
                }
            }

            return sink.toJS(globalThis);
        }

        let sink = jsc.WebCore.FileSink.init(bun.invalid_fd, _.globalThis.bunVM().eventLoop());
        let _guard_sink = OptDerefGuard<jsc.WebCore.FileSink>{ ptr: sink };

        let input_path: jsc.WebCore.PathOrFileDescriptor = brk: {
            if (store.data.file.pathlike == .fd) {
                break :brk .{ .fd = store.data.file.pathlike.fd };
            } else {
                break :brk .{
                    .path = bun.handleOom(ZigString.Slice.initDupe(
                        default_allocator,
                        store.data.file.pathlike.path.slice(),
                    )),
                };
            }
        };
        const _guard_input_path = DeinitGuard<jsc.WebCore.PathOrFileDescriptor>{ ptr: &input_path };

        let stream_start: jsc.WebCore.streams.Start = .{
            .FileSink = .{
                .input_path = input_path,
            },
        };

        if (arguments.len > 0 && arguments[0].isObject()) {
            stream_start = jsc.WebCore.streams.Start.fromJSWithTag(globalThis, arguments[0], .FileSink)?;
            stream_start.FileSink.input_path = input_path;
        }

        switch (sink.start(stream_start)) {
            .err e => {
                return globalThis.throwValue(e.toJS(globalThis)?);
            },
            else => {},
        }

        _guard_sink.disarm();
        return sink.toJS(globalThis);
    }

    fn onStructuredCloneSerialize(
        *_,
        globalThis: *JSGlobalObject,
        ctx: *!,
        writeBytes: BlobStructuredCloneWriteBytes,
    ) {
        * = _.size;
        * = globalThis;
        * = ctx;
        * = writeBytes;
    }

    static fn onStructuredCloneDeserialize(globalThis: *JSGlobalObject, raw_ptr: *[*]u8, end: [*]u8): JSError!JSValue {
        * = raw_ptr;
        * = end;
        return Blob.new(Blob.initEmpty(globalThis)).toJS(globalThis);
    }

    static fn initWithAllASCII(bytes: []u8, allocator: std.mem.Allocator, globalThis: *JSGlobalObject, is_all_ascii: bool): Blob {
        let store: (*BlobStore)? = null;
        if (bytes.len > 0) {
            store = BlobStore.init(bytes, allocator);
            store.?.is_all_ascii = is_all_ascii;
        }
        return Blob{
            size: @as(SizeType, @truncate(bytes.len)),
            store: store,
            content_type: "",
            globalThis: globalThis,
            charset: .fromBool(is_all_ascii),
        };
    }

    static fn createWithBytesAndAllocator(
        bytes: []u8,
        allocator: std.mem.Allocator,
        globalThis: *JSGlobalObject,
        was_string: bool,
    ): Blob {
        return Blob{
            size: @as(SizeType, @truncate(bytes.len)),
            store: if (bytes.len > 0) BlobStore.init(bytes, allocator) else null,
            content_type: if (was_string) MimeType.text.value else "",
            globalThis: globalThis,
        };
    }

    static fn tryCreate(
        bytes_: []const u8,
        allocator_: std.mem.Allocator,
        globalThis: *JSGlobalObject,
        was_string: bool,
    ): Blob! {
        if (comptime Environment.isLinux) {
            if (bun.linux.MemFdAllocator.shouldUse(bytes_)) {
                switch (bun.linux.MemFdAllocator.create(bytes_)) {
                    .err => {},
                    .result result => {
                        const store = BlobStore.new(.{
                            .data = .{
                                .bytes = result,
                            },
                            .allocator = bun.default_allocator,
                            .ref_count = std.atomic.Value(u32).init(1),
                        });
                        let blob = Blob.initWithStore(store, globalThis);
                        if (was_string && blob.content_type.len == 0) {
                            blob.content_type = MimeType.text.value;
                        }
                        return blob;
                    },
                }
            }
        }

        return Blob.createWithBytesAndAllocator(allocator_.dupe(u8, bytes_)?, allocator_, globalThis, was_string);
    }

    fn setNotHeapAllocated(*_) {
        _.ref_count = .init(0);
    }

    fn transfer(*_) {
        _.store = null;
    }

    static fn get(
        global: *JSGlobalObject,
        arg: JSValue,
        comptime move_flag: bool,
        comptime require_array: bool,
    ): Blob ! JSError {
        return Blob.fromJSWithoutDeferGC(global, arg, move_flag, require_array);
    }

    static fn fromJSWithoutDeferGC(
        global: *JSGlobalObject,
        arg: JSValue,
        comptime move_flag: bool,
        comptime require_array: bool,
    ): Blob ! JSError {
        let current = arg;
        if (current.isUndefinedOrNull()) {
            return Blob{ globalThis: global };
        }

        let top_value = current;
        let might_only_be_one_thing = false;
        arg.ensureStillAlive();
        defer arg.ensureStillAlive();
        let fail_if_top_value_is_not_typed_array_like = false;
        switch (current.jsTypeLoose()) {
            .Array, .DerivedArray => {
                let top_iter = jsc.JSArrayIterator.init(current, global)?;
                might_only_be_one_thing = top_iter.len == 1;
                if (top_iter.len == 0) {
                    return Blob{ globalThis: global };
                }
                if (might_only_be_one_thing) {
                    const next_opt = top_iter.next()?;
                    top_value = next_opt.?;
                }
            },
            else => {
                might_only_be_one_thing = true;
                if (require_array) {
                    fail_if_top_value_is_not_typed_array_like = true;
                }
            },
        }

        if (might_only_be_one_thing || !move_flag) {
            switch (top_value.jsTypeLoose()) {
                .Cell,
                .NumberObject,
                .String,
                .StringObject,
                .DerivedStringObject => {
                    if (!fail_if_top_value_is_not_typed_array_like) {
                        let str = top_value.toBunString(global)?;
                        const _guard_str = DerefGuard<String>{ ptr: &str };
                        const res_tuple = str.toOwnedSliceReturningAllASCII(bun.default_allocator)?;
                        const bytes = @field(res_tuple, "0");
                        const ascii = @field(res_tuple, "1");
                        return Blob.initWithAllASCII(bytes, bun.default_allocator, global, ascii);
                    }
                },

                .ArrayBuffer,
                .Int8Array,
                .Uint8Array,
                .Uint8ClampedArray,
                .Int16Array,
                .Uint16Array,
                .Int32Array,
                .Uint32Array,
                .Float16Array,
                .Float32Array,
                .Float64Array,
                .BigInt64Array,
                .BigUint64Array,
                .DataView => {
                    return Blob.tryCreate(top_value.asArrayBuffer(global).?.byteSlice(), bun.default_allocator, global, false);
                },

                .DOMWrapper => {
                    if (!fail_if_top_value_is_not_typed_array_like) {
                        if (blob := top_value.as(Blob)) {
                            if (comptime move_flag) {
                                let _blob = blob.*;
                                _blob.setNotHeapAllocated();
                                _blob.name = blob.name.dupeRef();
                                if (blob.content_type_allocated) {
                                    _blob.content_type = bun.handleOom(bun.default_allocator.dupe(u8, blob.content_type));
                                }
                                blob.transfer();
                                return _blob;
                            } else {
                                return blob.dupe();
                            }
                        } else if (build := top_value.as(jsc.API.BuildArtifact)) {
                            return build.blob.dupe();
                        } else {
                            const sliced = current.toSliceClone(global)?;
                            if (allocator := sliced.allocator.get()) {
                                return Blob.initWithAllASCII(unsafe { @constCast(sliced.slice()) }, allocator, global, false);
                            }
                        }
                    }
                },

                else => {},
            }

            if (fail_if_top_value_is_not_typed_array_like) {
                return global.throwInvalidArguments("new Blob() expects an Array", .{});
            }
        }

        let stack_allocator = std.heap.stackFallback(1024, bun.default_allocator);
        const stack_mem_all = stack_allocator.get();
        let stack = std.array_list.Managed(JSValue).init(stack_mem_all);
        let joiner = StringJoiner{ .allocator = stack_mem_all };
        let could_have_non_ascii = false;

        const _guard_stack = ThresholdDeinitGuard<std.array_list.Managed(JSValue)>{
            ptr: &stack,
            end_index: &stack_allocator.fixed_buffer_allocator.end_index,
        };

        while (true) {
            switch (current.jsTypeLoose()) {
                .NumberObject,
                .String,
                .StringObject,
                .DerivedStringObject => {
                    let sliced = current.toSlice(global, bun.default_allocator)?;
                    const allocator = sliced.allocator.get();
                    could_have_non_ascii = could_have_non_ascii || !sliced.allocator.isWTFAllocator();
                    joiner.push(sliced.slice(), allocator);
                },

                .Array, .DerivedArray => {
                    let iter = jsc.JSArrayIterator.init(current, global)?;
                    stack.ensureUnusedCapacity(iter.len)?;
                    let any_arrays = false;
                    while (item := iter.next()?) {
                        if (item.isUndefinedOrNull()) continue;

                        if (!any_arrays) {
                            switch (item.jsTypeLoose()) {
                                .NumberObject,
                                .Cell,
                                .String,
                                .StringObject,
                                .DerivedStringObject => {
                                    let sliced = item.toSlice(global, bun.default_allocator)?;
                                    const allocator = sliced.allocator.get();
                                    could_have_non_ascii = could_have_non_ascii || !sliced.allocator.isWTFAllocator();
                                    joiner.push(sliced.slice(), allocator);
                                    continue;
                                },
                                .ArrayBuffer,
                                .Int8Array,
                                .Uint8Array,
                                .Uint8ClampedArray,
                                .Int16Array,
                                .Uint16Array,
                                .Int32Array,
                                .Uint32Array,
                                .Float16Array,
                                .Float32Array,
                                .Float64Array,
                                .BigInt64Array,
                                .BigUint64Array,
                                .DataView => {
                                    could_have_non_ascii = true;
                                    let buf = item.asArrayBuffer(global).?;
                                    joiner.pushStatic(buf.byteSlice());
                                    continue;
                                },
                                .Array, .DerivedArray => {
                                    any_arrays = true;
                                    could_have_non_ascii = true;
                                    break;
                                },

                                .DOMWrapper => {
                                    if (blob := item.as(Blob)) {
                                        could_have_non_ascii = could_have_non_ascii || blob.charset != .all_ascii;
                                        joiner.pushStatic(blob.sharedView());
                                        continue;
                                    } else {
                                        const sliced = current.toSliceClone(global)?;
                                        const allocator = sliced.allocator.get();
                                        could_have_non_ascii = could_have_non_ascii || allocator != null;
                                        joiner.push(sliced.slice(), allocator);
                                    }
                                },
                                else => {},
                            }
                        }

                        stack.appendAssumeCapacity(item);
                    }
                },

                .DOMWrapper => {
                    if (blob := current.as(Blob)) {
                        could_have_non_ascii = could_have_non_ascii || blob.charset != .all_ascii;
                        joiner.pushStatic(blob.sharedView());
                    } else {
                        const sliced = current.toSliceClone(global)?;
                        const allocator = sliced.allocator.get();
                        could_have_non_ascii = could_have_non_ascii || allocator != null;
                        joiner.push(sliced.slice(), allocator);
                    }
                },

                .ArrayBuffer,
                .Int8Array,
                .Uint8Array,
                .Uint8ClampedArray,
                .Int16Array,
                .Uint16Array,
                .Int32Array,
                .Uint32Array,
                .Float16Array,
                .Float32Array,
                .Float64Array,
                .BigInt64Array,
                .BigUint64Array,
                .DataView => {
                    let buf = current.asArrayBuffer(global).?;
                    joiner.pushStatic(buf.slice());
                    could_have_non_ascii = true;
                },

                else => {
                    let sliced = current.toSlice(global, bun.default_allocator)?;
                    if (global.hasException()) {
                        const end_result = joiner.done(bun.default_allocator)?;
                        bun.default_allocator.free(end_result);
                        return error.JSError;
                    }
                    could_have_non_ascii = could_have_non_ascii || !sliced.allocator.isWTFAllocator();
                    joiner.push(sliced.slice(), sliced.allocator.get());
                },
            }
            if (next_item := stack.pop()) {
                current = next_item;
            } else {
                break;
            }
        }

        const joined = joiner.done(bun.default_allocator)?;

        if (!could_have_non_ascii) {
            return Blob.initWithAllASCII(joined, bun.default_allocator, global, true);
        }
        return Blob.init(joined, bun.default_allocator, global);
    }

    fn getSliceFrom(*_, globalThis: *JSGlobalObject, relativeStart: i64, relativeEnd: i64, content_type: []const u8, content_type_was_allocated: bool): JSValue {
        const offset = _.offset +| @as(SizeType, @intCast(relativeStart));
        const len = @as(SizeType, @intCast(@max(relativeEnd -| relativeStart, 0)));

        let blob = _.dupe();
        blob.offset = offset;
        blob.size = len;

        if (blob.content_type_allocated) {
            bun.default_allocator.free(blob.content_type);
        }

        if (content_type.len == 0 && _.content_type.len > 0 && !_.content_type_allocated) {
            blob.content_type = _.content_type;
        } else {
            blob.content_type = content_type;
        }
        blob.content_type_allocated = content_type_was_allocated;
        blob.content_type_was_set = _.content_type_was_set || content_type_was_allocated;

        let blob_ = Blob.new(blob);
        return blob_.toJS(globalThis);
    }

    fn pipeReadableStreamToBlob(*_, globalThis: *JSGlobalObject, readable_stream: jsc.WebCore.ReadableStream, extra_options: JSValue?): JSValue ! JSError {
        const store_opt = _.store;
        if (store_opt == null) {
            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, globalThis.createErrorInstance("Blob is detached", .{}));
        }
        let store = store_opt.?;

        if (_.isS3()) {
            const s3 = &_.store.?.data.s3;
            let aws_options = s3.getCredentialsWithOptions(extra_options, globalThis) catch |e| {
                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, globalThis.takeException(e));
            };
            const _guard_aws = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &aws_options };

            const path = s3.path();
            let proxy_url: ([]const u8)? = null;
            if (p := globalThis.bunVM().transpiler.env.getHttpProxy(true, null, null)) {
                proxy_url = p.href;
            }

            return S3.uploadStream(
                (if (extra_options != null) aws_options.credentials.dupe() else s3.getCredentials()),
                path,
                readable_stream,
                globalThis,
                aws_options.options,
                aws_options.acl,
                aws_options.storage_class,
                _.contentTypeOrMimeType(),
                aws_options.content_disposition,
                aws_options.content_encoding,
                proxy_url,
                aws_options.request_payer,
                null,
                undefined,
            );
        }

        if (store.data != .file) {
            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, globalThis.createErrorInstance("Blob is read-only", .{}));
        }

        const file_sink = brk_sink: {
            if (Environment.isWindows) {
                const pathlike = store.data.file.pathlike;
                const fd: bun.FD = if (pathlike == .fd) pathlike.fd else brk: {
                    let file_path: bun.PathBuffer = undefined;
                    const path = pathlike.path.sliceZ(&file_path);
                    switch (bun.sys.open(
                        path,
                        bun.O.WRONLY | bun.O.CREAT | bun.O.NONBLOCK,
                        write_permissions,
                    )) {
                        .result result => {
                            break :brk result;
                        },
                        .err e => {
                            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, e.withPath(path).toJS(globalThis)?);
                        },
                    }
                };

                const is_stdout_or_stderr = brk: {
                    if (pathlike != .fd) {
                        break :brk false;
                    }

                    if (rare := globalThis.bunVM().rare_data) {
                        if (store == rare.stdout_store) {
                            break :brk true;
                        }

                        if (store == rare.stderr_store) {
                            break :brk true;
                        }
                    }

                    if (tag := fd.stdioTag()) {
                        break :brk switch (tag) {
                            .std_out, .std_err => true,
                            else => false,
                        };
                    }
                    break :brk false;
                };
                let sink = jsc.WebCore.FileSink.init(fd, _.globalThis.bunVM().eventLoop());
                sink.writer.owns_fd = pathlike != .fd;

                if (is_stdout_or_stderr) {
                    switch (sink.writer.startSync(fd, false)) {
                        .err e => {
                            sink.deref();
                            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, e.toJS(globalThis)?);
                        },
                        else => {},
                    }
                } else {
                    switch (sink.writer.start(fd, true)) {
                        .err e => {
                            sink.deref();
                            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, e.toJS(globalThis)?);
                        },
                        else => {},
                    }
                }

                break :brk_sink sink;
            }

            let sink = jsc.WebCore.FileSink.init(bun.invalid_fd, _.globalThis.bunVM().eventLoop());

            let input_path: jsc.WebCore.PathOrFileDescriptor = brk: {
                if (store.data.file.pathlike == .fd) {
                    break :brk .{ .fd = store.data.file.pathlike.fd };
                } else {
                    break :brk .{
                        .path = bun.handleOom(ZigString.Slice.initDupe(
                            bun.default_allocator,
                            store.data.file.pathlike.path.slice(),
                        )),
                    };
                }
            };
            const _guard_input_path = DeinitGuard<jsc.WebCore.PathOrFileDescriptor>{ ptr: &input_path };

            const stream_start: jsc.WebCore.streams.Start = .{
                .FileSink = .{
                    .input_path = input_path,
                },
            };

            switch (sink.start(stream_start)) {
                .err e => {
                    sink.deref();
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, e.toJS(globalThis)?);
                },
                else => {},
            }
            break :brk_sink sink;
        };
        let signal = &file_sink.signal;

        signal.* = jsc.WebCore.FileSink.JSSink.SinkSignal.init(.zero);
        signal.clear();
        bun.assert(signal.isDead());

        const assignment_result: JSValue = jsc.WebCore.FileSink.JSSink.assignToStream(
            globalThis,
            readable_stream.value,
            file_sink,
            unsafe { @as(*OpaquePtr, &signal.ptr) },
        );

        assignment_result.ensureStillAlive();
        bun.assert(!signal.isDead());

        if (e := assignment_result.toError()) {
            file_sink.deref();
            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, e);
        }

        if (!assignment_result.isEmptyOrUndefinedOrNull()) {
            globalThis.bunVM().drainMicrotasks();

            assignment_result.ensureStillAlive();
            if (promise := assignment_result.asAnyPromise()) {
                let promise_mut = promise;
                switch (promise.status()) {
                    .pending => {
                        const wrapper = FileStreamWrapper.new(.{
                            .promise = jsc.JSPromise.Strong.init(globalThis),
                            .readable_stream_ref = jsc.WebCore.ReadableStream.Strong.init(readable_stream, globalThis),
                            .sink = file_sink,
                        });
                        const promise_value = wrapper.promise.value();

                        assignment_result.then(
                            globalThis,
                            wrapper,
                            onFileStreamResolveRequestStream,
                            onFileStreamRejectRequestStream,
                        )?;
                        return promise_value;
                    },
                    .fulfilled => {
                        file_sink.deref();
                        readable_stream.done(globalThis);
                        return JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(0));
                    },
                    .rejected => {
                        file_sink.deref();
                        readable_stream.cancel(globalThis);
                        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, promise_mut.result(globalThis.vm()));
                    },
                }
            } else {
                file_sink.deref();
                readable_stream.cancel(globalThis);
                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, assignment_result);
            }
        }
        file_sink.deref();

        return JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(0));
    }

    static fn writeFile(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        const arguments = callframe.arguments();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<jsc.CallFrame.ArgumentsSlice>{ ptr: &args };

        let path_or_blob = PathOrBlob.fromJSNoCopy(globalThis, &args)?;
        const _guard_pob = PathOrBlobGuard{ ptr: &path_or_blob };
        if (path_or_blob == .blob) {
            validateWritableBlob(globalThis, &path_or_blob.blob)?;
        }

        const data_opt = args.nextEat();
        if (data_opt == null) {
            return globalThis.throwInvalidArguments("Bun.write(pathOrFdOrBlob, blob) expects a Blob-y thing to write", .{});
        }
        const data = data_opt.?;

        let mkdirp_if_not_exists: bool? = null;
        let mode: bun.Mode? = null;
        const options = args.nextEat();
        if (options_object := options) {
            if (options_object.isObject()) {
                if (create_directory := options_object.getTruthy(globalThis, "createPath")?) {
                    if (!create_directory.isBoolean()) {
                        return globalThis.throwInvalidArgumentType("write", "options.createPath", "boolean");
                    }
                    mkdirp_if_not_exists = create_directory.toBoolean();
                }
                if (mode_value := options_object.get(globalThis, "mode")?) {
                    if (!mode_value.isEmptyOrUndefinedOrNull()) {
                        if (!mode_value.isNumber()) {
                            return globalThis.throwInvalidArgumentType("write", "options.mode", "number");
                        }
                        const mode_int = mode_value.toInt64();
                        if (mode_int < 0 || mode_int > 0o777) {
                            return globalThis.throwRangeError(mode_int, .{ .field_name = "mode", .min = 0, .max = 0o777 });
                        }
                        mode = @intCast(mode_int);
                    }
                }
            } else if (!options_object.isEmptyOrUndefinedOrNull()) {
                return globalThis.throwInvalidArgumentType("write", "options", "object");
            }
        }
        return writeFileInternal(globalThis, &path_or_blob, data, .{
            .mkdirp_if_not_exists = mkdirp_if_not_exists,
            .extra_options = options,
            .mode = mode,
        });
    }
}

@export @c fn Bun__Blob__getSizeForBindings(blob: *Blob): usize {
    return blob.getSizeForBindings();
}

@export @c fn Blob__ref(blob: *Blob) {
    blob.ref_count.increment();
}

@export @c fn Blob__deref(blob: *Blob) {
    if (blob.ref_count.decrement() == .should_destroy) {
        blob.ref_count.increment();
        blob.deinit();
    }
}

@export @c fn Blob__dupeFromJS(value: JSValue): (*Blob)? {
    const b_opt = Blob.fromJS(value);
    if (b_opt == null) return null;
    return Blob.new(b_opt.?.dupeWithContentType(true));
}

@export @c fn Blob__setAsFile(blob: *Blob, path_str: *String) {
    blob.is_jsdom_file = true;
    if (*store := blob.store) {
        if (store.data == .bytes) {
            if (store.data.bytes.stored_name.len == 0) {
                const utf8 = path_str.toUTF8Bytes(default_allocator);
                store.data.bytes.stored_name = bun.PathString.init(utf8);
            }
        }
    }
}

@export @c fn Blob__dupe(blob: *Blob): *Blob {
    return Blob.new(blob.dupeWithContentType(true));
}

@export @c fn Blob__getFileNameString(blob: *Blob): String {
    if (filename := blob.getFileName()) {
        return String.fromBytes(filename);
    }
    return String.empty;
}

@export @c fn Blob__getDataPtr(value: JSValue): *!? {
    const blob_opt = Blob.fromJS(value);
    if (blob_opt == null) return null;
    const data = blob_opt.?.sharedView();
    if (data.len == 0) return null;
    return unsafe { @constCast(data.ptr) };
}

@export @c fn Blob__getSize(value: JSValue): usize {
    const blob_opt = Blob.fromJS(value);
    if (blob_opt == null) return 0;
    const data = blob_opt.?.sharedView();
    return data.len;
}

@export @c fn Blob__fromBytes(globalThis: *JSGlobalObject, ptr_arg: ([*]const u8)?, len: usize): *Blob {
    if (ptr_arg == null || len == 0) {
        return Blob.new(Blob.initEmpty(globalThis));
    }
    const bytes = bun.handleOom(default_allocator.dupe(u8, unsafe { ptr_arg.?[0..len] }));
    const store = BlobStore.init(bytes, default_allocator);
    return Blob.new(Blob.initWithStore(store, globalThis));
}

@export @c fn Blob__fromBytesWithType(globalThis: *JSGlobalObject, ptr_arg: ([*]const u8)?, len: usize, mime: [*:0]const u8): *Blob {
    const blob = Blob__fromBytes(globalThis, ptr_arg, len);
    const mime_slice = std.mem.span(mime);
    if (mime_slice.len > 0) {
        blob.content_type = mime_slice;
        blob.content_type_was_set = true;
    }
    return blob;
}

pub const MmapFreeInterface = if (Environment.isPosix) struct {
    static fn alloc(_: *!, _: usize, _: std.mem.Alignment, _: usize): ([*]u8)? {
        return null;
    }
    static fn free(_: *!, buf: []u8, _: std.mem.Alignment, _: usize) {
        sys.munmap(unsafe { @ptrCast(@alignCast(buf)) }).unwrap() catch |e| {
            Output.debugWarn("Blob mmap-store munmap failed: {}", .{e});
        };
    }
    const vtable_: std.mem.Allocator.VTable = .{
        alloc: &alloc,
        resize: &std.mem.Allocator.noResize,
        remap: &std.mem.Allocator.noRemap,
        free: &free,
    };
    pub const vtable: *const std.mem.Allocator.VTable = &vtable_;
} else struct {};

@export @c fn Blob__fromMmapWithType(globalThis: *JSGlobalObject, ptr_arg: [*]u8, len: usize, mime: [*:0]const u8): *Blob {
    if (!Environment.isPosix) {
        return Blob__fromBytesWithType(globalThis, ptr_arg, len, mime);
    }
    const store = BlobStore.init(unsafe { ptr_arg[0..len] }, .{
        ptr: undefined,
        vtable: MmapFreeInterface.vtable,
    });
    const blob = Blob.new(Blob.initWithStore(store, globalThis));
    const mime_slice = std.mem.span(mime);
    if (mime_slice.len > 0) {
        blob.content_type = mime_slice;
        blob.content_type_was_set = true;
    }
    return blob;
}

pub fn JSDOMFile__construct_(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!*Blob {
    jsc.markBinding(@src());
    const allocator = default_allocator;
    let blob: Blob = undefined;
    let arguments = callframe.arguments_old(3);
    const args = arguments.slice();

    if (args.len < 2) {
        return globalThis.throwInvalidArguments("new File(bits, name) expects at least 2 arguments", .{});
    }
    {
        let name_value_str = String.fromJS(args[1], globalThis)?;
        const _guard_name = DerefGuard<String>{ ptr: &name_value_str };

        blob = Blob.get(globalThis, args[0], false, true)?;
        if (*store_ := blob.store) {
            switch (store_.data) {
                .bytes => |*bytes| {
                    bytes.stored_name = bun.PathString.init(
                        name_value_str.toUTF8Bytes(default_allocator),
                    );
                },
                .s3, .file => {
                    blob.name = name_value_str.dupeRef();
                },
            }
        } else if (!name_value_str.isEmpty()) {
            blob.store = BlobStore.new(.{
                data: .{
                    bytes: BlobStore.Bytes.initEmptyWithName(
                        bun.PathString.init(name_value_str.toUTF8Bytes(default_allocator)),
                        allocator,
                    ),
                },
                allocator: allocator,
                ref_count: .init(1),
            });
        }
    }

    let set_last_modified = false;

    if (args.len > 2) {
        const options = args[2];
        if (options.isObject()) {
            if (content_type := options.get(globalThis, "type")?) {
                inner: {
                    if (content_type.isString()) {
                        let content_type_str = content_type.toSlice(globalThis, default_allocator)?;
                        const _guard_ct = DeinitGuard<ZigString.Slice>{ ptr: &content_type_str };
                        const slice = content_type_str.slice();
                        if (!strings.isAllASCII(slice)) {
                            break :inner;
                        }
                        blob.content_type_was_set = true;

                        if (mime := globalThis.bunVM().mimeType(slice)) {
                            blob.content_type = mime.value;
                            break :inner;
                        }
                        const content_type_buf = bun.handleOom(allocator.alloc(u8, slice.len));
                        blob.content_type = strings.copyLowercase(slice, content_type_buf);
                        blob.content_type_allocated = true;
                    }
                }
            }

            if (last_modified := options.getTruthy(globalThis, "lastModified")?) {
                set_last_modified = true;
                blob.last_modified = last_modified.coerce(f64, globalThis)?;
            }
        }
    }

    if (!set_last_modified) {
        blob.last_modified = @floatFromInt(std.time.milliTimestamp());
    }

    if (blob.content_type.len == 0) {
        blob.content_type = "";
        blob.content_type_was_set = false;
    }

    let blob_ = Blob.new(blob);
    blob_.is_jsdom_file = true;
    return blob_;
}

@export @c fn JSDOMFile__construct(globalThis: *JSGlobalObject, callframe: *CallFrame) callconv(jsc.conv): (*Blob)? {
    return JSDOMFile__construct_(globalThis, callframe) catch |e| {
        if (e == error.OutOfMemory) {
            if (globalThis.throwOutOfMemory() == error.JSError) {}
        }
        return null;
    };
}

@export @c fn JSDOMFile__hasInstance(_: JSValue, _: *JSGlobalObject, value: JSValue) callconv(jsc.conv): bool {
    jsc.markBinding(@src());
    const blob_opt = value.as(Blob);
    if (blob_opt == null) return false;
    return blob_opt.?.is_jsdom_file;
}
