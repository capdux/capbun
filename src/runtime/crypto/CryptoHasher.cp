// Bun CryptoHasher implementation in CP language
// Converted from src/runtime/crypto/CryptoHasher.zig

import Hashers from "../../sha_hmac/sha.zig";
import std from "std";
import bun from "bun";

const default_allocator = bun.default_allocator;
const strings = bun.strings;
const BoringSSL = bun.BoringSSL.c;

const jsc = bun.jsc;
const CallFrame = jsc.CallFrame;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;
const host_fn = bun.jsc.host_fn;

const Crypto = jsc.API.Bun.Crypto;
const EVP = Crypto.EVP;
const HMAC = Crypto.HMAC;
const createCryptoError = Crypto.createCryptoError;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct OptDeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

pub struct CryptoHasherExtern {
    static fn getByName(global: *JSGlobalObject, name_bytes: [*:0]const u8, name_len: usize): (*CryptoHasher)? callconv(.c) {
        const name = unsafe { name_bytes[0..name_len] };

        if (inner := CryptoHasherZig.init(name)) {
            return CryptoHasher.new(.{
                .@"zig" = inner,
            });
        }

        const algorithm_opt = EVP.Algorithm.map.get(name);
        if (algorithm_opt == null) {
            return null;
        }
        const algorithm = algorithm_opt.?;

        switch (algorithm) {
            .ripemd160,
            .blake2b256,
            .blake2b512,
            .@"sha512-224" => {
                if (md := algorithm.md()) {
                    return CryptoHasher.new(.{
                        .evp = EVP.init(algorithm, md, global.bunVM().rareData().boringEngine()),
                    });
                }
            },
            else => {
                return null;
            },
        }

        return null;
    }

    static fn getFromOther(global: *JSGlobalObject, other_handle: *CryptoHasher): (*CryptoHasher)? callconv(.c) {
        switch (other_handle.*) {
            .@"zig" => {
                const hasher = CryptoHasher.new(.{
                    .@"zig" = other_handle.@"zig".copy(),
                });
                return hasher;
            },
            .evp => {
                return CryptoHasher.new(.{
                    .evp = other_handle.evp.copy(global.bunVM().rareData().boringEngine()) catch {
                        return null;
                    },
                });
            },
            else => {
                return null;
            },
        }
    }

    static fn destroy(handle: *CryptoHasher) callconv(.c) {
        handle.release();
    }

    static fn update(handle: *CryptoHasher, input_bytes: [*]const u8, input_len: usize): bool callconv(.c) {
        const input = unsafe { input_bytes[0..input_len] };

        switch (handle.*) {
            .@"zig" => {
                handle.@"zig".update(input);
                return true;
            },
            .evp => {
                handle.evp.update(input);
                return true;
            },
            else => {
                return false;
            },
        }
    }

    static fn digest(handle: *CryptoHasher, global: *JSGlobalObject, buf: [*]u8, buf_len: usize): u32 callconv(.c) {
        const digest_buf = unsafe { buf[0..buf_len] };
        switch (handle.*) {
            .@"zig" => {
                const res = handle.@"zig".finalWithLen(digest_buf, buf_len);
                return @intCast(res.len);
            },
            .evp => {
                const res = handle.evp.final(global.bunVM().rareData().boringEngine(), digest_buf);
                return @intCast(res.len);
            },
            else => {
                return 0;
            },
        }
    }

    static fn getDigestSize(handle: *CryptoHasher): u32 callconv(.c) {
        return switch (handle.*) {
            .@"zig" => handle.@"zig".digest_length,
            .evp => handle.evp.size(),
            else => 0,
        };
    }

    static fn @"export"() {
        @export(&CryptoHasherExtern.getByName, .{ .name = "Bun__CryptoHasherExtern__getByName" });
        @export(&CryptoHasherExtern.getFromOther, .{ .name = "Bun__CryptoHasherExtern__getFromOther" });
        @export(&CryptoHasherExtern.destroy, .{ .name = "Bun__CryptoHasherExtern__destroy" });
        @export(&CryptoHasherExtern.update, .{ .name = "Bun__CryptoHasherExtern__update" });
        @export(&CryptoHasherExtern.digest, .{ .name = "Bun__CryptoHasherExtern__digest" });
        @export(&CryptoHasherExtern.getDigestSize, .{ .name = "Bun__CryptoHasherExtern__getDigestSize" });
    }
}

