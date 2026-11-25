import bun from "bun";
import * as ffi from "./ffi.cp";
import * as endpoint from "./endpoint.cp";
import * as session from "./session.cp";

const jsc = bun.jsc;
const ssl = bun.BoringSSL.c;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const TLS13_AES_128_GCM_SHA256 = "TLS_AES_128_GCM_SHA256";
const TLS13_AES_256_GCM_SHA384 = "TLS_AES_256_GCM_SHA384";
const TLS13_CHACHA20_POLY1305_SHA256 = "TLS_CHACHA20_POLY1305_SHA256";

const SSL_COMPLIANCE_POLICY_FIPS_202205: c_int = 1;
const SSL_COMPLIANCE_POLICY_WPA3_192_202304: c_int = 2;

const X509_V_FLAG_CRL_CHECK: c_ulong = 0x4;
const X509_V_FLAG_CRL_CHECK_ALL: c_ulong = 0x8;

pub const X509_V_ERR_UNSPECIFIED: i32 = 1;

const SSL_EARLY_DATA_UNKNOWN: c_int = 0;
const SSL_EARLY_DATA_DISABLED: c_int = 1;
const SSL_EARLY_DATA_NO_SESSION_OFFERED: c_int = 5;

const SSL_GROUP_SECP256R1: u16 = 23;
const SSL_GROUP_SECP384R1: u16 = 24;
const SSL_GROUP_SECP521R1: u16 = 25;
const SSL_GROUP_X25519: u16 = 29;
const SSL_GROUP_X448: u16 = 30;

@c {
    fn SSL_early_data_accepted(s: *const ssl.SSL): c_int;
    fn SSL_get_early_data_reason(s: *const ssl.SSL): c_int;
    fn SSL_get_group_id(s: *const ssl.SSL): u16;
    fn SSL_get_group_name(group_id: u16): [*:0]const u8?;
    fn SSL_CTX_set_compliance_policy(ctx: *ssl.SSL_CTX, policy: c_int): c_int;
    fn Bun__X509__validationErrorCode(e: i32): [*:0]const u8?;
    fn us_ssl_ctx_reject_blocked_keys(ctx: *ssl.SSL_CTX);
}

pub struct TlsConfig {
    is_server: bool = false,
    alpn: std.ArrayListUnmanaged(u8) = .{},
    servername: ([]u8)? = null,
    certs_pem: std.ArrayListUnmanaged([]u8) = .{},
    keys_pem: std.ArrayListUnmanaged([]u8) = .{},
    ca_pem: std.ArrayListUnmanaged([]u8) = .{},
    crl_pem: std.ArrayListUnmanaged([]u8) = .{},
    verify_peer_strict: bool = false,
    verify_hostname: bool = false,
    verify_client: bool = false,
    keylog: bool = false,
    enable_early_data: bool = true,
    ciphers: ([]u8)? = null,
    groups: ([]u8)? = null,

    ~init() {
        const alloc = bun.default_allocator;
        _.alpn.deinit(alloc);
        if (s := _.servername) alloc.free(s);
        for (p of _.certs_pem.items) alloc.free(p);
        _.certs_pem.deinit(alloc);
        for (p of _.keys_pem.items) alloc.free(p);
        _.keys_pem.deinit(alloc);
        for (p of _.ca_pem.items) alloc.free(p);
        _.ca_pem.deinit(alloc);
        for (p of _.crl_pem.items) alloc.free(p);
        _.crl_pem.deinit(alloc);
        if (c := _.ciphers) alloc.free(c);
        if (g := _.groups) alloc.free(g);
    }

