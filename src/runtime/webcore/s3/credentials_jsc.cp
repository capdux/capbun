// S3 credentials options parser in CP
import * as MultiPartOptionsMod from "./multipart_options.zig";
const MultiPartUploadOptions = MultiPartOptionsMod.MultiPartUploadOptions;

import bun from "bun";
const jsc = bun.jsc;
const strings = bun.strings;

const ACL = bun.S3.ACL;
const S3Credentials = bun.S3.S3Credentials;
const S3CredentialsWithOptions = bun.S3.S3CredentialsWithOptions;
const StorageClass = bun.S3.StorageClass;

struct DeinitGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

struct DerefGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) {
            v.deref();
        }
    }
}

pub fn getCredentialsWithOptions(
    _: S3Credentials,
    default_options: MultiPartUploadOptions,
    options: jsc.JSValue?,
    default_acl: ACL?,
    default_storage_class: StorageClass?,
    default_request_payer: bool,
    globalObject: *jsc.JSGlobalObject,
): bun.JSError!S3CredentialsWithOptions {
    bun.analytics.Features.s3 += 1;

    let new_credentials = S3CredentialsWithOptions{
        .credentials = _,
        .options = default_options,
        .acl = default_acl,
        .storage_class = default_storage_class,
        .request_payer = default_request_payer,
    };
    let new_credentials_guard = DeinitGuard<S3CredentialsWithOptions>{ ptr: unsafe { &new_credentials } };

    if (opts := options) {
        if (opts.isObject()) {
            if (js_value := opts.getTruthyComptime(globalObject, "accessKeyId")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._accessKeyIdSlice = str.toUTF8(bun.default_allocator);
                            new_credentials.credentials.accessKeyId = new_credentials._accessKeyIdSlice.?.slice();
                            new_credentials.changed_credentials = true;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("accessKeyId", "string", js_value);
                    }
                }
            }
            if (js_value := opts.getTruthyComptime(globalObject, "secretAccessKey")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._secretAccessKeySlice = str.toUTF8(bun.default_allocator);
                            new_credentials.credentials.secretAccessKey = new_credentials._secretAccessKeySlice.?.slice();
                            new_credentials.changed_credentials = true;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("secretAccessKey", "string", js_value);
                    }
                }
            }
            if (js_value := opts.getTruthyComptime(globalObject, "region")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._regionSlice = str.toUTF8(bun.default_allocator);
                            new_credentials.credentials.region = new_credentials._regionSlice.?.slice();
                            new_credentials.changed_credentials = true;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("region", "string", js_value);
                    }
                }
            }
            if (js_value := opts.getTruthyComptime(globalObject, "endpoint")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            let raw_slice = str.toUTF8(bun.default_allocator);
                            let _raw_guard = DeinitGuard<jsc.ZigString.Slice>{ ptr: unsafe { &raw_slice } };
                            const raw_endpoint = raw_slice.slice();
                            if (raw_endpoint.len > 0) {
                                const parsed = S3Credentials.parseEndpoint(bun.default_allocator, raw_endpoint) catch {
                                    return globalObject.throwInvalidArgumentTypeValue("endpoint", "string", js_value);
                                };
                                new_credentials._endpointSlice = jsc.ZigString.Slice.init(bun.default_allocator, parsed.href);
                                new_credentials.credentials.endpoint = parsed.host_with_path;
                                new_credentials.credentials.insecure_http = parsed.insecure_http;
                                new_credentials.changed_credentials = true;
                            }
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("endpoint", "string", js_value);
                    }
                }
            }
            if (js_value := opts.getTruthyComptime(globalObject, "bucket")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._bucketSlice = str.toUTF8(bun.default_allocator);
                            new_credentials.credentials.bucket = new_credentials._bucketSlice.?.slice();
                            new_credentials.changed_credentials = true;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("bucket", "string", js_value);
                    }
                }
            }

            if (virtual_hosted_style := opts.getBooleanStrict(globalObject, "virtualHostedStyle")?) {
                new_credentials.credentials.virtual_hosted_style = virtual_hosted_style;
                new_credentials.changed_credentials = true;
            }

            if (js_value := opts.getTruthyComptime(globalObject, "sessionToken")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._sessionTokenSlice = str.toUTF8(bun.default_allocator);
                            new_credentials.credentials.sessionToken = new_credentials._sessionTokenSlice.?.slice();
                            new_credentials.changed_credentials = true;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("bucket", "string", js_value);
                    }
                }
            }

            if (pageSize_val := opts.get(globalObject, "pageSize")?) {
                if (!pageSize_val.isUndefinedOrNull()) {
                    const pageSize_f64 = pageSize_val.coerce(f64, globalObject)?;
                    const pageSize: i64 = if (std.math.isNan(pageSize_f64)) 0 else if (pageSize_f64 >= @as(f64, @floatFromInt(std.math.maxInt(i64)))) std.math.maxInt(i64) else if (pageSize_f64 <= @as(f64, @floatFromInt(std.math.minInt(i64)))) std.math.minInt(i64) else @intFromFloat(@trunc(pageSize_f64));
                    if (pageSize < MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE || pageSize > MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE) {
                        return globalObject.throwRangeError(pageSize, .{
                            .min = @intCast(MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE),
                            .max = @intCast(MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE),
                            .field_name = "pageSize",
                        });
                    } else {
                        new_credentials.options.partSize = @intCast(pageSize);
                    }
                }
            }
            if (partSize_val := opts.get(globalObject, "partSize")?) {
                if (!partSize_val.isUndefinedOrNull()) {
                    const partSize_f64 = partSize_val.coerce(f64, globalObject)?;
                    const partSize: i64 = if (std.math.isNan(partSize_f64)) 0 else if (partSize_f64 >= @as(f64, @floatFromInt(std.math.maxInt(i64)))) std.math.maxInt(i64) else if (partSize_f64 <= @as(f64, @floatFromInt(std.math.minInt(i64)))) std.math.minInt(i64) else @intFromFloat(@trunc(partSize_f64));
                    if (partSize < MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE || partSize > MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE) {
                        return globalObject.throwRangeError(partSize, .{
                            .min = @intCast(MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE),
                            .max = @intCast(MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE),
                            .field_name = "partSize",
                        });
                    } else {
                        new_credentials.options.partSize = @intCast(partSize);
                    }
                }
            }

            if (queueSize := opts.getOptional(globalObject, "queueSize", i32)?) {
                if (queueSize < 1) {
                    return globalObject.throwRangeError(queueSize, .{
                        .min = 1,
                        .field_name = "queueSize",
                    });
                } else {
                    new_credentials.options.queueSize = @intCast(@min(queueSize, std.math.maxInt(u8)));
                }
            }

            if (retry := opts.getOptional(globalObject, "retry", i32)?) {
                if (retry < 0 || retry > 255) {
                    return globalObject.throwRangeError(retry, .{
                        .min = 0,
                        .max = 255,
                        .field_name = "retry",
                    });
                } else {
                    new_credentials.options.retry = @intCast(retry);
                }
            }

            if (acl := opts.getOptionalEnum(globalObject, "acl", ACL)?) {
                new_credentials.acl = acl;
            }

            if (storage_class := opts.getOptionalEnum(globalObject, "storageClass", StorageClass)?) {
                new_credentials.storage_class = storage_class;
            }

            if (js_value := opts.getTruthyComptime(globalObject, "contentDisposition")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._contentDispositionSlice = str.toUTF8(bun.default_allocator);
                            const slice = new_credentials._contentDispositionSlice.?.slice();
                            if (containsNewlineOrCR(slice)) {
                                return globalObject.throwInvalidArguments("contentDisposition must not contain newline characters (CR/LF)", .{});
                            }
                            new_credentials.content_disposition = slice;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("contentDisposition", "string", js_value);
                    }
                }
            }

            if (js_value := opts.getTruthyComptime(globalObject, "type")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._contentTypeSlice = str.toUTF8(bun.default_allocator);
                            const slice = new_credentials._contentTypeSlice.?.slice();
                            if (containsNewlineOrCR(slice)) {
                                return globalObject.throwInvalidArguments("type must not contain newline characters (CR/LF)", .{});
                            }
                            new_credentials.content_type = slice;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("type", "string", js_value);
                    }
                }
            }

            if (js_value := opts.getTruthyComptime(globalObject, "contentEncoding")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        let _str_guard = DerefGuard<bun.String>{ val: str };
                        if (str.tag != .Empty && str.tag != .Dead) {
                            new_credentials._contentEncodingSlice = str.toUTF8(bun.default_allocator);
                            const slice = new_credentials._contentEncodingSlice.?.slice();
                            if (containsNewlineOrCR(slice)) {
                                return globalObject.throwInvalidArguments("contentEncoding must not contain newline characters (CR/LF)", .{});
                            }
                            new_credentials.content_encoding = slice;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("contentEncoding", "string", js_value);
                    }
                }
            }

            if (request_payer := opts.getBooleanStrict(globalObject, "requestPayer")?) {
                new_credentials.request_payer = request_payer;
            }
        }
    }
    new_credentials_guard.ptr = null;
    return new_credentials;
}

fn containsNewlineOrCR(value: []const u8): bool {
    return strings.indexOfAny(value, "\r\n") != null;
}