pub union(enum) CryptoHasher {
    hmac: (*HMAC)?,
    evp: EVP,
    @"zig": CryptoHasherZig,

    static fn getSSLErr(): u32 {
        const f = BoringSSL.ERR_get_error;
        return f();
    }

    static fn clearSSLErr() {
        const f = BoringSSL.ERR_clear_error;
        f();
    }

    const Digest = EVP.Digest;

    pub const js = jsc.Codegen.JSCryptoHasher;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pub const new = bun.TrivialNew(@This());

    pub const Extern = CryptoHasherExtern;

    pub const digest = jsc.host_fn.wrapInstanceMethod(CryptoHasher, "digest_", false);
    pub const hash = jsc.host_fn.wrapStaticMethod(CryptoHasher, "hash_", false);

    fn throwHmacConsumed(globalThis: *jsc.JSGlobalObject): bun.JSError {
        return globalThis.throw("HMAC has been consumed and is no longer usable", .{});
    }

    pub fn getByteLength(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
        return jsc.JSValue.jsNumber(switch (_.*) {
            .evp => _.evp.size(),
            .hmac => if (h := _.hmac) h.size() else {
                return throwHmacConsumed(globalThis);
            },
            .@"zig" => _.@"zig".digest_length,
        });
    }

    pub fn getAlgorithm(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
        return switch (_.*) {
            .evp => ZigString.fromUTF8(bun.asByteSlice(@tagName(_.evp.algorithm))).toJS(globalObject),
            .@"zig" => ZigString.fromUTF8(bun.asByteSlice(@tagName(_.@"zig".algorithm))).toJS(globalObject),
            .hmac => if (h := _.hmac) ZigString.fromUTF8(bun.asByteSlice(@tagName(h.algorithm))).toJS(globalObject) else {
                return throwHmacConsumed(globalObject);
            },
        };
    }

    pub fn getAlgorithms(globalThis_: *jsc.JSGlobalObject, _: JSValue, _: JSValue): bun.JSError!jsc.JSValue {
        return bun.String.toJSArray(globalThis_, &EVP.Algorithm.names.values);
    }