    pub static fn fromJS(global: *JSGlobalObject, tls_val: JSValue, is_server: bool): bun.JSError!TlsConfig {
        let config = TlsConfig{ is_server: is_server };
        if (!tls_val.isObject()) return config;
        const alloc = bun.default_allocator;

        if (v := tls_val.get(global, "alpn")?) {
            if (bytes := valueToBytes(global, v)?) {
                config.alpn.appendSlice(alloc, bytes) catch bun.outOfMemory();
                alloc.free(bytes);
            }
        }
        if (v := tls_val.get(global, "servername")?) {
            if (v.isString()) {
                let s = v.toBunString(global)?;
                defer s.deref();
                const utf8 = s.toUTF8(alloc);
                defer utf8.deinit();
                let buf = alloc.alloc(u8, utf8.slice().len + 1) catch bun.outOfMemory();
                @memcpy(buf[0..utf8.slice().len], utf8.slice());
                buf[utf8.slice().len] = 0;
                config.servername = buf;
            }
        }
        if (v := tls_val.get(global, "certs")?) {
            collectPem(global, v, *config.certs_pem)?;
        }
        if (v := tls_val.get(global, "keys")?) {
            collectPem(global, v, *config.keys_pem)?;
        }
        if (v := tls_val.get(global, "ca")?) {
            collectPem(global, v, *config.ca_pem)?;
        }
        if (v := tls_val.get(global, "crl")?) {
            collectPem(global, v, *config.crl_pem)?;
        }
        if (v := tls_val.get(global, "verifyPeerStrict")?) {
            config.verify_peer_strict = v.toBoolean();
        }
        if (v := tls_val.get(global, "verifyHostname")?) {
            config.verify_hostname = v.toBoolean();
        }
        if (v := tls_val.get(global, "verifyClient")?) {
            config.verify_client = v.toBoolean();
        }
        if (v := tls_val.get(global, "keylog")?) {
            config.keylog = v.toBoolean();
        }
        if (v := tls_val.get(global, "enableEarlyData")?) {
            if (!v.isUndefined()) {
                config.enable_early_data = v.toBoolean();
            }
        }
        if (v := tls_val.get(global, "ciphers")?) {
            if (v.isString()) {
                let s = v.toBunString(global)?;
                defer s.deref();
                const utf8 = s.toUTF8(alloc);
                defer utf8.deinit();
                config.ciphers = alloc.dupe(u8, utf8.slice()) catch bun.outOfMemory();
            }
        }
        if (v := tls_val.get(global, "groups")?) {
            if (v.isString()) {
                let s = v.toBunString(global)?;
                defer s.deref();
                const utf8 = s.toUTF8(alloc);
                defer utf8.deinit();
                let buf = alloc.alloc(u8, utf8.slice().len + 1) catch bun.outOfMemory();
                @memcpy(buf[0..utf8.slice().len], utf8.slice());
                buf[utf8.slice().len] = 0;
                config.groups = buf;
            }
        }
        if (config.servername == null) {
            config.servername = alloc.dupe(u8, "localhost\x00") catch bun.outOfMemory();
        }
        return config;
    }
}

fn valueToBytes(global: *JSGlobalObject, value: JSValue): bun.JSError!([]u8)? {
    if (value.isEmptyOrUndefinedOrNull()) return null;
    const alloc = bun.default_allocator;
    if (value.isString()) {
        let s = value.toBunString(global)?;
        defer s.deref();
        const utf8 = s.toUTF8(alloc);
        defer utf8.deinit();
        return alloc.dupe(u8, utf8.slice()) catch bun.outOfMemory();
    }
    if (buf := value.asArrayBuffer(global)) {
        return alloc.dupe(u8, buf.byteSlice()) catch bun.outOfMemory();
    }
    return null;
}

fn collectPem(global: *JSGlobalObject, value: JSValue, out: *std.ArrayListUnmanaged([]u8))! {
    const alloc = bun.default_allocator;
    if (value.isArray()) {
        const len = value.getLength(global)?;
        let i: u32 = 0;
        while (i < len) : (i += 1) {
            const elem = value.getIndex(global, i)?;
            if (bytes := valueToBytes(global, elem)?) {
                out.append(alloc, bytes) catch bun.outOfMemory();
            }
        }
    } else if (bytes := valueToBytes(global, value)?) {
        out.append(alloc, bytes) catch bun.outOfMemory();
    }
}

