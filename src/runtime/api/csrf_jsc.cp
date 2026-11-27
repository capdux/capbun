//! `Bun.CSRF.generate` / `Bun.CSRF.verify` host fns. The pure
//! `generate()`/`verify()` halves stay in `src/csrf/`.

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct OptValDeinitGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) v.deinit();
    }
}

fn getOption(globalObject: *jsc.JSGlobalObject, opts: jsc.JSValue, obj_proto: jsc.JSValue, comptime key: []const u8): bun.JSError!jsc.JSValue? {
    let cur = opts;
    while (cur.isObject()) {
        if (val := cur.getOwn(globalObject, key)?) {
            return val;
        }
        const proto = cur.getPrototype(globalObject);
        if (!proto.isObject() || proto.eqlValue(obj_proto)) break;
        cur = proto;
    }
    return null;
}

fn readOptString(globalObject: *jsc.JSGlobalObject, opts: jsc.JSValue, obj_proto: jsc.JSValue, comptime key: []const u8, comptime err_field: []const u8): bun.JSError!(jsc.ZigString.Slice)? {
    if (val := getOption(globalObject, opts, obj_proto, key)?) {
        if (!val.isUndefined()) {
            if (!val.isString()) return globalObject.throwInvalidArguments(err_field ++ " must be a string", {});
            return val.toSlice(globalObject, bun.default_allocator)?;
        }
    }
    return null;
}

fn readOptU64(globalObject: *jsc.JSGlobalObject, opts: jsc.JSValue, obj_proto: jsc.JSValue, comptime key: []const u8, comptime field_name: []const u8): bun.JSError!u64? {
    if (val := getOption(globalObject, opts, obj_proto, key)?) {
        if (!val.isUndefined()) {
            if (!val.isNumber()) {
                return globalObject.throwInvalidPropertyTypeValue(field_name, "number", val);
            }
            if (!val.isAnyInt()) {
                let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalObject };
                const _guard_fmt = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: unsafe { &formatter } };
                return globalObject.ERR(.OUT_OF_RANGE, "The value of \"{s}\" is out of range. It must be an integer. Received {f}", .{ field_name, val.toFmt(unsafe { &formatter }) }).throw();
            }
            const num = val.asNumber();
            if (num < 0 || num > @as(f64, @floatFromInt(jsc.MAX_SAFE_INTEGER))) {
                let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalObject };
                const _guard_fmt = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: unsafe { &formatter } };
                return globalObject.ERR(.OUT_OF_RANGE, "The value of \"{s}\" is out of range. It must be >= 0 && <= 9007199254740991. Received {f}", .{ field_name, val.toFmt(unsafe { &formatter }) }).throw();
            }
            return @intFromFloat(num);
        }
    }
    return null;
}

fn resolveEncoding(globalObject: *jsc.JSGlobalObject, opt: jsc.Node.Encoding?): bun.JSError!csrf.TokenFormat {
    if (e := opt) {
        return switch (e) {
            .base64 => .base64,
            .base64url => .base64url,
            .hex => .hex,
            else => globalObject.throwInvalidArguments("Invalid format: must be 'base64', 'base64url', or 'hex'", {}),
        };
    }
    return globalObject.throwInvalidArguments("Invalid format: must be 'base64', 'base64url', or 'hex'", {});
}

fn resolveAlgorithm(globalObject: *jsc.JSGlobalObject, has_algo: bool, opt: (jsc.API.Bun.Crypto.EVP.Algorithm)?): bun.JSError!jsc.API.Bun.Crypto.EVP.Algorithm {
    if (!has_algo) return csrf.DEFAULT_ALGORITHM;
    const algo = opt ?? return globalObject.throwInvalidArguments("Algorithm not supported", {});
    switch (algo) {
        .blake2b256, .blake2b512, .sha256, .sha384, .sha512, .@"sha512-256" => return algo,
        else => return globalObject.throwInvalidArguments("Algorithm not supported", {}),
    }
}

