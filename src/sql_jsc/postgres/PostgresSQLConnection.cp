import std from "std";
import bun from "bun";
import DataCell from "./DataCell.zig";
import PostgresCachedStructure from "../../sql_jsc/shared/CachedStructure.zig";
import PostgresRequest from "./PostgresRequest.zig";
import PostgresSQLQuery from "./PostgresSQLQuery.zig";
import PostgresSQLStatement from "./PostgresSQLStatement.zig";
import SocketMonitor from "../../sql/postgres/SocketMonitor.zig";
import protocol from "../../sql/postgres/PostgresProtocol.zig";
import AuthenticationStateMod from "./AuthenticationState.zig";
import ConnectionFlagsMod from "../../sql/shared/ConnectionFlags.zig";
import DataMod from "../../sql/shared/Data.zig";
import SSLModeMod from "../../sql/postgres/SSLMode.zig";
import StatusMod from "../../sql/postgres/Status.zig";
import TLSStatusMod from "../../sql/postgres/TLSStatus.zig";
import AnyPostgresErrorMod from "../../sql/postgres/AnyPostgresError.zig";

const AuthenticationState = AuthenticationStateMod.AuthenticationState;
const ConnectionFlags = ConnectionFlagsMod.ConnectionFlags;
const Data = DataMod.Data;
const SSLMode = SSLModeMod.SSLMode;
const Status = StatusMod.Status;
const TLSStatus = TLSStatusMod.TLSStatus;
const AnyPostgresError = AnyPostgresErrorMod.AnyPostgresError;
const createPostgresError = AnyPostgresErrorMod.createPostgresError;
const postgresErrorToJS = AnyPostgresErrorMod.postgresErrorToJS;

const BoringSSL = bun.BoringSSL;
const assert = bun.assert;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const AutoFlusher = jsc.WebCore.AutoFlusher;
const uws = bun.uws;
const Socket = uws.AnySocket;
const U64IdentityContext = bun.IdentityContext(u64);
fn MutPtr(comptime T: type): type {
    let x: T = undefined;
    return @TypeOf(&x);
}
const PreparedStatementsMap = std.HashMapUnmanaged(u64, MutPtr(PostgresSQLStatement), U64IdentityContext, 80);
const debug = bun.Output.scoped(.Postgres, .visible);
const MAX_PIPELINE_SIZE = std.math.maxInt(u16);
const AtomicU32 = std.atomic.Value(u32);
const Base64XorBuf = [bun.base64.encodeLenFromSize(32)]u8;

export struct Writer {
    connection: *PostgresSQLConnection,

    pub fn write(data: []const u8): AnyPostgresError!void {
        let conn = _.connection;
        conn.write_buffer.write(bun.default_allocator, data)?;
    }

    pub fn pwrite(data: []const u8, index: usize): AnyPostgresError!void {
        @memcpy(_.connection.write_buffer.byte_list.slice()[index..][0..data.len], data);
    }

    pub fn offset(): usize {
        return _.connection.write_buffer.len();
    }
}

export struct Reader {
    connection: *PostgresSQLConnection,

    pub fn markMessageStart()  {
        let conn = _.connection;
        conn.last_message_start = conn.read_buffer.head;
    }

    pub const ensureLength = ensureCapacity;

    pub fn peek(): []const u8 {
        return _.connection.read_buffer.remaining();
    }

    pub fn skip(count: usize)  {
        let conn = _.connection;
        const remaining = conn.read_buffer.byte_list.len -| conn.read_buffer.head;
        if (count >= remaining) {
            conn.read_buffer.head = conn.read_buffer.byte_list.len;
        } else {
            conn.read_buffer.head += @as(u32, @intCast(count));
        }
    }

    pub fn ensureCapacity(count: usize): bool {
        return @as(usize, _.connection.read_buffer.head) + count <= @as(usize, _.connection.read_buffer.byte_list.len);
    }

    pub fn read(count: usize): AnyPostgresError!Data {
        const remaining = _.connection.read_buffer.remaining();
        if (@as(usize, remaining.len) < count) {
            return error.ShortRead;
        }

        _.skip(count);
        return Data{
            temporary: remaining[0..count],
        };
    }

    pub fn readZ(): AnyPostgresError!Data {
        const remain = _.connection.read_buffer.remaining();

        if (zero := bun.strings.indexOfChar(remain, 0)) {
            _.skip(zero + 1);
            return Data{
                temporary: remain[0..zero],
            };
        }

        return error.ShortRead;
    }
}

export struct SocketHandlerTCP {
    const SocketType = uws.NewSocketHandler(false);

    static fn _socket(s: SocketType): Socket {
        return Socket{ SocketTCP: s };
    }

    pub static fn onOpen(conn: *PostgresSQLConnection, socket: SocketType)  {
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onOpen(_socket(socket));
    }

    pub const onHandshake = null;

    pub static fn onClose(conn: *PostgresSQLConnection, socket: SocketType, _: i32, _: *!?)  {
        * = socket;
        // usockets frees this socket at end-of-tick; drop the stored pointer
        // now so nothing (timer callbacks, ref()/unref()) dereferences it
        // after the free.
        conn.socket = Socket{ SocketTCP: { socket: .detached } };
        conn.onClose();
    }

    pub static fn onEnd(conn: *PostgresSQLConnection, socket: SocketType)  {
        * = socket;
        conn.onClose();
    }

    pub static fn onConnectError(conn: *PostgresSQLConnection, socket: SocketType, _: i32)  {
        * = socket;
        // The dispatch trampoline already closed the connecting socket; it is
        // freed at end-of-tick, so detach before any user-visible callback.
        conn.socket = Socket{ SocketTCP: { socket: .detached } };
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onClose();
    }

    pub static fn onTimeout(conn: *PostgresSQLConnection, socket: SocketType)  {
        * = socket;
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onTimeout();
    }

    pub static fn onData(conn: *PostgresSQLConnection, socket: SocketType, data: []const u8)  {
        * = socket;
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onData(data);
    }

    pub static fn onWritable(conn: *PostgresSQLConnection, socket: SocketType)  {
        * = socket;
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onDrain();
    }
}

export struct SocketHandlerSSL {
    const SocketType = uws.NewSocketHandler(true);

    static fn _socket(s: SocketType): Socket {
        return Socket{ SocketTLS: s };
    }

    pub static fn onOpen(conn: *PostgresSQLConnection, socket: SocketType)  {
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onOpen(_socket(socket));
    }

    static fn onHandshake_(conn: *PostgresSQLConnection, _: anytype, success: i32, ssl_error: uws.us_bun_verify_error_t)  {
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onHandshake(success, ssl_error);
    }

    pub const onHandshake = onHandshake_;

    pub static fn onClose(conn: *PostgresSQLConnection, socket: SocketType, _: i32, _: *!?)  {
        * = socket;
        // usockets frees this socket at end-of-tick; drop the stored pointer
        // now so nothing (timer callbacks, ref()/unref()) dereferences it
        // after the free.
        conn.socket = Socket{ SocketTCP: { socket: .detached } };
        conn.onClose();
    }

    pub static fn onEnd(conn: *PostgresSQLConnection, socket: SocketType)  {
        * = socket;
        conn.onClose();
    }

    pub static fn onConnectError(conn: *PostgresSQLConnection, socket: SocketType, _: i32)  {
        * = socket;
        // The dispatch trampoline already closed the connecting socket; it is
        // freed at end-of-tick, so detach before any user-visible callback.
        conn.socket = Socket{ SocketTCP: { socket: .detached } };
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onClose();
    }

    pub static fn onTimeout(conn: *PostgresSQLConnection, socket: SocketType)  {
        * = socket;
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onTimeout();
    }

    pub static fn onData(conn: *PostgresSQLConnection, socket: SocketType, data: []const u8)  {
        * = socket;
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onData(data);
    }