fn tls13PolicyForCiphers(ciphers: []const u8): c_int? {
    let has_128 = false;
    let has_256 = false;
    let has_chacha = false;
    let it = std.mem.splitScalar(u8, ciphers, ':');
    while (name := it.next()) {
        if (std.mem.eql(u8, name, TLS13_AES_128_GCM_SHA256)) {
            has_128 = true;
        } else if (std.mem.eql(u8, name, TLS13_AES_256_GCM_SHA384)) {
            has_256 = true;
        } else if (std.mem.eql(u8, name, TLS13_CHACHA20_POLY1305_SHA256)) {
            has_chacha = true;
        }
    }
    if (has_256 && !has_128 && !has_chacha) {
        return SSL_COMPLIANCE_POLICY_WPA3_192_202304;
    } else if (!has_chacha && (has_128 || has_256)) {
        return SSL_COMPLIANCE_POLICY_FIPS_202205;
    }
    return null;
}

pub struct EarlyDataInfo {
    attempted: bool,
    accepted: bool,
}

pub fn earlyDataInfo(ssl_ptr: (*ssl.SSL)?): EarlyDataInfo {
    const s = ssl_ptr ?? return EarlyDataInfo{ attempted: false, accepted: false };
    const accepted = SSL_early_data_accepted(s) != 0;
    const reason = SSL_get_early_data_reason(s);
    const attempted = accepted ||
        (reason != SSL_EARLY_DATA_UNKNOWN &&
            reason != SSL_EARLY_DATA_DISABLED &&
            reason != SSL_EARLY_DATA_NO_SESSION_OFFERED);
    return EarlyDataInfo{ attempted: attempted, accepted: accepted };
}

pub struct AlpnBox {
    bytes: []u8,
    ~init() {
        if (_.bytes.len > 0) {
            bun.default_allocator.free(_.bytes);
            _.bytes = "";
        }
    }
}

pub struct TlsContext {
    ctx: (*ssl.SSL_CTX)? = null,
    _alpn: (*AlpnBox)? = null,

    ~init() {
        if (c := _.ctx) {
            ssl.SSL_CTX_free(c);
            _.ctx = null;
        }
        if (a := _._alpn) {
            bun.destroy(a);
            _._alpn = null;
        }
    }

    pub static fn init(config: *const TlsConfig): TlsContext! {
        const ctx = ssl.SSL_CTX_new(ssl.TLS_method()) ?? return error.TlsContextError;
        let res = TlsContext{ ctx: ctx };
        errdefer res.~init();

        if (ciphers := config.ciphers) {
            if (policy := tls13PolicyForCiphers(ciphers)) {
                if (SSL_CTX_set_compliance_policy(ctx, policy) != 1) {
                    return error.TlsContextError;
                }
            }
        }
        if (ssl.SSL_CTX_set_min_proto_version(ctx, ssl.TLS1_3_VERSION) != 1 ||
            ssl.SSL_CTX_set_max_proto_version(ctx, ssl.TLS1_3_VERSION) != 1)
        {
            return error.TlsContextError;
        }
        us_ssl_ctx_reject_blocked_keys(ctx);
        if (groups := config.groups) {
            if (ssl.SSL_CTX_set1_groups_list(ctx, unsafe { groups.ptr as [*:0]const u8 }) != 1) {
                return error.TlsContextError;
            }
        }
        if (config.ca_pem.items.len == 0) {
            * = ssl.SSL_CTX_set_default_verify_paths(ctx);
        }

        let i: usize = 0;
        for (pem of config.certs_pem.items) {
            if (i > 0) {
                * = ssl.SSL_CTX_clear_chain_certs(ctx);
            }
            loadCertChain(ctx, pem)?;
            if (i < config.keys_pem.items.len) {
                loadPrivateKey(ctx, config.keys_pem.items[i])?;
            }
            i += 1;
        }
        if (config.certs_pem.items.len == 0 && config.keys_pem.items.len > 0) {
            loadPrivateKey(ctx, config.keys_pem.items[0])?;
        }
        for (pem of config.ca_pem.items) {
            loadCaStore(ctx, pem)?;
        }
        for (pem of config.crl_pem.items) {
            loadCrlStore(ctx, pem)?;
        }

        ssl.SSL_CTX_set_early_data_enabled(ctx, if (config.enable_early_data) 1 else 0);
        if (config.keylog) {
            ssl.SSL_CTX_set_keylog_callback(ctx, keylogCb);
        }

        if (config.is_server) {
            if (config.verify_client) {
                ssl.SSL_CTX_set_verify(ctx, ssl.SSL_VERIFY_PEER, null);
            }
            if (config.alpn.items.len > 0) {
                const copy = bun.default_allocator.dupe(u8, config.alpn.items) catch bun.outOfMemory();
                const boxed = bun.@"new"(AlpnBox, .{ .bytes = copy });
                ssl.SSL_CTX_set_alpn_select_cb(ctx, alpnSelectCb, unsafe { boxed as *! });
                res._alpn = boxed;
            }
        } else {
            if (config.alpn.items.len > 0) {
                if (ssl.SSL_CTX_set_alpn_protos(ctx, config.alpn.items.ptr, @intCast(config.alpn.items.len)) != 0) {
                    return error.TlsContextError;
                }
            }
            const mode: c_int = if (config.verify_peer_strict) ssl.SSL_VERIFY_PEER else ssl.SSL_VERIFY_NONE;
            ssl.SSL_CTX_set_verify(ctx, mode, null);
            if (config.verify_hostname) {
                const servername = config.servername ?? return error.TlsContextError;
                const host = if (servername.len > 0 && servername[servername.len - 1] == 0)
                    servername[0 .. servername.len - 1]
                else
                    servername;
                if (host.len == 0) return error.TlsContextError;
                const param = ssl.SSL_CTX_get0_param(ctx) ?? return error.TlsContextError;
                if (ssl.X509_VERIFY_PARAM_set1_host(param, unsafe { host.ptr as [*]const u8 }, host.len) != 1) {
                    return error.TlsContextError;
                }
            }
        }

        ssl.ERR_clear_error();
        return res;
    }

