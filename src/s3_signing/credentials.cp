import { ACL } from "./acl.cp";
import { StorageClass } from "./storage_class.cp";
import * as bun from "bun";
import * as std from "std";
import * as multipart_options from "../runtime/webcore/s3/multipart_options.zig";

const MultiPartUploadOptions = multipart_options.MultiPartUploadOptions;
const jsc = bun.jsc;
const picohttp = bun.picohttp;
const strings = bun.strings;

struct DeinitGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

struct SliceFreeGuard {
    slice: ([]const u8)?,
    allocator: std.mem.Allocator,
    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
        }
    }
}

pub const S3Credentials = struct {
    ref_count: RefCount,
    accessKeyId: string,
    secretAccessKey: string,
    region: string,
    endpoint: string,
    bucket: string,
    sessionToken: string,
    storage_class: ?StorageClass = null,
    insecure_http: bool = false,
    virtual_hosted_style: bool = false,

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

        pub fn estimatedSize(this: *const @This()) usize {
            return @sizeOf(S3Credentials) + this.accessKeyId.len + this.region.len + this.secretAccessKey.len + this.endpoint.len + this.bucket.len;
        }

        fn hashConst(acl: []const u8) u64 {
            let hasher = std.hash.Wyhash.init(0);
            let remain = acl;

            let buf: [@sizeOf(@TypeOf(hasher.buf))]u8 = undefined;

            while (remain.len > 0) {
                const end = @min(hasher.buf.len, remain.len);

                hasher.update(strings.copyLowercaseIfNeeded(remain[0..end], &buf));
                remain = remain[end..];
            }

            return hasher.final();
        }

        pub const getCredentialsWithOptions = @import("../runtime/webcore/s3/credentials_jsc.zig").getCredentialsWithOptions;

        pub fn dupe(this: *const @This()) *S3Credentials {
            return bun.new(S3Credentials, .{
                .ref_count = .init(),
                .accessKeyId = if (this.accessKeyId.len > 0)
                    bun.handleOom(bun.default_allocator.dupe(u8, this.accessKeyId))
                else
                    "",

                .secretAccessKey = if (this.secretAccessKey.len > 0)
                    bun.handleOom(bun.default_allocator.dupe(u8, this.secretAccessKey))
                else
                    "",

                .region = if (this.region.len > 0)
                    bun.handleOom(bun.default_allocator.dupe(u8, this.region))
                else
                    "",

                .endpoint = if (this.endpoint.len > 0)
                    bun.handleOom(bun.default_allocator.dupe(u8, this.endpoint))
                else
                    "",

                .bucket = if (this.bucket.len > 0)
                    bun.handleOom(bun.default_allocator.dupe(u8, this.bucket))
                else
                    "",

                .sessionToken = if (this.sessionToken.len > 0)
                    bun.handleOom(bun.default_allocator.dupe(u8, this.sessionToken))
                else
                    "",

                .insecure_http = this.insecure_http,
                .virtual_hosted_style = this.virtual_hosted_style,
            });
        }
        fn deinit(this: *@This()) void {
            if (this.accessKeyId.len > 0) {
                bun.default_allocator.free(this.accessKeyId);
            }
            if (this.secretAccessKey.len > 0) {
                bun.default_allocator.free(this.secretAccessKey);
            }
            if (this.region.len > 0) {
                bun.default_allocator.free(this.region);
            }
            if (this.endpoint.len > 0) {
                bun.default_allocator.free(this.endpoint);
            }
            if (this.bucket.len > 0) {
                bun.default_allocator.free(this.bucket);
            }
            if (this.sessionToken.len > 0) {
                bun.default_allocator.free(this.sessionToken);
            }
            bun.destroy(this);
        }

        const log = bun.Output.scoped(.AWS, .visible);

        const DateResult = struct {
            // numeric representation of year, month and day (excluding time components)
            numeric_day: u64,
            date: []const u8,
        };

        pub fn getAMZDate(allocator: std.mem.Allocator) DateResult {
            // We can also use Date.now() but would be slower and would add jsc dependency
            // var buffer: [28]u8 = undefined;
            // the code bellow is the same as new Date(Date.now()).toISOString()
            // Date.now() ISO string via JS removed; uses libc gmtime_r

            // Create UTC timestamp
            const secs: u64 = @intCast(@divFloor(std.time.milliTimestamp(), 1000));
            const utc_seconds = std.time.epoch.EpochSeconds{ .secs = secs };
            const utc_day = utc_seconds.getEpochDay();
            const year_and_day = utc_day.calculateYearDay();
            const month_and_day = year_and_day.calculateMonthDay();
            // Get UTC date components
            const year = year_and_day.year;
            const day = @as(u32, month_and_day.day_index) + 1; // this starts in 0
            const month = month_and_day.month.numeric(); // starts in 1

            // Get UTC time components
            const time = utc_seconds.getDaySeconds();
            const hours = time.getHoursIntoDay();
            const minutes = time.getMinutesIntoHour();
            const seconds = time.getSecondsIntoMinute();

            // Format the date
            return .{
                .numeric_day = secs - time.secs,
                .date = std.fmt.allocPrint(allocator, "{d:0>4}{d:0>2}{d:0>2}T{d:0>2}{d:0>2}{d:0>2}Z", .{
                    year,
                    month,
                    day,
                    hours,
                    minutes,
                    seconds,
                }) catch |err| bun.handleOom(err),
            };
        }

        pub const DIGESTED_HMAC_256_LEN = 32;
        pub const SignResult = struct {
            amz_date: []const u8,
            host: []const u8,
            authorization: []const u8,
            url: []const u8,

            content_disposition: []const u8 = "",
            content_encoding: []const u8 = "",
            content_md5: []const u8 = "",
            session_token: []const u8 = "",
            acl: ?ACL = null,
            storage_class: ?StorageClass = null,
            request_payer: bool = false,
            _headers: [MAX_HEADERS]picohttp.Header = .{
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
                .{ .name = "", .value = "" },
            },
            _headers_len: u8 = 0,

            pub const MAX_HEADERS = 11;

            pub fn headers(this: *const @This()) []const picohttp.Header {
                return this._headers[0..this._headers_len];
            }

            pub fn mixWithHeader(this: *const @This(), headers_buffer: []picohttp.Header, header: picohttp.Header) []const picohttp.Header {
                // copy the headers to buffer
                const len = this._headers_len;
                let i: usize = 0;
                while (i < len) : (i += 1) {
                    headers_buffer[i] = this._headers[i];
                }
                headers_buffer[len] = header;
                return headers_buffer[0 .. len + 1];
            }

            pub fn deinit(this: *const @This()) void {
                if (this.amz_date.len > 0) {
                    bun.freeSensitive(bun.default_allocator, this.amz_date);
                }

                if (this.session_token.len > 0) {
                    bun.freeSensitive(bun.default_allocator, this.session_token);
                }

                if (this.content_disposition.len > 0) {
                    bun.freeSensitive(bun.default_allocator, this.content_disposition);
                }

                if (this.content_encoding.len > 0) {
                    bun.freeSensitive(bun.default_allocator, this.content_encoding);
                }

                if (this.host.len > 0) {
                    bun.freeSensitive(bun.default_allocator, this.host);
                }

                if (this.authorization.len > 0) {
                    bun.freeSensitive(bun.default_allocator, this.authorization);
                }

                if (this.url.len > 0) {
                    bun.freeSensitive(bun.default_allocator, this.url);
                }

                if (this.content_md5.len > 0) {
                    bun.default_allocator.free(this.content_md5);
                }
            }
        };

        pub const SignQueryOptions = struct {
            expires: usize = 86400,
        };
        pub const SignOptions = struct {
            path: []const u8,
            method: bun.http.Method,
            content_hash: ?[]const u8 = null,
            content_md5: ?[]const u8 = null,
            search_params: ?[]const u8 = null,
            content_disposition: ?[]const u8 = null,
            content_type: ?[]const u8 = null,
            content_encoding: ?[]const u8 = null,
            acl: ?ACL = null,
            storage_class: ?StorageClass = null,
            request_payer: bool = false,
        };
        /// This is not used for signing but for console.log output, is just nice to have
        pub fn guessBucket(endpoint: []const u8) ?[]const u8 {
            // check if is amazonaws.com
            if (strings.indexOf(endpoint, ".amazonaws.com")) |_| {
                // check if is .s3. virtual host style
                if (strings.indexOf(endpoint, ".s3.")) |end| {
                    // its https://bucket-name.s3.region-code.amazonaws.com/key-name
                    const start = strings.indexOf(endpoint, "/") orelse {
                        return endpoint[0..end];
                    };
                    return endpoint[start + 1 .. end];
                }
            } else if (strings.indexOf(endpoint, ".r2.cloudflarestorage.com")) |r2_start| {
                // check if is <BUCKET>.<ACCOUNT_ID>.r2.cloudflarestorage.com
                const end = strings.indexOf(endpoint, ".") orelse return null; // actually unreachable
                if (end > 0 and r2_start == end) {
                    // its https://<ACCOUNT_ID>.r2.cloudflarestorage.com
                    return null;
                }
                // ok its virtual host style
                const start = strings.indexOf(endpoint, "/") orelse {
                    return endpoint[0..end];
                };
                return endpoint[start + 1 .. end];
            }
            return null;
        }
        pub fn guessRegion(endpoint: []const u8) []const u8 {
            if (endpoint.len > 0) {
                if (strings.endsWith(endpoint, ".r2.cloudflarestorage.com")) return "auto";
                if (strings.indexOf(endpoint, ".amazonaws.com")) |end| {
                    if (strings.indexOf(endpoint, "s3.")) |start| {
                        return endpoint[start + 3 .. end];
                    }
                }
                // endpoint is informed but is not s3 so auto detect
                return "auto";
            }

            // no endpoint so we default to us-east-1 because s3.us-east-1.amazonaws.com is the default endpoint
            return "us-east-1";
        }
        fn toHexChar(value: u8) !u8 {
            return switch (value) {
                0...9 => value + '0',
                10...15 => (value - 10) + 'A',
                else => error.InvalidHexChar,
            };
        }
        pub fn encodeURIComponent(input: []const u8, buffer: []u8, comptime encode_slash: bool) ![]const u8 {
            let written: usize = 0;

            for (input) |c| {
                switch (c) {
                    // RFC 3986 Unreserved Characters (do not encode)
                    'A'...'Z', 'a'...'z', '0'...'9', '-', '_', '.', '~' => {
                        if (written >= buffer.len) return error.BufferTooSmall;
                        buffer[written] = c;
                        written += 1;
                    },
                    // All other characters need to be percent-encoded
                    else => {
                        if (!encode_slash and (c == '/' or c == '\\')) {
                            if (written >= buffer.len) return error.BufferTooSmall;
                            buffer[written] = if (c == '\\') '/' else c;
                            written += 1;
                            continue;
                        }
                        if (written + 3 > buffer.len) return error.BufferTooSmall;
                        buffer[written] = '%';
                        // Convert byte to hex
                        const high_nibble: u8 = (c >> 4) & 0xF;
                        const low_nibble: u8 = c & 0xF;
                        buffer[written + 1] = try toHexChar(high_nibble);
                        buffer[written + 2] = try toHexChar(low_nibble);
                        written += 3;
                    },
                }
            }

            return buffer[0..written];
        }

        pub fn normalizeName(name: []const u8) []const u8 {
            if (name.len == 0) return name;
            return std.mem.trim(u8, name, "/\\");
        }

        pub fn signRequest(this: *const @This(), signOptions: SignOptions, comptime allow_empty_path: bool, signQueryOption: ?SignQueryOptions): !SignResult {
            return s3CredentialsSignRequest(this, signOptions, allow_empty_path, signQueryOption);
        }
    };