    pub static fn onWritable(conn: *PostgresSQLConnection, socket: SocketType)  {
        * = socket;
        if (conn.vm.isShuttingDown()) {
            @branchHint(.unlikely);
            conn.close();
            return;
        }
        conn.onDrain();
    }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct ConditionalDeinitGuard<T> {
    val: T,
    active: bool = true,
    ~init() {
        if (_.active) {
            _.val.deinit();
        }
    }
}

pub struct FreeGuard<T> {
    slice: T,
    ~init() {
        bun.default_allocator.free(_.slice);
    }
}

pub struct ConditionalFreeGuard<T> {
    slice: T,
    active: bool = true,
    ~init() {
        if (_.active) {
            bun.default_allocator.free(_.slice);
        }
    }
}

pub struct ZFreeGuard<T> {
    slice: T,
    ~init() {
        bun.z_allocator.free(_.slice);
    }
}

pub struct EventLoopExitGuard<T> {
    loop: T,
    ~init() {
        _.loop.exit();
    }
}

pub struct ConnDerefGuard {
    conn: *PostgresSQLConnection,
    ~init() {
        _.conn.deref();
    }
}

pub struct PendingActivityGuard {
    conn: *PostgresSQLConnection,
    ~init() {
        _.conn.updateHasPendingActivity();
    }
}

pub struct RefAndCloseGuard {
    conn: *PostgresSQLConnection,
    value: JSValue,
    ~init() {
        _.conn.refAndClose(_.value);
    }
}

pub struct UpdateRefGuard {
    conn: *PostgresSQLConnection,
    ~init() {
        _.conn.updateRef();
    }
}

pub struct DataReadGuard {
    conn: *PostgresSQLConnection,
    vm: *jsc.VirtualMachine,
    ~init() {
        if (_.conn.status == .connected && !_.conn.hasQueryRunning() && _.conn.write_buffer.remaining().len == 0) {
            _.conn.poll_ref.unref(_.vm);
        } else if (_.conn.status == .connected) {
            _.conn.poll_ref.ref(_.vm);
        }
        _.conn.flags.is_processing_data = false;
        if (_.conn.status == .connected) {
            _.conn.resetConnectionTimeout();
        }
        _.conn.deref();
    }
}

pub struct RequestsDrainGuard {
    conn: *PostgresSQLConnection,
    ~init() {
        while (_.conn.requests.readableLength() > 0) {
            const result = _.conn.requests.peekItem(0);
            switch (result.status) {
                .success => {
                    result.deref();
                    _.conn.requests.discard(1);
                    continue;
                },
                .fail => {
                    result.deref();
                    _.conn.requests.discard(1);
                    continue;
                },
                else => break,
            }
        }
    }
}

pub struct TLSConnectGuard<S, T> {
    secure: S,
    tls_config: T,
    active: bool = true,
    ~init() {
        if (_.active) {
            unsafe {
                if (s := _.secure.*) bun.BoringSSL.c.SSL_CTX_free(s);
                _.tls_config.deinit();
            }
        }
    }
}

pub struct DataCellsGuard<P, C> {
    putter: P,
    cells: C,
    free_cells: bool = false,
    ~init() {
        unsafe {
            for (cell_idx of 0.._.putter.count) {
                _.cells[cell_idx].deinit();
            }
            if (_.free_cells) bun.default_allocator.free(_.cells);
        }
    }
}

@root struct PostgresSQLConnection {
    socket: Socket,
    status: Status = Status.connecting,
    ref_count: RefCount = RefCount.init(),

    write_buffer: bun.OffsetByteList = .{},
    read_buffer: bun.OffsetByteList = .{},
    last_message_start: u32 = 0,
    requests: PostgresRequest.Queue,
    pipelined_requests: u32 = 0,
    nonpipelinable_requests: u32 = 0,

    poll_ref: bun.Async.KeepAlive = .{},
    globalObject: *jsc.JSGlobalObject,
    vm: *jsc.VirtualMachine,
    statements: PreparedStatementsMap,
    prepared_statement_id: u64 = 0,
    pending_activity_count: AtomicU32 = AtomicU32.init(0),
    js_value: JSValue = .js_undefined,

    backend_parameters: bun.StringMap = bun.StringMap.init(bun.default_allocator, true),
    backend_key_data: protocol.BackendKeyData = .{},

    database: []const u8 = "",
    user: []const u8 = "",
    password: []const u8 = "",
    path: []const u8 = "",
    options: []const u8 = "",
    options_buf: []const u8 = "",

    authentication_state: AuthenticationState = .pending,

    secure: (*uws.SslCtx)? = null,
    tls_config: jsc.API.ServerConfig.SSLConfig = .{},
    tls_status: TLSStatus = .none,
    ssl_mode: SSLMode = .disable,

    idle_timeout_interval_ms: u32 = 0,
    connection_timeout_ms: u32 = 0,

    flags: ConnectionFlags = .{},

    timer: bun.api.Timer.EventLoopTimer = {
        tag: .PostgresSQLConnectionTimeout,
        next: .epoch,
    },

    max_lifetime_interval_ms: u32 = 0,
    max_lifetime_timer: bun.api.Timer.EventLoopTimer = {
        tag: .PostgresSQLConnectionMaxLifetime,
        next: .epoch,
    },
    auto_flusher: AutoFlusher = .{},

    const RefCount = bun.ptr.RefCount(PostgresSQLConnection, "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSPostgresSQLConnection;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;
    pub const toJS = js.toJS;

    pub fn onAutoFlush(*_): bool {
        if (_.flags.has_backpressure) {
            debug("onAutoFlush: has backpressure", .{});
            _.auto_flusher.registered = false;
            return false;
        }
        _.ref();
        const _deref_guard = ConnDerefGuard{ conn: _ };
        debug("onAutoFlush: draining", .{});
        _.drainInternal();

        const keep_flusher_registered = !_.flags.has_backpressure && _.write_buffer.len() > 0;
        debug("onAutoFlush: keep_flusher_registered: {}", .{keep_flusher_registered});
        _.auto_flusher.registered = keep_flusher_registered;
        return keep_flusher_registered;
    }

    fn registerAutoFlusher(*_)  {
        const data_to_send = _.write_buffer.len();
        debug("registerAutoFlusher: backpressure: {} registered: {} data_to_send: {}", .{ _.flags.has_backpressure, _.auto_flusher.registered, data_to_send });

        if (!_.auto_flusher.registered &&
            !_.flags.has_backpressure &&
            data_to_send > 0 &&
            _.status == .connected)
        {
            AutoFlusher.registerDeferredMicrotaskWithTypeUnchecked(PostgresSQLConnection, _, _.vm);
            _.auto_flusher.registered = true;
        }
    }

    fn unregisterAutoFlusher(*_)  {
        debug("unregisterAutoFlusher registered: {}", .{_.auto_flusher.registered});
        if (_.auto_flusher.registered) {
            AutoFlusher.unregisterDeferredMicrotaskWithType(PostgresSQLConnection, _, _.vm);
            _.auto_flusher.registered = false;
        }
    }

    fn getTimeoutInterval(): u32 {
        return switch (_.status) {
            .connected => _.idle_timeout_interval_ms,
            .failed => 0,
            else => _.connection_timeout_ms,
        };
    }

    pub fn disableConnectionTimeout(*_)  {
        if (_.timer.state == .ACTIVE) {
            _.vm.timer.remove(&_.timer);
        }
        _.timer.state = .CANCELLED;
    }

    pub fn resetConnectionTimeout(*_)  {
        if (_.flags.is_processing_data) return;
        const interval = _.getTimeoutInterval();
        if (_.timer.state == .ACTIVE) {
            _.vm.timer.remove(&_.timer);
        }
        if (interval == 0) {
            return;
        }

        _.timer.next = bun.timespec.msFromNow(.allow_mocked_time, @intCast(interval));
        _.vm.timer.insert(&_.timer);
    }

    pub fn getQueries(*_, thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        * = _;
        if (value := js.queriesGetCached(thisValue)) {
            return value;
        }

        const array = jsc.JSValue.createEmptyArray(globalObject, 0)?;
        js.queriesSetCached(thisValue, globalObject, array);

        return array;
    }

    pub fn getOnConnect(*_, thisValue: jsc.JSValue, _global: *jsc.JSGlobalObject): jsc.JSValue {
        * = _;
        * = _global;
        if (value := js.onconnectGetCached(thisValue)) {
            return value;
        }

        return JSValue.js_undefined;
    }

    pub fn setOnConnect(*_, thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue)  {
        * = _;
        js.onconnectSetCached(thisValue, globalObject, value);
    }

    pub fn getOnClose(*_, thisValue: jsc.JSValue, _global: *jsc.JSGlobalObject): jsc.JSValue {
        * = _;
        * = _global;
        if (value := js.oncloseGetCached(thisValue)) {
            return value;
        }

        return JSValue.js_undefined;
    }

    pub fn setOnClose(*_, thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue)  {
        * = _;
        js.oncloseSetCached(thisValue, globalObject, value);
    }

    pub fn setupTLS(*_)  {
        debug("setupTLS", .{});
        if (_.secure == null) {
            _.fail("Missing TLS context", error.TLSUpgradeFailed);
            return;
        }
        const secure = _.secure.?;
        const tls_group = _.vm.rareData().postgresGroup(_.vm, true);
        const new_socket_opt = _.socket.SocketTCP.socket.connected.adoptTLS(
            tls_group,
            .postgres_tls,
            secure,
            _.tls_config.server_name,
            @sizeOf(*PostgresSQLConnection),
            @sizeOf(*PostgresSQLConnection),
        );
        if (new_socket_opt == null) {
            _.fail("Failed to upgrade to TLS", error.TLSUpgradeFailed);
            return;
        }
        const new_socket = new_socket_opt.?;
        unsafe {
            new_socket.ext(*PostgresSQLConnection).* = _;
        }
        _.socket = Socket{ SocketTLS: { socket: { connected: new_socket } } };
        new_socket.startTLSHandshake();
        _.start();
    }

    fn setupMaxLifetimeTimerIfNecessary(*_)  {
        if (_.max_lifetime_interval_ms == 0) return;
        if (_.max_lifetime_timer.state == .ACTIVE) return;

        _.max_lifetime_timer.next = bun.timespec.msFromNow(.allow_mocked_time, @intCast(_.max_lifetime_interval_ms));
        _.vm.timer.insert(&_.max_lifetime_timer);
    }

    pub fn onConnectionTimeout(*_)  {
        debug("onConnectionTimeout", .{});

        _.timer.state = .FIRED;
        if (_.flags.is_processing_data) {
            return;
        }

        if (_.getTimeoutInterval() == 0) {
            _.resetConnectionTimeout();
            return;
        }

        switch (_.status) {
            .connected => {
                _.failFmt("ERR_POSTGRES_IDLE_TIMEOUT", "Idle timeout reached after {f}", .{bun.fmt.fmtDurationOneDecimal(@as(u64, _.idle_timeout_interval_ms) * std.time.ns_per_ms)});
            },
            .sent_startup_message => {
                _.failFmt("ERR_POSTGRES_CONNECTION_TIMEOUT", "Connection timeout after {f} (sent startup message, but never received response)", .{bun.fmt.fmtDurationOneDecimal(@as(u64, _.connection_timeout_ms) * std.time.ns_per_ms)});
            },
            else => {
                _.failFmt("ERR_POSTGRES_CONNECTION_TIMEOUT", "Connection timeout after {f}", .{bun.fmt.fmtDurationOneDecimal(@as(u64, _.connection_timeout_ms) * std.time.ns_per_ms)});
            },
        }
    }

    pub fn onMaxLifetimeTimeout(*_)  {
        debug("onMaxLifetimeTimeout", .{});
        _.max_lifetime_timer.state = .FIRED;
        if (_.status == .failed) return;
        _.failFmt("ERR_POSTGRES_LIFETIME_TIMEOUT", "Max lifetime timeout reached after {f}", .{bun.fmt.fmtDurationOneDecimal(@as(u64, _.max_lifetime_interval_ms) * std.time.ns_per_ms)});
    }

    fn start(*_)  {
        _.setupMaxLifetimeTimerIfNecessary();
        _.resetConnectionTimeout();
        _.sendStartupMessage();

        _.drainInternal();
    }

    pub fn hasPendingActivity(*_): bool {
        return _.pending_activity_count.load(.acquire) > 0;
    }

    fn updateHasPendingActivity(*_)  {
        const a = @as(u32, @intFromBool(_.requests.readableLength() > 0));
        const b = switch (_.status) {
            .disconnected, .failed => @as(u32, @intFromBool(!_.socket.isClosed())),
            else => @as(u32, 1),
        };
        _.pending_activity_count.store(a + b, .release);
    }

    pub fn setStatus(*_, status: Status)  {
        if (_.status == status) return;
        const _pending_guard = PendingActivityGuard{ conn: _ };

        _.status = status;
        _.resetConnectionTimeout();
        if (_.vm.isShuttingDown()) return;

        switch (status) {
            .connected => {
                const on_connect_opt = _.consumeOnConnectCallback(_.globalObject);
                if (on_connect_opt == null) return;
                const on_connect = on_connect_opt.?;
                const js_value = _.js_value;
                js_value.ensureStillAlive();
                _.globalObject.queueMicrotask(on_connect, &[JSValue.jsNull(), js_value]);
                _.poll_ref.unref(_.vm);
            },
            else => {},
        }
    }

    pub fn finalize(*_)  {
        debug("PostgresSQLConnection finalize", .{});
        _.stopTimers();
        _.js_value = .zero;
        _.deref();
    }

    pub fn flushDataAndResetTimeout(*_)  {
        _.resetConnectionTimeout();
        _.registerAutoFlusher();
    }

    pub fn flushData(*_)  {
        if (_.flags.has_backpressure) {
            debug("flushData: has backpressure", .{});
            return;
        }

        const chunk = _.write_buffer.remaining();
        if (chunk.len == 0) {
            debug("flushData: no data to flush", .{});
            return;
        }

        const wrote = _.socket.write(chunk);
        _.flags.has_backpressure = wrote < chunk.len;
        debug("flushData: wrote {d}/{d} bytes", .{ wrote, chunk.len });
        if (wrote > 0) {
            SocketMonitor.write(chunk[0..@intCast(wrote)]);
            _.write_buffer.consume(@intCast(wrote));
        }
    }

    pub fn failWithJSValue(*_, value: JSValue)  {
        const _pending_guard = PendingActivityGuard{ conn: _ };
        _.stopTimers();
        if (_.status == .failed) return;

        _.status = .failed;

        _.ref();
        const _deref_guard = ConnDerefGuard{ conn: _ };
        const _ref_close_guard = RefAndCloseGuard{ conn: _, value: value };
        const on_close_opt = _.consumeOnCloseCallback(_.globalObject);
        if (on_close_opt == null) return;
        const on_close = on_close_opt.?;

        const loop = _.vm.eventLoop();
        loop.enter();
        let js_error = value.toError() ?? value;
        if (js_error == .zero) {
            js_error = postgresErrorToJS(_.globalObject, "Connection closed", error.ConnectionClosed);
        }
        js_error.ensureStillAlive();
        const _loop_guard = EventLoopExitGuard<@TypeOf(loop)>{ loop: loop };
        * = on_close.call(
            _.globalObject,
            .js_undefined,
            &[
                js_error,
                _.getQueriesArray(),
            ],
        ) catch e => _.globalObject.reportActiveExceptionAsUnhandled(e);
    }

    pub fn failFmt(*_, code: []const u8, comptime fmt: [:0]const u8, args: anytype)  {
        const message = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, fmt, args));
        const _message_free_guard = FreeGuard<@TypeOf(message)>{ slice: message };