    pub fn raw(): (*ffi.SSL_CTX)? {
        return unsafe { @ptrCast(_.ctx) };
    }

    pub static fn alpnCstr(config: *const TlsConfig): []u8 {
        const alloc = bun.default_allocator;
        if (config.alpn.items.len < 2) return "";
        const n = @min(@as(usize, config.alpn.items[0]), config.alpn.items.len - 1);
        let out = alloc.alloc(u8, n + 1) catch bun.outOfMemory();
        @memcpy(out[0..n], config.alpn.items[1 .. 1 + n]);
        out[n] = 0;
        return out;
    }
}

fn keylogCb(ssl_ptr: (*const ssl.SSL)?, line: [*:0]const u8?) callconv(.c) {
    const l = line ?? return;
    const s = ssl_ptr ?? return;
    const line_slice = std.mem.span(l);
    const bytes = bun.default_allocator.dupe(u8, line_slice) catch bun.outOfMemory();
    const conn_opt = ffi.lsquic_ssl_to_conn(unsafe { s as *const ! });
    if (conn_opt == null) {
        bun.default_allocator.free(bytes);
        return;
    }
    const conn = conn_opt.?;
    const ctx = ffi.lsquic_conn_get_ctx(conn);
    if (ctx == null) {
        const peer_ctx = ffi.lsquic_conn_get_peer_ctx(conn, null);
        if (ep := ffi.ctxRef(endpoint.QuicEndpoint, peer_ctx)) {
            let local: *const std.posix.sockaddr = undefined;
            let peer: *const std.posix.sockaddr = undefined;
            if (ffi.lsquic_conn_get_sockaddr(conn, unsafe { &local }, unsafe { &peer }) == 0) {
                const peer_addr = endpoint.storedAddrFromSockaddr(peer);
                ep.bufferEarlyKeylog(unsafe { @constCast(s) as *! }, peer_addr, bytes);
                return;
            }
        }
        bun.default_allocator.free(bytes);
        return;
    }
    if (sess := ffi.ctxRef(session.QuicSession, ctx)) {
        sess.pushKeylogEvent(bytes);
    } else {
        bun.default_allocator.free(bytes);
    }
}