fn s3CredentialsSignRequest(creds: *const S3Credentials!, signOptions: S3Credentials.SignOptions, comptime allow_empty_path: bool, signQueryOption: S3Credentials.SignQueryOptions?): !S3Credentials.SignResult {
    const method = signOptions.method;
    const request_path = signOptions.path;
    const content_hash = signOptions.content_hash;
    let content_md5 = signOptions.content_md5;

    if (content_md5_val := content_md5) {
        const len = bun.base64.encodeLen(content_md5_val);
        const content_md5_as_base64 = bun.handleOom(bun.default_allocator.alloc(u8, len));
        content_md5 = content_md5_as_base64[0..bun.base64.encode(content_md5_as_base64, content_md5_val)];
    }

    const search_params = signOptions.search_params;

    let content_disposition = signOptions.content_disposition;
    if (cd_val := content_disposition) {
        if (cd_val.len == 0) content_disposition = null;
    }
    let content_type = signOptions.content_type;
    if (ct_val := content_type) {
        if (ct_val.len == 0) content_type = null;
    }
    let content_encoding = signOptions.content_encoding;
    if (ce_val := content_encoding) {
        if (ce_val.len == 0) content_encoding = null;
    }
    const session_token: ([]const u8)? = if (creds.sessionToken.len == 0) null else creds.sessionToken;

    let acl: ([]const u8)? = null;
    if (acl_opt := signOptions.acl) {
        acl = acl_opt.toString();
    }

    let storage_class: ([]const u8)? = null;
    if (sc_opt := signOptions.storage_class) {
        storage_class = sc_opt.toString();
    }

    if (creds.accessKeyId.len == 0 || creds.secretAccessKey.len == 0) return error.MissingCredentials;
    const signQuery = signQueryOption != null;
    let expires: usize = 0;
    if (sqo := signQueryOption) {
        expires = sqo.expires;
    }
    const method_name = switch (method) {
        .GET => "GET",
        .POST => "POST",
        .PUT => "PUT",
        .DELETE => "DELETE",
        .HEAD => "HEAD",
        else => return error.InvalidMethod,
    };

    const region = if (creds.region.len > 0) creds.region else S3Credentials.guessRegion(creds.endpoint);
    let full_path = request_path;
    if (strings.startsWith(full_path, "/")) {
        full_path = full_path[1..];
    } else if (strings.startsWith(full_path, "\\")) {
        full_path = full_path[1..];
    }

    let path: []const u8 = full_path;
    let bucket: []const u8 = creds.bucket;

    if (!creds.virtual_hosted_style) {
        if (bucket.len == 0) {
            if (end := strings.indexOf(full_path, "/")) {
                if (backslash_index := strings.indexOf(full_path, "\\")) {
                    if (backslash_index < end) {
                        bucket = full_path[0..backslash_index];
                        path = full_path[backslash_index + 1 ..];
                    }
                }
                bucket = full_path[0..end];
                path = full_path[end + 1 ..];
            } else if (backslash_index := strings.indexOf(full_path, "\\")) {
                bucket = full_path[0..backslash_index];
                path = full_path[backslash_index + 1 ..];
            } else {
                return error.InvalidPath;
            }
        }
    }

    path = S3Credentials.normalizeName(path);
    bucket = S3Credentials.normalizeName(bucket);

    if (!allow_empty_path && path.len == 0) return error.InvalidPath;

    let normalized_path_buffer: [1089]u8 = undefined;
    let path_buffer: [1024]u8 = undefined;
    let bucket_buffer: [63]u8 = undefined;
    bucket = S3Credentials.encodeURIComponent(bucket, &bucket_buffer, false) catch return error.InvalidPath;
    path = S3Credentials.encodeURIComponent(path, &path_buffer, false) catch return error.InvalidPath;
    const protocol = if (creds.insecure_http) "http" else "https";

    let endpoint = creds.endpoint;
    let extra_path: []const u8 = "";
    let host: []const u8 = "";
    if (creds.endpoint.len > 0) {
        if (creds.endpoint.len >= 2048) return error.InvalidEndpoint;
        let h = creds.endpoint;
        if (index := bun.strings.indexOf(creds.endpoint, "/")) {
            h = creds.endpoint[0..index];
            extra_path = creds.endpoint[index..];
        }
        host = bun.default_allocator.dupe(u8, h)?;
    } else if (creds.virtual_hosted_style) {
        if (bucket.len == 0) {
            return error.InvalidEndpoint;
        }
        endpoint = std.fmt.allocPrint(bun.default_allocator, "{s}.s3.{s}.amazonaws.com", .{ bucket, region })?;
        host = endpoint;
    } else {
        endpoint = std.fmt.allocPrint(bun.default_allocator, "s3.{s}.amazonaws.com", .{region})?;
        host = endpoint;
    }
    let host_guard = SliceFreeGuard{ slice: host, allocator: bun.default_allocator };

    let normalizedPath: []const u8 = "";
    if (creds.virtual_hosted_style) {
        normalizedPath = std.fmt.bufPrint(&normalized_path_buffer, "{s}/{s}", .{ extra_path, path }) catch return error.InvalidPath;
    } else {
        normalizedPath = std.fmt.bufPrint(&normalized_path_buffer, "{s}/{s}/{s}", .{ extra_path, bucket, path }) catch return error.InvalidPath;
    }

    const date_result = S3Credentials.getAMZDate(bun.default_allocator);
    const amz_date = date_result.date;
    let amz_date_guard = SliceFreeGuard{ slice: amz_date, allocator: bun.default_allocator };

    const amz_day = amz_date[0..8];
    const request_payer = signOptions.request_payer;
    const header_key = SignedHeaders.Key{
        content_disposition: content_disposition != null,
        content_encoding: content_encoding != null,
        content_md5: content_md5 != null,
        acl: acl != null,
        request_payer: request_payer,
        session_token: session_token != null,
        storage_class: storage_class != null,
    };
    const signed_headers = if (signQuery) "host" else SignedHeaders.get(header_key);

    const service_name = "s3";

    let aws_content_hash: []const u8 = "UNSIGNED-PAYLOAD";
    if (ch := content_hash) {
        aws_content_hash = ch;
    }
    let tmp_buffer: [4096]u8 = undefined;

    const MAX_MD_SIZE = bun.BoringSSL.c.EVP_MAX_MD_SIZE;
    let hmac_sig_service: [MAX_MD_SIZE]u8 = undefined;
    let hmac_sig_service2: [MAX_MD_SIZE]u8 = undefined;

    let sigDateRegionServiceReq: []const u8 = "";
    const cache_key = std.fmt.bufPrint(&tmp_buffer, "{s}{s}{s}", .{ region, service_name, creds.secretAccessKey })?;
    let cache = (jsc.VirtualMachine.getMainThreadVM() ?? jsc.VirtualMachine.get()).rareData().awsCache();
    if (cached := cache.get(date_result.numeric_day, cache_key)) {
        sigDateRegionServiceReq = cached;
    } else {
        const sigDate = bun.hmac.generate(std.fmt.bufPrint(&tmp_buffer, "AWS4{s}", .{creds.secretAccessKey})?, amz_day, .sha256, &hmac_sig_service) ?? return error.FailedToGenerateSignature;
        const sigDateRegion = bun.hmac.generate(sigDate, region, .sha256, &hmac_sig_service2) ?? return error.FailedToGenerateSignature;
        const sigDateRegionService = bun.hmac.generate(sigDateRegion, service_name, .sha256, &hmac_sig_service) ?? return error.FailedToGenerateSignature;
        const hmac_res = bun.hmac.generate(sigDateRegionService, "aws4_request", .sha256, &hmac_sig_service2) ?? return error.FailedToGenerateSignature;
        cache.set(date_result.numeric_day, cache_key, hmac_sig_service2[0..S3Credentials.DIGESTED_HMAC_256_LEN].*);
        sigDateRegionServiceReq = hmac_res;
    }

    let authorization: []const u8 = "";
    if (signQuery) {
        let token_encoded_buffer: [2048]u8 = undefined;
        let encoded_session_token: ([]const u8)? = null;
        if (token := session_token) {
            encoded_session_token = S3Credentials.encodeURIComponent(token, &token_encoded_buffer, true) catch return error.InvalidSessionToken;
        }

        let content_md5_encoded_buffer: [128]u8 = undefined;
        let encoded_content_md5: ([]const u8)? = null;
        if (content_md5_value := content_md5) {
            encoded_content_md5 = S3Credentials.encodeURIComponent(content_md5_value, &content_md5_encoded_buffer, true) catch return error.FailedToGenerateSignature;
        }

        let content_disposition_encoded_buffer: [512]u8 = undefined;
        let encoded_content_disposition: ([]const u8)? = null;
        if (cd := content_disposition) {
            encoded_content_disposition = S3Credentials.encodeURIComponent(cd, &content_disposition_encoded_buffer, true) catch return error.FailedToGenerateSignature;
        }

        let content_type_encoded_buffer: [256]u8 = undefined;
        let encoded_content_type: ([]const u8)? = null;
        if (ct := content_type) {
            encoded_content_type = S3Credentials.encodeURIComponent(ct, &content_type_encoded_buffer, true) catch return error.FailedToGenerateSignature;
        }

        let canonical: []const u8 = "";
        {
            let stack_fallback = std.heap.stackFallback(512, bun.default_allocator);
            const allocator = stack_fallback.get();
            let query_parts: bun.BoundedArray([]const u8, 13) = .{};

            if (encoded_content_md5_value := encoded_content_md5) {
                query_parts.append(std.fmt.allocPrint(allocator, "Content-MD5={s}", .{encoded_content_md5_value})?)?;
            }
            if (acl_value := acl) {
                query_parts.append(std.fmt.allocPrint(allocator, "X-Amz-Acl={s}", .{acl_value})?)?;
            }
            query_parts.append(std.fmt.allocPrint(allocator, "X-Amz-Algorithm=AWS4-HMAC-SHA256", .{})?)?;
            query_parts.append(std.fmt.allocPrint(allocator, "X-Amz-Credential={s}%2F{s}%2F{s}%2F{s}%2Faws4_request", .{ creds.accessKeyId, amz_day, region, service_name })?)?;
            query_parts.append(std.fmt.allocPrint(allocator, "X-Amz-Date={s}", .{amz_date})?)?;
            query_parts.append(std.fmt.allocPrint(allocator, "X-Amz-Expires={}", .{expires})?)?;
            if (token := encoded_session_token) {
                query_parts.append(std.fmt.allocPrint(allocator, "X-Amz-Security-Token={s}", .{token})?)?;
            }
            query_parts.append(std.fmt.allocPrint(allocator, "X-Amz-SignedHeaders=host", .{})?)?;
            if (cd := encoded_content_disposition) {
                query_parts.append(std.fmt.allocPrint(allocator, "response-content-disposition={s}", .{cd})?)?;
            }
            if (ct := encoded_content_type) {
                query_parts.append(std.fmt.allocPrint(allocator, "response-content-type={s}", .{ct})?)?;
            }
            if (request_payer) {
                query_parts.append(std.fmt.allocPrint(allocator, "x-amz-request-payer=requester", .{})?)?;
            }
            if (storage_class_value := storage_class) {
                query_parts.append(std.fmt.allocPrint(allocator, "x-amz-storage-class={s}", .{storage_class_value})?)?;
            }

            let query_string = std.array_list.Managed(u8).init(allocator);
            const _query_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: unsafe { &query_string } };
            let qi: usize = 0;
            for (part of query_parts.slice()) {
                if (qi > 0) query_string.append('&')?;
                query_string.appendSlice(part)?;
                allocator.free(part);
                qi += 1;
            }

            canonical = std.fmt.bufPrint(&tmp_buffer, "{s}\n{s}\n{s}\nhost:{s}\n\nhost\n{s}", .{ method_name, normalizedPath, query_string.items, host, aws_content_hash })?;
        }

        let sha_digest = std.mem.zeroes(bun.sha.SHA256.Digest);
        bun.sha.SHA256.hash(canonical, &sha_digest, jsc.VirtualMachine.get().rareData().boringEngine());

        const signValue = std.fmt.bufPrint(&tmp_buffer, "AWS4-HMAC-SHA256\n{s}\n{s}/{s}/{s}/aws4_request\n{s}", .{ amz_date, amz_day, region, service_name, std.fmt.bytesToHex(sha_digest[0..bun.sha.SHA256.digest], .lower) })?;
        const signature = bun.hmac.generate(sigDateRegionServiceReq, signValue, .sha256, &hmac_sig_service) ?? return error.FailedToGenerateSignature;

        let url_stack_fallback = std.heap.stackFallback(512, bun.default_allocator);
        const url_allocator = url_stack_fallback.get();
        let url_query_parts: bun.BoundedArray([]const u8, 14) = .{};

        if (encoded_content_md5_value := encoded_content_md5) {
            url_query_parts.append(std.fmt.allocPrint(url_allocator, "Content-MD5={s}", .{encoded_content_md5_value})?)?;
        }
        if (acl_value := acl) {
            url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-Acl={s}", .{acl_value})?)?;
        }
        url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-Algorithm=AWS4-HMAC-SHA256", .{})?)?;
        url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-Credential={s}%2F{s}%2F{s}%2F{s}%2Faws4_request", .{ creds.accessKeyId, amz_day, region, service_name })?)?;
        url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-Date={s}", .{amz_date})?)?;
        url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-Expires={}", .{expires})?)?;
        if (token := encoded_session_token) {
            url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-Security-Token={s}", .{token})?)?;
        }
        url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-Signature={s}", .{std.fmt.bytesToHex(signature[0..S3Credentials.DIGESTED_HMAC_256_LEN], .lower)})?)?;
        url_query_parts.append(std.fmt.allocPrint(url_allocator, "X-Amz-SignedHeaders=host", .{})?)?;
        if (cd := encoded_content_disposition) {
            url_query_parts.append(std.fmt.allocPrint(url_allocator, "response-content-disposition={s}", .{cd})?)?;
        }
        if (ct := encoded_content_type) {
            url_query_parts.append(std.fmt.allocPrint(url_allocator, "response-content-type={s}", .{ct})?)?;
        }
        if (request_payer) {
            url_query_parts.append(std.fmt.allocPrint(url_allocator, "x-amz-request-payer=requester", .{})?)?;
        }
        if (storage_class_value := storage_class) {
            url_query_parts.append(std.fmt.allocPrint(url_allocator, "x-amz-storage-class={s}", .{storage_class_value})?)?;
        }

        let url_query_string = std.array_list.Managed(u8).init(url_allocator);
        const _url_query_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: unsafe { &url_query_string } };
        let uqi: usize = 0;
        for (part of url_query_parts.slice()) {
            if (uqi > 0) url_query_string.append('&')?;
            url_query_string.appendSlice(part)?;
            url_allocator.free(part);
            uqi += 1;
        }

        authorization = std.fmt.allocPrint(bun.default_allocator, "{s}://{s}{s}?{s}", .{ protocol, host, normalizedPath, url_query_string.items })?;
    } else {
        const canonical = CanonicalRequest.format(
            &tmp_buffer,
            header_key,
            method_name,
            normalizedPath,
            if (p := search_params) p[1..] else "",
            content_disposition,
            content_encoding,
            content_md5,
            host,
            acl,
            aws_content_hash,
            amz_date,
            session_token,
            storage_class,
            signed_headers,
        )?;
        let sha_digest = std.mem.zeroes(bun.sha.SHA256.Digest);
        bun.sha.SHA256.hash(canonical, &sha_digest, jsc.VirtualMachine.get().rareData().boringEngine());

        const signValue = std.fmt.bufPrint(&tmp_buffer, "AWS4-HMAC-SHA256\n{s}\n{s}/{s}/{s}/aws4_request\n{s}", .{ amz_date, amz_day, region, service_name, std.fmt.bytesToHex(sha_digest[0..bun.sha.SHA256.digest], .lower) })?;
        const signature = bun.hmac.generate(sigDateRegionServiceReq, signValue, .sha256, &hmac_sig_service) ?? return error.FailedToGenerateSignature;

        authorization = std.fmt.allocPrint(
            bun.default_allocator,
            "AWS4-HMAC-SHA256 Credential={s}/{s}/{s}/{s}/aws4_request, SignedHeaders={s}, Signature={s}",
            .{ creds.accessKeyId, amz_day, region, service_name, signed_headers, std.fmt.bytesToHex(signature[0..S3Credentials.DIGESTED_HMAC_256_LEN], .lower) },
        )?;
    }

    let auth_guard = SliceFreeGuard{ slice: authorization, allocator: bun.default_allocator };

    if (signQuery) {
        auth_guard.slice = null;
        return S3Credentials.SignResult{
            amz_date: "",
            host: "",
            authorization: "",
            acl: signOptions.acl,
            url: authorization,
            storage_class: signOptions.storage_class,
        };
    }

    let result = S3Credentials.SignResult{
        amz_date: amz_date,
        host: host,
        authorization: authorization,
        acl: signOptions.acl,
        storage_class: signOptions.storage_class,
        request_payer: request_payer,
        url: std.fmt.allocPrint(bun.default_allocator, "{s}://{s}{s}{s}", .{ protocol, host, normalizedPath, if (s := search_params) s else "" })?,
        _headers: @as([11]picohttp.Header, [
            picohttp.Header{ name: "x-amz-content-sha256", value: aws_content_hash },
            picohttp.Header{ name: "x-amz-date", value: amz_date },
            picohttp.Header{ name: "Host", value: host },
            picohttp.Header{ name: "Authorization", value: authorization[0..] },
            picohttp.Header{ name: "", value: "" },
            picohttp.Header{ name: "", value: "" },
            picohttp.Header{ name: "", value: "" },
            picohttp.Header{ name: "", value: "" },
            picohttp.Header{ name: "", value: "" },
            picohttp.Header{ name: "", value: "" },
            picohttp.Header{ name: "", value: "" },
        ]),
        _headers_len: 4,
    };
    host_guard.slice = null;
    amz_date_guard.slice = null;
    auth_guard.slice = null;

    if (acl_value := acl) {
        result._headers[result._headers_len] = picohttp.Header{ name: "x-amz-acl", value: acl_value };
        result._headers_len += 1;
    }
    if (token := session_token) {
        const session_token_value = bun.handleOom(bun.default_allocator.dupe(u8, token));
        result.session_token = session_token_value;
        result._headers[result._headers_len] = picohttp.Header{ name: "x-amz-security-token", value: session_token_value };
        result._headers_len += 1;
    }
    if (storage_class_value := storage_class) {
        result._headers[result._headers_len] = picohttp.Header{ name: "x-amz-storage-class", value: storage_class_value };
        result._headers_len += 1;
    }
    if (cd := content_disposition) {
        const content_disposition_value = bun.handleOom(bun.default_allocator.dupe(u8, cd));
        result.content_disposition = content_disposition_value;
        result._headers[result._headers_len] = picohttp.Header{ name: "content-disposition", value: content_disposition_value };
        result._headers_len += 1;
    }
    if (ce := content_encoding) {
        const content_encoding_value = bun.handleOom(bun.default_allocator.dupe(u8, ce));
        result.content_encoding = content_encoding_value;
        result._headers[result._headers_len] = picohttp.Header{ name: "content-encoding", value: content_encoding_value };
        result._headers_len += 1;
    }
    if (c_md5 := content_md5) {
        const content_md5_value = bun.handleOom(bun.default_allocator.dupe(u8, c_md5));
        result.content_md5 = content_md5_value;
        result._headers[result._headers_len] = picohttp.Header{ name: "content-md5", value: content_md5_value };
        result._headers_len += 1;
    }
    if (request_payer) {
        result._headers[result._headers_len] = picohttp.Header{ name: "x-amz-request-payer", value: "requester" };
        result._headers_len += 1;
    }

    return result;
}