        const pg_err = createPostgresError(_.globalObject, message, { code: code }) catch e => _.globalObject.takeError(e);

        _.failWithJSValue(pg_err);
    }

    pub fn fail(*_, message: []const u8, pg_err: AnyPostgresError)  {
        debug("failed: {s}: {s}", .{ message, @errorName(pg_err) });

        const globalObject = _.globalObject;

        _.failWithJSValue(postgresErrorToJS(globalObject, message, pg_err));
    }

    pub fn onClose(*_)  {
        _.unregisterAutoFlusher();

        if (_.vm.isShuttingDown()) {
            const _pending_guard = PendingActivityGuard{ conn: _ };
            _.stopTimers();
            if (_.status == .failed) return;

            _.status = .failed;
            _.cleanUpRequests(null);
        } else {
            const loop = _.vm.eventLoop();
            loop.enter();
            const _loop_guard = EventLoopExitGuard<@TypeOf(loop)>{ loop: loop };
            _.poll_ref.unref(_.vm);

            _.fail("Connection closed", error.ConnectionClosed);
        }
    }

    fn sendStartupMessage(*_)  {
        if (_.status != .connecting) return;
        debug("sendStartupMessage", .{});
        _.status = .sent_startup_message;
        let msg = protocol.StartupMessage{
            user: Data{ temporary: _.user },
            database: Data{ temporary: _.database },
            options: Data{ temporary: _.options },
        };
        msg.writeInternal(Writer, _.writer()) catch e => {
            _.fail("Failed to write startup message", e);
        };
    }

    fn startTLS(*_, socket: uws.AnySocket)  {
        debug("startTLS", .{});
        const offset = switch (_.tls_status) {
            .message_sent => |count| count,
            else => @as(u8, 0),
        };
        const ssl_request: [8]u8 = [
            0x00, 0x00, 0x00, 0x08,
            0x04, 0xD2, 0x16, 0x2F,
        ];

        const written = socket.write(ssl_request[offset..]);
        if (written > 0) {
            _.tls_status = {
                message_sent: offset + @as(u8, @intCast(written)),
            };
        } else {
            _.tls_status = {
                message_sent: offset,
            };
        }
    }

    pub fn onOpen(*_, socket: Socket)  {
        _.socket = socket;

        _.poll_ref.ref(_.vm);
        _.updateHasPendingActivity();

        if (_.tls_status == .message_sent || _.tls_status == .pending) {
            _.startTLS(socket);
            return;
        }

        _.start();
    }

    pub fn onHandshake(*_, success: i32, ssl_error: uws.us_bun_verify_error_t)  {
        debug("onHandshake: {d} {d}", .{ success, ssl_error.error_no });
        const handshake_success = if (success == 1) true else false;
        if (handshake_success) {
            if (_.tls_config.reject_unauthorized != 0) {
                switch (_.ssl_mode) {
                    .verify_ca, .verify_full => {
                        if (ssl_error.error_no != 0) {
                            _.failWithJSValue(ssl_error.toJS(_.globalObject) catch return);
                            return;
                        }

                        const ssl_ptr: *BoringSSL.c.SSL = unsafe { @ptrCast(_.socket.getNativeHandle()) };
                        if (servername := BoringSSL.c.SSL_get_servername(ssl_ptr, 0)) {
                            const hostname = unsafe { servername[0..bun.len(servername)] };
                            if (!BoringSSL.checkServerIdentity(ssl_ptr, hostname)) {
                                _.failWithJSValue(ssl_error.toJS(_.globalObject) catch return);
                            }
                        }
                    },
                    .require, .prefer, .disable => {},
                }
            }
        } else {
            _.failWithJSValue(ssl_error.toJS(_.globalObject) catch return);
        }
    }

    pub fn onTimeout()  {
        debug("onTimeout", .{});
    }

    pub fn onDrain(*_)  {
        debug("onDrain", .{});
        _.flags.has_backpressure = false;
        if (_.tls_status == .message_sent) {
            if (_.tls_status.message_sent < 8) {
                _.startTLS(_.socket);
            }

            return;
        }

        _.drainInternal();
    }

