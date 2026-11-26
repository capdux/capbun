import bun from "bun";
import { Output, S3, strings, http, jsc, JSError, JSTerminated, default_allocator, handleOom, destroy, TrivialNew } from "bun";
import { S3Client } from "./S3Client.zig";
import * as S3ClientNS from "./S3Client.zig";
import { S3Stat } from "./S3Stat.zig";

const JSTerminatedError = JSTerminated;
const Method = http.Method;
const JSValue = jsc.JSValue;
const JSGlobalObject = jsc.JSGlobalObject;
const CallFrame = jsc.CallFrame;
const JSPromise = jsc.JSPromise;
const Blob = jsc.WebCore.Blob;
const PathOrBlob = jsc.Node.PathOrBlob;

struct DeinitGuard<T> {
    ptr: *T!;
    ~init() { unsafe { _.ptr.deinit(); } }
}

struct ErrDeinitGuard<T> {
    ptr: (*T!)?;
    ~errinit() { if (p := _.ptr) unsafe { p.deinit(); } }
    fn disarm(*_) { _.ptr = null; }
}

struct PathOrBlobGuard {
    ptr: (*PathOrBlob!)?;
    ~errinit() {
        if (p := _.ptr) {
            unsafe { if (p.* == .path) p.path.deinit(); }
        }
    }
    fn disarm(*_) { _.ptr = null; }
}

pub fn writeFormat(
    s3: *Blob.Store.S3,
    @comptime Formatter: type,
    formatter: *Formatter,
    writer: anytype,
    @comptime enable_ansi_colors: bool,
    content_type: []const u8,
    offset: usize,
)! {
    writer.writeAll(@comptime(Output.prettyFmt("<r>S3Ref<r>", enable_ansi_colors)))?;
    const credentials = s3.getCredentials();
    const bucket_name = if (credentials.virtual_hosted_style && credentials.endpoint.len > 0)
        S3.S3Credentials.guessBucket(credentials.endpoint) ?? credentials.bucket
    else
        credentials.bucket;

    if (bucket_name.len > 0) {
        writer.print(
            @comptime(Output.prettyFmt(" (<green>\"{s}/{s}\"<r>)<r> {{", enable_ansi_colors)),
            .{ bucket_name, s3.path() },
        )?;
    } else {
        writer.print(
            @comptime(Output.prettyFmt(" (<green>\"{s}\"<r>)<r> {{", enable_ansi_colors)),
            .{ s3.path() },
        )?;
    }

    if (content_type.len > 0) {
        writer.writeAll("\n")?;
        formatter.indent += 1;
        defer formatter.indent = if (formatter.indent > 0) formatter.indent - 1 else 0;

        formatter.writeIndent(@typeOf(writer), writer)?;
        writer.print(
            @comptime(Output.prettyFmt("type<d>:<r> <green>\"{s}\"<r>", enable_ansi_colors)),
            .{ content_type },
        )?;

        formatter.printComma(@typeOf(writer), writer, enable_ansi_colors)?;
        if (offset > 0) {
            writer.writeAll("\n")?;
        }
    }

    if (offset > 0) {
        formatter.indent += 1;
        defer formatter.indent = if (formatter.indent > 0) formatter.indent - 1 else 0;

        formatter.writeIndent(@typeOf(writer), writer)?;
        writer.print(
            @comptime(Output.prettyFmt("offset<d>:<r> <yellow>{d}<r>", enable_ansi_colors)),
            .{ offset },
        )?;

        formatter.printComma(@typeOf(writer), writer, enable_ansi_colors)?;
    }

    S3ClientNS.writeFormatCredentials(credentials, s3.options, s3.acl, Formatter, formatter, writer, enable_ansi_colors)?;
    formatter.writeIndent(@typeOf(writer), writer)?;
    writer.writeAll("}")?;
    formatter.resetLine();
}

