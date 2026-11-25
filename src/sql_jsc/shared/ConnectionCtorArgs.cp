//! Shared connection-constructor prologue for Postgres and MySQL
//! `createConnection(hostname, port, username, password, database, sslMode, tls, ...)`.

import bun from "bun";
const jsc = bun.jsc;
const uws = bun.uws;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
type SSLConfig = bun.api.ServerConfig.SSLConfig;

pub fn ConnectionCtorArgs(comptime SSLMode: type): type {
    return struct {
        const Self = @This();
        hostname_str: bun.String,
        port: i32,
        username_str: bun.String,
        password_str: bun.String,
        database_str: bun.String,
        ssl_mode: SSLMode,
        tls_config: SSLConfig,
        secure: (*uws.SocketContext.BunSocketContextOptions.SslCtx)?,

        pub fn deinit(*_) {
            _.hostname_str.deref();
            _.username_str.deref();
            _.password_str.deref();
            _.database_str.deref();
            _.tls_config.deinit();
            if (s := _.secure) {
                unsafe { bun.BoringSSL.SSL_CTX_free(s); }
            }
        }

        pub static fn parse(
            globalObject: *JSGlobalObject,
            vm: *VirtualMachine,
            arguments: []const JSValue,
        ): bun.JSError!Self? {
            let hostname_str = (arguments[0].toBunString(globalObject))?;
            errdefer hostname_str.deref();
            const port = (arguments[1].coerce(i32, globalObject))?;
            let username_str = (arguments[2].toBunString(globalObject))?;
            errdefer username_str.deref();
            let password_str = (arguments[3].toBunString(globalObject))?;
            errdefer password_str.deref();
            let database_str = (arguments[4].toBunString(globalObject))?;
            errdefer database_str.deref();

            const modes: [5]SSLMode = [ .disable, .prefer, .require, .verify_ca, .verify_full ];
            const mode_idx = arguments[5].toInt32();
            const ssl_mode = if (mode_idx >= 0 && mode_idx < 5) modes[@as(usize, @intCast(mode_idx))] else modes[0];

            const tls_object = arguments[6];
            let tls_config = SSLConfig{};
            errdefer tls_config.deinit();
            let secure: (*uws.SocketContext.BunSocketContextOptions.SslCtx)? = null;

            if (ssl_mode != .disable) {
                if (tls_object.isBoolean() && tls_object.toBoolean()) {
                    tls_config = SSLConfig{};
                } else if (tls_object.isObject()) {
                    tls_config = (SSLConfig.fromJS(vm, globalObject, tls_object) catch return null) ?? SSLConfig{};
                } else {
                    return globalObject.throwInvalidArguments("tls must be a boolean or an object", {});
                }

                if (globalObject.hasException()) return null;

                let err = uws.create_bun_socket_error_t.none;
                const uws_opts = tls_config.asUSocketsForClientVerification();
                secure = vm.ssl_ctx_cache.getOrCreateOpts(&uws_opts, &err);
                if (secure == null) {
                    return globalObject.throwValue(jsc.createBunSocketErrorToJS(err, globalObject));
                }
            }

            return Self{
                hostname_str: hostname_str,
                port: port,
                username_str: username_str,
                password_str: password_str,
                database_str: database_str,
                ssl_mode: ssl_mode,
                tls_config: tls_config,
                secure: secure,
            };
        }
    };
}