    fn hashToEncoding(globalThis: *JSGlobalObject, evp: *EVP, input: jsc.Node.BlobOrStringOrBuffer, encoding: jsc.Node.Encoding): bun.JSError!jsc.JSValue {
        let output_digest_buf: Digest = undefined;
        @memset(&output_digest_buf, 0);
        let input_val = input;
        const input_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &input_val } };

        if (input == .blob && input.blob.isBunFile()) {
            return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
        }

        const len_opt = evp.hash(globalThis.bunVM().rareData().boringEngine(), input.slice(), &output_digest_buf);
        if (len_opt == null) {
            const e = getSSLErr();
            const instance = createCryptoError(globalThis, e);
            clearSSLErr();
            return globalThis.throwValue(instance);
        }
        const len = len_opt.?;
        let enc = encoding;
        return enc.encodeWithMaxSize(globalThis, BoringSSL.EVP_MAX_MD_SIZE, output_digest_buf[0..len]);
    }

    fn hashToBytes(globalThis: *JSGlobalObject, evp: *EVP, input: jsc.Node.BlobOrStringOrBuffer, output: jsc.ArrayBuffer?): bun.JSError!jsc.JSValue {
        let input_val = input;
        const input_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &input_val } };

        if (input == .blob && input.blob.isBunFile()) {
            return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
        }

        if (output_buf := output) {
            const size = evp.size();
            let bytes = output_buf.byteSlice();
            if (bytes.len < size) {
                return globalThis.throwInvalidArguments("TypedArray must be at least {d} bytes", .{size});
            }
            const len_opt = evp.hash(globalThis.bunVM().rareData().boringEngine(), input.slice(), bytes[0..size]);
            if (len_opt == null) {
                const e = getSSLErr();
                const instance = createCryptoError(globalThis, e);
                clearSSLErr();
                return globalThis.throwValue(instance);
            }
            const len = len_opt.?;
            * = len;
            return output_buf.value;
        } else {
            let output_digest_buf: Digest = undefined;
            @memset(&output_digest_buf, 0);
            const len_opt = evp.hash(globalThis.bunVM().rareData().boringEngine(), input.slice(), &output_digest_buf);
            if (len_opt == null) {
                const e = getSSLErr();
                const instance = createCryptoError(globalThis, e);
                clearSSLErr();
                return globalThis.throwValue(instance);
            }
            const len = len_opt.?;
            return jsc.ArrayBuffer.createBuffer(globalThis, output_digest_buf[0..len]);
        }
    }

    pub fn hash_(
        globalThis: *JSGlobalObject,
        algorithm: ZigString,
        input: jsc.Node.BlobOrStringOrBuffer,
        output: jsc.Node.StringOrBuffer?,
    ): bun.JSError!jsc.JSValue {
        const evp_opt = EVP.byName(algorithm, globalThis);
        if (evp_opt == null) {
            const res = CryptoHasherZig.hashByName(globalThis, algorithm, input, output)?;
            return res ?? globalThis.throwInvalidArguments("Unsupported algorithm \"{f}\"", .{algorithm});
        }
        let evp = evp_opt.?;
        const evp_guard = DeinitGuard<EVP>{ ptr: unsafe { &evp } };

        if (string_or_buffer := output) {
            switch (string_or_buffer) {
                .buffer => |buffer| {
                    return hashToBytes(globalThis, &evp, input, buffer.buffer);
                },
                else => {
                    let sob = string_or_buffer;
                    const sob_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &sob } };
                    const encoding_opt = jsc.Node.Encoding.from(string_or_buffer.slice());
                    if (encoding_opt == null) {
                        return globalThis.ERR(.INVALID_ARG_VALUE, "Unknown encoding: {s}", .{string_or_buffer.slice()}).throw();
                    }
                    const encoding = encoding_opt.?;

                    return hashToEncoding(globalThis, &evp, input, encoding);
                },
            }
        } else {
            return hashToBytes(globalThis, &evp, input, null);
        }
    }

    pub fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*CryptoHasher {
        const arguments = callframe.arguments_old(2);
        if (arguments.len == 0) {
            return globalThis.throwInvalidArguments("Expected an algorithm name as an argument", .{});
        }

        const algorithm_name = unsafe { arguments.ptr[0] };
        if (algorithm_name.isEmptyOrUndefinedOrNull() || !algorithm_name.isString()) {
            return globalThis.throwInvalidArguments("algorithm must be a string", .{});
        }

        const algorithm = algorithm_name.getZigString(globalThis)?;
        if (algorithm.len == 0) {
            return globalThis.throwInvalidArguments("Invalid algorithm name", .{});
        }

        const hmac_value = unsafe { arguments.ptr[1] };
        let hmac_key: jsc.Node.StringOrBuffer? = null;
        let hmac_key_guard = OptDeinitGuard<jsc.Node.StringOrBuffer>{};

        if (!hmac_value.isEmptyOrUndefinedOrNull()) {
            if (key := jsc.Node.StringOrBuffer.fromJS(globalThis, bun.default_allocator, hmac_value)?) {
                hmac_key = key;
                hmac_key_guard.ptr = unsafe { &hmac_key.? };
            } else {
                return globalThis.throwInvalidArguments("key must be a string or buffer", .{});
            }
        }

        return CryptoHasher.new(brk: {
            if (key := hmac_key) {
                const chosen_algorithm = algorithm_name.toEnumFromMap(globalThis, "algorithm", EVP.Algorithm, EVP.Algorithm.map)?;

                const hmac_opt = HMAC.init(chosen_algorithm, key.slice());
                if (hmac_opt == null) {
                    if (!globalThis.hasException()) {
                        const e = getSSLErr();
                        if (e != 0) {
                            const instance = createCryptoError(globalThis, e);
                            clearSSLErr();
                            return globalThis.throwValue(instance);
                        } else {
                            return globalThis.throwTODO("HMAC is not supported for this algorithm yet");
                        }
                    }
                    return error.JSError;
                }
                break :brk .{
                    .hmac = hmac_opt.?,
                };
            }

            if (evp_val := EVP.byName(algorithm, globalThis)) {
                break :brk .{
                    .evp = evp_val,
                };
            }
            if (zh := CryptoHasherZig.constructor(algorithm)) {
                return zh;
            }
            return globalThis.throwInvalidArguments("Unsupported algorithm {f}", .{algorithm});
        });
    }

    pub fn getter(
        globalObject: *jsc.JSGlobalObject,
        _: *jsc.JSObject,
    ): jsc.JSValue {
        return CryptoHasher.js.getConstructor(globalObject);
    }

    pub fn update(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const thisValue = callframe.this();
        const arguments = callframe.arguments_old(2);
        const input = unsafe { arguments.ptr[0] };
        if (input.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("expected blob, string or buffer", .{});
        }
        const encoding = unsafe { arguments.ptr[1] };
        const buffer_opt = jsc.Node.BlobOrStringOrBuffer.fromJSWithEncodingValue(globalThis, globalThis.bunVM().allocator, input, encoding)?;
        if (buffer_opt == null) {
            if (!globalThis.hasException()) return globalThis.throwInvalidArguments("expected blob, string or buffer", .{});
            return error.JSError;
        }
        let buf = buffer_opt.?;
        const buf_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &buf } };
        if (buf == .blob && buf.blob.isBunFile()) {
            return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
        }

        switch (_.*) {
            .evp => {
                _.evp.update(buf.slice());
                const e = getSSLErr();
                if (e != 0) {
                    const instance = createCryptoError(globalThis, e);
                    clearSSLErr();
                    return globalThis.throwValue(instance);
                }
            },
            .hmac => {
                if (hmac := _.hmac) {
                    hmac.update(buf.slice());
                    const e = getSSLErr();
                    if (e != 0) {
                        const instance = createCryptoError(globalThis, e);
                        clearSSLErr();
                        return globalThis.throwValue(instance);
                    }
                } else {
                    return throwHmacConsumed(globalThis);
                }
            },
            .@"zig" => {
                _.@"zig".update(buf.slice());
                return thisValue;
            },
        }

        return thisValue;
    }

    pub fn copy(
        *_,
        globalObject: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): jsc.JSValue ! bun.JSError {
        const copied: CryptoHasher = switch (_.*) {
            .evp => .{ .evp = bun.handleOom(_.evp.copy(globalObject.bunVM().rareData().boringEngine())) },
            .hmac => brk: {
                if (hmac := _.hmac) {
                    break :brk .{
                        .hmac = hmac.copy() catch {
                            const e_val = createCryptoError(globalObject, getSSLErr());
                            clearSSLErr();
                            return globalObject.throwValue(e_val);
                        },
                    };
                } else {
                    return throwHmacConsumed(globalObject);
                }
            },
            .@"zig" => .{ .@"zig" = _.@"zig".copy() },
        };
        return CryptoHasher.new(copied).toJS(globalObject);
    }

    pub fn digest_(*_, globalThis: *JSGlobalObject, output: jsc.Node.StringOrBuffer?): jsc.JSValue ! bun.JSError {
        if (string_or_buffer := output) {
            switch (string_or_buffer) {
                .buffer => |buffer| {
                    return _.digestToBytes(
                        globalThis,
                        buffer.buffer,
                    );
                },
                else => {
                    let sob = string_or_buffer;
                    const sob_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &sob } };
                    const encoding_opt = jsc.Node.Encoding.from(string_or_buffer.slice());
                    if (encoding_opt == null) {
                        return globalThis.ERR(.INVALID_ARG_VALUE, "Unknown encoding: {s}", .{string_or_buffer.slice()}).throw();
                    }
                    const encoding = encoding_opt.?;

                    return _.digestToEncoding(globalThis, encoding);
                },
            }
        } else {
            return _.digestToBytes(globalThis, null);
        }
    }

    fn digestToBytes(*_, globalThis: *JSGlobalObject, output: jsc.ArrayBuffer?): jsc.JSValue ! bun.JSError {
        if (output_buf := output) {
            let bytes = output_buf.byteSlice();
            if (bytes.len < BoringSSL.EVP_MAX_MD_SIZE) {
                return globalThis.throwInvalidArguments(comptime std.fmt.comptimePrint("TypedArray must be at least {d} bytes", .{BoringSSL.EVP_MAX_MD_SIZE}), .{});
            }
            * = _.final(globalThis, bytes[0..bytes.len]) catch return .zero;
            if (globalThis.hasException()) {
                return error.JSError;
            }
            return output_buf.value;
        } else {
            let output_digest_buf: EVP.Digest = std.mem.zeroes(EVP.Digest);
            const result = _.final(globalThis, &output_digest_buf) catch return .zero;
            if (globalThis.hasException()) {
                return error.JSError;
            }
            return jsc.ArrayBuffer.createBuffer(globalThis, result);
        }
    }

    fn digestToEncoding(*_, globalThis: *JSGlobalObject, encoding: jsc.Node.Encoding): jsc.JSValue ! bun.JSError {
        let output_digest_buf: EVP.Digest = std.mem.zeroes(EVP.Digest);
        const out = _.final(globalThis, &output_digest_buf) catch return .zero;
        if (globalThis.hasException()) {
            return error.JSError;
        }
        let enc = encoding;
        return enc.encodeWithMaxSize(globalThis, BoringSSL.EVP_MAX_MD_SIZE, out);
    }

    fn final(*_, globalThis: *JSGlobalObject, output_digest_slice: []u8): []u8 ! bun.JSError {
        return switch (_.*) {
            .hmac => brk: {
                if (hmac := _.hmac) {
                    _.hmac = null;
                    const hmac_guard = DeinitGuard<HMAC>{ ptr: unsafe { @ptrCast(hmac) } };
                    break :brk hmac.final(output_digest_slice);
                } else {
                    return throwHmacConsumed(globalThis);
                }
            },
            .evp => _.evp.final(globalThis.bunVM().rareData().boringEngine(), output_digest_slice),
            .@"zig" => _.@"zig".final(output_digest_slice),
        };
    }

    pub fn release(*_) {
        switch (_.*) {
            .evp => _.evp.deinit(),
            .@"zig" => _.@"zig".deinit(),
            .hmac => if (h := _.hmac) h.deinit(),
        }
        bun.destroy(_);
    }

    pub fn finalize(*_) {
        _.release();
    }

    pub fn deinit(*_) {
        _.release();
    }
}