pub fn presign(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
    const arguments = callframe.arguments_old(3).slice();
    let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
    const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

    let path_or_blob = PathOrBlob.fromJSNoCopy(globalThis, &args)?;
    let _guard_pob = PathOrBlobGuard{ ptr: &path_or_blob };

    if (path_or_blob == .blob && (path_or_blob.blob.store == null || path_or_blob.blob.store.?.data != .s3)) {
        return globalThis.throwInvalidArguments("Expected a S3 or path to presign", .{});
    }

    match (path_or_blob) {
        .path => |path| {
            if (path == .fd) {
                return globalThis.throwInvalidArguments("Expected a S3 or path to presign", .{});
            }
            const options = args.nextEat();
            let blob = constructS3FileInternalStore(globalThis, path.path, options)?;
            _guard_pob.disarm();
            const _guard_blob = DeinitGuard<Blob>{ ptr: &blob };
            return getPresignUrlFrom(&blob, globalThis, options);
        },
        .blob => return getPresignUrlFrom(&path_or_blob.blob, globalThis, args.nextEat()),
    }
}

pub fn unlink(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
    const arguments = callframe.arguments_old(3).slice();
    let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
    const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

    let path_or_blob = PathOrBlob.fromJSNoCopy(globalThis, &args)?;
    let _guard_pob = PathOrBlobGuard{ ptr: &path_or_blob };
    if (path_or_blob == .blob && (path_or_blob.blob.store == null || path_or_blob.blob.store.?.data != .s3)) {
        return globalThis.throwInvalidArguments("Expected a S3 or path to delete", .{});
    }

    match (path_or_blob) {
        .path => |path| {
            if (path == .fd) {
                return globalThis.throwInvalidArguments("Expected a S3 or path to delete", .{});
            }
            const options = args.nextEat();
            let blob = constructS3FileInternalStore(globalThis, path.path, options)?;
            _guard_pob.disarm();
            const _guard_blob = DeinitGuard<Blob>{ ptr: &blob };
            return blob.store.?.data.s3.unlink(blob.store.?, globalThis, options);
        },
        .blob => |blob| {
            return blob.store.?.data.s3.unlink(blob.store.?, globalThis, args.nextEat());
        },
    }
}

pub fn write(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
    const arguments = callframe.arguments_old(3).slice();
    let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
    const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

    let path_or_blob = PathOrBlob.fromJSNoCopy(globalThis, &args)?;
    let _guard_pob = PathOrBlobGuard{ ptr: &path_or_blob };

    if (path_or_blob == .blob && (path_or_blob.blob.store == null || path_or_blob.blob.store.?.data != .s3)) {
        return globalThis.throwInvalidArguments("Expected a S3 or path to upload", .{});
    }

    const data_opt = args.nextEat();
    if (data_opt == null) {
        return globalThis.ERR(.MISSING_ARGS, "Expected a Blob-y thing to upload", .{}).throw();
    }
    const data = data_opt.?;

    match (path_or_blob) {
        .path => |path| {
            const options = args.nextEat();
            if (path == .fd) {
                return globalThis.throwInvalidArguments("Expected a S3 or path to upload", .{});
            }
            let blob = constructS3FileInternalStore(globalThis, path.path, options)?;
            _guard_pob.disarm();
            const _guard_blob = DeinitGuard<Blob>{ ptr: &blob };

            let blob_internal: PathOrBlob = .{ .blob = blob };
            return Blob.writeFileInternal(globalThis, &blob_internal, data, .{
                .mkdirp_if_not_exists = false,
                .extra_options = options,
            });
        },
        .blob => return Blob.writeFileInternal(globalThis, &path_or_blob, data, .{
            .mkdirp_if_not_exists = false,
            .extra_options = args.nextEat(),
        }),
    }
}

