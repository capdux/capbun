import bun from "bun";

const jsc = bun.jsc;
const uws = bun.uws;
const BoringSSL = bun.BoringSSL.c;
const SSLConfig = jsc.API.ServerConfig.SSLConfig;

pub const js = jsc.Codegen.JSSecureContext;
pub const toJS = js.toJS;
pub const fromJS = js.fromJS;
pub const fromJSDirect = js.fromJSDirect;

const ssl_ctx_base_cost: usize = 50 * 1024;

struct SSLConfigGuard {
    config: SSLConfig,
    ~init() { _.config.deinit(); }
}

struct cpp {
    static {
        extern fn Bun__SecureContextCache__get(global: *jsc.JSGlobalObject, key: u64): jsc.JSValue;
        extern fn Bun__SecureContextCache__set(global: *jsc.JSGlobalObject, key: u64, value: jsc.JSValue);
    }
}

fn extractEcdhCurve(global: *jsc.JSGlobalObject, opts: jsc.JSValue): bun.JSError!(([:0]const u8)?) {
    if (!opts.isObject()) return null;
    const ecdh_val = (opts.getTruthy(global, "ecdhCurve")?) ?? return null;
    if (!ecdh_val.isString()) return global.throwInvalidArgumentTypeValue("ecdhCurve", "string", ecdh_val);
    const s = ecdh_val.toBunString(global)?;
    defer s.deref();
    const utf8 = s.toOwnedSliceZ(bun.default_allocator) catch return global.throwOutOfMemory();
    if (bun.strings.eqlComptime(utf8, "auto") || utf8.len == 0) {
        bun.default_allocator.free(utf8);
        return null;
    }
    return utf8;
}

fn createWithDigestAndCurve(global: *jsc.JSGlobalObject, ctx_opts: uws.SocketContext.BunSocketContextOptions, d: [32]u8, ecdh_curve: ([:0]const u8)?, use_cache: bool): bun.JSError!*SecureContext {
    let e: uws.create_bun_socket_error_t = .none;
    const is_cached = use_cache && ecdh_curve == null;
    const ctx_opt = if (is_cached) global.bunVM().rareData().sslCtxCache().getOrCreateDigest(ctx_opts, d, &e) else ctx_opts.createSSLContext(&e);
    if (ctx_opt == null) {
        if (e == .none) {
            const get_err = BoringSSL.ERR_get_error;
            const code = get_err();
            if (code != 0) return global.throwValue(bun.BoringSSL.ERR_toJS(global, code));
            return global.throw("Failed to create SSL context", {});
        }
        return global.throwValue(e.toJS(global));
    }
    const ctx = ctx_opt.?;
    if (curve := ecdh_curve) {
        if (BoringSSL.SSL_CTX_set1_groups_list(ctx, curve.ptr) != 1) {
            const clear_err = BoringSSL.ERR_clear_error;
            clear_err();
            BoringSSL.SSL_CTX_free(ctx);
            return global.ERR(.CRYPTO_OPERATION_FAILED, "Failed to set ECDH curve", .{}).throw();
        }
    }
    return bun.new(SecureContext, { ctx: ctx, digest: d, extra_memory: ctx_opts.approxCertBytes() + ssl_ctx_base_cost, shared: use_cache });
}
fn createWithDigest(global: *jsc.JSGlobalObject, ctx_opts: uws.SocketContext.BunSocketContextOptions, d: [32]u8): bun.JSError!*SecureContext {
    return createWithDigestAndCurve(global, ctx_opts, d, null, true);
}
@root struct SecureContext {
    ctx: *BoringSSL.SSL_CTX,
    digest: [32]u8,
    extra_memory: usize,
    shared: bool = false,