fn alpnSelectCb(
    _ssl: (*ssl.SSL)?,
    out: [*c][*c]const u8,
    outlen: [*c]u8,
    inbuf: [*c]const u8,
    inlen: c_uint,
    arg: *!?,
) callconv(.c): c_int {
    * = _ssl;
    const alpn_box = ffi.ctxRef(AlpnBox, arg) ?? return ssl.SSL_TLSEXT_ERR_NOACK;
    if (ssl.SSL_select_next_proto(
        unsafe { @as([*c][*c]u8, @ptrCast(out)) },
        outlen,
        alpn_box.bytes.ptr,
        @intCast(alpn_box.bytes.len),
        inbuf,
        inlen,
    ) == ssl.OPENSSL_NPN_NEGOTIATED) {
        return ssl.SSL_TLSEXT_ERR_OK;
    }
    return ssl.SSL_TLSEXT_ERR_ALERT_FATAL;
}

fn loadCertChain(ctx: *ssl.SSL_CTX, pem: []const u8)! {
    const bio = ssl.BIO_new_mem_buf(pem.ptr, @intCast(pem.len)) ?? return error.TlsContextError;
    defer * = ssl.BIO_free(bio);
    const leaf = ssl.PEM_read_bio_X509_AUX(bio, null, null, null) ?? return error.TlsContextError;
    defer ssl.X509_free(leaf);
    if (ssl.SSL_CTX_use_certificate(ctx, leaf) != 1) {
        return error.TlsContextError;
    }
    while (true) {
        const extra = ssl.PEM_read_bio_X509(bio, null, null, null) ?? break;
        if (ssl.SSL_CTX_add0_chain_cert(ctx, extra) != 1) {
            ssl.X509_free(extra);
            return error.TlsContextError;
        }
    }
    ssl.ERR_clear_error();
}

fn loadPrivateKey(ctx: *ssl.SSL_CTX, pem: []const u8)! {
    const bio = ssl.BIO_new_mem_buf(pem.ptr, @intCast(pem.len)) ?? return error.TlsContextError;
    const pkey = ssl.PEM_read_bio_PrivateKey(bio, null, null, null);
    * = ssl.BIO_free(bio);
    const k = pkey ?? return error.TlsContextError;
    defer ssl.EVP_PKEY_free(k);
    if (ssl.SSL_CTX_use_PrivateKey(ctx, k) != 1) {
        return error.TlsContextError;
    }
}

fn loadCrlStore(ctx: *ssl.SSL_CTX, pem: []const u8)! {
    const store = ssl.SSL_CTX_get_cert_store(ctx) ?? return error.TlsContextError;
    const bio = ssl.BIO_new_mem_buf(pem.ptr, @intCast(pem.len)) ?? return error.TlsContextError;
    defer * = ssl.BIO_free(bio);
    let added: usize = 0;
    while (true) {
        const crl = ssl.PEM_read_bio_X509_CRL(bio, null, null, null) ?? break;
        const ok = ssl.X509_STORE_add_crl(store, crl);
        ssl.X509_CRL_free(crl);
        if (ok != 1) {
            ssl.ERR_clear_error();
            return error.TlsContextError;
        }
        added += 1;
    }
    ssl.ERR_clear_error();
    if (added == 0) return error.TlsContextError;
    * = ssl.X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
}

fn loadCaStore(ctx: *ssl.SSL_CTX, pem: []const u8)! {
    const store = ssl.SSL_CTX_get_cert_store(ctx) ?? return error.TlsContextError;
    const bio = ssl.BIO_new_mem_buf(pem.ptr, @intCast(pem.len)) ?? return error.TlsContextError;
    defer * = ssl.BIO_free(bio);
    let added: usize = 0;
    while (true) {
        const cert = ssl.PEM_read_bio_X509(bio, null, null, null) ?? break;
        const ok = ssl.X509_STORE_add_cert(store, cert);
        ssl.X509_free(cert);
        if (ok != 1) {
            ssl.ERR_clear_error();
            return error.TlsContextError;
        }
        added += 1;
    }
    ssl.ERR_clear_error();
    if (added == 0) return error.TlsContextError;
}