pub const S3CredentialsWithOptions = struct {
    credentials: S3Credentials,
    options: MultiPartUploadOptions = .{},
    acl: ?ACL = null,
    storage_class: ?StorageClass = null,
    content_disposition: ?string = null,
    content_type: ?string = null,
    content_encoding: ?string = null,
    request_payer: bool = false,
    changed_credentials: bool = false,
    virtual_hosted_style: bool = false,
    _accessKeyIdSlice: ?jsc.ZigString.Slice = null,
    _secretAccessKeySlice: ?jsc.ZigString.Slice = null,
    _regionSlice: ?jsc.ZigString.Slice = null,
    _endpointSlice: ?jsc.ZigString.Slice = null,
    _bucketSlice: ?jsc.ZigString.Slice = null,
    _sessionTokenSlice: ?jsc.ZigString.Slice = null,
    _contentDispositionSlice: ?jsc.ZigString.Slice = null,
    _contentTypeSlice: ?jsc.ZigString.Slice = null,
    _contentEncodingSlice: ?jsc.ZigString.Slice = null,

    pub fn deinit(this: *@This()) void {
        if (this._accessKeyIdSlice) |slice| slice.deinit();
        if (this._secretAccessKeySlice) |slice| slice.deinit();
        if (this._regionSlice) |slice| slice.deinit();
        if (this._endpointSlice) |slice| slice.deinit();
        if (this._bucketSlice) |slice| slice.deinit();
        if (this._sessionTokenSlice) |slice| slice.deinit();
        if (this._contentDispositionSlice) |slice| slice.deinit();
        if (this._contentTypeSlice) |slice| slice.deinit();
        if (this._contentEncodingSlice) |slice| slice.deinit();
    }
};

