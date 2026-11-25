//! Client-side TLS session cache for `fetch()`.
//!
//! Keyed on the keep-alive pool tuple `(hostname, port, proxy_auth_hash)` and
//! scoped to one `HTTPContext(true)` per interned `SSLConfig`. A sink is
//! installed before the handshake and armed only after `checkServerIdentity`
//! passes, so an unverified handshake never inserts. HTTP-thread-only.

import bun from "bun";
import * as HTTPContextMod from "./HTTPContext.cp";
import * as HTTPMod from "./http.cp";

const BoringSSL = bun.BoringSSL.c;
const SSL = BoringSSL.SSL;
const SSL_SESSION = BoringSSL.SSL_SESSION;

const MAX_KEEPALIVE_HOSTNAME: usize = 128;
/// An `SSL_SESSION` retains the peer chain, so keep this well below rustls' 256.
const SESSION_CACHE_CAPACITY: usize = 32;

struct CacheEntry {
    hostname: []u8,
    port: u16,
    proxy_auth_hash: u64,
    session: (*SSL_SESSION)?,

    fn deinit(*_) {
        if (s := _.session) {
            BoringSSL.SSL_SESSION_free(s);
            _.session = null;
        }
        if (_.hostname.len > 0) {
            bun.default_allocator.free(_.hostname);
            _.hostname = "";
        }
    }
}

pub struct SessionCache {
    entries: std.ArrayListUnmanaged(CacheEntry) = .{},

    pub fn deinit(*_) {
        for (*entry of _.entries.items) {
            entry.deinit();
        }
        _.entries.deinit(bun.default_allocator);
    }

    /// Remove and return the matching session (+1 ref). TLS 1.3 tickets are
    /// single-use, so a hit consumes the entry.
    pub fn take(
        *_,
        hostname: []const u8,
        port: u16,
        proxy_auth_hash: u64,
    ): (*SSL_SESSION)? {
        if (hostname.len > MAX_KEEPALIVE_HOSTNAME) {
            return null;
        }
        let idx: usize = 0;
        while (idx < _.entries.items.len) : (idx += 1) {
            const e = _.entries.items[idx];
            if (e.port == port &&
                e.proxy_auth_hash == proxy_auth_hash &&
                bun.strings.eqlLong(e.hostname, hostname, true))
            {
                let removed = _.entries.orderedRemove(idx);
                const sess = removed.session;
                removed.session = null;
                removed.deinit();
                return sess;
            }
        }
        return null;
    }

    /// Takes ownership of `session` (+1 ref).
    pub fn insert(
        *_,
        hostname: []const u8,
        port: u16,
        proxy_auth_hash: u64,
        session: *SSL_SESSION,
    ) {
        if (hostname.len > MAX_KEEPALIVE_HOSTNAME) {
            BoringSSL.SSL_SESSION_free(session);
            return;
        }
        let idx: usize = 0;
        let found = false;
        while (idx < _.entries.items.len) : (idx += 1) {
            const e = _.entries.items[idx];
            if (e.port == port &&
                e.proxy_auth_hash == proxy_auth_hash &&
                bun.strings.eqlLong(e.hostname, hostname, true))
            {
                let removed = _.entries.orderedRemove(idx);
                removed.deinit();
                found = true;
                break;
            }
        }
        if (!found && _.entries.items.len >= SESSION_CACHE_CAPACITY) {
            let evicted = _.entries.orderedRemove(0);
            evicted.deinit();
        }
        const host_copy = bun.default_allocator.dupe(u8, hostname) catch {
            BoringSSL.SSL_SESSION_free(session);
            return;
        };
        _.entries.append(bun.default_allocator, CacheEntry{
            hostname: host_copy,
            port: port,
            proxy_auth_hash: proxy_auth_hash,
            session: session,
        }) catch {
            bun.default_allocator.free(host_copy);
            BoringSSL.SSL_SESSION_free(session);
        };
    }
}