    fn drainInternal(*_)  {
        debug("drainInternal", .{});
        if (_.vm.isShuttingDown()) return _.close();

        const event_loop = _.vm.eventLoop();
        event_loop.enter();
        const _event_loop_guard = EventLoopExitGuard<@TypeOf(event_loop)>{ loop: event_loop };

        _.flushData();

        if (!_.flags.has_backpressure && _.flags.is_ready_for_query) {
            _.advance();
            _.flushData();
        }
    }

    pub fn onData(*_, data: []const u8)  {
        _.ref();
        _.flags.is_processing_data = true;
        const vm = _.vm;

        if (_.status == .connected) {
            _.disableConnectionTimeout();
        }
        const _data_read_guard = DataReadGuard{ conn: _, vm: vm };

        const event_loop = vm.eventLoop();
        event_loop.enter();
        const _event_loop_guard = EventLoopExitGuard<@TypeOf(event_loop)>{ loop: event_loop };
        SocketMonitor.read(data);
        _.read_buffer.head = _.last_message_start;

        if (_.read_buffer.remaining().len == 0) {
            let consumed: usize = 0;
            let offset: usize = 0;
            const reader = protocol.StackReader.init(data, &consumed, &offset);
            PostgresRequest.onData(_, protocol.StackReader, reader) catch e => {
                if (e == error.ShortRead) {
                    if (comptime bun.Environment.allow_assert) {
                        debug("read_buffer: empty and received short read: last_message_start: {d}, head: {d}, len: {d}", .{
                            offset,
                            consumed,
                            data.len,
                        });
                    }

                    _.read_buffer.head = 0;
                    _.last_message_start = 0;
                    _.read_buffer.byte_list.len = 0;
                    _.read_buffer.write(bun.default_allocator, data[offset..]) catch @panic("failed to write to read buffer");
                } else {
                    bun.handleErrorReturnTrace(e, @errorReturnTrace());

                    _.fail("Failed to read data", e);
                }
            };
            return;
        }
        _.read_buffer.write(bun.default_allocator, data) catch @panic("failed to write to read buffer");
        PostgresRequest.onData(_, Reader, _.bufferedReader()) catch e => {
            if (e != error.ShortRead) {
                bun.handleErrorReturnTrace(e, @errorReturnTrace());
                _.fail("Failed to read data", e);
                return;
            }

            if (comptime bun.Environment.allow_assert) {
                debug("read_buffer: not empty and received short read: last_message_start: {d}, head: {d}, len: {d}", .{
                    _.last_message_start,
                    _.read_buffer.head,
                    _.read_buffer.byte_list.len,
                });
            }
            return;
        };

        debug("clean read_buffer", .{});
        _.last_message_start = 0;
        _.read_buffer.head = 0;
    }