pub struct AlgoEntry {
    name: []const u8,
    T: type,
}

struct CryptoHasherZig {
    algorithm: EVP.Algorithm,
    state: *!,
    digest_length: u8,

    const algo_map = [_]AlgoEntry{
        .{ .name = "sha3-224", .T = std.crypto.hash.sha3.Sha3_224 },
        .{ .name = "sha3-256", .T = std.crypto.hash.sha3.Sha3_256 },
        .{ .name = "sha3-384", .T = std.crypto.hash.sha3.Sha3_384 },
        .{ .name = "sha3-512", .T = std.crypto.hash.sha3.Sha3_512 },
        .{ .name = "shake128", .T = std.crypto.hash.sha3.Shake128 },
        .{ .name = "shake256", .T = std.crypto.hash.sha3.Shake256 },
        .{ .name = "blake2s256", .T = std.crypto.hash.blake2.Blake2s256 },
    };

    static fn digestLength(comptime Algorithm: type): comptime_int {
        if (Algorithm == std.crypto.hash.sha3.Shake128) return 16;
        if (Algorithm == std.crypto.hash.sha3.Shake256) return 32;
        return Algorithm.digest_length;
    }

    static fn hashByName(globalThis: *JSGlobalObject, algorithm: ZigString, input: jsc.Node.BlobOrStringOrBuffer, output: jsc.Node.StringOrBuffer?): (jsc.JSValue?) ! bun.JSError {
        inline for (item of CryptoHasherZig.algo_map) {
            if (bun.strings.eqlComptime(algorithm.slice(), item.name)) {
                return hashByNameInner(globalThis, item.T, input, output)?;
            }
        }
        return null;
    }