pub fn size(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
    const arguments = callframe.arguments_old(3).slice();
    let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
    const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

    let path_or_blob = PathOrBlob.fromJSNoCopy(globalThis, &args)?;
    let _guard_pob = PathOrBlobGuard{ ptr: &path_or_blob };

    if (path_or_blob == .blob && (path_or_blob.blob.store == null || path_or_blob.blob.store.?.data != .s3)) {
        return globalThis.throwInvalidArguments("Expected a S3 or path to get size", .{});
    }

    match (path_or_blob) {
        .path => |path| {
            const options = args.nextEat();
            if (path == .fd) {
                return globalThis.throwInvalidArguments("Expected a S3 or path to get size", .{});
            }
            let blob = constructS3FileInternalStore(globalThis, path.path, options)?;
            _guard_pob.disarm();
            const _guard_blob = DeinitGuard<Blob>{ ptr: &blob };

            return S3BlobStatTask.size(globalThis, &blob);
        },
        .blob *blob => {
            return Blob.getSize(blob, globalThis);
        },
    }
}

pub fn exists(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
    const arguments = callframe.arguments_old(3).slice();
    let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
    const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

    let path_or_blob = PathOrBlob.fromJSNoCopy(globalThis, &args)?;
    let _guard_pob = PathOrBlobGuard{ ptr: &path_or_blob };

    if (path_or_blob == .blob && (path_or_blob.blob.store == null || path_or_blob.blob.store.?.data != .s3)) {
        return globalThis.throwInvalidArguments("Expected a S3 or path to check if it exists", .{});
    }

    match (path_or_blob) {
        .path => |path| {
            const options = args.nextEat();
            if (path == .fd) {
                return globalThis.throwInvalidArguments("Expected a S3 or path to check if it exists", .{});
            }
            let blob = constructS3FileInternalStore(globalThis, path.path, options)?;
            _guard_pob.disarm();
            const _guard_blob = DeinitGuard<Blob>{ ptr: &blob };

            return S3BlobStatTask.exists(globalThis, &blob);
        },
        .blob *blob => {
            return Blob.getExists(blob, globalThis, callframe);
        },
    }
}

fn constructS3FileInternalStore(
    globalObject: *JSGlobalObject,
    path: jsc.Node.PathLike,
    options: JSValue?,
): JSError!Blob {
    const existing_credentials = globalObject.bunVM().transpiler.env.getS3Credentials();
    return constructS3FileWithS3Credentials(globalObject, path, options, existing_credentials);
}

pub fn constructS3FileWithS3CredentialsAndOptions(
    globalObject: *JSGlobalObject,
    path: jsc.Node.PathLike,
    options: JSValue?,
    default_credentials: *S3.S3Credentials,
    default_options: S3.MultiPartUploadOptions,
    default_acl: S3.ACL?,
    default_storage_class: S3.StorageClass?,
    default_request_payer: bool,
): JSError!Blob {
    let aws_options = S3.S3Credentials.getCredentialsWithOptions(
        default_credentials.*,
        default_options,
        options,
        default_acl,
        default_storage_class,
        default_request_payer,
        globalObject,
    )?;
    const _guard_aws = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &aws_options };

    const store = brk: {
        if (aws_options.changed_credentials) {
            break :brk handleOom(Blob.Store.initS3(path, null, aws_options.credentials, default_allocator));
        } else {
            break :brk handleOom(Blob.Store.initS3WithReferencedCredentials(path, null, default_credentials, default_allocator));
        }
    };
    let _guard_store = ErrDeinitGuard<Blob.Store>{ ptr: store };
    store.data.s3.options = aws_options.options;
    store.data.s3.acl = aws_options.acl;
    store.data.s3.storage_class = aws_options.storage_class;
    store.data.s3.request_payer = aws_options.request_payer;

    let blob = Blob.initWithStore(store, globalObject);
    if (opts := options) {
        if (opts.isObject()) {
            if (file_type := opts.getTruthyComptime(globalObject, "type")?) {
                inner: {
                    if (file_type.isString()) {
                        const allocator = default_allocator;
                        let str = file_type.toSlice(globalObject, default_allocator)?;
                        const _guard_str = DeinitGuard<jsc.ZigString.Slice>{ ptr: &str };
                        const slice = str.slice();
                        for (c of slice) {
                            if (c < 0x20 || c >= 0x7f) {
                                break :inner;
                            }
                        }
                        blob.content_type_was_set = true;
                        if (entry := globalObject.bunVM().mimeType(str.slice())) {
                            blob.content_type = entry.value;
                            break :inner;
                        }
                        const content_type_buf = handleOom(allocator.alloc(u8, slice.len));
                        blob.content_type = strings.copyLowercase(slice, content_type_buf);
                        blob.content_type_allocated = true;
                    }
                }
            }
        }
    }
    _guard_store.disarm();
    return blob;
}