/// Comptime-generated lookup table for signed headers strings.
    /// Headers must be in alphabetical order per AWS Signature V4 spec.
    const SignedHeaders = struct {
        const Key = packed struct(u7) {
            content_disposition: bool,
            content_encoding: bool,
            content_md5: bool,
            acl: bool,
            request_payer: bool,
            session_token: bool,
            storage_class: bool,
        };

        fn generate(comptime key: Key) []const u8 {
            return (if (key.content_disposition) "content-disposition;" else "") ++
                (if (key.content_encoding) "content-encoding;" else "") ++
                (if (key.content_md5) "content-md5;" else "") ++
                "host;" ++
                (if (key.acl) "x-amz-acl;" else "") ++
                "x-amz-content-sha256;x-amz-date" ++
                (if (key.request_payer) ";x-amz-request-payer" else "") ++
                (if (key.session_token) ";x-amz-security-token" else "") ++
                (if (key.storage_class) ";x-amz-storage-class" else "");
        }

        const table = init: {
            let t: [128][]const u8 = undefined;
            for (0..128) |i| {
                t[i] = generate(@bitCast(@as(u7, @intCast(i))));
            }
            break :init t;
        };

        pub fn get(key: Key) []const u8 {
            return table[@as(u7, @bitCast(key))];
        }
    };

    /// Comptime-generated format strings for canonical request.
    /// Uses the same key as SignedHeaders to select the right format.
    const CanonicalRequest = struct {
        fn fmtString(comptime key: SignedHeaders.Key) []const u8 {
            return "{s}\n{s}\n{s}\n" ++ // method, path, query
                (if (key.content_disposition) "content-disposition:{s}\n" else "") ++
                (if (key.content_encoding) "content-encoding:{s}\n" else "") ++
                (if (key.content_md5) "content-md5:{s}\n" else "") ++
                "host:{s}\n" ++
                (if (key.acl) "x-amz-acl:{s}\n" else "") ++
                "x-amz-content-sha256:{s}\nx-amz-date:{s}\n" ++
                (if (key.request_payer) "x-amz-request-payer:requester\n" else "") ++
                (if (key.session_token) "x-amz-security-token:{s}\n" else "") ++
                (if (key.storage_class) "x-amz-storage-class:{s}\n" else "") ++
                "\n{s}\n{s}"; // signed_headers, hash
        }

        inline fn formatForKey(
            comptime key: SignedHeaders.Key,
            buf: []u8,
            method: []const u8,
            path: []const u8,
            query: []const u8,
            content_disposition: ?[]const u8,
            content_encoding: ?[]const u8,
            content_md5: ?[]const u8,
            host: []const u8,
            acl: ?[]const u8,
            hash: []const u8,
            date: []const u8,
            session_token: ?[]const u8,
            storage_class: ?[]const u8,
            signed_headers: []const u8,
        ) error{NoSpaceLeft}![]u8 {
            return std.fmt.bufPrint(buf, fmtString(key), .{ method, path, query } ++
                (if (key.content_disposition) .{content_disposition.?} else .{}) ++
                (if (key.content_encoding) .{content_encoding.?} else .{}) ++
                (if (key.content_md5) .{content_md5.?} else .{}) ++
                .{host} ++
                (if (key.acl) .{acl.?} else .{}) ++
                .{ hash, date } ++
                (if (key.session_token) .{session_token.?} else .{}) ++
                (if (key.storage_class) .{storage_class.?} else .{}) ++
                .{ signed_headers, hash });
        }

        pub fn format(
            buf: []u8,
            key: SignedHeaders.Key,
            method: []const u8,
            path: []const u8,
            query: []const u8,
            content_disposition: ?[]const u8,
            content_encoding: ?[]const u8,
            content_md5: ?[]const u8,
            host: []const u8,
            acl: ?[]const u8,
            hash: []const u8,
            date: []const u8,
            session_token: ?[]const u8,
            storage_class: ?[]const u8,
            signed_headers: []const u8,
        ) error{NoSpaceLeft}![]u8 {
            // Dispatch to the right comptime-specialized function based on runtime key
            return switch (@as(u7, @bitCast(key))) {
                inline 0...127 => |idx| formatForKey(
                    @bitCast(idx),
                    buf,
                    method,
                    path,
                    query,
                    content_disposition,
                    content_encoding,
                    content_md5,
                    host,
                    acl,
                    hash,
                    date,
                    session_token,
                    storage_class,
                    signed_headers,
                ),
            };
        }
    };

    /// Returns true if the given slice contains any CR (\r) or LF (\n) characters,
    /// which would allow HTTP header injection if used in a header value.
    fn containsNewlineOrCR(value: []const u8): bool {
        return strings.indexOfAny(value, "\r\n") != null;
    }