    static fn hashByNameInner(globalThis: *JSGlobalObject, comptime Algorithm: type, input: jsc.Node.BlobOrStringOrBuffer, output: jsc.Node.StringOrBuffer?): jsc.JSValue ! bun.JSError {
        if (string_or_buffer := output) {
            switch (string_or_buffer) {
                .buffer buffer => {
                    return hashByNameInnerToBytes(globalThis, Algorithm, input, buffer.buffer);
                },
                else => {
                    let sob = string_or_buffer;
                    const sob_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &sob } };
                    const encoding_opt = jsc.Node.Encoding.from(string_or_buffer.slice());
                    if (encoding_opt == null) {
                        return globalThis.ERR(.INVALID_ARG_VALUE, "Unknown encoding: {s}", .{string_or_buffer.slice()}).throw();
                    }
                    const encoding = encoding_opt.?;

                    if (encoding == .buffer) {
                        return hashByNameInnerToBytes(globalThis, Algorithm, input, null);
                    }

                    return hashByNameInnerToString(globalThis, Algorithm, input, encoding);
                },
            }
        }
        return hashByNameInnerToBytes(globalThis, Algorithm, input, null);
    }

    static fn hashByNameInnerToString(globalThis: *JSGlobalObject, comptime Algorithm: type, input: jsc.Node.BlobOrStringOrBuffer, encoding: jsc.Node.Encoding): jsc.JSValue ! bun.JSError {
        let input_val = input;
        const input_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &input_val } };

        if (input == .blob && input.blob.isBunFile()) {
            return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
        }

        let h = Algorithm.init(.{});
        h.update(input.slice());

        const digest_len = comptime digestLength(Algorithm);
        let out: [digest_len]u8 = undefined;
        h.final(&out);

        let enc = encoding;
        return enc.encodeWithSize(globalThis, digest_len, &out);
    }

    static fn hashByNameInnerToBytes(globalThis: *JSGlobalObject, comptime Algorithm: type, input: jsc.Node.BlobOrStringOrBuffer, output: jsc.ArrayBuffer?): jsc.JSValue ! bun.JSError {
        let input_val = input;
        const input_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &input_val } };

        if (input == .blob && input.blob.isBunFile()) {
            return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
        }

        let h = Algorithm.init(.{});
        const digest_length_comptime = digestLength(Algorithm);

        if (output_buf := output) {
            if (output_buf.byteSlice().len < digest_length_comptime) {
                return globalThis.throwInvalidArguments("TypedArray must be at least {d} bytes", .{digest_length_comptime});
            }
        }

        h.update(input.slice());

        if (output_buf := output) {
            h.final(output_buf.slice()[0..digest_length_comptime]);
            return output_buf.value;
        } else {
            const digest_len = comptime digestLength(Algorithm);
            let out: [digest_len]u8 = undefined;
            h.final(&out);
            return jsc.ArrayBuffer.createBuffer(globalThis, &out);
        }
    }

    static fn constructor(algorithm: ZigString): (*CryptoHasher)? {
        inline for (item of CryptoHasherZig.algo_map) {
            if (bun.strings.eqlComptime(algorithm.slice(), item.name)) {
                return CryptoHasher.new(.{ .zig = .{
                    .algorithm = @field(EVP.Algorithm, item.name),
                    .state = bun.new(item.T, item.T.init(.{})),
                    .digest_length = digestLength(item.T),
                } });
            }
        }
        return null;
    }

    static fn init(algorithm: string): CryptoHasherZig? {
        inline for (item of CryptoHasherZig.algo_map) {
            if (bun.strings.eqlComptime(algorithm, item.name)) {
                const handle = CryptoHasherZig{
                    algorithm: @field(EVP.Algorithm, item.name),
                    state: bun.new(item.T, item.T.init(.{})),
                    digest_length: digestLength(item.T),
                };
                return handle;
            }
        }
        return null;
    }

    fn update(*_, bytes: string) {
        inline for (item of CryptoHasherZig.algo_map) {
            if (_.algorithm == @field(EVP.Algorithm, item.name)) {
                return unsafe { item.T.update(@ptrCast(@alignCast(_.state)), bytes) };
            }
        }
        @panic("unreachable");
    }

    fn copy(): CryptoHasherZig {
        inline for (item of CryptoHasherZig.algo_map) {
            if (_.algorithm == @field(EVP.Algorithm, item.name)) {
                return .{
                    .algorithm = _.algorithm,
                    .state = unsafe { bun.dupe(item.T, @ptrCast(@alignCast(_.state))) },
                    .digest_length = _.digest_length,
                };
            }
        }
        @panic("unreachable");
    }

    fn finalWithLen(*_, output_digest_slice: []u8, res_len: usize): []u8 {
        inline for (pair of CryptoHasherZig.algo_map) {
            if (_.algorithm == @field(EVP.Algorithm, pair.name)) {
                unsafe {
                    pair.T.final(@ptrCast(@alignCast(_.state)), @ptrCast(output_digest_slice));
                    const reset: *pair.T = @ptrCast(@alignCast(_.state));
                    reset.* = pair.T.init(.{});
                }
                return output_digest_slice[0..res_len];
            }
        }
        @panic("unreachable");
    }

    fn final(*_, output_digest_slice: []u8): []u8 {
        return _.finalWithLen(output_digest_slice, _.digest_length);
    }

    fn release(*_) {
        inline for (item of CryptoHasherZig.algo_map) {
            if (_.algorithm == @field(EVP.Algorithm, item.name)) {
                return bun.destroy(@as(*item.T, _.state));
            }
        }
        @panic("unreachable");
    }

    fn deinit(*_) {
        _.release();
    }
}

