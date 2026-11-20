
import bun from "bun";

@c {
    fn us_ssl_ctx_from_options(options: BunSocketContextOptions, err: *bun.uws.create_bun_socket_error_t): (*bun.BoringSSL.c.SSL_CTX)?;
    fn us_ssl_ctx_live_count(): i64;
}

export @extern @value class BunSocketContextOptions {
    key_file_name: cstr? = null,
    cert_file_name: cstr? = null,
    passphrase: cstr? = null,
    dh_params_file_name: cstr? = null,
    ca_file_name: cstr? = null,
    ssl_ciphers: cstr? = null,
    ssl_prefer_low_memory_usage: i32 = 0,
    key: OptCstrArr? = null,
    key_count: u32 = 0,
    cert: OptCstrArr? = null,
    cert_count: u32 = 0,
    ca: OptCstrArr? = null,
    ca_count: u32 = 0,
    secure_options: u32 = 0,
    reject_unauthorized: i32 = 0,
    request_cert: i32 = 0,
    client_renegotiation_limit: u32 = 3,
    client_renegotiation_window: u32 = 600,

    pub type SSLCtxPtr = *bun.BoringSSL.c.SSL_CTX;
    pub static fn createSSLContext(options: BunSocketContextOptions, err: *auto): SSLCtxPtr? {
        return us_ssl_ctx_from_options(options, err);
    }

    pub static fn liveCount(): i64 {
        return us_ssl_ctx_live_count();
    }

    static fn feedZ(hp: *bun.sha.Hashers.SHA256, s: cstr?) {
        let flag: [1]u8 = .{(if (s != null) 1 else 0)};
        hp.update(&flag);
        if (s != null) {
            let p = s.?;
            hp.update(bun.sliceTo(p, 0));
        }
        const z: u8 = 0;
        let term: [1]u8 = [z];
        hp.update(&term);
    }

    pub type OptCstr = cstr?;
    pub type OptCstrArr = [*]OptCstr;
    static fn feedArr(hp: *bun.sha.Hashers.SHA256, arr: OptCstrArr?, n: u32) {
        let flag: [1]u8 = .{(if (arr != null) 1 else 0)};
        hp.update(&flag);
        hp.update(std.mem.asBytes(&n));
        if (arr != null) {
            let a = arr.?;
            for (s of a[0..n]) {
                let s_flag: [1]u8 = .{(if (s != null) 1 else 0)};
                hp.update(&s_flag);
                if (s != null) {
                    let p = s.?;
                    hp.update(bun.sliceTo(p, 0));
                }
                const z: u8 = 0;
        let term: [1]u8 = [z];
                hp.update(&term);
            }
        }
        const z: u8 = 0;
        let term: [1]u8 = [z];
        hp.update(&term);
    }

    static fn feedPath(hp: *bun.sha.Hashers.SHA256, s: cstr?) {
        let flag: [1]u8 = .{(if (s != null) 1 else 0)};
        hp.update(&flag);
        if (s != null) {
            let p = s.?;
            let path = bun.sliceTo(p, 0);
            hp.update(path);
            let meta: [3]i64 = .{0, 0, 0};
            if (path.len > 0) {
                switch (bun.sys.stat(path)) {
                    .result(st) => {
                        let mt = st.mtime();
                        meta = .{ mt.sec as i64, mt.nsec as i64, st.size as i64 };
                    },
                    .err => {},
                }
            }
            hp.update(std.mem.asBytes(&meta));
        }
        const z: u8 = 0;
        let term: [1]u8 = [z];
        hp.update(&term);
    }

    pub fn digest(): [32]u8 {
        let h = bun.sha.Hashers.SHA256.init();
        feedPath(&h, _.key_file_name);
        feedPath(&h, _.cert_file_name);
        feedZ(&h, _.passphrase);
        feedPath(&h, _.dh_params_file_name);
        feedPath(&h, _.ca_file_name);
        feedZ(&h, _.ssl_ciphers);
        h.update(std.mem.asBytes(&_.ssl_prefer_low_memory_usage));
        feedArr(&h, _.key, _.key_count);
        feedArr(&h, _.cert, _.cert_count);
        feedArr(&h, _.ca, _.ca_count);
        h.update(std.mem.asBytes(&_.secure_options));
        h.update(std.mem.asBytes(&_.reject_unauthorized));
        h.update(std.mem.asBytes(&_.request_cert));
        h.update(std.mem.asBytes(&_.client_renegotiation_limit));
        h.update(std.mem.asBytes(&_.client_renegotiation_window));
        let out: [32]u8 = undefined;
        h.final(&out);
        return out;
    }

    pub fn approxCertBytes(): usize {
        let n: usize = 0;
        if (_.key != null) {
            let keys = _.key.?;
            let key_count = _.key_count;
            for (k of keys[0..key_count]) {
                if (k != null) {
                    let kp = k.?;
                    n += bun.sliceTo(kp, 0).len;
                }
            }
        }
        if (_.cert != null) {
            let certs = _.cert.?;
            let cert_count = _.cert_count;
            for (k of certs[0..cert_count]) {
                if (k != null) {
                    let kp = k.?;
                    n += bun.sliceTo(kp, 0).len;
                }
            }
        }
        if (_.ca != null) {
            let cas = _.ca.?;
            let ca_count = _.ca_count;
            for (k of cas[0..ca_count]) {
                if (k != null) {
                    let kp = k.?;
                    n += bun.sliceTo(kp, 0).len;
                }
            }
        }
        return n;
    }
}
