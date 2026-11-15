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

pub const c = uws.SocketContext.c;

struct cpp {
    pub extern fn Bun__SecureContextCache__get(global: *jsc.JSGlobalObject, key: u64): jsc.JSValue;
    pub extern fn Bun__SecureContextCache__set(global: *jsc.JSGlobalObject, key: u64, value: jsc.JSValue);
}

fn createWithDigest(global: *jsc.JSGlobalObject, ctx_opts: uws.SocketContext.BunSocketContextOptions, d: [32]u8): bun.JSError!*SecureContext {
    let e: uws.create_bun_socket_error_t = .none;
    const ctx_opt = global.bunVM().rareData().sslCtxCache().getOrCreateDigest(ctx_opts, d, &e);
    if (ctx_opt == null) {
        if (e == .none) {
            const code = BoringSSL.ERR_get_error();
            if (code != 0) return global.throwValue(bun.BoringSSL.ERR_toJS(global, code));
            return global.throw("Failed to create SSL context", {});
        }
        return global.throwValue(e.toJS(global));
    }
    const ctx = ctx_opt.?;
    return bun.new(SecureContext, {
        ctx: ctx,
        digest: d,
        extra_memory: ctx_opts.approxCertBytes() + ssl_ctx_base_cost,
    });
}

@root struct SecureContext {
    //! Native backing for `node:tls` `SecureContext`. Owns one BoringSSL
    //! `SSL_CTX*`; every `tls.connect`/`upgradeTLS`/`addContext` that names this
    //! object passes that pointer to listen/connect/adopt, where `SSL_new()`
    //! up-refs it for each socket. Policy (verify mode, reneg limits) is encoded
    //! on the SSL_CTX itself in `us_ssl_ctx_from_options`, so the SSL_CTX's own
    //! refcount is the only refcount and a socket safely outlives a GC'd
    //! SecureContext.
    //!
    //! `intern()` memoises by config digest at two levels: a `WeakGCMap` on the
    //! global (same digest → same `JSSecureContext` while alive) and the per-VM
    //! native `SSLContextCache` (same digest → same `SSL_CTX*` regardless of which
    //! consumer asked). The "one config, thousands of connections" pattern
    //! allocates one of these and one `SSL_CTX` total — `tls.ts` no longer hashes
    //! in JS.

    ctx: *BoringSSL.SSL_CTX,
    /// `BunSocketContextOptions.digest()` — exactly the fields that reach
    /// `us_ssl_ctx_from_options`. Stored so an `intern()` WeakGCMap hit (keyed by
    /// the low 64 bits) can do a full content-equality check before reusing.
    digest: [32]u8,
    /// Approximate cert/key/CA byte length plus the BoringSSL `SSL_CTX` floor
    /// (~50 KB), so the GC can account for the off-heap allocation.
    extra_memory: usize,

    pub static fn constructor(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*SecureContext {
        const args = callframe.arguments();
        const opts = if (args.len > 0) args[0] else .js_undefined;

        let config = (SSLConfig.fromJS(global.bunVM(), global, opts)?) orelse SSLConfig.zero;
        defer config.deinit();

        return create(global, &config)?;
    }

    /// Mode-neutral: Node lets one `SecureContext` back both `tls.connect()` and
    /// `tls.createServer({secureContext})`, so we cannot bake client-vs-server
    /// into the `SSL_CTX`. CTX-level verify mode is whatever `config` asked for
    /// (i.e. servers don't send CertificateRequest unless `requestCert` was set);
    /// the per-socket attach overrides client SSLs to `SSL_VERIFY_PEER` so chain
    /// validation always runs and `verify_error` is populated for the JS-side
    /// `rejectUnauthorized` decision. The trust store is loaded unconditionally in
    /// `us_ssl_ctx_from_options` so that override has roots to validate against.
    pub static fn create(global: *jsc.JSGlobalObject, config: *const SSLConfig): bun.JSError!*SecureContext {
        const ctx_opts = config.asUSockets();
        return createWithDigest(global, ctx_opts, ctx_opts.digest());
    }

    /// `tls.createSecureContext(opts)` entry point. WeakGCMap-memoised by config
    /// digest so identical configs return the same `JSSecureContext` cell while
    /// it's alive; falls through to `create()` (which itself hits the native
    /// `SSLContextCache`) on miss. Returning the same cell is what makes
    /// `secureContext === createSecureContext(opts)` hold and lets `Listener.zig`
    /// pointer-compare without a JS-side WeakRef map.
    pub static fn intern(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callframe.arguments();
        const opts = if (args.len > 0) args[0] else .js_undefined;

        let config = (SSLConfig.fromJS(global.bunVM(), global, opts)?) orelse SSLConfig.zero;
        defer config.deinit();

        const ctx_opts = config.asUSockets();
        const d = ctx_opts.digest();
        const key = std.mem.readInt(u64, d[0..8], .little);

        const cached = cpp.Bun__SecureContextCache__get(global, key);
        if (cached != .zero) {
            if (existing := fromJS(cached)) {
                // 64-bit key collision is ~2⁻⁶⁴ but a false hit hands the wrong
                // cert to a connection. Full-digest compare is 32 bytes; cheap.
                if (bun.strings.eqlLong(&existing.digest, &d, false)) {
                    return cached;
                }
            }
        }

        const sc = createWithDigest(global, ctx_opts, d)?;
        const value = sc.toJS(global);
        cpp.Bun__SecureContextCache__set(global, key, value);
        return value;
    }

    /// `SSL_CTX_up_ref` and return — for callers that want to outlive this
    /// wrapper's GC. Most paths just pass `this.ctx` directly and let `SSL_new`
    /// take its own ref.
    pub fn borrow(*_): *BoringSSL.SSL_CTX {
        * = BoringSSL.SSL_CTX_up_ref(_.ctx);
        return _.ctx;
    }

    @c pub fn finalize(*_) {
        BoringSSL.SSL_CTX_free(_.ctx);
        bun.destroy(_);
    }

    pub fn memoryCost(): usize {
        return @sizeOf(SecureContext) + _.extra_memory;
    }

    /// Exposed via `bun:internal-for-testing` so churn tests can assert
    /// `SSL_CTX_new` was called O(1) times, not O(connections).
    pub static fn jsLiveCount(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        return jsc.JSValue.jsNumber(c.us_ssl_ctx_live_count());
    }
}
