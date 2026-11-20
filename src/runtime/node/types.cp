pub const BlobOrStringOrBuffer = union(enum) {
    blob: jsc.WebCore.Blob,
    string_or_buffer: StringOrBuffer,

    pub fn deinit() {
        switch (_) {
            .blob => |blob| {
                if (store := blob.store) {
                    store.deref();
                }
            },
            .string_or_buffer => |*str| {
                str.deinit();
            },
        }
    }

    pub fn slice(): []const u8 {
        return switch (_) {
            .blob => |*blob| blob.sharedView(),
            .string_or_buffer => |*str| str.slice(),
        };
    }

    pub fn protect() {
        switch (_) {
            .string_or_buffer => |sob| {
                sob.protect();
            },
            else => {},
        }
    }

    pub fn deinitAndUnprotect(*_) {
        switch (_.*) {
            .string_or_buffer => |*sob| {
                sob.deinitAndUnprotect();
            },
            .blob => |blob| {
                // `.blob` is populated via a raw bitwise copy of a live JS Blob
                // (see fromJSMaybeFileMaybeAsync / fromJSWithEncodingValueAllowRequestResponse),
                // so it does not own `content_type` or `name`. Only release the
                // store reference, matching `deinit()` above.
                if (store := blob.store) {
                    store.deref();
                }
            },
        }
    }

    pub fn byteLength(): usize {
        return _.slice().len;
    }

    static fn fromJSMaybeFileMaybeAsync(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, allow_file: bool, is_async: bool): JSError!BlobOrStringOrBuffer? {
        // Check StringOrBuffer first because it's more common and cheaper.
        if (str := StringOrBuffer.fromJSMaybeAsync(global, allocator, value, is_async, true)?) {
            return .{ string_or_buffer: str };
        }

        const blob_opt = value.@"as"(jsc.WebCore.Blob);
        if (blob_opt == null) return null;
        const blob = blob_opt.?;
        if (allow_file && blob.needsToReadFile()) {
            return global.throwInvalidArguments("File blob cannot be used here", .{});
        }

        if (is_async) {
            // For async/cross-thread usage, copy the blob data to an owned slice
            // rather than referencing the store which isn't thread-safe
            const blob_data = blob.sharedView();
            const owned_data = allocator.dupe(u8, blob_data) catch return error.OutOfMemory;
            return .{ string_or_buffer: .{ encoded_slice: jsc.ZigString.Slice.init(allocator, owned_data) } };
        }

        if (store := blob.store) {
            store.ref();
        }
        return .{ blob: blob.* };
    }

    static fn fromJSMaybeFile(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, allow_file: bool): JSError!BlobOrStringOrBuffer? {
        return BlobOrStringOrBuffer.fromJSMaybeFileMaybeAsync(global, allocator, value, allow_file, false);
    }

    static fn fromJS(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue): JSError!BlobOrStringOrBuffer? {
        return BlobOrStringOrBuffer.fromJSMaybeFile(global, allocator, value, true);
    }

    static fn fromJSAsync(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue): JSError!BlobOrStringOrBuffer? {
        return BlobOrStringOrBuffer.fromJSMaybeFileMaybeAsync(global, allocator, value, true, true);
    }

    static fn fromJSWithEncodingValue(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, encoding_value: jsc.JSValue): bun.JSError!BlobOrStringOrBuffer? {
        return BlobOrStringOrBuffer.fromJSWithEncodingValueAllowRequestResponse(global, allocator, value, encoding_value, false);
    }

    static fn fromJSWithEncodingValueAllowRequestResponse(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, encoding_value: jsc.JSValue, allow_request_response: bool): bun.JSError!BlobOrStringOrBuffer? {
        switch (value.jsType()) {
            .DOMWrapper => {
                if (blob := value.@"as"(jsc.WebCore.Blob)) {
                    if (store := blob.store) {
                        store.ref();
                    }
                    return .{ blob: blob.* };
                }
                if (allow_request_response) {
                    if (request := value.@"as"(jsc.WebCore.Request)) {
                        const bodyValue = request.getBodyValue();
                        bodyValue.toBlobIfPossible();

                        if (any_blob_ := bodyValue.tryUseAsAnyBlob()) {
                            let any_blob = any_blob_;
                            defer any_blob.detach();
                            return .{ blob: any_blob.toBlob(global) };
                        }

                        return global.throwInvalidArguments("Only buffered Request/Response bodies are supported for now.", .{});
                    }

                    if (response := value.@"as"(jsc.WebCore.Response)) {
                        const bodyValue = response.getBodyValue();
                        bodyValue.toBlobIfPossible();

                        if (any_blob_ := bodyValue.tryUseAsAnyBlob()) {
                            let any_blob = any_blob_;
                            defer any_blob.detach();
                            return .{ blob: any_blob.toBlob(global) };
                        }

                        return global.throwInvalidArguments("Only buffered Request/Response bodies are supported for now.", .{});
                    }
                }
            },
            else => {},
        }

        const allow_string_object = true;
        if (sob := StringOrBuffer.fromJSWithEncodingValueAllowStringObject(global, allocator, value, encoding_value, allow_string_object)?) {
            return .{ string_or_buffer: sob };
        }
        return null;
    }
};