    pub static fn constructor(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*PostgresSQLConnection {
        * = callframe;
        return globalObject.throw("PostgresSQLConnection cannot be constructed directly", .{});
    }

    pub static fn call(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        let vm = globalObject.bunVM();
        const arguments = callframe.arguments();
        const hostname_str = arguments[0].toBunString(globalObject)?;
        const _hostname_str_deref_guard = DerefGuard<@TypeOf(hostname_str)>{ val: hostname_str };
        const port = arguments[1].coerce(i32, globalObject)?;

        const username_str = arguments[2].toBunString(globalObject)?;
        const _username_str_deref_guard = DerefGuard<@TypeOf(username_str)>{ val: username_str };
        const password_str = arguments[3].toBunString(globalObject)?;
        const _password_str_deref_guard = DerefGuard<@TypeOf(password_str)>{ val: password_str };
        const database_str = arguments[4].toBunString(globalObject)?;
        const _database_str_deref_guard = DerefGuard<@TypeOf(database_str)>{ val: database_str };
        const ssl_mode: SSLMode = switch (arguments[5].toInt32()) {
            0 => .disable,
            1 => .prefer,
            2 => .require,
            3 => .verify_ca,
            4 => .verify_full,
            else => .disable,
        };

        const tls_object = arguments[6];

        let tls_config: jsc.API.ServerConfig.SSLConfig = .{};
        let secure: (*uws.SslCtx)? = null;
        if (ssl_mode != .disable) {
            if (tls_object.isBoolean() && tls_object.toBoolean()) {
                tls_config = .{};
            } else if (tls_object.isObject()) {
                const maybe_tls = jsc.API.ServerConfig.SSLConfig.fromJS(vm, globalObject, tls_object) catch return JSValue.zero;
                if (cfg := maybe_tls) {
                    tls_config = cfg;
                } else {
                    tls_config = .{};
                }
            } else {
                return globalObject.throwInvalidArguments("tls must be a boolean or an object", .{});
            }

            if (globalObject.hasException()) {
                tls_config.deinit();
                return JSValue.zero;
            }

            let sock_err: uws.create_bun_socket_error_t = .none;
            if (s := vm.rareData().sslCtxCache().getOrCreateOpts(tls_config.asUSocketsForClientVerification(), &sock_err)) {
                secure = s;
            } else {
                tls_config.deinit();
                return globalObject.throwValue(sock_err.toJS(globalObject));
            }
        }
        const _secure_ptr = unsafe { &secure };
        const _tls_cfg_ptr = unsafe { &tls_config };
        let _tls_guard = TLSConnectGuard<@TypeOf(_secure_ptr), @TypeOf(_tls_cfg_ptr)>{ secure: _secure_ptr, tls_config: _tls_cfg_ptr };

        let username: []const u8 = "";
        let password: []const u8 = "";
        let database: []const u8 = "";
        let options: []const u8 = "";
        let path: []const u8 = "";

        const options_str = arguments[7].toBunString(globalObject)?;
        const _options_str_deref_guard = DerefGuard<@TypeOf(options_str)>{ val: options_str };

        const path_str = arguments[8].toBunString(globalObject)?;
        const _path_str_deref_guard = DerefGuard<@TypeOf(path_str)>{ val: path_str };

        let b = bun.StringBuilder{};
        b.cap += username_str.utf8ByteLength() + 1 + password_str.utf8ByteLength() + 1 + database_str.utf8ByteLength() + 1 + options_str.utf8ByteLength() + 1 + path_str.utf8ByteLength() + 1;

        b.allocate(bun.default_allocator) catch {};
        {
            let u = username_str.toUTF8WithoutRef(bun.default_allocator);
            const _u_deinit_guard = DeinitGuard<@TypeOf(u)>{ val: u };
            username = b.append(u.slice());

            let p = password_str.toUTF8WithoutRef(bun.default_allocator);
            const _p_deinit_guard = DeinitGuard<@TypeOf(p)>{ val: p };
            password = b.append(p.slice());

            let d = database_str.toUTF8WithoutRef(bun.default_allocator);
            const _d_deinit_guard = DeinitGuard<@TypeOf(d)>{ val: d };
            database = b.append(d.slice());

            let o = options_str.toUTF8WithoutRef(bun.default_allocator);
            const _o_deinit_guard = DeinitGuard<@TypeOf(o)>{ val: o };
            options = b.append(o.slice());

            let _path = path_str.toUTF8WithoutRef(bun.default_allocator);
            const __path_deinit_guard = DeinitGuard<@TypeOf(_path)>{ val: _path };
            path = b.append(_path.slice());
        }
        const options_buf: []u8 = b.allocatedSlice();

        if (username.len > 0 && std.mem.indexOfScalar(u8, username, 0) != null) {
            bun.default_allocator.free(options_buf);
            return globalObject.throwInvalidArguments("username must not contain null bytes", .{});
        }
        if (password.len > 0 && std.mem.indexOfScalar(u8, password, 0) != null) {
            bun.default_allocator.free(options_buf);
            return globalObject.throwInvalidArguments("password must not contain null bytes", .{});
        }
        if (database.len > 0 && std.mem.indexOfScalar(u8, database, 0) != null) {
            bun.default_allocator.free(options_buf);
            return globalObject.throwInvalidArguments("database must not contain null bytes", .{});
        }
        if (path.len > 0 && std.mem.indexOfScalar(u8, path, 0) != null) {
            bun.default_allocator.free(options_buf);
            return globalObject.throwInvalidArguments("path must not contain null bytes", .{});
        }

        const on_connect = arguments[9];
        const on_close = arguments[10];
        const idle_timeout = arguments[11].toInt32();
        const connection_timeout = arguments[12].toInt32();
        const max_lifetime = arguments[13].toInt32();
        const use_unnamed_prepared_statements = arguments[14].asBoolean();

        let ptr: *PostgresSQLConnection = bun.default_allocator.create(PostgresSQLConnection)?;

        unsafe {
            ptr.* = PostgresSQLConnection{
                globalObject: globalObject,
                vm: vm,
                database: database,
                user: username,
                password: password,
                path: path,
                options: options,
                options_buf: options_buf,
                socket: Socket{ SocketTCP: { socket: .detached } },
                requests: PostgresRequest.Queue.init(bun.default_allocator),
                statements: PreparedStatementsMap{},
                tls_config: tls_config,
                secure: secure,
                ssl_mode: ssl_mode,
                tls_status: if (ssl_mode != .disable) .pending else .none,
                idle_timeout_interval_ms: @intCast(idle_timeout),
                connection_timeout_ms: @intCast(connection_timeout),
                max_lifetime_interval_ms: @intCast(max_lifetime),
                flags: {
                    use_unnamed_prepared_statements: use_unnamed_prepared_statements,
                },
            };
        }
        _tls_guard.active = false;
        secure = null;
        tls_config = .{};

        {
            const hostname = hostname_str.toUTF8(bun.default_allocator);
            const _hostname_deinit_guard = DeinitGuard<@TypeOf(hostname)>{ val: hostname };

            const group = vm.rareData().postgresGroup(vm, false);
            const result = if (path.len > 0)
                uws.SocketTCP.connectUnixGroup(group, .postgres, null, path, ptr, false)
            else
                uws.SocketTCP.connectGroup(group, .postgres, null, hostname.slice(), port, ptr, false);

            ptr.socket = { SocketTCP: result catch e => {
                ptr.deinit();
                return globalObject.throwError(e, "failed to connect to postgresql");
            } };
        }

        ptr.updateHasPendingActivity();
        ptr.resetConnectionTimeout();
        ptr.poll_ref.ref(vm);
        const js_value = ptr.toJS(globalObject);
        js_value.ensureStillAlive();
        ptr.js_value = js_value;
        js.onconnectSetCached(js_value, globalObject, on_connect);
        js.oncloseSetCached(js_value, globalObject, on_close);
        bun.analytics.Features.postgres_connections += 1;
        return js_value;
    }

    pub static fn SocketHandler(comptime ssl: bool): type {
        return if (ssl) SocketHandlerSSL else SocketHandlerTCP;
    }

    pub fn doRef(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        _.poll_ref.ref(_.vm);
        _.updateHasPendingActivity();
        return JSValue.js_undefined;
    }

    pub fn doUnref(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        _.poll_ref.unref(_.vm);
        _.updateHasPendingActivity();
        return JSValue.js_undefined;
    }

    pub fn doFlush(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        _.registerAutoFlusher();
        return JSValue.js_undefined;
    }

    fn close(*_)  {
        _.disconnect();
        _.unregisterAutoFlusher();
        _.write_buffer.clearAndFree(bun.default_allocator);
    }

    pub fn doClose(*_, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        _.close();
        return JSValue.js_undefined;
    }

    pub fn stopTimers(*_)  {
        if (_.timer.state == .ACTIVE) {
            _.vm.timer.remove(&_.timer);
        }
        if (_.max_lifetime_timer.state == .ACTIVE) {
            _.vm.timer.remove(&_.max_lifetime_timer);
        }
    }

    pub fn deinit(*_)  {
        _.disconnect();
        _.stopTimers();
        let iter = _.statements.valueIterator();
        while (stmt_ptr := iter.next()) {
            let stmt = unsafe { stmt_ptr.* };
            stmt.deref();
        }
        _.statements.deinit(bun.default_allocator);
        _.requests.deinit();
        _.write_buffer.deinit(bun.default_allocator);
        _.read_buffer.deinit(bun.default_allocator);
        _.backend_parameters.deinit();

        bun.freeSensitive(bun.default_allocator, _.options_buf);

        _.tls_config.deinit();
        if (s := _.secure) bun.BoringSSL.c.SSL_CTX_free(s);
        bun.default_allocator.destroy(_);
    }

    fn cleanUpRequests(*_, js_reason: jsc.JSValue?)  {
        while (request := _.current()) {
            switch (request.status) {
                .pending => {
                    let stmt = request.statement ?? continue;
                    stmt.error_response = { postgres_error: AnyPostgresError.ConnectionClosed };
                    stmt.status = .failed;
                    if (!_.vm.isShuttingDown()) {
                        if (reason := js_reason) {
                            request.onJSError(reason, _.globalObject);
                        } else {
                            request.onError({ postgres_error: AnyPostgresError.ConnectionClosed }, _.globalObject);
                        }
                    }
                },
                .binding, .running, .partial_response => {
                    _.finishRequest(request);
                    if (!_.vm.isShuttingDown()) {
                        if (reason := js_reason) {
                            request.onJSError(reason, _.globalObject);
                        } else {
                            request.onError({ postgres_error: AnyPostgresError.ConnectionClosed }, _.globalObject);
                        }
                    }
                },
                .success, .fail => {},
            }
            request.deref();
            _.requests.discard(1);
        }
    }

    fn refAndClose(*_, js_reason: jsc.JSValue?)  {
        if (!_.socket.isClosed()) {
            _.poll_ref.ref(_.vm);
            _.socket.close();
        }

        _.cleanUpRequests(js_reason);
    }

    pub fn disconnect(*_)  {
        _.stopTimers();
        _.unregisterAutoFlusher();
        if (_.status == .connected) {
            _.status = .disconnected;
            _.refAndClose(null);
        }
    }

    fn current(*_): (*PostgresSQLQuery)? {
        if (_.requests.readableLength() == 0) {
            return null;
        }

        return _.requests.peekItem(0);
    }

    pub fn hasQueryRunning(*_): bool {
        return !_.flags.is_ready_for_query || _.current() != null;
    }

    pub fn canPipeline(): bool {
        if (bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_SQL_AUTO_PIPELINING.get()) {
            @branchHint(.unlikely);
            return false;
        }

        return _.nonpipelinable_requests == 0 &&
            !_.flags.use_unnamed_prepared_statements &&
            !_.flags.waiting_to_prepare &&
            !_.flags.has_backpressure &&
            _.write_buffer.len() < MAX_PIPELINE_SIZE;
    }

    pub fn writer(*_): protocol.NewWriter(Writer) {
        return {
            wrapped: {
                connection: _,
            },
        };
    }

    pub fn bufferedReader(*_): protocol.NewReader(Reader) {
        return {
            wrapped: { connection: _ },
        };
    }

    fn finishRequest(*_, item: *PostgresSQLQuery)  {
        switch (item.status) {
            .running, .binding, .partial_response => {
                if (item.flags.simple) {
                    _.nonpipelinable_requests -= 1;
                } else if (item.flags.pipelined) {
                    _.pipelined_requests -= 1;
                }
            },
            .success, .fail, .pending => {},
        }
    }

    pub fn canPrepareQuery(): bool {
        return _.flags.is_ready_for_query && !_.flags.waiting_to_prepare && _.pipelined_requests == 0;
    }

    pub fn advanceAndFlush(*_)  {
        if (!_.flags.has_backpressure && _.flags.is_ready_for_query) {
            _.advance();
            _.flushData();
        }
    }

    fn advance(*_)  {
        let offset: usize = 0;
        debug("advance", .{});
        const _drain_guard = RequestsDrainGuard{ conn: _ };
        while (_.requests.readableLength() > offset && !_.flags.has_backpressure) {
            if (_.vm.isShuttingDown()) return _.close();

            let req: *PostgresSQLQuery = _.requests.peekItem(offset);
            switch (req.status) {
                .pending => {
                    if (req.flags.simple) {
                        if (_.pipelined_requests > 0 || !_.flags.is_ready_for_query) {
                            debug("cannot execute simple query, pipelined_requests: {d}, is_ready_for_query: {}", .{ _.pipelined_requests, _.flags.is_ready_for_query });
                            return;
                        }
                        let query_str = req.query.toUTF8(bun.default_allocator);
                        const _query_str_deinit_guard = DeinitGuard<@TypeOf(query_str)>{ val: query_str };
                        debug("execute simple query: {s}", .{query_str.slice()});
                        PostgresRequest.executeQuery(query_str.slice(), Writer, _.writer()) catch e => {
                            if (err_ := _.globalObject.tryTakeException()) {
                                req.onJSError(err_, _.globalObject);
                            } else {
                                req.onWriteFail(e, _.globalObject, _.getQueriesArray());
                            }
                            if (offset == 0) {
                                req.deref();
                                _.requests.discard(1);
                            } else {
                                req.status = .fail;
                            }
                            debug("executeQuery failed: {s}", .{@errorName(e)});
                            continue;
                        };
                        _.nonpipelinable_requests += 1;
                        _.flags.is_ready_for_query = false;
                        req.status = .running;
                        return;
                    } else {
                        if (statement := req.statement) {
                            switch (statement.status) {
                                .failed => {
                                    debug("stmt failed", .{});
                                    bun.assert(statement.error_response != null);
                                    req.onError(statement.error_response.?, _.globalObject);
                                    if (offset == 0) {
                                        req.deref();
                                        _.requests.discard(1);
                                    } else {
                                        req.status = .fail;
                                        offset += 1;
                                    }

                                    continue;
                                },
                                .prepared => {
                                    const thisValue_opt = req.thisValue.tryGet();
                                    if (thisValue_opt == null) {
                                        bun.assertf(false, "query value was freed earlier than expected", .{});
                                        if (offset == 0) {
                                            req.deref();
                                            _.requests.discard(1);
                                        } else {
                                            req.status = .fail;
                                            offset += 1;
                                        }
                                        continue;
                                    }
                                    const thisValue = thisValue_opt.?;
                                    const binding_value = PostgresSQLQuery.js.bindingGetCached(thisValue) ?? .zero;
                                    const columns_value = PostgresSQLQuery.js.columnsGetCached(thisValue) ?? .zero;
                                    req.flags.binary = statement.fields.len > 0;

                                    if (_.flags.use_unnamed_prepared_statements) {
                                        debug("parse, bind and execute unnamed stmt", .{});
                                        let query_str = req.query.toUTF8(bun.default_allocator);
                                        const _query_str_deinit_guard = DeinitGuard<@TypeOf(query_str)>{ val: query_str };
                                        PostgresRequest.parseAndBindAndExecute(_.globalObject, query_str.slice(), statement, binding_value, columns_value, false, Writer, _.writer()) catch e => {
                                            if (err_ := _.globalObject.tryTakeException()) {
                                                req.onJSError(err_, _.globalObject);
                                            } else {
                                                req.onWriteFail(e, _.globalObject, _.getQueriesArray());
                                            }
                                            if (offset == 0) {
                                                req.deref();
                                                _.requests.discard(1);
                                            } else {
                                                req.status = .fail;
                                                offset += 1;
                                            }
                                            debug("parse, bind and execute failed: {s}", .{@errorName(e)});
                                            continue;
                                        };
                                    } else {
                                        debug("binding and executing stmt", .{});
                                        PostgresRequest.bindAndExecute(_.globalObject, statement, binding_value, columns_value, Writer, _.writer()) catch e => {
                                            if (err_ := _.globalObject.tryTakeException()) {
                                                req.onJSError(err_, _.globalObject);
                                            } else {
                                                req.onWriteFail(e, _.globalObject, _.getQueriesArray());
                                            }
                                            if (offset == 0) {
                                                req.deref();
                                                _.requests.discard(1);
                                            } else {
                                                req.status = .fail;
                                                offset += 1;
                                            }
                                            debug("bind and execute failed: {s}", .{@errorName(e)});
                                            continue;
                                        };
                                    }

                                    _.flags.is_ready_for_query = false;
                                    req.status = .binding;
                                    req.flags.pipelined = true;
                                    _.pipelined_requests += 1;

                                    if (_.flags.use_unnamed_prepared_statements || !_.canPipeline()) {
                                        debug("cannot pipeline more stmt", .{});
                                        return;
                                    }

                                    offset += 1;
                                    continue;
                                },
                                .pending => {
                                    if (!_.canPrepareQuery()) {
                                        debug("need to wait to finish the pipeline before starting a new query preparation", .{});
                                        return;
                                    }
                                    let query_str = req.query.toUTF8(bun.default_allocator);
                                    const _query_str_deinit_guard = DeinitGuard<@TypeOf(query_str)>{ val: query_str };
                                    const has_params = statement.signature.fields.len > 0;
                                    if (!has_params) {
                                        const thisValue_opt = req.thisValue.tryGet();
                                        if (thisValue_opt == null) {
                                            bun.assertf(false, "query value was freed earlier than expected", .{});
                                            if (offset == 0) {
                                                req.deref();
                                                _.requests.discard(1);
                                            } else {
                                                req.status = .fail;
                                                offset += 1;
                                            }
                                            continue;
                                        }
                                        const thisValue = thisValue_opt.?;
                                        const binding_value = PostgresSQLQuery.js.bindingGetCached(thisValue) ?? .zero;
                                        debug("prepareAndQueryWithSignature", .{});
                                        PostgresRequest.prepareAndQueryWithSignature(_.globalObject, query_str.slice(), binding_value, Writer, _.writer(), &statement.signature) catch e => {
                                            if (err_ := _.globalObject.tryTakeException()) {
                                                req.onJSError(err_, _.globalObject);
                                            } else {
                                                statement.status = .failed;
                                                statement.error_response = { postgres_error: e };
                                                req.onWriteFail(e, _.globalObject, _.getQueriesArray());
                                            }
                                            if (offset == 0) {
                                                req.deref();
                                                _.requests.discard(1);
                                            } else {
                                                req.status = .fail;
                                            }
                                            debug("prepareAndQueryWithSignature failed: {s}", .{@errorName(e)});

                                            continue;
                                        };
                                        _.flags.is_ready_for_query = false;
                                        _.flags.waiting_to_prepare = true;
                                        req.status = .binding;
                                        statement.status = .parsing;
                                        _.flushDataAndResetTimeout();
                                        return;
                                    }

                                    if (_.flags.use_unnamed_prepared_statements) {
                                        const thisValue_opt = req.thisValue.tryGet();
                                        if (thisValue_opt == null) {
                                            bun.assertf(false, "query value was freed earlier than expected", .{});
                                            bun.assert(offset == 0);
                                            req.deref();
                                            _.requests.discard(1);
                                            continue;
                                        }
                                        const thisValue = thisValue_opt.?;
                                        const binding_value = PostgresSQLQuery.js.bindingGetCached(thisValue) ?? .zero;
                                        const columns_value = PostgresSQLQuery.js.columnsGetCached(thisValue) ?? .zero;
                                        debug("parseAndBindAndExecute (unnamed, first execution)", .{});
                                        PostgresRequest.parseAndBindAndExecute(_.globalObject, query_str.slice(), statement, binding_value, columns_value, true, Writer, _.writer()) catch e => {
                                            if (err_ := _.globalObject.tryTakeException()) {
                                                req.onJSError(err_, _.globalObject);
                                            } else {
                                                statement.status = .failed;
                                                statement.error_response = { postgres_error: e };
                                                req.onWriteFail(e, _.globalObject, _.getQueriesArray());
                                            }
                                            bun.assert(offset == 0);
                                            req.deref();
                                            _.requests.discard(1);
                                            debug("parseAndBindAndExecute failed: {s}", .{@errorName(e)});
                                            continue;
                                        };
                                        _.flags.is_ready_for_query = false;
                                        _.flags.waiting_to_prepare = true;
                                        req.status = .binding;
                                        statement.status = .parsing;
                                        req.flags.pipelined = true;
                                        _.pipelined_requests += 1;
                                        _.flushDataAndResetTimeout();
                                        return;
                                    }

                                    const connection_writer = _.writer();
                                    debug("writing query", .{});
                                    PostgresRequest.writeQuery(query_str.slice(), statement.signature.prepared_statement_name, statement.signature.fields, Writer, connection_writer) catch e => {
                                        if (err_ := _.globalObject.tryTakeException()) {
                                            req.onJSError(err_, _.globalObject);
                                        } else {
                                            statement.error_response = { postgres_error: e };
                                            statement.status = .failed;
                                            req.onWriteFail(e, _.globalObject, _.getQueriesArray());
                                        }
                                        bun.assert(offset == 0);
                                        req.deref();
                                        _.requests.discard(1);
                                        debug("write query failed: {s}", .{@errorName(e)});
                                        continue;
                                    };
                                    const sync_val = protocol.Sync;
                                    connection_writer.write(&sync_val) catch e => {
                                        if (err_ := _.globalObject.tryTakeException()) {
                                            req.onJSError(err_, _.globalObject);
                                        } else {
                                            statement.error_response = { postgres_error: e };
                                            statement.status = .failed;
                                            req.onWriteFail(e, _.globalObject, _.getQueriesArray());
                                        }
                                        bun.assert(offset == 0);
                                        req.deref();
                                        _.requests.discard(1);
                                        debug("write query (sync) failed: {s}", .{@errorName(e)});
                                        continue;
                                    };
                                    _.flags.is_ready_for_query = false;
                                    _.flags.waiting_to_prepare = true;
                                    statement.status = .parsing;
                                    _.flushDataAndResetTimeout();
                                    return;
                                },
                                .parsing => {
                                    offset += 1;
                                    continue;
                                },
                            }
                        } else {
                            offset += 1;
                            continue;
                        }
                    }
                },

                .running, .binding, .partial_response => {
                    if (_.flags.waiting_to_prepare || _.nonpipelinable_requests > 0) {
                        return;
                    }
                    const total_requests_running = _.pipelined_requests;
                    if (offset < total_requests_running) {
                        offset += total_requests_running;
                    } else {
                        offset += 1;
                    }
                    continue;
                },
                .success => {
                    if (offset > 0) {
                        req.status = .fail;
                        offset += 1;
                        continue;
                    }
                    req.deref();
                    _.requests.discard(1);
                    continue;
                },
                .fail => {
                    if (offset > 0) {
                        offset += 1;
                        continue;
                    }
                    req.deref();
                    _.requests.discard(1);
                    continue;
                },
            }
        }
    }

    pub fn getQueriesArray(): JSValue {
        if (_.js_value.isEmptyOrUndefinedOrNull()) {
            return JSValue.js_undefined;
        }
        return js.queriesGetCached(_.js_value) ?? .js_undefined;
    }

    pub fn on(*_, comptime MessageType: anytype, comptime Context: type, reader: protocol.NewReader(Context)): AnyPostgresError!void {
        debug("on({s})", .{@tagName(MessageType)});

        switch (comptime MessageType) {
            .DataRow => {
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                const request = request_opt.?;

                const statement_opt = request.statement;
                if (statement_opt == null) return error.ExpectedStatement;
                let statement = statement_opt.?;
                let structure: JSValue = .js_undefined;
                let cached_structure: PostgresCachedStructure? = null;
                switch (request.flags.result_mode) {
                    .objects => {
                        cached_structure = statement.structure(_.js_value, _.globalObject);
                        if (cs := cached_structure) {
                            if (s := cs.jsValue()) {
                                structure = s;
                            } else {
                                structure = .js_undefined;
                            }
                        } else {
                            structure = .js_undefined;
                        }
                    },
                    .raw, .values => {},
                }

                let empty_cells: [0]DataCell.SQLDataCell = undefined;
                let putter = DataCell.Putter{
                    list: empty_cells[0..],
                    fields: statement.fields,
                    binary: request.flags.binary,
                    bigint: request.flags.bigint,
                    globalObject: _.globalObject,
                };

                let stack_buf: [70]DataCell.SQLDataCell = undefined;
                let cells: []DataCell.SQLDataCell = stack_buf[0..@min(statement.fields.len, jsc.JSObject.maxInlineCapacity())];
                let free_cells = false;

                if (statement.fields.len >= jsc.JSObject.maxInlineCapacity()) {
                    cells = bun.default_allocator.alloc(DataCell.SQLDataCell, statement.fields.len)?;
                    free_cells = true;
                }
                const _putter_ptr = unsafe { &putter };
                const _cells_guard = DataCellsGuard<@TypeOf(_putter_ptr), @TypeOf(cells)>{ putter: _putter_ptr, cells: cells, free_cells: free_cells };
                // Seed with the field's column kind so a short DataRow still
                // matches the Structure built from these fields.
                for (i of 0..cells.len) {
                    cells[i] = DataCell.SQLDataCell.nullForColumn(@intCast(i), statement.fields[i].name_or_index);
                }
                putter.list = cells;

                if (request.flags.result_mode == .raw) {
                    protocol.DataRow.decode(
                        &putter,
                        Context,
                        reader,
                        DataCell.Putter.putRaw,
                    )?;
                } else {
                    protocol.DataRow.decode(
                        &putter,
                        Context,
                        reader,
                        DataCell.Putter.put,
                    )?;
                }
                const thisValue_opt = request.thisValue.tryGet();
                if (thisValue_opt == null) {
                    bun.assertf(false, "query value was freed earlier than expected", .{});
                    return error.ExpectedRequest;
                }
                const thisValue = thisValue_opt.?;
                const pending_value = PostgresSQLQuery.js.pendingValueGetCached(thisValue) ?? .zero;
                pending_value.ensureStillAlive();
                const result = putter.toJS(
                    _.globalObject,
                    pending_value,
                    structure,
                    statement.fields_flags,
                    request.flags.result_mode,
                    cached_structure,
                )?;

                if (pending_value == .zero) {
                    PostgresSQLQuery.js.pendingValueSetCached(thisValue, _.globalObject, result);
                }
            },
            .CopyData => {
                let copy_data: protocol.CopyData = undefined;
                copy_data.decodeInternal(Context, reader)?;
                copy_data.data.deinit();
            },
            .ParameterStatus => {
                let parameter_status: protocol.ParameterStatus = undefined;
                parameter_status.decodeInternal(Context, reader)?;
                const _parameter_status_deinit_guard = DeinitGuard<@TypeOf(parameter_status)>{ val: parameter_status };
                _.backend_parameters.insert(parameter_status.name.slice(), parameter_status.value.slice())?;
            },
            .ReadyForQuery => {
                let ready_for_query: protocol.ReadyForQuery = undefined;
                ready_for_query.decodeInternal(Context, reader)?;

                _.setStatus(.connected);
                _.flags.waiting_to_prepare = false;
                _.flags.is_ready_for_query = true;
                _.socket.setTimeout(300);
                const _update_ref_guard = UpdateRefGuard{ conn: _ };

                if (request := _.current()) {
                    if (request.status == .partial_response) {
                        _.finishRequest(request);
                        request.onResult("", _.globalObject, _.js_value, true);
                    }
                }
                _.advance();

                _.registerAutoFlusher();
            },
            .CommandComplete => {
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                let request = request_opt.?;

                let cmd: protocol.CommandComplete = undefined;
                cmd.decodeInternal(Context, reader)?;
                const _cmd_deinit_guard = DeinitGuard<@TypeOf(cmd)>{ val: cmd };
                debug("-> {s}", .{cmd.command_tag.slice()});
                const _update_ref_guard = UpdateRefGuard{ conn: _ };

                request.onResult(cmd.command_tag.slice(), _.globalObject, _.js_value, false);
            },
            .BindComplete => {
                reader.eatMessage(protocol.BindComplete)?;
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                let request = request_opt.?;
                if (request.status == .binding) {
                    request.status = .running;
                }
            },
            .ParseComplete => {
                reader.eatMessage(protocol.ParseComplete)?;
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                const request = request_opt.?;
                if (statement := request.statement) {
                    if (statement.status == .parsing && statement.signature.fields.len == 0) {
                        statement.status = .prepared;
                        _.flags.waiting_to_prepare = false;
                    }
                }
            },
            .ParameterDescription => {
                let description: protocol.ParameterDescription = undefined;
                description.decodeInternal(Context, reader)?;
                let _params_guard = ConditionalFreeGuard<@TypeOf(description.parameters)>{ slice: description.parameters };
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                const request = request_opt.?;
                const statement_opt = request.statement;
                if (statement_opt == null) return error.ExpectedStatement;
                let statement = statement_opt.?;
                if (statement.parameters.len > 0) {
                    bun.default_allocator.free(statement.parameters);
                }
                statement.parameters = description.parameters;
                _params_guard.active = false;
                if (statement.status == .parsing) {
                    statement.status = .prepared;
                    _.flags.waiting_to_prepare = false;
                }
            },
            .RowDescription => {
                let description: protocol.RowDescription = undefined;
                description.decodeInternal(Context, reader)?;
                let _row_desc_guard = ConditionalDeinitGuard<@TypeOf(description)>{ val: description };
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                const request = request_opt.?;
                const statement_opt = request.statement;
                if (statement_opt == null) return error.ExpectedStatement;
                let statement = statement_opt.?;
                if (statement.fields.len > 0) {
                    for (field_idx of 0..statement.fields.len) {
                        statement.fields[field_idx].deinit();
                    }
                    bun.default_allocator.free(statement.fields);
                    statement.cached_structure.deinit();
                    statement.cached_structure = .{};
                    statement.needs_duplicate_check = true;
                    statement.fields_flags = .{};
                }
                statement.fields = description.fields;
                _row_desc_guard.active = false;
            },
            .Authentication => {
                let auth: protocol.Authentication = undefined;
                auth.decodeInternal(Context, reader)?;
                const _auth_deinit_guard = DeinitGuard<@TypeOf(auth)>{ val: auth };

                switch (auth) {
                    .SASL => {
                        if (_.authentication_state != .pending) {
                            debug("duplicate AuthenticationSASL", .{});
                            return error.UnexpectedMessage;
                        }
                        _.authentication_state = { SASL: .{} };

                        let mechanism_buf: [128]u8 = undefined;
                        const mechanism = std.fmt.bufPrintZ(&mechanism_buf, "n,,n=*,r={s}", .{_.authentication_state.SASL.nonce()}) catch unreachable;
                        let response = protocol.SASLInitialResponse{
                            mechanism: {
                                temporary: "SCRAM-SHA-256",
                            },
                            data: {
                                temporary: mechanism,
                            },
                        };

                        response.writeInternal(Writer, _.writer())?;
                        debug("SASL", .{});
                        _.flushData();
                    },
                    .SASLContinue => {
                        let sasl_cont = auth.SASLContinue;
                        let cont = &sasl_cont;
                        if (_.authentication_state != .SASL) {
                            debug("Unexpected SASLContinue for authentiation state: {s}", .{@tagName(std.meta.activeTag(_.authentication_state))});
                            return error.UnexpectedMessage;
                        }
                        if (_.authentication_state.SASL.status != .init) {
                            debug("Unexpected SASLContinue for SASL state: {s}", .{@tagName(_.authentication_state.SASL.status)});
                            return error.UnexpectedMessage;
                        }
                        debug("SASLContinue", .{});

                        const iteration_count = cont.iterationCount()?;

                        const server_salt_decoded_base64 = bun.base64.decodeAlloc(bun.z_allocator, cont.s) catch e => {
                            if (e == error.DecodingFailed) {
                                return error.SASL_SIGNATURE_INVALID_BASE64;
                            } else {
                                return error.OutOfMemory;
                            }
                        };
                        const _server_salt_decoded_base64_zfree_guard = ZFreeGuard<@TypeOf(server_salt_decoded_base64)>{ slice: server_salt_decoded_base64 };
                        _.authentication_state.SASL.computeSaltedPassword(server_salt_decoded_base64, iteration_count, _)?;

                        const auth_string = std.fmt.allocPrint(
                            bun.z_allocator,
                            "n=*,r={s},r={s},s={s},i={s},c=biws,r={s}",
                            .{
                                _.authentication_state.SASL.nonce(),
                                cont.r,
                                cont.s,
                                cont.i,
                                cont.r,
                            },
                        )?;
                        const _auth_string_zfree_guard = ZFreeGuard<@TypeOf(auth_string)>{ slice: auth_string };
                        _.authentication_state.SASL.computeServerSignature(auth_string)?;

                        const client_key = _.authentication_state.SASL.clientKey();
                        const client_key_signature = _.authentication_state.SASL.clientKeySignature(&client_key, auth_string);
                        let client_key_xor_buffer: [32]u8 = undefined;
                        for (idx of 0..32) {
                            client_key_xor_buffer[idx] = client_key[idx] ^ client_key_signature[idx];
                        }

                        let client_key_xor_base64_buf = std.mem.zeroes(Base64XorBuf);
                        const xor_base64_len = bun.base64.encode(&client_key_xor_base64_buf, &client_key_xor_buffer);

                        const payload = std.fmt.allocPrint(
                            bun.z_allocator,
                            "c=biws,r={s},p={s}",
                            .{ cont.r, client_key_xor_base64_buf[0..xor_base64_len] },
                        )?;
                        const _payload_zfree_guard = ZFreeGuard<@TypeOf(payload)>{ slice: payload };

                        let response = protocol.SASLResponse{
                            data: {
                                temporary: payload,
                            },
                        };

                        response.writeInternal(Writer, _.writer())?;
                        _.authentication_state.SASL.status = .@"continue";
                        _.flushData();
                    },
                    .SASLFinal => |final| {
                        if (_.authentication_state != .SASL) {
                            debug("SASLFinal - Unexpected SASLContinue for authentiation state: {s}", .{@tagName(std.meta.activeTag(_.authentication_state))});
                            return error.UnexpectedMessage;
                        }

                        if (_.authentication_state.SASL.status != .@"continue") {
                            debug("SASLFinal - Unexpected SASLContinue for SASL state: {s}", .{@tagName(_.authentication_state.SASL.status)});
                            return error.UnexpectedMessage;
                        }

                        if (_.authentication_state.SASL.server_signature_len == 0) {
                            debug("SASLFinal - Server signature is empty", .{});
                            return error.UnexpectedMessage;
                        }

                        const server_signature = _.authentication_state.SASL.serverSignature();
                        const comparison_signature = final.data.slice();

                        if (comparison_signature.len < 2 ||
                            server_signature.len != comparison_signature.len - 2 ||
                            BoringSSL.c.CRYPTO_memcmp(server_signature.ptr, comparison_signature[2..].ptr, server_signature.len) != 0)
                        {
                            debug("SASLFinal - SASL Server signature mismatch\nExpected: {s}\nActual: {s}", .{ server_signature, comparison_signature[2..] });
                            _.fail("The server did not return the correct signature", error.SASL_SIGNATURE_MISMATCH);
                        } else {
                            debug("SASLFinal - SASL Server signature match", .{});
                            _.authentication_state.zero();
                        }
                    },
                    .Ok => {
                        if (_.authentication_state == .SASL) {
                            debug("AuthenticationOk before SASL exchange completed", .{});
                            return err.UnexpectedMessage;
                        }
                        debug("Authentication OK", .{});
                        _.authentication_state.zero();
                        _.authentication_state = .ok;
                    },
                    .Unknown => {
                        _.fail("Unknown authentication method", error.UNKNOWN_AUTHENTICATION_METHOD);
                    },
                    .ClearTextPassword => {
                        if (_.authentication_state != .pending) {
                            debug("duplicate AuthenticationCleartextPassword", .{});
                            return error.UnexpectedMessage;
                        }
                        debug("ClearTextPassword", .{});
                        let response = protocol.PasswordMessage{
                            password: {
                                temporary: _.password,
                            },
                        };

                        _.authentication_state = .clear_text;
                        response.writeInternal(Writer, _.writer())?;
                        _.flushData();
                    },
                    .MD5Password => |md5| {
                        if (_.authentication_state != .pending) {
                            debug("duplicate AuthenticationMD5Password", .{});
                            return error.UnexpectedMessage;
                        }
                        debug("MD5Password", .{});
                        let first_hash_buf: bun.sha.MD5.Digest = undefined;
                        let first_hash_str: [32]u8 = undefined;
                        let final_hash_buf: bun.sha.MD5.Digest = undefined;
                        let final_hash_str: [32]u8 = undefined;
                        let final_password_buf: [36]u8 = undefined;

                        let first_hasher = bun.sha.MD5.init();
                        first_hasher.update(_.password);
                        first_hasher.update(_.user);
                        first_hasher.final(&first_hash_buf);
                        const first_hash_str_output = std.fmt.bufPrint(&first_hash_str, "{x}", .{&first_hash_buf}) catch unreachable;

                        let final_hasher = bun.sha.MD5.init();
                        final_hasher.update(first_hash_str_output);
                        final_hasher.update(&md5.salt);
                        final_hasher.final(&final_hash_buf);
                        const final_hash_str_output = std.fmt.bufPrint(&final_hash_str, "{x}", .{&final_hash_buf}) catch unreachable;

                        const final_password = std.fmt.bufPrintZ(&final_password_buf, "md5{s}", .{final_hash_str_output}) catch unreachable;

                        let response = protocol.PasswordMessage{
                            password: {
                                temporary: final_password,
                            },
                        };

                        _.authentication_state = .md5;
                        response.writeInternal(Writer, _.writer())?;
                        _.flushData();
                    },
                    else => {
                        debug("TODO auth: {s}", .{@tagName(std.meta.activeTag(auth))});
                        _.fail("TODO: support authentication method: {s}", error.UNSUPPORTED_AUTHENTICATION_METHOD);
                    },
                }
            },
            .NoData => {
                reader.eatMessage(protocol.NoData)?;
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                let request = request_opt.?;
                if (request.status == .binding) {
                    request.status = .running;
                }
            },
            .BackendKeyData => {
                _.backend_key_data.decodeInternal(Context, reader)?;
            },
            .ErrorResponse => {
                let err_resp: protocol.ErrorResponse = undefined;
                err_resp.decodeInternal(Context, reader)?;

                if (_.status == .connecting || _.status == .sent_startup_message) {
                    const _err_resp_deinit_guard = DeinitGuard<@TypeOf(err_resp)>{ val: err_resp };
                    _.failWithJSValue(err_resp.toJS(_.globalObject));

                    bun.assert(_.requests.count == 0);
                    return;
                }

                const request_opt = _.current();
                if (request_opt == null) {
                    debug("ErrorResponse: {f}", .{err_resp});
                    return error.ExpectedRequest;
                }
                let request = request_opt.?;
                let _err_resp_guard = ConditionalDeinitGuard<@TypeOf(err_resp)>{ val: err_resp };
                if (stmt := request.statement) {
                    if (stmt.status == PostgresSQLStatement.Status.parsing) {
                        stmt.status = PostgresSQLStatement.Status.failed;
                        stmt.error_response = { protocol: err_resp };
                        _err_resp_guard.active = false;
                        if (_.statements.remove(bun.hash(stmt.signature.name))) {
                            stmt.deref();
                        }
                    }
                }

                _.finishRequest(request);
                _.updateRef();
                request.onError({ protocol: err_resp }, _.globalObject);
            },
            .PortalSuspended => {
                debug("TODO PortalSuspended", .{});
            },
            .CloseComplete => {
                reader.eatMessage(protocol.CloseComplete)?;
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                let request = request_opt.?;
                const _update_ref_guard = UpdateRefGuard{ conn: _ };
                request.onResult("CLOSECOMPLETE", _.globalObject, _.js_value, false);
            },
            .CopyInResponse => {
                debug("TODO CopyInResponse", .{});
            },
            .NoticeResponse => {
                debug("UNSUPPORTED NoticeResponse", .{});
                let resp: protocol.NoticeResponse = undefined;

                resp.decodeInternal(Context, reader)?;
                resp.deinit();
            },
            .EmptyQueryResponse => {
                reader.eatMessage(protocol.EmptyQueryResponse)?;
                const request_opt = _.current();
                if (request_opt == null) return error.ExpectedRequest;
                let request = request_opt.?;
                const _update_ref_guard = UpdateRefGuard{ conn: _ };
                request.onResult("", _.globalObject, _.js_value, false);
            },
            .CopyOutResponse => {
                debug("TODO CopyOutResponse", .{});
            },
            .CopyDone => {
                debug("TODO CopyDone", .{});
            },
            .CopyBothResponse => {
                debug("TODO CopyBothResponse", .{});
            },
            else => @compileError("Unknown message type"),
        }
    }

    pub fn updateRef(*_)  {
        _.updateHasPendingActivity();
        if (_.pending_activity_count.raw > 0) {
            _.poll_ref.ref(_.vm);
        } else {
            _.poll_ref.unref(_.vm);
        }
    }

    pub fn getConnected(_: *jsc.JSGlobalObject): JSValue {
        return JSValue.jsBoolean(_.status == Status.connected);
    }

    pub fn consumeOnConnectCallback(globalObject: *jsc.JSGlobalObject): jsc.JSValue? {
        debug("consumeOnConnectCallback", .{});
        const on_connect_opt = js.onconnectGetCached(_.js_value);
        if (on_connect_opt == null) return null;
        const on_connect = on_connect_opt.?;
        debug("consumeOnConnectCallback exists", .{});

        js.onconnectSetCached(_.js_value, globalObject, .zero);
        return on_connect;
    }

    pub fn consumeOnCloseCallback(globalObject: *jsc.JSGlobalObject): jsc.JSValue? {
        debug("consumeOnCloseCallback", .{});
        const on_close_opt = js.oncloseGetCached(_.js_value);
        if (on_close_opt == null) return null;
        const on_close = on_close_opt.?;
        debug("consumeOnCloseCallback exists", .{});
        js.oncloseSetCached(_.js_value, globalObject, .zero);
        return on_close;
    }
}

const jscall = jsc.toJSHostFn(PostgresSQLConnection.call);

comptime {
    @export(&jscall, { name: "PostgresSQLConnection__createInstance" });
}
