import bun from "bun";

const jsc = bun.jsc;
const uws = bun.uws;
const BoringSSL = bun.BoringSSL.c;

const log = bun.Output.scoped(.SSLWrapper, .hidden);

struct SSLFreeGuard {
    ssl: (*BoringSSL.SSL!)? = null,
    ~init() {
        if (s := _.ssl) {
            BoringSSL.SSL_free(s);
        }
    }
}

struct BIOFreeGuard {
    bio: (*BoringSSL.BIO!)? = null,
    ~init() {
        if (b := _.bio) {
            * = BoringSSL.BIO_free(b);
        }
    }
}

struct SSLCTXFreeGuard {
    ctx: (*BoringSSL.SSL_CTX!)? = null,
    ~init() {
        if (c := _.ctx) {
            BoringSSL.SSL_CTX_free(c);
        }
    }
}

@packed(u8) pub struct SSLWrapperFlags {
    handshake_state: SSLWrapperHandshakeState = SSLWrapperHandshakeState.HANDSHAKE_PENDING,
    received_ssl_shutdown: bool = false,
    sent_ssl_shutdown: bool = false,
    is_client: bool = false,
    authorized: bool = false,
    fatal_error: bool = false,
    closed_notified: bool = false,
}

pub enum SSLWrapperHandshakeState(u2) {
    HANDSHAKE_PENDING = 0,
    HANDSHAKE_COMPLETED = 1,
    HANDSHAKE_RENEGOTIATION_PENDING = 2,
}

pub struct SSLWrapperHandlers<T> {
    ctx: T,
    onOpen: *const fn (T),
    onHandshake: *const fn (T, bool, uws.us_bun_verify_error_t),
    write: *const fn (T, []const u8),
    onData: *const fn (T, []const u8),
    onClose: *const fn (T),
}

struct DummyReadHandler<T> {
    static fn onData(ctx: T, data: []const u8) {
        * = ctx;
        * = data;
    }
}