pub fn constructS3FileWithS3Credentials(
    globalObject: *JSGlobalObject,
    path: jsc.Node.PathLike,
    options: JSValue?,
    existing_credentials: S3.S3Credentials,
): JSError!Blob {
    let aws_options = S3.S3Credentials.getCredentialsWithOptions(existing_credentials, .{}, options, null, null, false, globalObject)?;
    const _guard_aws = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &aws_options };
    const store = handleOom(Blob.Store.initS3(path, null, aws_options.credentials, default_allocator));
    let _guard_store = ErrDeinitGuard<Blob.Store>{ ptr: store };
    store.data.s3.options = aws_options.options;
    store.data.s3.acl = aws_options.acl;
    store.data.s3.storage_class = aws_options.storage_class;
    store.data.s3.request_payer = aws_options.request_payer;

    let blob = Blob.initWithStore(store, globalObject);
    if (opts := options) {
        if (opts.isObject()) {
            if (file_type := opts.getTruthyComptime(globalObject, "type")?) {
                inner: {
                    if (file_type.isString()) {
                        const allocator = default_allocator;
                        let str = file_type.toSlice(globalObject, default_allocator)?;
                        const _guard_str = DeinitGuard<jsc.ZigString.Slice>{ ptr: &str };
                        const slice = str.slice();
                        for (c of slice) {
                            if (c < 0x20 || c >= 0x7f) {
                                break :inner;
                            }
                        }
                        blob.content_type_was_set = true;
                        if (entry := globalObject.bunVM().mimeType(str.slice())) {
                            blob.content_type = entry.value;
                            break :inner;
                        }
                        const content_type_buf = handleOom(allocator.alloc(u8, slice.len));
                        blob.content_type = strings.copyLowercase(slice, content_type_buf);
                        blob.content_type_allocated = true;
                    }
                }
            }
        }
    }
    _guard_store.disarm();
    return blob;
}

fn constructS3FileInternal(
    globalObject: *JSGlobalObject,
    path: jsc.Node.PathLike,
    options: JSValue?,
): JSError!*Blob {
    return Blob.new(constructS3FileInternalStore(globalObject, path, options)?);
}

export class S3BlobStatTask {
    promise: JSPromise.Strong;
    global: *JSGlobalObject;
    store: *Blob.Store;

    pub const new = TrivialNew(S3BlobStatTask);

    pub static fn onS3ExistsResolved(result: S3.S3StatResult, task: *S3BlobStatTask): JSTerminatedError!void {
        const _guard_this = DeinitGuard<S3BlobStatTask>{ ptr: task };
        match (result) {
            .not_found => {
                task.promise.resolve(task.global, .false)?;
            },
            .success => |_| {
                task.promise.resolve(task.global, .true)?;
            },
            .failure => |err| {
                task.promise.reject(task.global, err.toJSWithAsyncStack(task.global, task.store.data.s3.path(), task.promise.get()))?;
            },
        }
    }

    pub static fn onS3SizeResolved(result: S3.S3StatResult, task: *S3BlobStatTask): JSTerminatedError!void {
        const _guard_this = DeinitGuard<S3BlobStatTask>{ ptr: task };

        match (result) {
            .success => |stat_result| {
                task.promise.resolve(task.global, JSValue.jsNumber(stat_result.size))?;
            },
            .not_found, .failure => |err| {
                task.promise.reject(task.global, err.toJSWithAsyncStack(task.global, task.store.data.s3.path(), task.promise.get()))?;
            },
        }
    }

