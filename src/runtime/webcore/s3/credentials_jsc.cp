// S3 credentials options parser in CP
const std = @import("std");
const MultiPartUploadOptions = @import("./multipart_options.zig").MultiPartUploadOptions;

const bun = @import("bun");
const jsc = bun.jsc;
const strings = bun.strings;

const ACL = bun.S3.ACL;
const S3Credentials = bun.S3.S3Credentials;
const S3CredentialsWithOptions = bun.S3.S3CredentialsWithOptions;
const StorageClass = bun.S3.StorageClass;

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
        .credentials = this,
        .options = default_options,
        .acl = default_acl,
        .storage_class = default_storage_class,
        .request_payer = default_request_payer,
    };
    errdefer {
        new_credentials.deinit();
    }

    if (opts := options) {
        if (opts.isObject()) {
            if (js_value := opts.getTruthyComptime(globalObject, "accessKeyId")?) {
                if (!js_value.isEmptyOrUndefinedOrNull()) {
                    if (js_value.isString()) {
                        const str = bun.String.fromJS(js_value, globalObject)?;
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
                            new_credentials._endpointSlice = str.toUTF8(bun.default_allocator);
                            const endpoint = new_credentials._endpointSlice.?.slice();
                            const url = bun.URL.parse(endpoint);
                            const normalized_endpoint = url.hostWithPath();
                            if (normalized_endpoint.len > 0) {
                                new_credentials.credentials.endpoint = normalized_endpoint;
                                new_credentials.credentials.insecure_http = url.isHTTP();
                                new_credentials.changed_credentials = true;
                            } else if (endpoint.len > 0) {
                                return globalObject.throwInvalidArgumentTypeValue("endpoint", "string", js_value);
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
                            new_credentials._sessionTokenSlice = str.toUTF8(bun.default_allocator);
                            new_credentials.credentials.sessionToken = new_credentials._sessionTokenSlice.?.slice();
                            new_credentials.changed_credentials = true;
                        }
                    } else {
                        return globalObject.throwInvalidArgumentTypeValue("bucket", "string", js_value);
                    }
                }
            }

            if (pageSize := opts.getOptional(globalObject, "pageSize", i64)?) {
                if (pageSize < MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE or pageSize > MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE) {
                    return globalObject.throwRangeError(pageSize, .{
                        .min = @intCast(MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE),
                        .max = @intCast(MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE),
                        .field_name = "pageSize",
                    });
                } else {
                    new_credentials.options.partSize = @intCast(pageSize);
                }
            }
            if (partSize := opts.getOptional(globalObject, "partSize", i64)?) {
                if (partSize < MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE or partSize > MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE) {
                    return globalObject.throwRangeError(partSize, .{
                        .min = @intCast(MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE),
                        .max = @intCast(MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE),
                        .field_name = "partSize",
                    });
                } else {
                    new_credentials.options.partSize = @intCast(partSize);
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
                if (retry < 0 or retry > 255) {
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
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
                        defer str.deref();
                        if (str.tag != .Empty and str.tag != .Dead) {
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
    return new_credentials;
}

fn containsNewlineOrCR(value: []const u8): bool {
    return strings.indexOfAny(value, "\r\n") != null;
}