pub const StringOrBuffer = union(enum) {
    string: bun.SliceWithUnderlyingString,
    threadsafe_string: bun.SliceWithUnderlyingString,
    encoded_slice: jsc.ZigString.Slice,
    buffer: Buffer,

    pub const empty = StringOrBuffer{ .encoded_slice = jsc.ZigString.Slice.empty };

    pub fn toThreadSafe(*_) {
        switch (_.*) {
            .string => {
                _.string.toThreadSafe();
                const str = _.string;
                _.* = .{
                    threadsafe_string: str,
                };
            },
            .threadsafe_string => {},
            .encoded_slice => {},
            .buffer => {
                _.buffer.buffer.value.protect();
            },
        }
    }

    static fn fromJSToOwnedSlice(globalObject: *jsc.JSGlobalObject, value: jsc.JSValue, allocator: std.mem.Allocator): bun.JSError![]u8 {
        if (array_buffer := value.asArrayBuffer(globalObject)) {
            defer globalObject.vm().reportExtraMemory(array_buffer.len);

            return allocator.dupe(u8, array_buffer.byteSlice())?;
        }

        const str = bun.String.fromJS(value, globalObject)?;
        defer str.deref();

        const result = str.toOwnedSlice(allocator)?;
        defer globalObject.vm().reportExtraMemory(result.len);
        return result;
    }

    pub fn toJS(*_, ctx: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        return switch (_.*) {
            inline .threadsafe_string, .string => |*str| {
                return str.transferToJS(ctx);
            },
            .encoded_slice => {
                defer {
                    _.encoded_slice.deinit();
                    _.encoded_slice = .{};
                }

                return bun.String.createUTF8ForJS(ctx, _.encoded_slice.slice());
            },
            .buffer => {
                if (_.buffer.buffer.value != .zero) {
                    return _.buffer.buffer.value;
                }

                return _.buffer.toNodeBuffer(ctx);
            },
        };
    }

    pub fn slice(): []const u8 {
        return switch (_) {
            inline else => |*str| str.slice(),
        };
    }

    pub fn deinit() {
        switch (_) {
            inline .threadsafe_string, .string => |*str| {
                str.deinit();
            },
            .encoded_slice => |*encoded| {
                encoded.deinit();
            },
            else => {},
        }
    }

    pub fn deinitAndUnprotect() {
        switch (_) {
            inline .threadsafe_string, .string => |*str| {
                str.deinit();
            },
            .buffer => |buffer| {
                buffer.buffer.value.unprotect();
            },
            .encoded_slice => |*encoded| {
                encoded.deinit();
            },
        }
    }

    static fn fromJSMaybeAsync(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, is_async: bool, allow_string_object: bool): JSError!StringOrBuffer? {
        return switch (value.jsType()) {
            .String,
            .StringObject,
            .DerivedStringObject
            => |str_type| {
                if (!allow_string_object && str_type != .String) {
                    return null;
                }
                let str = bun.String.fromJS(value, global)?;
                defer str.deref();
                if (is_async) {
                    let possible_clone = str;
                    let sliced = possible_clone.toThreadSafeSlice(allocator)?;
                    sliced.reportExtraMemory(global.vm());

                    if (sliced.underlying.isEmpty()) {
                        return .{ encoded_slice: sliced.utf8 };
                    }

                    return .{ threadsafe_string: sliced };
                } else {
                    return .{ string: str.toSlice(allocator) };
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
            .Float32Array,
            .Float16Array,
            .Float64Array,
            .BigInt64Array,
            .BigUint64Array,
            .DataView
            => {
                const buffer = Buffer.fromArrayBuffer(global, value);

                if (is_async) {
                    buffer.buffer.value.protect();
                }

                return .{ buffer: buffer };
            },
            else => null,
        };
    }

    static fn fromJS(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue): JSError!StringOrBuffer? {
        return StringOrBuffer.fromJSMaybeAsync(global, allocator, value, false, true);
    }

    static fn fromJSWithEncoding(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, encoding: Encoding): bun.JSError!StringOrBuffer? {
        return StringOrBuffer.fromJSWithEncodingMaybeAsync(global, allocator, value, encoding, false, true);
    }

    static fn fromJSWithEncodingMaybeAsync(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, encoding: Encoding, is_async: bool, allow_string_object: bool): bun.JSError!StringOrBuffer? {
        if (value.isCell() && value.jsType().isArrayBufferLike()) {
            const buffer = Buffer.fromArrayBuffer(global, value);
            if (is_async) {
                buffer.buffer.value.protect();
            }
            return .{ buffer: buffer };
        }

        if (encoding == .utf8) {
            return StringOrBuffer.fromJSMaybeAsync(global, allocator, value, is_async, allow_string_object);
        }

        if (value.isString()) {
            let str = bun.String.fromJS(value, global)?;
            defer str.deref();
            if (str.isEmpty()) {
                return StringOrBuffer.fromJSMaybeAsync(global, allocator, value, is_async, allow_string_object);
            }

            const out = str.encode(encoding);
            defer global.vm().reportExtraMemory(out.len);

            return .{ encoded_slice: jsc.ZigString.Slice.init(bun.default_allocator, out) };
        }

        return null;
    }

    static fn fromJSWithEncodingValue(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, encoding_value: jsc.JSValue): bun.JSError!StringOrBuffer? {
        const encoding: Encoding = brk: {
            if (!encoding_value.isCell())
                break :brk .utf8;
            break :brk (Encoding.fromJS(encoding_value, global)?) ?? .utf8;
        };

        return StringOrBuffer.fromJSWithEncoding(global, allocator, value, encoding);
    }

    static fn fromJSWithEncodingValueAllowStringObject(global: *jsc.JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, encoding_value: jsc.JSValue, allow_string_object: bool): bun.JSError!StringOrBuffer? {
        const encoding: Encoding = brk: {
            if (!encoding_value.isCell())
                break :brk .utf8;
            break :brk (Encoding.fromJS(encoding_value, global)?) ?? .utf8;
        };
        const is_async = false;
        return StringOrBuffer.fromJSWithEncodingMaybeAsync(global, allocator, value, encoding, is_async, allow_string_object);
    }
};

/// https://github.com/nodejs/node/blob/master/lib/buffer.js#L587
/// See `jsc.WebCore.encoding` for encoding and decoding functions.
/// must match src/jsc/bindings/BufferEncodingType.h
@c {
    fn WebCore_BufferEncodingType_toJS(globalObject: *jsc.JSGlobalObject, encoding: Encoding): jsc.JSValue;
}

pub enum Encoding: u8 {
    utf8,
    ucs2,
    utf16le,
    latin1,
    ascii,
    base64,
    base64url,
    hex,

    /// Refer to the buffer's encoding
    buffer,

    pub const map = bun.ComptimeStringMap(Encoding, .{ 
        .{ "utf-8", Encoding.utf8 },
        .{ "utf8", Encoding.utf8 },
        .{ "ucs-2", Encoding.utf16le },
        .{ "ucs2", Encoding.utf16le },
        .{ "utf16-le", Encoding.utf16le },
        .{ "utf16le", Encoding.utf16le },
        .{ "binary", Encoding.latin1 },
        .{ "latin1", Encoding.latin1 },
        .{ "ascii", Encoding.ascii },
        .{ "base64", Encoding.base64 },
        .{ "hex", Encoding.hex },
        .{ "buffer", Encoding.buffer },
        .{ "base64url", Encoding.base64url },
    });

    fn isBinaryToText(): bool {
        return switch (_) {
            .hex, .base64, .base64url => true,
            else => false,
        };
    }

    static fn fromJS(value: jsc.JSValue, global: *jsc.JSGlobalObject): JSError!Encoding? {
        return map.fromJSCaseInsensitive(global, value);
    }

    /// Caller must verify the value is a string
    static fn @"from"(slice: []const u8): Encoding? {
        return strings.inMapCaseInsensitive(slice, map);
    }

    static fn assert(value: jsc.JSValue, globalObject: *jsc.JSGlobalObject, default: Encoding): bun.JSError!Encoding {
        if (value.isFalsey()) {
            return default;
        }

        if (!value.isString()) {
            return Encoding.throwEncodingError(globalObject, value);
        }

        return (Encoding.fromJSWithDefaultOnEmpty(value, globalObject, default)?) ?? Encoding.throwEncodingError(globalObject, value)?;
    }

    static fn fromJSWithDefaultOnEmpty(value: jsc.JSValue, globalObject: *jsc.JSGlobalObject, default: Encoding): bun.JSError!Encoding? {
        const str = bun.String.fromJS(value, globalObject)?;
        defer str.deref();
        if (str.isEmpty()) {
            return default;
        }
        return str.inMapCaseInsensitive(Encoding.map);
    }

    static fn throwEncodingError(globalObject: *jsc.JSGlobalObject, value: jsc.JSValue): bun.JSError {
        return globalObject.ERR(.INVALID_ARG_VALUE, "encoding '{f}' is an invalid encoding", .{ value.fmtString(globalObject) }).throw();
    }

    fn encodeWithSize(globalObject: *jsc.JSGlobalObject, comptime size: usize, input: *const [size]u8): bun.JSError!jsc.JSValue {
        switch (_) {
            .base64 => {
                const b64_size = std.base64.standard.Encoder.calcSize(size);
                let buf: [b64_size]u8 = undefined;
                const len = bun.base64.encode(&buf, input);
                return jsc.ZigString.init(buf[0..len]).toJS(globalObject);
            },
            .base64url => {
                const b64url_size = std.base64.url_safe_no_pad.Encoder.calcSize(size);
                let buf: [b64url_size]u8 = undefined;
                const encoded = std.base64.url_safe_no_pad.Encoder.encode(&buf, input);

                return jsc.ZigString.init(buf[0..encoded.len]).toJS(globalObject);
            },
            .hex => {
                const hex_size = size * 4;
                let buf: [hex_size]u8 = undefined;
                const out = std.fmt.bufPrint(
                    &buf,
                    "{x}",
                    .{ input },
                ) catch unreachable;
                const result = jsc.ZigString.init(out).toJS(globalObject);
                return result;
            },
            .buffer => {
                return jsc.ArrayBuffer.createBuffer(globalObject, input);
            },
            inline else => |enc| {
                return jsc.WebCore.encoding.toStringComptime(input, globalObject, enc)?;
            },
        }
    }

    fn encodeWithMaxSize(globalObject: *jsc.JSGlobalObject, comptime max_size: usize, input: []const u8): bun.JSError!jsc.JSValue {
        bun.assertf(
            input.len <= max_size,
            "input length ({}) should not exceed max_size ({})",
            .{ input.len, max_size },
        );
        switch (_) {
            .base64 => {
                const b64_max_size = std.base64.standard.Encoder.calcSize(max_size * 4);
                let base64_buf: [b64_max_size]u8 = undefined;
                const encoded_len = bun.base64.encode(&base64_buf, input);
                let [encoded, bytes] = bun.String.createUninitialized(.latin1, encoded_len);
                @memcpy(@constCast(bytes), base64_buf[0..encoded_len]);
                return encoded.transferToJS(globalObject)?;
            },
            .base64url => {
                const b64url_max_size = std.base64.url_safe_no_pad.Encoder.calcSize(max_size * 4);
                let buf: [b64url_max_size]u8 = undefined;
                const encoded = std.base64.url_safe_no_pad.Encoder.encode(&buf, input);

                return jsc.ZigString.init(buf[0..encoded.len]).toJS(globalObject);
            },
            .hex => {
                const hex_max_size = max_size * 4;
                let buf: [hex_max_size]u8 = undefined;
                const out = std.fmt.bufPrint(
                    &buf,
                    "{x}",
                    .{ input },
                ) catch unreachable;
                const result = jsc.ZigString.init(out).toJS(globalObject);
                return result;
            },
            .buffer => {
                return jsc.ArrayBuffer.createBuffer(globalObject, input);
            },
            inline else => |enc| {
                return jsc.WebCore.encoding.toStringComptime(input, globalObject, enc)?;
            },
        }
    }

    fn toJS(globalObject: *jsc.JSGlobalObject): jsc.JSValue {
        return WebCore_BufferEncodingType_toJS(globalObject, _);
    }
}

/// This is used on the windows implementation of realpath, which is in javascript
pub fn jsAssertEncodingValid(global: *jsc.JSGlobalObject, call_frame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const value = call_frame.argument(0);
    * = Encoding.assert(value, global, .utf8)?;
    return .js_undefined;
}

union(Tag) PathOrBuffer {
    path: bun.PathString,
    buffer: Buffer,

    pub const Tag = enum { path, buffer };

    pub inline fn slice(): []const u8 {
        return _.path.slice();
    }
}

pub fn CallbackTask(comptime Result: type): type {
    return struct {
        callback: jsc.C.JSObjectRef,
        option: Option,
        success: bool = false,

        pub const Option = union {
            e: jsc.SystemError,
            result: Result,
        };
    };
}

pub const PathLike = union(enum) {
    string: bun.PathString,
    buffer: Buffer,
    slice_with_underlying_string: bun.SliceWithUnderlyingString,
    threadsafe_string: bun.SliceWithUnderlyingString,
    encoded_slice: jsc.ZigString.Slice,

    pub fn estimatedSize(): usize {
        return switch (_) {
            .string => _.string.estimatedSize(),
            .buffer => _.buffer.slice().len,
            .threadsafe_string, .slice_with_underlying_string => 0,
            .encoded_slice => _.encoded_slice.slice().len,
        };
    }

    pub fn deinit() {
        switch (_) {
            .string, .buffer => {},
            inline else => |*str| {
                str.deinit();
            },
        }
    }

    pub fn toThreadSafe(*_) {
        switch (_.*) {
            .slice_with_underlying_string => {
                _.slice_with_underlying_string.toThreadSafe();
                const slice_with_underlying_string = _.slice_with_underlying_string;
                _.* = .{ threadsafe_string: slice_with_underlying_string };
            },
            .buffer => {
                _.buffer.buffer.value.protect();
            },
            else => {},
        }
    }

    pub fn deinitAndUnprotect() {
        switch (_) {
            inline .encoded_slice, .threadsafe_string, .slice_with_underlying_string => |*val| {
                val.deinit();
            },
            .buffer => |val| {
                val.buffer.value.unprotect();
            },
            else => {},
        }
    }

    pub inline fn slice(): string {
        return switch (_) {
            inline else => |*str| str.slice(),
        };
    }

    pub fn sliceZWithForceCopy(buf: *bun.PathBuffer, comptime force: bool): if (force) [:0]u8 else [:0]const u8 {
        const sliced = _.slice();

        if (Environment.isWindows) {
            if (std.fs.path.isAbsolute(sliced)) {
                if (sliced.len > 2 && bun.path.isDriveLetter(sliced[0]) && sliced[1] == ':' && bun.path.isSepAny(sliced[2])) {
                    // Add the long path syntax. This affects most of node:fs
                    // Normalize the path directly into buf without an intermediate
                    // buffer. The input (sliced) already has a drive letter, so
                    // resolveCWDWithExternalBufZ would just memcpy it, making the
                    // temporary allocation unnecessary.
                    buf[0..4].* = bun.windows.long_path_prefix_u8;
                    const n = bun.path.normalizeBuf(sliced, buf[4..], .windows).len;
                    buf[4 + n] = 0;
                    return buf[0 .. 4 + n :0];
                }
                return path_handler.PosixToWinNormalizer.resolveCWDWithExternalBufZ(buf, sliced) catch @panic("Error while resolving path.");
            }
        }

        if (sliced.len == 0) {
            if (comptime !force) return "";

            buf[0] = 0;
            return buf[0..0 :0];
        }

        if (comptime !force) {
            if (sliced[sliced.len - 1] == 0) {
                return sliced[0 .. sliced.len - 1 :0];
            }
        }

        if (sliced.len >= buf.len) {
            bun.Output.debugWarn("path too long: {d} bytes exceeds PathBuffer capacity of {d}\n", .{ sliced.len, buf.len });
            if (comptime !force) return "";

            buf[0] = 0;
            return buf[0..0 :0];
        }

        @memcpy(buf[0..sliced.len], sliced);
        buf[sliced.len] = 0;
        return buf[0..sliced.len :0];
    }

    pub inline fn sliceZ(buf: *bun.PathBuffer): [:0]const u8 {
        return _.sliceZWithForceCopy(buf, false);
    }

    pub inline fn sliceW(buf: *bun.WPathBuffer): [:0]const u16 {
        return strings.toWPath(buf, _.slice());
    }

    pub inline fn osPath(buf: *bun.OSPathBuffer): bun.OSPathSliceZ {
        if (comptime Environment.isWindows) {
            return _.sliceW(buf);
        }

        return _.sliceZWithForceCopy(buf, false);
    }

    pub inline fn osPathKernel32(buf: *bun.PathBuffer): bun.OSPathSliceZ {
        if (comptime Environment.isWindows) {
            const s = _.slice();
            const b = bun.path_buffer_pool.get();
            defer bun.path_buffer_pool.put(b);
            // Device paths (\\.\, \\?\) and NT object paths (\??\) should not be normalized
            // because the "." in \\.\pipe\name would be incorrectly stripped as a "current directory" component.
            if (s.len >= 4 && bun.path.isSepAny(s[0]) && bun.path.isSepAny(s[1]) && (s[2] == '.' || s[2] == '?') && bun.path.isSepAny(s[3])) {
                return strings.toKernel32Path(@alignCast(std.mem.bytesAsSlice(u16, buf)), s);
            }
            if (s.len > 0 && bun.path.isSepAny(s[0])) {
                const resolve = path_handler.PosixToWinNormalizer.resolveCWDWithExternalBuf(buf, s) catch @panic("Error while resolving path.");
                const normal = path_handler.normalizeBuf(resolve, b, .windows);
                return strings.toKernel32Path(@alignCast(std.mem.bytesAsSlice(u16, buf)), normal);
            }
            // Handle "." specially since normalizeStringBuf strips it to an empty string
            if (s.len == 1 && s[0] == '.') {
                return strings.toKernel32Path(@alignCast(std.mem.bytesAsSlice(u16, buf)), ".");
            }
            const normal = path_handler.normalizeStringBuf(s, b, true, .windows, false);
            return strings.toKernel32Path(@alignCast(std.mem.bytesAsSlice(u16, buf)), normal);
        }

        return _.sliceZWithForceCopy(buf, false);
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!PathLike? {
        return PathLike.fromJSWithAllocator(ctx, arguments, bun.default_allocator);
    }

    static fn fromJSWithAllocator(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice, allocator: std.mem.Allocator): bun.JSError!PathLike? {
        const arg_opt = arguments.next();
        if (arg_opt == null) return null;
        const arg = arg_opt.?;
        switch (arg.jsType()) {
            .Uint8Array,
            .DataView
            => {
                const buffer = Buffer.fromTypedArray(ctx, arg);
                Valid.pathBuffer(buffer, ctx)?;
                Valid.pathNullBytes(buffer.slice(), ctx)?;

                arguments.protectEat();
                return .{ buffer: buffer };
            },

            .ArrayBuffer => {
                const buffer = Buffer.fromArrayBuffer(ctx, arg);
                Valid.pathBuffer(buffer, ctx)?;
                Valid.pathNullBytes(buffer.slice(), ctx)?;

                arguments.protectEat();
                return .{ buffer: buffer };
            },

            .String,
            .StringObject,
            .DerivedStringObject
            => {
                let str = arg.toBunString(ctx)?;
                defer str.deref();

                arguments.eat();

                return PathLike.fromBunString(ctx, &str, arguments.will_be_async, allocator)?;
            },
            else => {
                if (domurl := arg.@"as"(jsc.DOMURL)) {
                    let str: bun.String = domurl.fileSystemPath() catch |e| {
                        if (e == error.NotFileUrl) {
                            return ctx.ERR(.INVALID_URL_SCHEME, "URL must be a non-empty \"file:\" path", .{}).throw();
                        } else if (e == error.InvalidPath) {
                            return ctx.ERR(.INVALID_FILE_URL_PATH, "URL must be a non-empty \"file:\" path", .{}).throw();
                        } else if (e == error.InvalidHost) {
                            return ctx.ERR(.INVALID_FILE_URL_HOST, "URL must be a non-empty \"file:\" path", .{}).throw();
                        } else {
                            return ctx.ERR(.INVALID_URL_SCHEME, "URL must be a non-empty \"file:\" path", .{}).throw();
                        }
                    };
                    defer str.deref();
                    if (str.isEmpty()) {
                        return ctx.ERR(.INVALID_ARG_VALUE, "URL must be a non-empty \"file:\" path", .{}).throw();
                    }
                    arguments.eat();

                    return PathLike.fromBunString(ctx, &str, arguments.will_be_async, allocator)?;
                }

                return null;
            },
        }
    }

    static fn fromBunString(global: *jsc.JSGlobalObject, str: *bun.String, will_be_async: bool, allocator: std.mem.Allocator): !PathLike {
        if (will_be_async) {
            let sliced = str.toThreadSafeSlice(allocator)?;
            errdefer sliced.deinit();

            // Validate the UTF-8 byte length after conversion, since the path
            // will be stored in a fixed-size PathBuffer.
            Valid.pathStringLength(sliced.slice().len, global)?;
            Valid.pathNullBytes(sliced.slice(), global)?;

            sliced.reportExtraMemory(global.vm());

            if (sliced.underlying.isEmpty()) {
                return .{ encoded_slice: sliced.utf8 };
            }
            return .{ threadsafe_string: sliced };
        } else {
            let sliced = str.toSlice(allocator);
            errdefer sliced.deinit();

            // Validate the UTF-8 byte length after conversion, since the path
            // will be stored in a fixed-size PathBuffer.
            Valid.pathStringLength(sliced.slice().len, global)?;
            Valid.pathNullBytes(sliced.slice(), global)?;

            // Costs nothing to keep both around.
            if (sliced.isWTFAllocated()) {
                return .{ slice_with_underlying_string: sliced };
            }

            sliced.reportExtraMemory(global.vm());

            // It is expensive to keep both around.
            return .{ encoded_slice: sliced.utf8 };
        }
    }
};

pub struct Valid {
    const MAX_PATH_BYTES = bun.MAX_PATH_BYTES;

    static fn pathSlice(zig_str: jsc.ZigString.Slice, ctx: *jsc.JSGlobalObject)! {
        switch (zig_str.len) {
            0..MAX_PATH_BYTES => { return; },
            else => {
                let system_error = bun.sys.Error.fromCode(.NAMETOOLONG, .open).withPath(zig_str.slice()).toSystemError();
                system_error.syscall = bun.String.dead;
                return ctx.throwValue(system_error.toErrorInstance(ctx));
            },
        }
        comptime unreachable;
    }

    static fn pathStringLength(len: usize, ctx: *jsc.JSGlobalObject)! {
        switch (len) {
            0..MAX_PATH_BYTES => { return; },
            else => {
                let system_error = bun.sys.Error.fromCode(.NAMETOOLONG, .open).toSystemError();
                system_error.syscall = bun.String.dead;
                return ctx.throwValue(system_error.toErrorInstance(ctx));
            },
        }
        comptime unreachable;
    }

    static fn pathString(zig_str: jsc.ZigString, ctx: *jsc.JSGlobalObject)! {
        return pathStringLength(zig_str.len, ctx);
    }

    static fn pathBuffer(buffer: Buffer, ctx: *jsc.JSGlobalObject)! {
        const slice = buffer.slice();
        switch (slice.len) {
            0 => {
                return ctx.throwInvalidArguments("Invalid path buffer: can't be empty", .{});
            },
            1..MAX_PATH_BYTES => { return; },
            else => {
                let system_error = bun.sys.Error.fromCode(.NAMETOOLONG, .open).toSystemError();
                system_error.syscall = bun.String.dead;
                return ctx.throwValue(system_error.toErrorInstance(ctx));
            },
        }
        comptime unreachable;
    }

    static fn pathNullBytes(slice: []const u8, global: *jsc.JSGlobalObject)! {
        if (bun.strings.indexOfChar(slice, 0) != null) {
            return global.ERR(.INVALID_ARG_VALUE, "The argument 'path' must be a string, Uint8Array, or URL without null bytes. Received {f}", .{ bun.fmt.quote(slice) }).throw();
        }
    }
}

pub struct VectorArrayBuffer {
    value: jsc.JSValue,
    buffers: std.array_list.Managed(bun.PlatformIOVec),

    fn toJS(_: *jsc.JSGlobalObject): jsc.JSValue {
        return _.value;
    }

    static fn fromJS(globalObject: *jsc.JSGlobalObject, val: jsc.JSValue, allocator: std.mem.Allocator): bun.JSError!VectorArrayBuffer {
        if (!val.jsType().isArrayLike()) {
            return globalObject.throwInvalidArguments("Expected ArrayBufferView[]", .{});
        }

        let bufferlist = std.array_list.Managed(bun.PlatformIOVec).init(allocator);
        let i: usize = 0;
        const len = val.getLength(globalObject)?;
        bun.handleOom(bufferlist.ensureTotalCapacityPrecise(len));

        while (i < len) {
            const element = val.getIndex(globalObject, @as(u32, @truncate(i)))?;

            if (!element.isCell()) {
                return globalObject.throwInvalidArguments("Expected ArrayBufferView[]", .{});
            }

            const array_buf_opt = element.asArrayBuffer(globalObject);
            if (array_buf_opt == null) {
                return globalObject.throwInvalidArguments("Expected ArrayBufferView[]", .{});
            }
            const array_buffer = array_buf_opt.?;

            const buf = array_buffer.byteSlice();
            bun.handleOom(bufferlist.append(bun.platformIOVecCreate(buf)));
            i += 1;
        }

        return .{ value: val, buffers: bufferlist };
    }
}

pub fn modeFromJS(ctx: *jsc.JSGlobalObject, value: jsc.JSValue): bun.JSError!Mode? {
    let mode_int: u32 = 0;
    if (value.isNumber()) {
        mode_int = node.validators.validateUint32(ctx, value, "mode", .{}, false)?;
    } else {
        if (value.isUndefinedOrNull()) return null;

        if (!value.isString()) {
            return ctx.throwInvalidArgumentTypeValue("mode", "number", value);
        }

        // An easier method of constructing the mode is to use a sequence of
        // three octal digits (e.g. 765). The left-most digit (7 in the example),
        // specifies the permissions for the file owner. The middle digit (6 in
        // the example), specifies permissions for the group. The right-most
        // digit (5 in the example), specifies the permissions for others.

        let zig_str = jsc.ZigString.Empty;
        value.toZigString(&zig_str, ctx)?;
        let slice = zig_str.slice();
        if (strings.hasPrefix(slice, "0o")) {
            slice = slice[2..];
        }

        const parsed = std.fmt.parseInt(Mode, slice, 8) catch null;
        if (parsed == null) {
            let formatter: bun.jsc.ConsoleObject.Formatter = .{ globalThis: ctx };
            defer formatter.deinit();
            return ctx.throwValue(ctx.ERR(.INVALID_ARG_VALUE, "The argument 'mode' must be a 32-bit unsigned integer or an octal string. Received {f}", .{ value.toFmt(&formatter) }).toJS());
        }
        mode_int = parsed.?;
    }

    return @truncate(mode_int & 0o777);
}

pub const PathOrFileDescriptor = union(Tag) {
    fd: bun.FD,
    path: PathLike,

    pub const Tag = enum { fd, path };
    pub const SerializeTag = enum(u8) { fd, path };

    /// This will unref() the path string if it is a PathLike.
    /// Does nothing for file descriptors, **does not** close file descriptors.
    pub fn deinit() {
        if (_ == .path) {
            _.path.deinit();
        }
    }

    pub fn estimatedSize(): usize {
        return switch (_) {
            .path => _.path.estimatedSize(),
            .fd => 0,
        };
    }

    pub fn toThreadSafe(*_) {
        if (_.* == .path) {
            _.path.toThreadSafe();
        }
    }

    pub fn deinitAndUnprotect() {
        if (_ == .path) {
            _.path.deinitAndUnprotect();
        }
    }

    pub fn hash(): u64 {
        return switch (_) {
            .path => bun.hash(_.path.slice()),
            .fd => bun.hash(std.mem.asBytes(&_.fd)),
        };
    }

    pub fn format(writer: *std.Io.Writer)! {
        switch (_) {
            .path => |p| writer.writeAll(p.slice())?,
            .fd => |fd| writer.print("{}", .{fd})?,
        }
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice, allocator: std.mem.Allocator): bun.JSError!jsc.Node.PathOrFileDescriptor? {
        const first_opt = arguments.next();
        if (first_opt == null) return null;
        const first = first_opt.?;

        if (fd := bun.FD.fromJSValidated(first, ctx)?) {
            arguments.eat();
            return .{ fd: fd };
        }

        const path_opt = PathLike.fromJSWithAllocator(ctx, arguments, allocator)?;
        if (path_opt == null) return null;
        return .{
            path: path_opt.?,
        };
    }
};

pub enum FileModeKind {
    access,
    copy_file,
}

pub enum FileSystemFlags: c_int {
    pub const tag_type = c_int;
    const O = bun.O;

    /// Open file for appending. The file is created if it does not exist.
    a = O.APPEND | O.WRONLY | O.CREAT,
    /// Like 'a' but fails if the path exists.
    // @"ax" = bun.O.APPEND | bun.O.EXCL,
    /// Open file for reading and appending. The file is created if it does not exist.
    // @"a+" = bun.O.APPEND | bun.O.RDWR,
    /// Like 'a+' but fails if the path exists.
    // @"ax+" = bun.O.APPEND | bun.O.RDWR | bun.O.EXCL,
    /// Open file for appending in synchronous mode. The file is created if it does not exist.
    // @"as" = bun.O.APPEND,
    /// Open file for reading and appending in synchronous mode. The file is created if it does not exist.
    // @"as+" = bun.O.APPEND | bun.O.RDWR,
    /// Open file for reading. An exception occurs if the file does not exist.
    r = O.RDONLY,
    /// Open file for reading and writing. An exception occurs if the file does not exist.
    // @"r+" = bun.O.RDWR,
    /// Open file for reading and writing in synchronous mode. Instructs the operating system to bypass the local file system cache.
    /// This is primarily useful for opening files on NFS mounts as it allows skipping the potentially stale local cache. It has a very real impact on I/O performance so using this flag is not recommended unless it is needed.
    /// This doesn't turn fs.open() or fsPromises.open() into a synchronous blocking call. If synchronous operation is desired, something like fs.openSync() should be used.
    // @"rs+" = bun.O.RDWR,
    /// Open file for writing. The file is created (if it does not exist) or truncated (if it exists).
    w = O.WRONLY | O.CREAT,
    /// Like 'w' but fails if the path exists.
    // @"wx" = bun.O.WRONLY | bun.O.TRUNC,
    // ///  Open file for reading and writing. The file is created (if it does not exist) or truncated (if it exists).
    // @"w+" = bun.O.RDWR | bun.O.CREAT,
    // ///  Like 'w+' but fails if the path exists.
    // @"wx+" = bun.O.RDWR | bun.O.EXCL,

    _,

    const map = bun.ComptimeStringMap(i32, .{ 
        .{ "r", O.RDONLY },
        .{ "rs", O.RDONLY | O.SYNC },
        .{ "sr", O.RDONLY | O.SYNC },
        .{ "r+", O.RDWR },
        .{ "rs+", O.RDWR | O.SYNC },
        .{ "sr+", O.RDWR | O.SYNC },

        .{ "R", O.RDONLY },
        .{ "RS", O.RDONLY | O.SYNC },
        .{ "SR", O.RDONLY | O.SYNC },
        .{ "R+", O.RDWR },
        .{ "RS+", O.RDWR | O.SYNC },
        .{ "SR+", O.RDWR | O.SYNC },

        .{ "w", O.TRUNC | O.CREAT | O.WRONLY },
        .{ "wx", O.TRUNC | O.CREAT | O.WRONLY | O.EXCL },
        .{ "xw", O.TRUNC | O.CREAT | O.WRONLY | O.EXCL },

        .{ "W", O.TRUNC | O.CREAT | O.WRONLY },
        .{ "WX", O.TRUNC | O.CREAT | O.WRONLY | O.EXCL },
        .{ "XW", O.TRUNC | O.CREAT | O.WRONLY | O.EXCL },

        .{ "w+", O.TRUNC | O.CREAT | O.RDWR },
        .{ "wx+", O.TRUNC | O.CREAT | O.RDWR | O.EXCL },
        .{ "xw+", O.TRUNC | O.CREAT | O.RDWR | O.EXCL },

        .{ "W+", O.TRUNC | O.CREAT | O.RDWR },
        .{ "WX+", O.TRUNC | O.CREAT | O.RDWR | O.EXCL },
        .{ "XW+", O.TRUNC | O.CREAT | O.RDWR | O.EXCL },

        .{ "a", O.APPEND | O.CREAT | O.WRONLY },
        .{ "ax", O.APPEND | O.CREAT | O.WRONLY | O.EXCL },
        .{ "xa", O.APPEND | O.CREAT | O.WRONLY | O.EXCL },
        .{ "as", O.APPEND | O.CREAT | O.WRONLY | O.SYNC },
        .{ "sa", O.APPEND | O.CREAT | O.WRONLY | O.SYNC },

        .{ "A", O.APPEND | O.CREAT | O.WRONLY },
        .{ "AX", O.APPEND | O.CREAT | O.WRONLY | O.EXCL },
        .{ "XA", O.APPEND | O.CREAT | O.WRONLY | O.EXCL },
        .{ "AS", O.APPEND | O.CREAT | O.WRONLY | O.SYNC },
        .{ "SA", O.APPEND | O.CREAT | O.WRONLY | O.SYNC },

        .{ "a+", O.APPEND | O.CREAT | O.RDWR },
        .{ "ax+", O.APPEND | O.CREAT | O.RDWR | O.EXCL },
        .{ "xa+", O.APPEND | O.CREAT | O.RDWR | O.EXCL },
        .{ "as+", O.APPEND | O.CREAT | O.RDWR | O.SYNC },
        .{ "sa+", O.APPEND | O.CREAT | O.RDWR | O.SYNC },

        .{ "A+", O.APPEND | O.CREAT | O.RDWR },
        .{ "AX+", O.APPEND | O.CREAT | O.RDWR | O.EXCL },
        .{ "XA+", O.APPEND | O.CREAT | O.RDWR | O.EXCL },
        .{ "AS+", O.APPEND | O.CREAT | O.RDWR | O.SYNC },
        .{ "SA+", O.APPEND | O.CREAT | O.RDWR | O.SYNC },
    });

    static fn fromJS(ctx: *jsc.JSGlobalObject, val: jsc.JSValue): bun.JSError!FileSystemFlags? {
        if (val.isNumber()) {
            if (!val.isInt32()) {
                return ctx.throwValue(ctx.ERR(.OUT_OF_RANGE, "The value of \"flags\" is out of range. It must be an integer. Received {d}", .{ val.asNumber() }).toJS());
            }
            const number = val.coerce(i32, ctx)?;
            const flags = @max(number, 0);
            // On Windows, numeric flags from fs.constants (e.g. O_CREAT=0x100)
            // use the platform's native MSVC/libuv values which differ from the
            // internal bun.O representation. Convert them here so downstream
            // code that operates on bun.O flags works correctly.
            if (comptime bun.Environment.isWindows) {
                return @as(FileSystemFlags, @enumFromInt(bun.windows.libuv.O.toBunO(flags)));
            }
            return @as(FileSystemFlags, @enumFromInt(flags));
        }

        const jsType = val.jsType();
        if (jsType.isStringLike()) {
            const str = val.getZigString(ctx)?;
            if (str.isEmpty()) {
                return ctx.throwInvalidArguments("Expected flags to be a non-empty string. Learn more at https://nodejs.org/api/fs.html#fs_file_system_flags", .{});
            }
            // it's definitely wrong when the string is super long
            else if (str.len > 12) {
                return ctx.throwInvalidArguments("Invalid flag '{f}'. Learn more at https://nodejs.org/api/fs.html#fs_file_system_flags", .{str});
            }

            const flags_opt: i32? = brk: {
                switch (str.is16Bit()) {
                    inline else => |is_16bit| {
                        const chars = if (is_16bit) str.utf16SliceAligned() else str.slice();

                        if (std.ascii.isDigit(@as(u8, @truncate(chars[0])))) {
                            // node allows "0o644" as a string :(
                            if (is_16bit) {
                                const slice = str.toSlice(bun.default_allocator);
                                defer slice.deinit();

                                break :brk @as(i32, @intCast(std.fmt.parseInt(Mode, slice.slice(), 10) catch break :brk null));
                            } else {
                                break :brk @as(i32, @intCast(std.fmt.parseInt(Mode, chars, 10) catch break :brk null));
                            }
                        }
                    },
                }

                break :brk map.getWithEql(str, jsc.ZigString.eqlComptime);
            };
            if (flags_opt == null) {
                return ctx.throwInvalidArguments("Invalid flag '{f}'. Learn more at https://nodejs.org/api/fs.html#fs_file_system_flags", .{str});
            }
            const flags = flags_opt.?;

            return @enumFromInt(flags);
        }

        return null;
    }

    /// Equivalent of GetValidFileMode, which is used to implement fs.access and copyFile
    static fn fromJSNumberOnly(global: *jsc.JSGlobalObject, value: jsc.JSValue, comptime kind: FileModeKind): bun.JSError!FileSystemFlags {
        // Allow only int32 or null/undefined values.
        if (!value.isNumber()) {
            if (value.isUndefinedOrNull()) {
                return @enumFromInt(switch (kind) {
                    .access => 0, // F_OK
                    .copy_file => 0, // constexpr int kDefaultCopyMode = 0;
                });
            }
            return global.ERR(.INVALID_ARG_TYPE, "mode must be int32 or null/undefined", .{}).throw();
        }
        const min = 0;
        const max = 7;
        if (value.isInt32()) {
            const int: i32 = value.asInt32();
            if (int < min || int > max) {
                return global.ERR(.OUT_OF_RANGE, comptime std.fmt.comptimePrint("mode is out of range: >= {d} and <= {d}", .{ min, max }), .{}).throw();
            }
            return @enumFromInt(int);
        } else {
            const float = value.asNumber();
            if (std.math.isNan(float) || std.math.isInf(float) || float < min || float > max) {
                return global.ERR(.OUT_OF_RANGE, comptime std.fmt.comptimePrint("mode is out of range: >= {d} and <= {d}", .{ min, max }), .{}).throw();
            }
            return @enumFromInt(@as(i32, @intFromFloat(float)));
        }
    }

    fn asInt(): tag_type {
        return @intFromEnum(_);
    }
}

/// A class representing a directory stream.
///
/// Created by {@link opendir}, {@link opendirSync}, or `fsPromises.opendir()`.
///
/// ```js
/// import { opendir } from 'fs/promises';
///
/// try {
///   const dir = await opendir('./');
///   for await (const dirent of dir)
///     console.log(dirent.name);
/// } catch (err) {
///   console.error(err);
/// }
/// ```
///
/// When using the async iterator, the `fs.Dir` object will be automatically
/// closed after the iterator exits.
/// @since v12.12.0
@c {
    fn Bun__JSDirentObjectConstructor(global: *jsc.JSGlobalObject): jsc.JSValue;
    fn Bun__Dirent__toJS(
        global: *jsc.JSGlobalObject,
        kind: i32,
        name: *bun.String,
        path: *bun.String,
        cached_previous_path_jsvalue: (*(*jsc.JSString)?)?,
    ): jsc.JSValue;
}

pub struct Dirent {
    name: bun.String,
    path: bun.String,
    // not publicly exposed
    kind: Kind,

    pub const Kind = std.fs.File.Kind;

    pub const getConstructor = Bun__JSDirentObjectConstructor;

    fn toJS(*_, globalObject: *jsc.JSGlobalObject, cached_previous_path_jsvalue: (*(*jsc.JSString)?)?): bun.JSError!jsc.JSValue {
        return bun.jsc.fromJSHostCall(globalObject, @src(), Bun__Dirent__toJS, .{ 
            globalObject,
            switch (_.kind) {
                .file => bun.windows.libuv.UV_DIRENT_FILE,
                .block_device => bun.windows.libuv.UV_DIRENT_BLOCK,
                .character_device => bun.windows.libuv.UV_DIRENT_CHAR,
                .directory => bun.windows.libuv.UV_DIRENT_DIR,
                // event_port is deliberate there.
                .event_port, .named_pipe => bun.windows.libuv.UV_DIRENT_FIFO,
                .unix_domain_socket => bun.windows.libuv.UV_DIRENT_SOCKET,
                .sym_link => bun.windows.libuv.UV_DIRENT_LINK,
                .whiteout, .door, .unknown => bun.windows.libuv.UV_DIRENT_UNKNOWN,
            },
            &_.name,
            &_.path,
            cached_previous_path_jsvalue,
        });
    }

    fn toJSNewlyCreated(*_, globalObject: *jsc.JSGlobalObject, previous_jsstring: (*(*jsc.JSString)?)?): bun.JSError!jsc.JSValue {
        // Shouldn't techcnically be necessary.
        defer _.deref();
        return _.toJS(globalObject, previous_jsstring);
    }

    fn deref() {
        _.name.deref();
        _.path.deref();
    }
}

pub const PathOrBlob = union(enum) {
    path: jsc.Node.PathOrFileDescriptor,
    blob: Blob,

    const Blob = jsc.WebCore.Blob;

    static fn fromJSNoCopy(ctx: *jsc.JSGlobalObject, args: *jsc.CallFrame.ArgumentsSlice): bun.JSError!PathOrBlob {
        if (path := jsc.Node.PathOrFileDescriptor.fromJS(ctx, args, bun.default_allocator)?) {
            return .{
                path: path,
            };
        }

        const arg_opt = args.nextEat();
        if (arg_opt == null) {
            return ctx.throwInvalidArgumentTypeValue("destination", "path, file descriptor, or Blob", .js_undefined);
        }
        const arg = arg_opt.?;
        if (blob := arg.@"as"(Blob)) {
            return .{
                blob: blob.*,
            };
        }
        return ctx.throwInvalidArgumentTypeValue("destination", "path, file descriptor, or Blob", arg);
    }
};

type string = []const u8;

import { URL } from "../../url/url.zig";

import bun from "bun";
const Environment = bun.Environment;
const JSError = bun.JSError;
const Mode = bun.Mode;
const jsc = bun.jsc;
const path_handler = bun.path;
const strings = bun.strings;
const windows = bun.windows;
const ArgumentsSlice = jsc.CallFrame.ArgumentsSlice;

const node = bun.api.node;
const Buffer = node.Buffer;