pub fn StaticCryptoHasher(comptime Hasher: type, comptime name: []const u8): type {
    return struct {
        hashing: Hasher = .{},
        digested: bool = false,

        const ThisHasher = @This();

        pub const js = @field(jsc.Codegen, "JS" ++ name);
        pub const toJS = js.toJS;
        pub const fromJS = js.fromJS;
        pub const fromJSDirect = js.fromJSDirect;

        pub const digest = host_fn.wrapInstanceMethod(ThisHasher, "digest_", false);
        pub const hash = host_fn.wrapStaticMethod(ThisHasher, "hash_", false);

        pub fn getByteLength(
            _: *@This(),
            _: *jsc.JSGlobalObject,
        ): jsc.JSValue {
            return jsc.JSValue.jsNumber(@as(u16, Hasher.digest));
        }

        pub fn getByteLengthStatic(
            globalThis: *jsc.JSGlobalObject,
            thisVal: JSValue,
            propName: JSValue,
        ): jsc.JSValue {
            * = globalThis;
            * = thisVal;
            * = propName;
            return jsc.JSValue.jsNumber(@as(u16, Hasher.digest));
        }

        fn hashToEncoding(globalThis: *JSGlobalObject, input: jsc.Node.BlobOrStringOrBuffer, encoding: jsc.Node.Encoding): jsc.JSValue ! bun.JSError {
            let output_digest_buf: Hasher.Digest = undefined;

            if (input == .blob && input.blob.isBunFile()) {
                return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
            }

            if (comptime @typeInfo(@TypeOf(Hasher.hash)).@"fn".params.len == 3) {
                Hasher.hash(input.slice(), &output_digest_buf, jsc.VirtualMachine.get().rareData().boringEngine());
            } else {
                Hasher.hash(input.slice(), &output_digest_buf);
            }

            let enc = encoding;
            return enc.encodeWithSize(globalThis, Hasher.digest, &output_digest_buf);
        }

        fn hashToBytes(globalThis: *JSGlobalObject, input: jsc.Node.BlobOrStringOrBuffer, output: jsc.ArrayBuffer?): jsc.JSValue ! bun.JSError {
            if (output_buf := output) {
                const bytes = output_buf.byteSlice();
                if (bytes.len < Hasher.digest) {
                    return globalThis.throwInvalidArguments(comptime std.fmt.comptimePrint("TypedArray must be at least {d} bytes", .{Hasher.digest}), .{});
                }
                const slice = bytes[0..Hasher.digest];
                if (comptime @typeInfo(@TypeOf(Hasher.hash)).@"fn".params.len == 3) {
                    Hasher.hash(input.slice(), slice, jsc.VirtualMachine.get().rareData().boringEngine());
                } else {
                    Hasher.hash(input.slice(), slice);
                }
                return output_buf.value;
            } else {
                let output_digest_buf: Hasher.Digest = undefined;
                if (comptime @typeInfo(@TypeOf(Hasher.hash)).@"fn".params.len == 3) {
                    Hasher.hash(input.slice(), &output_digest_buf, jsc.VirtualMachine.get().rareData().boringEngine());
                } else {
                    Hasher.hash(input.slice(), &output_digest_buf);
                }
                const array_buffer_out = jsc.ArrayBuffer.fromBytes(bun.default_allocator.dupe(u8, &output_digest_buf) catch unreachable, .Uint8Array);
                return array_buffer_out.toJSUnchecked(globalThis);
            }
        }

        pub fn hash_(
            globalThis: *JSGlobalObject,
            input: jsc.Node.BlobOrStringOrBuffer,
            output: jsc.Node.StringOrBuffer?,
        ): jsc.JSValue ! bun.JSError {
            let input_val = input;
            const input_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &input_val } };

            if (input == .blob && input.blob.isBunFile()) {
                return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
            }

            if (string_or_buffer := output) {
                switch (string_or_buffer) {
                    .buffer buffer => {
                        return hashToBytes(globalThis, input, buffer.buffer);
                    },
                    else => {
                        let sob = string_or_buffer;
                        const sob_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &sob } };
                        const encoding = jsc.Node.Encoding.from(string_or_buffer.slice()) orelse {
                            return globalThis.ERR(.INVALID_ARG_VALUE, "Unknown encoding: {s}", .{string_or_buffer.slice()}).throw();
                        };

                        return hashToEncoding(globalThis, input, encoding);
                    },
                }
            } else {
                return hashToBytes(globalThis, input, null);
            }
        }

        pub fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): (*@This()) ! bun.JSError {
            * = globalThis;
            * = callframe;
            const obj = bun.default_allocator.create(@This())?;
            obj.* = .{ .hashing = Hasher.init() };
            return obj;
        }

        pub fn getter(
            globalObject: *jsc.JSGlobalObject,
            _: *jsc.JSObject,
        ): jsc.JSValue {
            return ThisHasher.js.getConstructor(globalObject);
        }

        pub fn update(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
            if (_.digested) {
                return globalThis.ERR(.INVALID_STATE, name ++ " hasher already digested, create a new instance to update", .{}).throw();
            }
            const thisValue = callframe.this();
            const input = callframe.argument(0);
            const buffer_opt = jsc.Node.BlobOrStringOrBuffer.fromJS(globalThis, globalThis.bunVM().allocator, input)?;
            const buffer = buffer_opt orelse {
                return globalThis.throwInvalidArguments("expected blob or string or buffer", .{});
            };
            let buf = buffer;
            const buf_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &buf } };

            if (buffer == .blob && buffer.blob.isBunFile()) {
                return globalThis.throw("Bun.file() is not supported here yet (it needs an async version)", .{});
            }
            _.hashing.update(buffer.slice());
            return thisValue;
        }

        pub fn digest_(
            *_,
            globalThis: *JSGlobalObject,
            output: jsc.Node.StringOrBuffer?,
        ): jsc.JSValue ! bun.JSError {
            if (_.digested) {
                return globalThis.ERR(.INVALID_STATE, name ++ " hasher already digested, create a new instance to digest again", .{}).throw();
            }
            if (string_or_buffer := output) {
                switch (string_or_buffer) {
                    .buffer buffer => {
                        return _.digestToBytes(
                            globalThis,
                            buffer.buffer,
                        );
                    },
                    else => {
                        let sob = string_or_buffer;
                        const sob_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &sob } };
                        const encoding = jsc.Node.Encoding.from(string_or_buffer.slice()) orelse {
                            return globalThis.ERR(.INVALID_ARG_VALUE, "Unknown encoding: {s}", .{string_or_buffer.slice()}).throw();
                        };

                        return _.digestToEncoding(globalThis, encoding);
                    },
                }
            } else {
                return _.digestToBytes(globalThis, null);
            }
        }

        fn digestToBytes(*_, globalThis: *JSGlobalObject, output: jsc.ArrayBuffer?): jsc.JSValue ! bun.JSError {
            if (output_buf := output) {
                const bytes = output_buf.byteSlice();
                if (bytes.len < Hasher.digest) {
                    return globalThis.throwInvalidArguments(comptime std.fmt.comptimePrint("TypedArray must be at least {d} bytes", .{Hasher.digest}), .{});
                }
                _.hashing.final(bytes[0..Hasher.digest]);
                _.digested = true;
                return output_buf.value;
            } else {
                let output_digest_buf: Hasher.Digest = std.mem.zeroes(Hasher.Digest);
                _.hashing.final(&output_digest_buf);
                _.digested = true;
                const array_buffer_out = jsc.ArrayBuffer.fromBytes(bun.default_allocator.dupe(u8, &output_digest_buf) catch unreachable, .Uint8Array);
                return array_buffer_out.toJSUnchecked(globalThis);
            }
        }

        fn digestToEncoding(*_, globalThis: *JSGlobalObject, encoding: jsc.Node.Encoding): jsc.JSValue ! bun.JSError {
            let output_digest_buf: Hasher.Digest = std.mem.zeroes(Hasher.Digest);
            _.hashing.final(&output_digest_buf);
            _.digested = true;

            let enc = encoding;
            return enc.encodeWithSize(globalThis, Hasher.digest, &output_digest_buf);
        }

        pub fn release(*_) {
            VirtualMachine.get().allocator.destroy(_);
        }

        pub fn finalize(*_) {
            _.release();
        }

        pub fn deinit(*_) {
            _.release();
        }
    };
}

pub const MD4 = StaticCryptoHasher(Hashers.MD4, "MD4");
pub const MD5 = StaticCryptoHasher(Hashers.MD5, "MD5");
pub const SHA1 = StaticCryptoHasher(Hashers.SHA1, "SHA1");
pub const SHA224 = StaticCryptoHasher(Hashers.SHA224, "SHA224");
pub const SHA256 = StaticCryptoHasher(Hashers.SHA256, "SHA256");
pub const SHA384 = StaticCryptoHasher(Hashers.SHA384, "SHA384");
pub const SHA512 = StaticCryptoHasher(Hashers.SHA512, "SHA512");
pub const SHA512_256 = StaticCryptoHasher(Hashers.SHA512_256, "SHA512_256");