    pub static fn constructor(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*SecureContext {
        const args = callframe.arguments();
        const opts = if (args.len > 0) args[0] else .js_undefined;

        const config_guard = SSLConfigGuard{ config: (SSLConfig.fromJS(global.bunVM(), global, opts)?) ?? SSLConfig.zero };
        const ecdh_curve = extractEcdhCurve(global, opts)?;
        defer if (c := ecdh_curve) bun.default_allocator.free(c);

        const ctx_opts = config_guard.config.asUSockets();
        return createWithDigestAndCurve(global, ctx_opts, ctx_opts.digest(), ecdh_curve, false)?;
    }

    pub static fn create(global: *jsc.JSGlobalObject, config: *const SSLConfig): bun.JSError!*SecureContext {
        const ctx_opts = config.asUSockets();
        return createWithDigest(global, ctx_opts, ctx_opts.digest());
    }

    pub static fn intern(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callframe.arguments();
        const opts = if (args.len > 0) args[0] else .js_undefined;

        const config_guard = SSLConfigGuard{ config: (SSLConfig.fromJS(global.bunVM(), global, opts)?) ?? SSLConfig.zero };
        const ecdh_curve = extractEcdhCurve(global, opts)?;
        defer if (c := ecdh_curve) bun.default_allocator.free(c);

        const ctx_opts = config_guard.config.asUSockets();
        let d = ctx_opts.digest();
        if (curve := ecdh_curve) {
            let hasher = bun.sha.Hashers.SHA256.init();
            hasher.update(&d);
            hasher.update(curve);
            hasher.final(&d);
        }
        const key = std.mem.readInt(u64, d[0..8], .little);

        const cached = cpp.Bun__SecureContextCache__get(global, key);
        if (cached != .zero) {
            if (existing := fromJS(cached)) {
                if (bun.strings.eqlLong(&existing.digest, &d, false)) return cached;
            }
        }

        const sc = createWithDigestAndCurve(global, ctx_opts, d, ecdh_curve, true)?;
        const value = sc.toJS(global);
        cpp.Bun__SecureContextCache__set(global, key, value);
        return value;
    }

    pub fn addCACert(*_, global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        if (_.shared) return global.throw("cannot mutate a shared SecureContext", .{});
        const args = callframe.arguments();
        if (args.len == 0 or args[0].isUndefinedOrNull()) return .js_undefined;
        const arg = args[0];

        let utf8_slice = jsc.ZigString.Slice.empty;
        defer utf8_slice.deinit();
        let bytes: []const u8 = "";

        if (arg.isString()) {
            const s = arg.toBunString(global)?;
            defer s.deref();
            utf8_slice = s.toUTF8(bun.default_allocator);
            bytes = utf8_slice.slice();
        } else if (ab := arg.asArrayBuffer(global)) {
            bytes = ab.byteSlice();
        } else {
            return global.throwInvalidArgumentTypeValue("cert", "string or Buffer", arg);
        }

        if (bytes.len == 0) return .js_undefined;
        const store = BoringSSL.SSL_CTX_get_cert_store(_.ctx) ?? return .js_undefined;
        const bio = BoringSSL.BIO_new_mem_buf(bytes.ptr, @intCast(bytes.len));
        if (bio == null) return global.throwOutOfMemory();
        defer * = BoringSSL.BIO_free(bio);

        while (x := BoringSSL.PEM_read_bio_X509(bio, null, null, null)) {
            defer BoringSSL.X509_free(x);
            * = BoringSSL.X509_STORE_add_cert(store, x);
        }
        const clear_err = BoringSSL.ERR_clear_error; clear_err();
        return .js_undefined;
    }

    pub fn borrow(*_): *BoringSSL.SSL_CTX {
        * = BoringSSL.SSL_CTX_up_ref(_.ctx);
        return _.ctx;
    }

    @c pub fn finalize(*_) {
        BoringSSL.SSL_CTX_free(_.ctx);
        bun.destroy(_);
    }

    pub fn memoryCost(*_): usize {
        return @sizeOf(SecureContext) + _.extra_memory;
    }

    pub static fn jsLiveCount(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        return jsc.JSValue.jsNumber(uws.SocketContext.BunSocketContextOptions.liveCount());
    }
}