    pub static fn onS3StatResolved(result: S3.S3StatResult, task: *S3BlobStatTask): JSError!void {
        const _guard_this = DeinitGuard<S3BlobStatTask>{ ptr: task };
        const globalThis = task.global;
        match (result) {
            .success => |stat_result| {
                const stat_obj = S3Stat.init(
                    stat_result.size,
                    stat_result.etag,
                    stat_result.contentType,
                    stat_result.lastModified,
                    globalThis,
                )?;
                task.promise.resolve(globalThis, stat_obj.toJS(globalThis))?;
            },
            .not_found, .failure => |err| {
                task.promise.reject(globalThis, err.toJSWithAsyncStack(globalThis, task.store.data.s3.path(), task.promise.get()))?;
            },
        }
    }

    pub static fn exists(globalThis: *JSGlobalObject, blob: *Blob): JSTerminatedError!JSValue {
        if (blob.store == null || blob.store.?.data != .s3) {
            return jsc.JSPromise.rejectedPromise(globalThis, globalThis.ERR(.INVALID_THIS, "Expected a S3File instance", .{}).toJS()).toJS();
        }
        const task = S3BlobStatTask.new(.{
            .promise = JSPromise.Strong.init(globalThis),
            .store = blob.store.?,
            .global = globalThis,
        });
        task.store.ref();
        let _guard_task = ErrDeinitGuard<S3BlobStatTask>{ ptr: task };
        const promise = task.promise.value();
        const s3_store = &blob.store.?.data.s3;
        const credentials = s3_store.getCredentials();
        const path = s3_store.path();
        const env = globalThis.bunVM().transpiler.env;
        const maybe_proxy = env.getHttpProxy(true, null, null);
        let proxy_url: ([]const u8)? = null;
        if (proxy := maybe_proxy) {
            proxy_url = proxy.href;
        }

        S3.stat(credentials, path, unsafe { @ptrCast(&S3BlobStatTask.onS3ExistsResolved) }, task, proxy_url, s3_store.request_payer)?;
        _guard_task.disarm();
        return promise;
    }

    pub static fn stat(globalThis: *JSGlobalObject, blob: *Blob): JSTerminatedError!JSValue {
        if (blob.store == null || blob.store.?.data != .s3) {
            return jsc.JSPromise.rejectedPromise(globalThis, globalThis.ERR(.INVALID_THIS, "Expected a S3File instance", .{}).toJS()).toJS();
        }
        const task = S3BlobStatTask.new(.{
            .promise = JSPromise.Strong.init(globalThis),
            .store = blob.store.?,
            .global = globalThis,
        });
        task.store.ref();
        let _guard_task = ErrDeinitGuard<S3BlobStatTask>{ ptr: task };
        const promise = task.promise.value();
        const s3_store = &blob.store.?.data.s3;
        const credentials = s3_store.getCredentials();
        const path = s3_store.path();
        const env = globalThis.bunVM().transpiler.env;
        const maybe_proxy = env.getHttpProxy(true, null, null);
        let proxy_url: ([]const u8)? = null;
        if (proxy := maybe_proxy) {
            proxy_url = proxy.href;
        }

        S3.stat(credentials, path, unsafe { @ptrCast(&S3BlobStatTask.onS3StatResolved) }, task, proxy_url, s3_store.request_payer)?;
        _guard_task.disarm();
        return promise;
    }

    pub static fn size(globalThis: *JSGlobalObject, blob: *Blob): JSTerminatedError!JSValue {
        if (blob.store == null || blob.store.?.data != .s3) {
            return jsc.JSPromise.rejectedPromise(globalThis, globalThis.ERR(.INVALID_THIS, "Expected a S3File instance", .{}).toJS()).toJS();
        }
        const task = S3BlobStatTask.new(.{
            .promise = JSPromise.Strong.init(globalThis),
            .store = blob.store.?,
            .global = globalThis,
        });
        task.store.ref();
        let _guard_task = ErrDeinitGuard<S3BlobStatTask>{ ptr: task };
        const promise = task.promise.value();
        const s3_store = &blob.store.?.data.s3;
        const credentials = s3_store.getCredentials();
        const path = s3_store.path();
        const env = globalThis.bunVM().transpiler.env;
        const maybe_proxy = env.getHttpProxy(true, null, null);
        let proxy_url: ([]const u8)? = null;
        if (proxy := maybe_proxy) {
            proxy_url = proxy.href;
        }

        S3.stat(credentials, path, unsafe { @ptrCast(&S3BlobStatTask.onS3SizeResolved) }, task, proxy_url, s3_store.request_payer)?;
        _guard_task.disarm();
        return promise;
    }