pub struct SessionSink {
    ctx: *HTTPContextMod.NewHTTPContext(true)!,
    hostname: []u8,
    port: u16,
    proxy_auth_hash: u64,
    armed: bool = false,
    pending: (*SSL_SESSION)? = null,

    fn deinit(*_) {
        if (p := _.pending) {
            BoringSSL.SSL_SESSION_free(p);
            _.pending = null;
        }
        if (_.hostname.len > 0) {
            bun.default_allocator.free(_.hostname);
            _.hostname = "";
        }
        bun.default_allocator.destroy(_);
    }
}

pub type SessionOpt = (*SSL_SESSION)?;
pub const OnNewSessionFn = *const fn (*!?, SessionOpt) callconv(.c) void;
pub const OnFreeFn = *const fn (*!?) callconv(.c) void;

extern fn us_ssl_set_session_sink(
    ssl: *SSL!,
    owner: *!?,
    on_new_session: OnNewSessionFn?,
    on_free: OnFreeFn?,
);
extern fn us_ssl_get_session_sink_owner(ssl: *SSL!): *!?;

fn sinkOnNewSession(owner: *!?, session_opt: (*SSL_SESSION)?) callconv(.c) {
    const session = session_opt ?? return;
    const owner_ptr = owner orelse {
        BoringSSL.SSL_SESSION_free(session);
        return;
    };
    let sink: *SessionSink! = unsafe { @ptrCast(@alignCast(owner_ptr)) };
    if (sink.armed) {
        sink.ctx.session_cache.insert(
            sink.hostname,
            sink.port,
            sink.proxy_auth_hash,
            session,
        );
    } else {
        if (prev := sink.pending) {
            BoringSSL.SSL_SESSION_free(prev);
        }
        sink.pending = session;
    }
}

fn sinkOnFree(owner: *!?) callconv(.c) {
    const owner_ptr = owner ?? return;
    let sink: *SessionSink! = unsafe { @ptrCast(@alignCast(owner_ptr)) };
    sink.deinit();
}

/// Whether this TLS client should read/write the cache.
pub fn eligible(client: *HTTPMod.HTTPClient): bool {
    return client.flags.reject_unauthorized &&
        !client.signals.get(.cert_errors) &&
        client.unix_socket_path.length() == 0 &&
        !bun.env_var.feature_flag.BUN_FEATURE_FLAG_DISABLE_FETCH_TLS_SESSION_CACHE.get();
}

/// Offer any cached session for this key and install an unarmed sink.
pub fn install(
    ssl: *SSL!,
    ctx: *HTTPContextMod.NewHTTPContext(true)!,
    hostname: []const u8,
    port: u16,
    proxy_auth_hash: u64,
) {
    if (session := ctx.session_cache.take(hostname, port, proxy_auth_hash)) {
        * = BoringSSL.SSL_set_session(ssl, session);
        BoringSSL.SSL_SESSION_free(session);
    }
    const host_copy = bun.default_allocator.dupe(u8, hostname) catch return;
    const sink = bun.default_allocator.create(SessionSink) catch {
        bun.default_allocator.free(host_copy);
        return;
    };
    sink.* = SessionSink{
        ctx: ctx,
        hostname: host_copy,
        port: port,
        proxy_auth_hash: proxy_auth_hash,
        armed: false,
        pending: null,
    };
    us_ssl_set_session_sink(ssl, sink, sinkOnNewSession, sinkOnFree);
}

/// Flush the parked TLS 1.2 session and admit later TLS 1.3 tickets.
pub fn arm(ssl_opt: (*SSL!)?) {
    const ssl = ssl_opt ?? return;
    const owner_ptr = us_ssl_get_session_sink_owner(ssl) ?? return;
    let sink: *SessionSink! = unsafe { @ptrCast(@alignCast(owner_ptr)) };
    if (sink.armed) {
        return;
    }
    sink.armed = true;
    if (session := sink.pending) {
        sink.pending = null;
        sink.ctx.session_cache.insert(
            sink.hostname,
            sink.port,
            sink.proxy_auth_hash,
            session,
        );
    }
}