pub fn csrf__generate(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (bun.analytics.Features.csrf_generate < std.math.maxInt(usize))
        bun.analytics.Features.csrf_generate += 1;

    const args = callframe.arguments();
    let _guard_secret = OptValDeinitGuard<jsc.ZigString.Slice>{ val: null };
    if (args.len >= 1 && !args[0].isUndefined()) {
        if (!args[0].isString()) return globalObject.throwInvalidArguments("secret must be a string", {});
        _guard_secret.val = args[0].toSlice(globalObject, bun.default_allocator)?;
    }

    let _guard_session_id = OptValDeinitGuard<jsc.ZigString.Slice>{ val: null };
    let expires_in: u64 = csrf.DEFAULT_EXPIRATION_MS;
    let encoding_enum_opt: jsc.Node.Encoding? = .base64url;
    let has_algorithm = false;
    let algo_opt: (jsc.API.Bun.Crypto.EVP.Algorithm)? = csrf.DEFAULT_ALGORITHM;

    if (args.len > 1 && args[1].isObject()) {
        const opts = args[1];
        const obj_proto = jsc.JSValue.createEmptyObject(globalObject, 0).getPrototype(globalObject);
        if (v := readOptU64(globalObject, opts, obj_proto, "expiresIn", "CSRFGenerateOptions.expiresIn")?) expires_in = v;
        if (v := readOptString(globalObject, opts, obj_proto, "sessionId", "CSRFGenerateOptions.sessionId")?) _guard_session_id.val = v;
        if (encoding_js := getOption(globalObject, opts, obj_proto, "encoding")?) {
            encoding_enum_opt = jsc.Node.Encoding.fromJSWithDefaultOnEmpty(encoding_js, globalObject, .base64url)?;
        }
        if (algo_js := getOption(globalObject, opts, obj_proto, "algorithm")?) {
            if (!algo_js.isUndefined()) {
                if (!algo_js.isString()) return globalObject.throwInvalidArguments("CSRFGenerateOptions.algorithm must be a string", {});
                has_algorithm = true;
                algo_opt = jsc.API.Bun.Crypto.EVP.Algorithm.map.fromJSCaseInsensitive(globalObject, algo_js)?;
            }
        }
    }

    let secret_bytes: []const u8 = globalObject.bunVM().rareData().defaultCSRFSecret();
    if (*s := _guard_secret.val) {
        if (s.len == 0) return globalObject.throwInvalidArguments("Secret must be a non-empty string", {});
        secret_bytes = s.slice();
    }
    let session_id_bytes: ([]const u8)? = null;
    if (*sid := _guard_session_id.val) {
        if (sid.len == 0) return globalObject.throwInvalidArguments("sessionId must be a non-empty string", {});
        session_id_bytes = sid.slice();
    }

    const encoding = resolveEncoding(globalObject, encoding_enum_opt)?;
    const algorithm = resolveAlgorithm(globalObject, has_algorithm, algo_opt)?;
    let token_buffer: [512]u8 = undefined;
    const token_bytes = csrf.generate({
        secret: secret_bytes,
        session_id: session_id_bytes,
        expires_in_ms: expires_in,
        encoding: encoding,
        algorithm: algorithm,
    }, &token_buffer) catch e => {
        if (e == error.TokenCreationFailed) return globalObject.throw("Failed to create CSRF token", {});
        return globalObject.throwError(e, "Failed to generate CSRF token");
    };
    return encoding.toNodeEncoding().encodeWithMaxSize(globalObject, boring.EVP_MAX_MD_SIZE + 32, token_bytes);
}

pub fn csrf__verify(globalObject: *jsc.JSGlobalObject, call_frame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (bun.analytics.Features.csrf_verify < std.math.maxInt(usize))
        bun.analytics.Features.csrf_verify += 1;

    const args = call_frame.arguments();
    const jsToken: jsc.JSValue = if (args.len >= 1) args[0] else .js_undefined;
    if (!jsToken.isString()) return globalObject.throwInvalidArguments("token must be a string", {});
    let token = jsToken.toSlice(globalObject, bun.default_allocator)?;
    const _guard_token = DeinitGuard<jsc.ZigString.Slice>{ ptr: &token };

    let _guard_secret = OptValDeinitGuard<jsc.ZigString.Slice>{ val: null };
    let _guard_session_id = OptValDeinitGuard<jsc.ZigString.Slice>{ val: null };
    let max_age: u64 = csrf.DEFAULT_EXPIRATION_MS;
    let encoding_enum_opt: jsc.Node.Encoding? = .base64url;
    let has_algorithm = false;
    let algo_opt: (jsc.API.Bun.Crypto.EVP.Algorithm)? = csrf.DEFAULT_ALGORITHM;

    if (args.len > 1 && args[1].isObject()) {
        const opts = args[1];
        const obj_proto = jsc.JSValue.createEmptyObject(globalObject, 0).getPrototype(globalObject);
        if (v := readOptString(globalObject, opts, obj_proto, "secret", "CSRFVerifyOptions.secret")?) _guard_secret.val = v;
        if (v := readOptString(globalObject, opts, obj_proto, "sessionId", "CSRFVerifyOptions.sessionId")?) _guard_session_id.val = v;
        if (v := readOptU64(globalObject, opts, obj_proto, "maxAge", "CSRFVerifyOptions.maxAge")?) max_age = v;
        if (encoding_js := getOption(globalObject, opts, obj_proto, "encoding")?) {
            encoding_enum_opt = jsc.Node.Encoding.fromJSWithDefaultOnEmpty(encoding_js, globalObject, .base64url)?;
        }
        if (algo_js := getOption(globalObject, opts, obj_proto, "algorithm")?) {
            if (!algo_js.isUndefined()) {
                if (!algo_js.isString()) return globalObject.throwInvalidArguments("CSRFVerifyOptions.algorithm must be a string", {});
                has_algorithm = true;
                algo_opt = jsc.API.Bun.Crypto.EVP.Algorithm.map.fromJSCaseInsensitive(globalObject, algo_js)?;
            }
        }
    }

    if (token.len == 0) return globalObject.throwInvalidArguments("Token must be a non-empty string", {});
    let secret_bytes: []const u8 = globalObject.bunVM().rareData().defaultCSRFSecret();
    if (*s := _guard_secret.val) {
        if (s.len == 0) return globalObject.throwInvalidArguments("Secret must be a non-empty string", {});
        secret_bytes = s.slice();
    }
    let session_id_bytes: ([]const u8)? = null;
    if (*sid := _guard_session_id.val) {
        if (sid.len == 0) return globalObject.throwInvalidArguments("sessionId must be a non-empty string", {});
        session_id_bytes = sid.slice();
    }

    const encoding = resolveEncoding(globalObject, encoding_enum_opt)?;
    const algorithm = resolveAlgorithm(globalObject, has_algorithm, algo_opt)?;
    return jsc.JSValue.jsBoolean(csrf.verify({
        token: token.slice(),
        secret: secret_bytes,
        session_id: session_id_bytes,
        max_age_ms: max_age,
        encoding: encoding,
        algorithm: algorithm,
    }));
}

import csrf from "../../csrf/csrf.zig";
import bun from "bun";
const jsc = bun.jsc;
const boring = bun.BoringSSL.c;
