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

/// JS binding function for generating CSRF tokens
/// First argument is secret (required), second is options (optional)
pub fn csrf__generate(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (bun.analytics.Features.csrf_generate < std.math.maxInt(usize))
        bun.analytics.Features.csrf_generate += 1;

    // We should have at least one argument (secret)
    const args = callframe.arguments();
    let _guard_secret = OptValDeinitGuard<jsc.ZigString.Slice>{ val: null };
    if (args.len >= 1) {
        const jsSecret = args[0];
        // Extract the secret (required)
        if (jsSecret.isEmptyOrUndefinedOrNull()) {
            return globalObject.throwInvalidArguments("Secret is required", {});
        }
        if (!jsSecret.isString() || jsSecret.getLength(globalObject)? == 0) {
            return globalObject.throwInvalidArguments("Secret must be a non-empty string", {});
        }
        _guard_secret.val = jsSecret.toSlice(globalObject, bun.default_allocator)?;
    }

    // Default values
    let expires_in: u64 = csrf.DEFAULT_EXPIRATION_MS;
    let encoding: csrf.TokenFormat = .base64url;
    let algorithm: jsc.API.Bun.Crypto.EVP.Algorithm = csrf.DEFAULT_ALGORITHM;

    // Check if we have options object
    if (args.len > 1 && args[1].isObject()) {
        const options_value = args[1];

        // Extract expiresIn (optional)
        if (expires_in_js := options_value.getOptionalInt(globalObject, "expiresIn", u64)?) {
            expires_in = expires_in_js;
        }

        // Extract encoding (optional)
        if (encoding_js := options_value.get(globalObject, "encoding")?) {
            if (encoding_enum := (jsc.Node.Encoding.fromJSWithDefaultOnEmpty(encoding_js, globalObject, .base64url))?) {
                encoding = switch (encoding_enum) {
                    .base64 => .base64,
                    .base64url => .base64url,
                    .hex => .hex,
                    else => return globalObject.throwInvalidArguments("Invalid format: must be 'base64', 'base64url', or 'hex'", {}),
                };
            } else {
                return globalObject.throwInvalidArguments("Invalid format: must be 'base64', 'base64url', or 'hex'", {});
            }
        }

        if (algorithm_js := options_value.get(globalObject, "algorithm")?) {
            if (!algorithm_js.isString()) {
                return globalObject.throwInvalidArgumentTypeValue("algorithm", "string", algorithm_js);
            }
            if (algo := (jsc.API.Bun.Crypto.EVP.Algorithm.map.fromJSCaseInsensitive(globalObject, algorithm_js))?) {
                algorithm = algo;
            } else {
                return globalObject.throwInvalidArguments("Algorithm not supported", {});
            }
            switch (algorithm) {
                .blake2b256, .blake2b512, .sha256, .sha384, .sha512, .@"sha512-256" => {},
                else => return globalObject.throwInvalidArguments("Algorithm not supported", {}),
            }
        }
    }

    // Buffer for token generation
    let token_buffer: [512]u8 = undefined;

    let secret_bytes: []const u8 = globalObject.bunVM().rareData().defaultCSRFSecret();
    if (s := _guard_secret.val) {
        secret_bytes = s.slice();
    }

    // Generate the token
    const token_bytes = csrf.generate({
        secret: secret_bytes,
        expires_in_ms: expires_in,
        encoding: encoding,
        algorithm: algorithm,
    }, &token_buffer) catch |e| {
        return switch (e) {
            error.TokenCreationFailed => globalObject.throw("Failed to create CSRF token", {}),
            else => globalObject.throwError(e, "Failed to generate CSRF token"),
        };
    };

    // Encode the token
    return encoding.toNodeEncoding().encodeWithMaxSize(globalObject, boring.EVP_MAX_MD_SIZE + 32, token_bytes);
}