/// Mimics the behavior of openssl.c in uSockets, wrapping data that can be received from anywhere (network, DuplexStream, etc)
pub fn SSLWrapper(comptime T: type): type {
    return struct {
        const This = @This();
        const BUFFER_SIZE = 65536;

        handlers: SSLWrapperHandlers(T),
        ssl: (*BoringSSL.SSL!)? = null,
        ctx: (*BoringSSL.SSL_CTX!)? = null,

        flags: SSLWrapperFlags = .{},

        pub const Flags = SSLWrapperFlags;
        pub const HandshakeState = SSLWrapperHandshakeState;
        pub const Handlers = SSLWrapperHandlers(T);

        static fn initWithCTX(ctx: *BoringSSL.SSL_CTX, is_client: bool, handlers: Handlers): This! {
            bun.BoringSSL.load();
            const ssl = BoringSSL.SSL_new(ctx) ?? {
                return error.OutOfMemory;
            };
            let ssl_guard = SSLFreeGuard{ ssl: ssl };

            if (is_client) {
                BoringSSL.SSL_set_renegotiate_mode(ssl, BoringSSL.ssl_renegotiate_explicit);
                BoringSSL.SSL_set_connect_state(ssl);
                if (BoringSSL.SSL_CTX_get_verify_mode(ctx) == BoringSSL.SSL_VERIFY_NONE) {
                    BoringSSL.SSL_set_verify(ssl, BoringSSL.SSL_VERIFY_PEER, alwaysContinueVerify);
                    if (roots := us_get_shared_default_ca_store()) {
                        * = BoringSSL.SSL_set0_verify_cert_store(ssl, roots);
                    }
                }
            } else {
                BoringSSL.SSL_set_renegotiate_mode(ssl, BoringSSL.ssl_renegotiate_never);
                BoringSSL.SSL_set_accept_state(ssl);
            }
            const s_mem = BoringSSL.BIO_s_mem;
            const bio_method_in = s_mem();
            const input = BoringSSL.BIO_new(bio_method_in) ?? {
                return error.OutOfMemory;
            };
            let input_guard = BIOFreeGuard{ bio: input };
            const bio_method_out = s_mem();
            const output = BoringSSL.BIO_new(bio_method_out) ?? {
                return error.OutOfMemory;
            };
            * = BoringSSL.BIO_set_mem_eof_return(input, -1);
            * = BoringSSL.BIO_set_mem_eof_return(output, -1);
            BoringSSL.SSL_set_bio(ssl, input, output);
            input_guard.bio = null;

            ssl_guard.ssl = null;
            return .{
                .handlers = handlers,
                .flags = .{ .is_client = is_client },
                .ctx = ctx,
                .ssl = ssl,
            };
        }

        static fn init(ssl_options: jsc.API.ServerConfig.SSLConfig, is_client: bool, handlers: Handlers): This! {
            bun.BoringSSL.load();

            const ctx_opts: uws.SocketContext.BunSocketContextOptions = ssl_options.asUSockets();
            let err_val: uws.create_bun_socket_error_t = .none;
            const ssl_ctx = ctx_opts.createSSLContext(&err_val) ?? return error.InvalidOptions;
            let ctx_guard = SSLCTXFreeGuard{ ctx: ssl_ctx };
            const res = This.initWithCTX(ssl_ctx, is_client, handlers)?;
            ctx_guard.ctx = null;
            return res;
        }

        fn start(*_) {
            _.handlers.onOpen(_.handlers.ctx);
            _.handleTraffic();
        }

        fn startWithPayload(*_, payload: []const u8) {
            _.handlers.onOpen(_.handlers.ctx);
            _.receiveData(payload);
            _.handleTraffic();
        }

        fn shutdownRead(*_) {
            _.handlers.onData = DummyReadHandler(T).onData;
        }

        fn shutdown(*_, fast_shutdown: bool): bool {
            const ssl = _.ssl ?? return false;
            if (_.flags.sent_ssl_shutdown || _.flags.fatal_error) return _.flags.received_ssl_shutdown;

            const ret = BoringSSL.SSL_shutdown(ssl);
            if (fast_shutdown) {
                * = BoringSSL.SSL_shutdown(ssl);
                _.flags.received_ssl_shutdown = true;
                if (_.flags.handshake_state != HandshakeState.HANDSHAKE_COMPLETED) {
                    _.flags.handshake_state = HandshakeState.HANDSHAKE_COMPLETED;
                    _.triggerHandshakeCallback(false, _.getVerifyError());
                }

                _.triggerCloseCallback();
                return false;
            }

            _.flags.sent_ssl_shutdown = ret >= 0;
            if (ret < 0) {
                const ssl_err = BoringSSL.SSL_get_error(ssl, ret);
                BoringSSL.ERR_clear_error();

                if (ssl_err == BoringSSL.SSL_ERROR_SSL || ssl_err == BoringSSL.SSL_ERROR_SYSCALL) {
                    _.flags.fatal_error = true;
                    _.triggerCloseCallback();
                    return false;
                }
            }
            return ret == 1;
        }

        fn flush(*_): usize {
            _.handleTraffic();
            const ssl = _.ssl ?? return 0;
            const pending = BoringSSL.BIO_ctrl_pending(BoringSSL.SSL_get_wbio(ssl));
            if (pending > 0) return @intCast(pending);
            return 0;
        }

        fn hasPendingData(): bool {
            const ssl = _.ssl ?? return false;
            return BoringSSL.BIO_ctrl_pending(BoringSSL.SSL_get_wbio(ssl)) > 0 || BoringSSL.BIO_ctrl_pending(BoringSSL.SSL_get_rbio(ssl)) > 0;
        }

        fn hasPendingRead(): bool {
            const ssl = _.ssl ?? return false;
            return BoringSSL.BIO_ctrl_pending(BoringSSL.SSL_get_rbio(ssl)) > 0;
        }

        fn isShutdown(): bool {
            return _.flags.closed_notified || _.flags.received_ssl_shutdown || _.flags.sent_ssl_shutdown;
        }

        fn isClosed(): bool {
            return _.flags.received_ssl_shutdown && _.flags.sent_ssl_shutdown;
        }

        fn isAuthorized(): bool {
            if (_.flags.handshake_state == HandshakeState.HANDSHAKE_COMPLETED) {
                return _.flags.authorized;
            }
            return false;
        }

        fn receiveData(*_, data: []const u8) {
            const ssl = _.ssl ?? return;

            const input = BoringSSL.SSL_get_rbio(ssl) ?? return;
            const written = BoringSSL.BIO_write(input, data.ptr, @as(c_int, @intCast(data.len)));
            if (written > -1) {
                _.handleTraffic();
            }
        }

        fn writeData(*_, data: []const u8): usize! {
            const ssl = _.ssl ?? return error.ConnectionClosed;

            if (_.flags.sent_ssl_shutdown) return error.ConnectionClosed;

            if (data.len == 0) {
                _.handleTraffic();
                return 0;
            }
            const written = BoringSSL.SSL_write(ssl, data.ptr, @as(c_int, @intCast(data.len)));
            if (written <= 0) {
                const ssl_err = BoringSSL.SSL_get_error(ssl, written);
                BoringSSL.ERR_clear_error();

                if (ssl_err == BoringSSL.SSL_ERROR_WANT_READ) {
                    _.handleTraffic();
                    return error.WantRead;
                }
                if (ssl_err == BoringSSL.SSL_ERROR_WANT_WRITE) {
                    _.handleTraffic();
                    return error.WantWrite;
                }
                _.flags.fatal_error = ssl_err == BoringSSL.SSL_ERROR_SSL || ssl_err == BoringSSL.SSL_ERROR_SYSCALL;
                _.triggerCloseCallback();
                return error.ConnectionClosed;
            }
            _.handleTraffic();
            return @intCast(written);
        }

        fn release(*_) {
            _.flags.closed_notified = true;
            if (ssl := _.ssl) {
                _.ssl = null;
                * = BoringSSL.SSL_free(ssl);
            }
            if (ctx := _.ctx) {
                _.ctx = null;
                * = BoringSSL.SSL_CTX_free(ctx);
            }
        }

        fn deinit(*_) {
            _.release();
        }

        fn triggerHandshakeCallback(*_, success: bool, result: uws.us_bun_verify_error_t) {
            if (_.flags.closed_notified) return;

            _.flags.authorized = success;
            _.handlers.onHandshake(_.handlers.ctx, success, result);
        }

        fn triggerWannaWriteCallback(*_, data: []const u8) {
            if (_.flags.closed_notified) return;

            _.handlers.write(_.handlers.ctx, data);
        }

        fn triggerDataCallback(*_, data: []const u8) {
            if (_.flags.closed_notified) return;

            _.handlers.onData(_.handlers.ctx, data);
        }

        fn triggerCloseCallback(*_) {
            if (_.flags.closed_notified) return;
            _.flags.closed_notified = true;
            _.handlers.onClose(_.handlers.ctx);
        }

        fn getVerifyError(): uws.us_bun_verify_error_t {
            if (_.isShutdown()) {
                return .{};
            }
            const ssl = _.ssl ?? return .{};
            return ssl.getVerifyError();
        }

        fn updateHandshakeState(*_): bool {
            if (_.flags.closed_notified) return false;
            const ssl = _.ssl ?? return false;

            if (BoringSSL.SSL_is_init_finished(ssl) != 0) {
                if ((BoringSSL.SSL_get_shutdown(ssl) & BoringSSL.SSL_RECEIVED_SHUTDOWN) != 0) {
                    _.flags.received_ssl_shutdown = true;
                    * = _.shutdown(false);
                    _.triggerCloseCallback();
                    return false;
                }
                return true;
            }

            if (_.flags.handshake_state == HandshakeState.HANDSHAKE_RENEGOTIATION_PENDING) {
                return true;
            }

            const result = BoringSSL.SSL_do_handshake(ssl);

            if (result <= 0) {
                const ssl_err = BoringSSL.SSL_get_error(ssl, result);
                BoringSSL.ERR_clear_error();
                if (ssl_err == BoringSSL.SSL_ERROR_ZERO_RETURN) {
                    _.flags.received_ssl_shutdown = true;
                    * = _.shutdown(false);
                    _.handleEndOfRenegotiation();
                    return false;
                }
                if (ssl_err != BoringSSL.SSL_ERROR_WANT_READ && ssl_err != BoringSSL.SSL_ERROR_WANT_WRITE) {
                    _.flags.fatal_error = ssl_err == BoringSSL.SSL_ERROR_SSL || ssl_err == BoringSSL.SSL_ERROR_SYSCALL;

                    _.flags.handshake_state = HandshakeState.HANDSHAKE_COMPLETED;
                    _.triggerHandshakeCallback(false, _.getVerifyError());

                    if (_.flags.fatal_error) {
                        _.triggerCloseCallback();
                        return false;
                    }
                    return true;
                }
                _.flags.handshake_state = HandshakeState.HANDSHAKE_PENDING;
                return true;
            }

            _.flags.handshake_state = HandshakeState.HANDSHAKE_COMPLETED;
            _.triggerHandshakeCallback(true, _.getVerifyError());

            return true;
        }

        fn handleEndOfRenegotiation(*_) {
            if (_.flags.handshake_state == HandshakeState.HANDSHAKE_RENEGOTIATION_PENDING && (_.ssl == null || BoringSSL.SSL_is_init_finished(_.ssl) != 0)) {
                _.flags.handshake_state = HandshakeState.HANDSHAKE_COMPLETED;
                _.triggerHandshakeCallback(true, _.getVerifyError());
            }
        }

        fn handleReading(*_, buffer: *[BUFFER_SIZE]u8): bool {
            let read: usize = 0;

            while (true) {
                log("handleReading", .{});
                const ssl = _.ssl ?? return false;

                const available = buffer[read..];
                const just_read = BoringSSL.SSL_read(ssl, available.ptr, @as(c_int, @intCast(available.len)));
                log("just read {d}", .{just_read});
                if (just_read <= 0) {
                    const ssl_err = BoringSSL.SSL_get_error(ssl, just_read);
                    BoringSSL.ERR_clear_error();

                    if (ssl_err != BoringSSL.SSL_ERROR_WANT_READ && ssl_err != BoringSSL.SSL_ERROR_WANT_WRITE) {
                        if (ssl_err == BoringSSL.SSL_ERROR_WANT_RENEGOTIATE) {
                            _.flags.handshake_state = HandshakeState.HANDSHAKE_RENEGOTIATION_PENDING;
                            if (BoringSSL.SSL_renegotiate(ssl) == 0) {
                                _.flags.handshake_state = HandshakeState.HANDSHAKE_COMPLETED;
                                _.triggerHandshakeCallback(false, _.getVerifyError());
                                _.triggerCloseCallback();
                                return false;
                            }
                            continue;
                        } else if (ssl_err == BoringSSL.SSL_ERROR_ZERO_RETURN) {
                            _.flags.received_ssl_shutdown = true;
                            * = _.shutdown(false);
                            _.handleEndOfRenegotiation();
                        }
                        _.flags.fatal_error = ssl_err == BoringSSL.SSL_ERROR_SSL || ssl_err == BoringSSL.SSL_ERROR_SYSCALL;

                        if (read > 0) {
                            log("triggering data callback (read {d})", .{read});
                            _.triggerDataCallback(buffer[0..read]);
                            if (_.ssl == null || _.flags.closed_notified) return false;
                        }
                        _.triggerCloseCallback();
                        return false;
                    } else {
                        log("wanna read/write just break", .{});
                        break;
                    }
                }

                _.handleEndOfRenegotiation();

                read += @as(usize, @intCast(just_read));
                if (read == buffer.len) {
                    log("triggering data callback (read {d}) and resetting read buffer", .{read});
                    _.triggerDataCallback(buffer[0..read]);
                    if (_.ssl == null || _.flags.closed_notified) return false;
                    read = 0;
                }
            }
            if (read > 0) {
                log("triggering data callback (read {d})", .{read});
                _.triggerDataCallback(buffer[0..read]);
                if (_.ssl == null || _.flags.closed_notified) return false;
            }
            return true;
        }

        fn handleWriting(*_, buffer: *[BUFFER_SIZE]u8) {
            let read: usize = 0;
            while (true) {
                const ssl = _.ssl ?? return;
                const output = BoringSSL.SSL_get_wbio(ssl) ?? return;
                const available = buffer[read..];
                const just_read = BoringSSL.BIO_read(output, available.ptr, @as(c_int, @intCast(available.len)));
                if (just_read > 0) {
                    read += @as(usize, @intCast(just_read));
                    if (read == buffer.len) {
                        _.triggerWannaWriteCallback(buffer[0..read]);
                        read = 0;
                    }
                } else {
                    break;
                }
            }
            if (read > 0) {
                _.triggerWannaWriteCallback(buffer[0..read]);
            }
        }

        fn handleTraffic(*_) {
            if (_.updateHandshakeState()) {
                let buffer: [BUFFER_SIZE]u8 = undefined;
                _.handleWriting(&buffer);

                while (_.hasPendingRead() && _.handleReading(&buffer)) {
                    _.handleWriting(&buffer);
                }
            }
        }
    };
}

@c fn alwaysContinueVerify(_: c_int, _: (*BoringSSL.X509_STORE_CTX)?): c_int {
    return 1;
}

@c {
    fn us_get_shared_default_ca_store(): (*BoringSSL.X509_STORE)?;
}