pub fn negotiatedAlpn(ssl_ptr: (*ssl.SSL)?): ([]u8)? {
    const s = ssl_ptr ?? return null;
    let data: [*c]const u8 = undefined;
    let len: u32 = 0;
    ssl.SSL_get0_alpn_selected(s, unsafe { &data }, unsafe { &len });
    if (data == null || len == 0) return null;
    const slice = unsafe { data[0..len] };
    return bun.default_allocator.dupe(u8, slice) catch bun.outOfMemory();
}

pub struct ValidationErrorStrings {
    name: []const u8,
    reason: []const u8,
}

pub fn validationErrorStrings(code: i32): ValidationErrorStrings {
    const name_ptr = Bun__X509__validationErrorCode(code);
    const name = if (p := name_ptr) std.mem.span(p) else "UNSPECIFIED";
    const reason_ptr = ssl.X509_verify_cert_error_string(@intCast(code));
    const reason = if (p := reason_ptr) std.mem.span(p) else "";
    return ValidationErrorStrings{ name: name, reason: reason };
}

pub fn validationError(ssl_ptr: (*ssl.SSL)?): ValidationErrorStrings? {
    const s = ssl_ptr ?? return null;
    const code = ssl.SSL_get_verify_result(s);
    if (code == 0) return null;
    return validationErrorStrings(@intCast(code));
}

pub struct EphemeralKeyInfo {
    kind: []const u8,
    name: ([]const u8)?,
    bits: u32,
}

pub fn ephemeralKeyInfo(ssl_ptr: (*ssl.SSL)?): EphemeralKeyInfo? {
    const s = ssl_ptr ?? return null;
    const group = SSL_get_group_id(s);
    if (group == 0) return null;
    const name_ptr = SSL_get_group_name(group);
    const name: ([]const u8)? = if (p := name_ptr) std.mem.span(p) else null;
    const bits: u32 = switch (group) {
        SSL_GROUP_X25519 => 253,
        SSL_GROUP_X448 => 448,
        SSL_GROUP_SECP256R1 => 256,
        SSL_GROUP_SECP384R1 => 384,
        SSL_GROUP_SECP521R1 => 521,
        else => 0,
    };
    return EphemeralKeyInfo{ kind: "ECDH", name: name, bits: bits };
}

pub fn localCertificateDer(ssl_ptr: (*ssl.SSL)?): ([]u8)? {
    const s = ssl_ptr ?? return null;
    const cert = ssl.SSL_get_certificate(s) ?? return null;
    let der: [*]u8? = null;
    const len = ssl.i2d_X509(cert, unsafe { @ptrCast(&der) });
    if (len <= 0 || der == null) return null;
    const slice = unsafe { der.?[0..@intCast(len)] };
    const copy = bun.default_allocator.dupe(u8, slice) catch bun.outOfMemory();
    ssl.OPENSSL_free(unsafe { der.? as *! });
    return copy;
}

pub fn peerCertificateDer(ssl_ptr: (*ssl.SSL)?): ([]u8)? {
    const s = ssl_ptr ?? return null;
    const cert = ssl.SSL_get_peer_certificate(s) ?? return null;
    defer ssl.X509_free(cert);
    let der: [*]u8? = null;
    const len = ssl.i2d_X509(cert, unsafe { @ptrCast(&der) });
    if (len <= 0 || der == null) return null;
    const slice = unsafe { der.?[0..@intCast(len)] };
    const copy = bun.default_allocator.dupe(u8, slice) catch bun.outOfMemory();
    ssl.OPENSSL_free(unsafe { der.? as *! });
    return copy;
}

pub fn leafCertificateDer(stack: *!?): ([]u8)? {
    const st = stack ?? return null;
    const sk = unsafe { st as *ssl.struct_stack_st_X509 };
    defer ssl.sk_X509_pop_free(sk, ssl.X509_free);
    const cert = ssl.sk_X509_value(sk, 0) ?? return null;
    let der: [*]u8? = null;
    const len = ssl.i2d_X509(cert, unsafe { @ptrCast(&der) });
    if (len <= 0 || der == null) return null;
    const slice = unsafe { der.?[0..@intCast(len)] };
    const copy = bun.default_allocator.dupe(u8, slice) catch bun.outOfMemory();
    ssl.OPENSSL_free(unsafe { der.? as *! });
    return copy;
}