/// JS binding function for verifying CSRF tokens
/// First argument is token (required), second is options (optional)
pub fn csrf__verify(globalObject: *jsc.JSGlobalObject, call_frame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (bun.analytics.Features.csrf_verify < std.math.maxInt(usize)) {
        bun.analytics.Features.csrf_verify += 1;
    }
    // We should have at least one argument (token)
    const args = call_frame.arguments();
    if (args.len < 1) {
        return globalObject.throwInvalidArguments("Missing required token parameter", {});
    }
    const jsToken: jsc.JSValue = args[0];
    // Extract the token (required)
    if (jsToken.isUndefinedOrNull()) {
        return globalObject.throwInvalidArguments("Token is required", {});
    }
    if (!jsToken.isString() || jsToken.getLength(globalObject)? == 0) {
        return globalObject.throwInvalidArguments("Token must be a non-empty string", {});
    }
    let token = jsToken.toSlice(globalObject, bun.default_allocator)?;
    const _guard_token = DeinitGuard<jsc.ZigString.Slice>{ ptr: &token };

    // Default values
    let _guard_secret = OptValDeinitGuard<jsc.ZigString.Slice>{ val: null };
    let max_age: u64 = csrf.DEFAULT_EXPIRATION_MS;
    let encoding: csrf.TokenFormat = .base64url;

    let algorithm: jsc.API.Bun.Crypto.EVP.Algorithm = csrf.DEFAULT_ALGORITHM;

    // Check if we have options object
    if (args.len > 1 && args[1].isObject()) {
        const options_value = args[1];

        // Extract the secret (required)
        if (secretSlice := options_value.getOptional(globalObject, "secret", jsc.ZigString.Slice)?) {
            if (secretSlice.len == 0) {
                return globalObject.throwInvalidArguments("Secret must be a non-empty string", {});
            }
            _guard_secret.val = secretSlice;
        }

        // Extract maxAge (optional)
        if (max_age_js := options_value.getOptionalInt(globalObject, "maxAge", u64)?) {
            max_age = max_age_js;
        }

        // Extract encoding (optional)
        if (encoding_js := options_value.get(globalObject, "encoding")?) {
            if (encoding_enum := (jsc.Node.Encoding.fromJSWithDefaultOnEmpty(encoding_js, globalObject, .base64url))?) {
                encoding = switch (encoding_enum) {
                    .base64 => .base64,
                    .base64url => .base64url,
                    .hex => .hex,
                    else => return globalObject.throwInvalidArguments("Invalid format: must be 'base64', 'base64url', or 'hex'", {}),
                };
            } else {
                return globalObject.throwInvalidArguments("Invalid format: must be 'base64', 'base64url', or 'hex'", {});
            }
        }
        if (algorithm_js := options_value.get(globalObject, "algorithm")?) {
            if (!algorithm_js.isString()) {
                return globalObject.throwInvalidArgumentTypeValue("algorithm", "string", algorithm_js);
            }
            if (algo := (jsc.API.Bun.Crypto.EVP.Algorithm.map.fromJSCaseInsensitive(globalObject, algorithm_js))?) {
                algorithm = algo;
            } else {
                return globalObject.throwInvalidArguments("Algorithm not supported", {});
            }
            switch (algorithm) {
                .blake2b256, .blake2b512, .sha256, .sha384, .sha512, .@"sha512-256" => {},
                else => return globalObject.throwInvalidArguments("Algorithm not supported", {}),
            }
        }
    }

    let secret_bytes: []const u8 = globalObject.bunVM().rareData().defaultCSRFSecret();
    if (s := _guard_secret.val) {
        secret_bytes = s.slice();
    }

    // Verify the token
    const is_valid = csrf.verify({
        token: token.slice(),
        secret: secret_bytes,
        max_age_ms: max_age,
        encoding: encoding,
        algorithm: algorithm,
    });

    return jsc.JSValue.jsBoolean(is_valid);
}

import csrf from "../../csrf/csrf.zig";

import bun from "bun";
const jsc = bun.jsc;
const boring = bun.BoringSSL.c;