    pub fn deinit(*_) {
        _.store.deref();
        _.promise.deinit();
        destroy(_);
    }
}

pub fn getPresignUrlFrom(*_: *Blob, globalThis: *JSGlobalObject, extra_options: JSValue?): JSError!JSValue {
    if (!_.isS3()) {
        return globalThis.ERR(.INVALID_THIS, "presign is only possible for s3:// files", .{}).throw();
    }

    let method: Method = .GET;
    let expires: usize = 86400;

    const s3 = &_.store.?.data.s3;
    let credentialsWithOptions: S3.S3CredentialsWithOptions = .{
        .credentials = s3.getCredentials().*,
        .request_payer = s3.request_payer,
    };
    const _guard_creds = DeinitGuard<S3.S3CredentialsWithOptions>{ ptr: &credentialsWithOptions };

    if (options := extra_options) {
        if (options.isObject()) {
            if (method_ := options.getTruthyComptime(globalThis, "method")?) {
                const maybe_method = Method.fromJS(globalThis, method_)?;
                if (m := maybe_method) {
                    method = m;
                } else {
                    return globalThis.throwInvalidArguments("method must be GET, PUT, DELETE or HEAD when using s3 protocol", .{});
                }
            }
            if (expires_ := options.getOptional(globalThis, "expiresIn", i32)?) {
                if (expires_ <= 0) return globalThis.throwInvalidArguments("expiresIn must be greather than 0", .{});
                expires = @as(usize, expires_);
            }
        }
        const new_creds = s3.getCredentialsWithOptions(options, globalThis)?;
        credentialsWithOptions.deinit();
        credentialsWithOptions = new_creds;
    }
    const path = s3.path();

    let result = credentialsWithOptions.credentials.signRequest(.{
        .path = path,
        .method = method,
        .acl = credentialsWithOptions.acl,
        .storage_class = credentialsWithOptions.storage_class,
        .request_payer = credentialsWithOptions.request_payer,
        .content_disposition = credentialsWithOptions.content_disposition,
        .content_type = credentialsWithOptions.content_type,
    }, false, .{ .expires = expires }) catch sign_err => {
        return S3.throwSignError(sign_err, globalThis);
    };
    const _guard_result = DeinitGuard<S3.S3Credentials.SignResult>{ ptr: &result };
    return bun.String.createUTF8ForJS(globalThis, result.url);
}

pub fn getBucketName(_: *const Blob): ([]const u8)? {
    const store = _.store ?? return null;
    if (store.data != .s3) return null;
    const credentials = store.data.s3.getCredentials();
    let full_path = store.data.s3.path();
    if (strings.startsWith(full_path, "/")) {
        full_path = full_path[1..];
    }
    let bucket: []const u8 = credentials.bucket;

    if (bucket.len == 0) {
        if (end := strings.indexOf(full_path, "/")) {
            bucket = full_path[0..end];
            if (bucket.len > 0) {
                return bucket;
            }
        }
        return null;
    }
    return bucket;
}

@c fn getBucket(*_: *Blob, globalThis: *JSGlobalObject): JSValue {
    if (name := getBucketName(_)) {
        return bun.String.createUTF8ForJS(globalThis, name) catch .zero;
    }
    return .js_undefined;
}

pub fn getPresignUrl(*_: *Blob, globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
    const args = callframe.arguments_old(1);
    return getPresignUrlFrom(_, globalThis, if (args.len > 0) unsafe { args.ptr[0] } else null);
}

@c fn getStat(*_: *Blob, globalThis: *JSGlobalObject, _: *CallFrame): JSValue {
    return S3BlobStatTask.stat(globalThis, _) catch .zero;
}

pub fn stat(globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
    const arguments = callframe.arguments_old(3).slice();
    let args = CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
    const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

    let path_or_blob = PathOrBlob.fromJSNoCopy(globalThis, &args)?;
    let _guard_pob = PathOrBlobGuard{ ptr: &path_or_blob };

    if (path_or_blob == .blob && (path_or_blob.blob.store == null || path_or_blob.blob.store.?.data != .s3)) {
        return globalThis.throwInvalidArguments("Expected a S3 or path to get size", .{});
    }

    match (path_or_blob) {
        .path => |path| {
            const options = args.nextEat();
            if (path == .fd) {
                return globalThis.throwInvalidArguments("Expected a S3 or path to get size", .{});
            }
            let blob = constructS3FileInternalStore(globalThis, path.path, options)?;
            _guard_pob.disarm();
            const _guard_blob = DeinitGuard<Blob>{ ptr: &blob };

            return S3BlobStatTask.stat(globalThis, &blob);
        },
        .blob *blob => {
            return S3BlobStatTask.stat(globalThis, blob);
        },
    }
}

pub fn constructInternalJS(
    globalObject: *JSGlobalObject,
    path: jsc.Node.PathLike,
    options: JSValue?,
): JSError!JSValue {
    const blob = constructS3FileInternal(globalObject, path, options)?;
    return blob.toJS(globalObject);
}

pub fn toJSUnchecked(
    globalObject: *JSGlobalObject,
    blob: *Blob,
): JSValue {
    return BUN__createJSS3FileUnsafely(globalObject, blob);
}

pub fn constructInternal(
    globalObject: *JSGlobalObject,
    callframe: *CallFrame,
): JSError!*Blob {
    const vm = globalObject.bunVM();
    const arguments = callframe.arguments_old(2).slice();
    let args = CallFrame.ArgumentsSlice.init(vm, arguments);
    const _guard_args = DeinitGuard<CallFrame.ArgumentsSlice>{ ptr: &args };

    const path_opt = jsc.Node.PathLike.fromJS(globalObject, &args)?;
    if (path_opt == null) {
        return globalObject.throwInvalidArguments("Expected file path string", .{});
    }
    const path = path_opt.?;
    return constructS3FileInternal(globalObject, path, args.nextEat());
}

@c fn construct(globalObject: *JSGlobalObject, callframe: *CallFrame): (*Blob)? {
    return constructInternal(globalObject, callframe) catch e => {
        if (e == error.OutOfMemory) {
            * = globalObject.throwOutOfMemoryValue();
        }
        return null;
    };
}

@c fn hasInstance(_: JSValue, _: *JSGlobalObject, value: JSValue): bool {
    jsc.markBinding(@src());
    const blob_opt = value.as(Blob);
    if (blob_opt == null) return false;
    const blob = blob_opt.?;
    return blob.isS3();
}

comptime {
    @export(&@field(exports, "JSS3File__presign"), .{ name: "JSS3File__presign" });
    @export(&construct, .{ name: "JSS3File__construct" });
    @export(&hasInstance, .{ name: "JSS3File__hasInstance" });
    @export(&getBucket, .{ name: "JSS3File__bucket" });
    @export(&getStat, .{ name: "JSS3File__stat" });
}

pub struct exports {
    pub const JSS3File__presign = jsc.toJSHostFnWithContext(Blob, getPresignUrl);
    pub const JSS3File__stat = jsc.toJSHostFnWithContext(Blob, getStat);
}

@c extern fn BUN__createJSS3File(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;
@c extern fn BUN__createJSS3FileUnsafely(globalObject: *JSGlobalObject, blob: *Blob): JSValue;

@c pub fn createJSS3File(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue {
    return BUN__createJSS3File(globalObject, callframe);
}
