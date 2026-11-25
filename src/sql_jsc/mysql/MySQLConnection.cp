import std from "std";
import bun from "bun";
import ErrorPacketMod from "../../sql/mysql/protocol/ErrorPacket.zig";
import AnyMySQLError from "../../sql/mysql/protocol/AnyMySQLError.zig";
import Auth from "../../sql/mysql/protocol/Auth.zig";
import AuthSwitchRequest from "../../sql/mysql/protocol/AuthSwitchRequest.zig";
import AuthSwitchResponse from "../../sql/mysql/protocol/AuthSwitchResponse.zig";
import Capabilities from "../../sql/mysql/Capabilities.zig";
import ColumnDefinition41 from "../../sql/mysql/protocol/ColumnDefinition41.zig";
import EOFPacket from "../../sql/mysql/protocol/EOFPacket.zig";
import HandshakeResponse41 from "../../sql/mysql/protocol/HandshakeResponse41.zig";
import HandshakeV10 from "../../sql/mysql/protocol/HandshakeV10.zig";
import JSMySQLConnection from "./JSMySQLConnection.zig";
import JSMySQLQuery from "./JSMySQLQuery.zig";
import LocalInfileRequest from "../../sql/mysql/protocol/LocalInfileRequest.zig";
import MySQLRequestQueue from "./MySQLRequestQueue.zig";
import MySQLStatement from "./MySQLStatement.zig";
import OKPacket from "../../sql/mysql/protocol/OKPacket.zig";
import PacketHeader from "../../sql/mysql/protocol/PacketHeader.zig";
import ResultSetHeader from "../../sql/mysql/protocol/ResultSetHeader.zig";
import SSLRequest from "../../sql/mysql/protocol/SSLRequest.zig";
import SocketMonitor from "../../sql/postgres/SocketMonitor.zig";
import StackReader from "../../sql/mysql/protocol/StackReader.zig";
import StmtPrepareOKPacket from "../../sql/mysql/protocol/StmtPrepareOKPacket.zig";
import AuthMethodMod from "../../sql/mysql/AuthMethod.zig";
import CharacterSetMod from "../../sql/mysql/protocol/CharacterSet.zig";
import ConnectionFlagsMod from "../../sql/shared/ConnectionFlags.zig";
import ConnectionStateMod from "../../sql/mysql/ConnectionState.zig";
import DataMod from "../../sql/shared/Data.zig";
import NewReaderMod from "../../sql/mysql/protocol/NewReader.zig";
import NewWriterMod from "../../sql/mysql/protocol/NewWriter.zig";
import PacketTypeMod from "../../sql/mysql/protocol/PacketType.zig";
import SSLModeMod from "../../sql/mysql/SSLMode.zig";
import StatusFlagsMod from "../../sql/mysql/StatusFlags.zig";
import TLSStatusMod from "../../sql/mysql/TLSStatus.zig";
import CachedStructure from "../shared/CachedStructure.zig";

pub const ErrorPacket = ErrorPacketMod;
const AuthMethod = AuthMethodMod.AuthMethod;
const CharacterSet = CharacterSetMod.CharacterSet;
const ConnectionFlags = ConnectionFlagsMod.ConnectionFlags;
const ConnectionState = ConnectionStateMod.ConnectionState;
const Data = DataMod.Data;
const NewReader = NewReaderMod.NewReader;
const NewWriter = NewWriterMod.NewWriter;
const PacketType = PacketTypeMod.PacketType;
const SSLMode = SSLModeMod.SSLMode;
const StatusFlags = StatusFlagsMod.StatusFlags;
const TLSStatus = TLSStatusMod.TLSStatus;
const BoringSSL = bun.BoringSSL;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const uws = bun.uws;
const Socket = uws.AnySocket;
const OffsetByteList = bun.OffsetByteList;
const ByteList = bun.ByteList;
const SSLConfig = jsc.API.ServerConfig.SSLConfig;
const ManagedByteList = std.array_list.Managed(u8);
const U64IdentityContext = bun.IdentityContext(u64);
const _mysql_stmt_ptr_sample: *MySQLStatement = undefined;
const PreparedStatementsMap = std.HashMapUnmanaged(u64, @TypeOf(_mysql_stmt_ptr_sample), U64IdentityContext, 80);
pub const PreparedStatementsMapGetOrPutResult = PreparedStatementsMap.GetOrPutResult;
const MAX_PIPELINE_SIZE = std.math.maxInt(u16);
const debug = bun.Output.scoped(.MySQLConnection, .visible);
let empty_columns: [0]ColumnDefinition41 = undefined;
pub err FlushQueueError {
    AuthenticationFailed,
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deref();
    }
}

export enum CachingSha2State {
    fast_auth,
    full_auth,
    waiting_key,
}

pub union(enum) AuthState {
    pending: void,
    native_password: void,
    caching_sha2: CachingSha2State,
    ok: void,

    pub const CachingSha2 = CachingSha2State;
}

export struct Writer {
    connection: *MySQLConnection,

    pub fn write(data: []const u8): AnyMySQLError.Error!void {
        _.connection.@"#write_buffer".write(bun.default_allocator, data)?;
    }

    pub fn pwrite(data: []const u8, index: usize): AnyMySQLError.Error!void {
        @memcpy(_.connection.@"#write_buffer".byte_list.slice()[index..][0..data.len], data);
    }

    pub fn offset(): usize {
        return _.connection.@"#write_buffer".len();
    }

    pub fn truncate(offset_: usize) {
        let buffer = &_.connection.@"#write_buffer";
        const head: usize = buffer.head;
        buffer.byte_list.len = @intCast(head + offset_);
    }
}

export struct Reader {
    connection: *MySQLConnection,

    pub fn markMessageStart()  {
        let conn = _.connection;
        conn.@"#last_message_start" = conn.@"#read_buffer".head;
    }

    pub fn setOffsetFromStart(offset: usize)  {
        let conn = _.connection;
        const clamped: u32 = @intCast(@min(offset, @as(usize, conn.@"#read_buffer".byte_list.len -| conn.@"#last_message_start")));
        conn.@"#read_buffer".head = conn.@"#last_message_start" + clamped;
    }

    pub const ensureLength = ensureCapacity;

    static fn peek(r: Reader): []const u8 {
        return r.connection.@"#read_buffer".remaining();
    }

    pub fn skip(count: isize)  {
        let conn = _.connection;
        if (count < 0) {
            const abs_count = @abs(count);
            if (abs_count > conn.@"#read_buffer".head) {
                conn.@"#read_buffer".head = 0;
                return;
            }
            conn.@"#read_buffer".head -= @as(u32, @intCast(abs_count));
            return;
        }

        const ucount: usize = @intCast(count);
        if (conn.@"#read_buffer".head + ucount > conn.@"#read_buffer".byte_list.len) {
            conn.@"#read_buffer".head = conn.@"#read_buffer".byte_list.len;
            return;
        }

        conn.@"#read_buffer".head += @as(u32, @intCast(ucount));
    }

    pub fn ensureCapacity(count: usize): bool {
        return _.connection.@"#read_buffer".remaining().len >= count;
    }

    pub fn read(count: usize): AnyMySQLError.Error!Data {
        const remaining = _.peek();
        if (remaining.len < count) {
            return AnyMySQLError.Error.ShortRead;
        }

        _.skip(@intCast(count));
        return Data{
            temporary: remaining[0..count],
        };
    }

    pub fn readZ(): AnyMySQLError.Error!Data {
        const remaining = _.peek();
        if (zero := bun.strings.indexOfChar(remaining, 0)) {
            _.skip(@intCast(zero + 1));
            return Data{
                temporary: remaining[0..zero],
            };
        }

        return error.ShortRead;
    }
}

@root struct MySQLConnection {
    @"#socket": Socket = Socket{ SocketTCP: { socket: .detached } },
    status: ConnectionState = .disconnected,

    @"#write_buffer": OffsetByteList = OffsetByteList{},
    @"#read_buffer": OffsetByteList = OffsetByteList{},
    @"#last_message_start": u32 = 0,
    @"#sequence_id": u8 = 0,

    queue: MySQLRequestQueue = MySQLRequestQueue.init(),
    statements: PreparedStatementsMap = PreparedStatementsMap{},

    @"#server_version": ByteList = ByteList{},
    @"#connection_id": u32 = 0,
    @"#capabilities": Capabilities = Capabilities{},
    @"#character_set": CharacterSet = CharacterSet.default,
    @"#status_flags": StatusFlags = StatusFlags{},

    @"#auth_plugin": AuthMethod? = null,
    @"#auth_state": AuthState = .pending,

    @"#auth_data": ManagedByteList = ManagedByteList.init(bun.default_allocator),
    @"#database": []const u8 = "",
    @"#user": []const u8 = "",
    @"#password": []const u8 = "",
    @"#options": []const u8 = "",
    @"#options_buf": []const u8 = "",
    @"#secure": (*uws.SslCtx)? = null,
    @"#tls_config": SSLConfig = SSLConfig{},
    @"#tls_status": TLSStatus = .none,
    @"#ssl_mode": SSLMode = .disable,
    @"#flags": ConnectionFlags = ConnectionFlags{},

    pub static fn init(
        database: []const u8,
        username: []const u8,
        password: []const u8,
        options: []const u8,
        options_buf: []const u8,
        tls_config: SSLConfig,
        secure: (*uws.SslCtx)?,
        ssl_mode: SSLMode,
    ): MySQLConnection {
        return MySQLConnection{
            @"#database": database,
            @"#user": username,
            @"#password": password,
            @"#options": options,
            @"#options_buf": options_buf,
            @"#socket": Socket{ SocketTCP: { socket: { detached: undefined } } },
            queue: MySQLRequestQueue.init(),
            statements: PreparedStatementsMap{},
            @"#tls_config": tls_config,
            @"#secure": secure,
            @"#ssl_mode": ssl_mode,
            @"#tls_status": if (ssl_mode != .disable) .pending else .none,
            @"#character_set": CharacterSet.default,
        };
    }

    pub fn canPipeline(*_): bool {
        return _.queue.canPipeline(_.getJSConnection());
    }

    pub fn canPrepareQuery(*_): bool {
        return _.queue.canPrepareQuery(_.getJSConnection());
    }

    pub fn canExecuteQuery(*_): bool {
        return _.queue.canExecuteQuery(_.getJSConnection());
    }

    pub inline fn isAbleToWrite(_: *const MySQLConnection): bool {
        return _.status == .connected &&
            !_.@"#flags".has_backpressure &&
            _.@"#write_buffer".len() < MAX_PIPELINE_SIZE;
    }

    pub inline fn isProcessingData(*_): bool {
        return _.@"#flags".is_processing_data;
    }

    pub inline fn hasBackpressure(_: *const MySQLConnection): bool {
        return _.@"#flags".has_backpressure;
    }

    pub inline fn resetBackpressure(*_)  {
        _.@"#flags".has_backpressure = false;
    }

    pub inline fn canFlush(_: *const MySQLConnection): bool {
        if (_.@"#flags".has_backpressure || _.status != .connected) return false;
        if (_.@"#write_buffer".len() > 0) return true;
        if (request := _.queue.current()) {
            return request.isPending() && !request.isBeingPrepared();
        }
        return false;
    }

    pub inline fn isIdle(_: *const MySQLConnection): bool {
        return _.queue.current() == null && _.@"#write_buffer".len() == 0;
    }

    pub inline fn enqueueRequest(*_, request: *JSMySQLQuery)  {
        _.queue.add(request);
    }

    pub fn flushQueue(*_): FlushQueueError!void {
        _.flushData();
        if (!_.@"#flags".has_backpressure) {
            if (_.@"#tls_status" == .message_sent) {
                _.upgradeToTLS()?;
            } else {
                _.queue.advance(_.getJSConnection());
                _.flushData();
            }
        }
    }

    fn flushData(*_)  {
        if (_.@"#flags".has_backpressure) {
            debug("flushData: has backpressure", .{});
            return;
        }

        const chunk = _.@"#write_buffer".remaining();
        if (chunk.len == 0) {
            return;
        }

        const wrote = _.@"#socket".write(chunk);
        _.@"#flags".has_backpressure = wrote < chunk.len;
        debug("flushData: wrote {d}/{d} bytes", .{ wrote, chunk.len });
        if (wrote > 0) {
            SocketMonitor.write(chunk[0..@intCast(wrote)]);
            _.@"#write_buffer".consume(@intCast(wrote));
        }
    }

    pub fn close(*_)  {
        _.@"#socket".close();
        _.@"#write_buffer".clearAndFree(bun.default_allocator);
    }

    pub fn cleanQueueAndClose(*_, js_reason: jsc.JSValue?, js_queries_array: JSValue)  {
        _.queue.clean(
            js_reason,
            if (js_queries_array != .zero) js_queries_array else .js_undefined,
        );
        _.close();
    }

    pub fn cleanup(*_)  {
        let queue = _.queue;
        const _guard_queue = DeinitGuard<MySQLRequestQueue>{ ptr: &queue };
        _.queue = MySQLRequestQueue.init();
        let write_buffer = _.@"#write_buffer";
        let read_buffer = _.@"#read_buffer";
        let statements = _.statements;
        let tls_config = _.@"#tls_config";
        const options_buf = _.@"#options_buf";
        _.@"#write_buffer" = OffsetByteList{};
        _.@"#read_buffer" = OffsetByteList{};
        _.statements = PreparedStatementsMap{};
        _.@"#tls_config" = SSLConfig{};
        _.@"#options_buf" = "";
        write_buffer.deinit(bun.default_allocator);
        read_buffer.deinit(bun.default_allocator);

        let iter = statements.valueIterator();
        while (statement := iter.next()) {
            let stmt = unsafe { statement.* };
            stmt.deref();
        }
        statements.deinit(bun.default_allocator);

        tls_config.deinit();
        _.@"#auth_data".deinit();
        if (s := _.@"#secure") {
            bun.BoringSSL.c.SSL_CTX_free(s);
            _.@"#secure" = null;
        }

        if (options_buf.len > 0) {
            bun.default_allocator.free(options_buf);
        }
    }

    pub fn upgradeToTLS(*_): FlushQueueError!void {
        if (_.@"#socket" == .SocketTCP) {
            const secure = _.@"#secure" ?? return error.AuthenticationFailed;
            const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
            const tls_group = vm.rareData().mysqlGroup(vm, true);
            const _js_mysql_conn_ptr_sample: *JSMySQLConnection = undefined;
            const new_socket_opt = _.@"#socket".SocketTCP.socket.connected.adoptTLS(
                tls_group,
                .mysql_tls,
                secure,
                _.@"#tls_config".server_name,
                @sizeOf(@TypeOf(_js_mysql_conn_ptr_sample)),
                @sizeOf(@TypeOf(_js_mysql_conn_ptr_sample)),
            );
            if (new_socket_opt == null) return error.AuthenticationFailed;
            const new_socket = new_socket_opt.?;
            unsafe {
                new_socket.ext(@TypeOf(_js_mysql_conn_ptr_sample)).* = _.getJSConnection();
            }
            _.@"#socket" = Socket{ SocketTLS: { socket: { connected: new_socket } } };
            new_socket.startTLSHandshake();
        }
    }

    pub fn setSocket(*_, socket: Socket)  {
        _.@"#socket" = socket;
    }

    pub fn isActive(*_): bool {
        if (_.status == .disconnected || _.status == .failed) {
            return false;
        }
        return true;
    }

    pub inline fn isConnected(*_): bool {
        return _.status == .connected;
    }

    pub fn doHandshake(*_, success: i32, ssl_error: uws.us_bun_verify_error_t): anyerror!bool {
        debug("onHandshake: {d} {d} {s}", .{ success, ssl_error.error_no, @tagName(_.@"#ssl_mode") });
        const handshake_success = if (success == 1) true else false;
        _.@"#sequence_id" = _.@"#sequence_id" +% 1;
        if (handshake_success) {
            _.@"#tls_status" = .ssl_ok;
            if (_.@"#tls_config".reject_unauthorized != 0) {
                switch (_.@"#ssl_mode") {
                    .verify_ca, .verify_full => {
                        if (ssl_error.error_no != 0) {
                            _.@"#tls_status" = .ssl_failed;
                            return false;
                        }

                        const ssl_ptr: *BoringSSL.c.SSL = unsafe { @ptrCast(_.@"#socket".getNativeHandle()) };
                        if (servername := BoringSSL.c.SSL_get_servername(ssl_ptr, 0)) {
                            const hostname = unsafe { servername[0..bun.len(servername)] };
                            if (!BoringSSL.checkServerIdentity(ssl_ptr, hostname)) {
                                _.@"#tls_status" = .ssl_failed;
                                return false;
                            }
                        }
                    },
                    .require, .prefer, .disable => {},
                }
            }
            _.sendHandshakeResponse()?;
            return true;
        }
        _.@"#tls_status" = .ssl_failed;
        return false;
    }

    pub fn readAndProcessData(*_, data: []const u8): anyerror!void {
        _.@"#flags".is_processing_data = true;
        defer _.@"#flags".is_processing_data = false;
        _.@"#socket".setTimeout(0);

        SocketMonitor.read(data);

        if (_.@"#read_buffer".remaining().len == 0) {
            let consumed: usize = 0;
            let offset: usize = 0;
            const reader = StackReader.init(data, &consumed, &offset);
            _.processPackets(StackReader, reader) catch e => {
                debug("processPackets without buffer: {s}", .{@errorName(e)});
                if (e == error.ShortRead) {
                    if (comptime bun.Environment.allow_assert) {
                        debug("Received short read: last_message_start: {d}, head: {d}, len: {d}", .{
                            offset,
                            consumed,
                            data.len,
                        });
                    }

                    _.@"#read_buffer".head = 0;
                    _.@"#last_message_start" = 0;
                    _.@"#read_buffer".byte_list.len = 0;
                    _.@"#read_buffer".write(bun.default_allocator, data[offset..]) catch @panic("failed to write to read buffer");
                } else {
                    if (comptime bun.Environment.allow_assert) {
                        bun.handleErrorReturnTrace(e, @errorReturnTrace());
                    }
                    return e;
                }
            };
            return;
        }

        _.@"#read_buffer".head = _.@"#last_message_start";
        _.@"#read_buffer".write(bun.default_allocator, data) catch @panic("failed to write to read buffer");
        _.processPackets(Reader, _.bufferedReader()) catch e => {
            debug("processPackets with buffer: {s}", .{@errorName(e)});
            if (e != error.ShortRead) {
                if (comptime bun.Environment.allow_assert) {
                    if (trace := @errorReturnTrace()) {
                        debug("Error: {s}\n{f}", .{ @errorName(e), trace });
                    }
                }
                return e;
            }

            if (comptime bun.Environment.allow_assert) {
                debug("Received short read: last_message_start: {d}, head: {d}, len: {d}", .{
                    _.@"#last_message_start",
                    _.@"#read_buffer".head,
                    _.@"#read_buffer".byte_list.len,
                });
            }

            return;
        };

        _.@"#last_message_start" = 0;
        _.@"#read_buffer".head = 0;
    }

    pub fn processPackets(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        while (true) {
            reader.markMessageStart();

            const header_opt = PacketHeader.decode(reader.peek());
            if (header_opt == null) return AnyMySQLError.Error.ShortRead;
            const header = header_opt.?;
            const header_length = header.length;
            const packet_length: usize = header_length + PacketHeader.size;
            debug("sequence_id: {d} header: {d}", .{ _.@"#sequence_id", header_length });
            reader.ensureCapacity(packet_length) catch return AnyMySQLError.Error.ShortRead;
            defer reader.setOffsetFromStart(packet_length);
            reader.skip(PacketHeader.size);

            _.@"#sequence_id" = header.sequence_id +% 1;

            switch (_.status) {
                .handshaking => {
                    _.handleHandshake(Context, reader)?;
                    // If the handshake negotiated TLS, the SSLRequest has been sent and
                    // everything after this packet must arrive over the encrypted channel.
                    // Any bytes already buffered behind the handshake packet are plaintext
                    // a man-in-the-middle could have injected (CVE-2021-23222 class), so
                    // reject them instead of feeding them to the auth/command handlers.
                    if (_.@"#tls_status" == .message_sent) {
                        reader.setOffsetFromStart(packet_length);
                        const remaining_bytes = reader.peek();
                        if (remaining_bytes.len > 0) {
                            return error.UnexpectedPacket;
                        }
                    }
                },
                .authenticating, .authentication_awaiting_pk => _.handleAuth(Context, reader, header_length)?,
                .connected => _.handleCommand(Context, reader, header_length)?,
                else => {
                    debug("Unexpected packet in state {s}", .{@tagName(_.status)});
                    return error.UnexpectedPacket;
                },
            }
        }
    }

    pub fn handleHandshake(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        let handshake = HandshakeV10{};
        handshake.decode(reader)?;
        const _guard_handshake = DeinitGuard<HandshakeV10>{ ptr: &handshake };

        _.@"#server_version" = handshake.server_version.toOwned()?;
        _.@"#connection_id" = handshake.connection_id;
        _.@"#capabilities" = Capabilities.getDefaultCapabilities(_.@"#ssl_mode" != .disable, _.@"#database".len > 0)
            .intersect(handshake.capability_flags);

        _.@"#character_set" = CharacterSet.default;
        _.@"#status_flags" = handshake.status_flags;

        debug(
            "Handshake\n   Server Version: {s}\n   Connection ID:  {d}\n   Character Set:  {d} ({s})\n   Server Capabilities:   [ {f} ] 0x{x:0>8}\n   Negotiated Capabilities: [ {f} ] 0x{x:0>8}\n   Status Flags:   [ {f} ]\n",
            .{
                _.@"#server_version".slice(),
                _.@"#connection_id",
                _.@"#character_set",
                _.@"#character_set".label(),
                handshake.capability_flags,
                handshake.capability_flags.toInt(),
                _.@"#capabilities",
                _.@"#capabilities".toInt(),
                _.@"#status_flags",
            },
        );

        _.@"#auth_data".clearAndFree();

        _.@"#auth_data".ensureTotalCapacity(handshake.auth_plugin_data_part_1.len + handshake.auth_plugin_data_part_2.len)?;
        _.@"#auth_data".appendSlice(handshake.auth_plugin_data_part_1[0..])?;
        _.@"#auth_data".appendSlice(handshake.auth_plugin_data_part_2[0..])?;

        if (handshake.auth_plugin_name.slice().len > 0) {
            if (auth_method := AuthMethod.fromString(handshake.auth_plugin_name.slice())) {
                _.@"#auth_plugin" = auth_method;
            } else {
                return error.UnsupportedAuthPlugin;
            }
        }

        _.setStatus(.authenticating);

        if (_.@"#capabilities".CLIENT_SSL) {
            let response = SSLRequest{
                capability_flags: _.@"#capabilities",
                max_packet_size: 0,
                character_set: CharacterSet.default,
                has_connection_attributes: true,
            };
            const _guard_response = DeinitGuard<SSLRequest>{ ptr: &response };
            response.write(_.writer())?;
            _.@"#capabilities" = response.capability_flags;
            _.@"#tls_status" = .message_sent;
            _.flushData();
            if (!_.@"#flags".has_backpressure) {
                _.upgradeToTLS()?;
            }
            return;
        }
        if (_.@"#tls_status" != .none) {
            _.@"#tls_status" = .ssl_not_available;

            switch (_.@"#ssl_mode") {
                .require, .verify_ca, .verify_full => return error.AuthenticationFailed,
                .prefer, .disable => {},
            }
        }
        _.sendHandshakeResponse()?;
    }

    fn handleHandshakeDecodePublicKey(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        let response = Auth.caching_sha2_password.PublicKeyResponse{};
        response.decode(reader)?;
        const _guard_response = DeinitGuard<Auth.caching_sha2_password.PublicKeyResponse>{ ptr: &response };
        _.setStatus(.authenticating);

        let encrypted_password = Auth.caching_sha2_password.EncryptedPassword{
            password: _.@"#password",
            public_key: response.data.slice(),
            nonce: _.@"#auth_data".items,
            sequence_id: _.@"#sequence_id",
        };
        encrypted_password.write(_.writer())?;
        _.flushData();
    }

    pub fn setStatus(*_, status: ConnectionState)  {
        if (_.status == status) return;

        _.status = status;

        switch (status) {
            .connected => _.getJSConnection().onConnectionEstabilished(),
            else => {},
        }
    }

    pub fn handleAuth(*_, comptime Context: type, reader: NewReader(Context), header_length: u24): anyerror!void {
        const first_byte = reader.int(u8)?;
        reader.skip(-1);

        debug("Auth packet: 0x{x:0>2}", .{first_byte});

        switch (first_byte) {
            0x00 => {
                let ok = OKPacket{
                    packet_size: header_length,
                };
                ok.decode(reader)?;
                const _guard_ok = DeinitGuard<OKPacket>{ ptr: &ok };

                _.setStatus(.connected);

                _.@"#status_flags" = ok.status_flags;
                _.@"#flags".is_ready_for_query = true;
                _.queue.markAsReadyForQuery();
                _.queue.advance(_.getJSConnection());
            },
            0xff => {
                let err_pkt = ErrorPacket{};
                err_pkt.decode(reader)?;
                const _guard_err_pkt = DeinitGuard<ErrorPacket>{ ptr: &err_pkt };

                _.getJSConnection().onErrorPacket(null, err_pkt);
                return error.AuthenticationFailed;
            },
            0x01 => {
                if (plugin := _.@"#auth_plugin") {
                    switch (plugin) {
                        .sha256_password, .caching_sha2_password => {
                            reader.skip(1);

                            if (_.status == .authentication_awaiting_pk) {
                                return _.handleHandshakeDecodePublicKey(Context, reader);
                            }

                            let response = Auth.caching_sha2_password.Response{};
                            response.decode(reader)?;
                            const _guard_response = DeinitGuard<Auth.caching_sha2_password.Response>{ ptr: &response };

                            switch (response.status) {
                                .success => {
                                    // Fast auth succeeded; the server still sends an OK packet to conclude authentication.
                                    debug("fast auth success, awaiting OK packet", .{});
                                },
                                .continue_auth => {
                                    debug("continue auth", .{});

                                    if (_.@"#ssl_mode" == .disable) {
                                        _.setStatus(.authentication_awaiting_pk);
                                        debug("awaiting public key", .{});
                                        let packet = _.writer().start(_.@"#sequence_id")?;

                                        let request = Auth.caching_sha2_password.PublicKeyRequest{};
                                        request.write(_.writer())?;
                                        packet.end()?;
                                        _.flushData();
                                    } else {
                                        debug("sending password TLS enabled", .{});
                                        let packet = _.writer().start(_.@"#sequence_id")?;
                                        _.writer().writeZ(_.@"#password")?;
                                        packet.end()?;
                                        _.flushData();
                                    }
                                },
                                else => return error.AuthenticationFailed,
                            }
                        },
                        else => {
                            debug("Unexpected auth continuation for plugin: {s}", .{@tagName(plugin)});
                            return error.UnexpectedPacket;
                        },
                    }
                } else if (first_byte == 0xfb) {
                    let infile = LocalInfileRequest{
                        packet_size: header_length,
                    };
                    infile.decode(reader)?;
                    const _guard_infile = DeinitGuard<LocalInfileRequest>{ ptr: &infile };
                    return error.LocalInfileNotSupported;
                } else {
                    debug("Received auth continuation without plugin", .{});
                    return error.UnexpectedPacket;
                }
            },
            0xfe => {
                let auth_switch = AuthSwitchRequest{
                    packet_size: header_length,
                };
                auth_switch.decode(reader)?;
                const _guard_auth_switch = DeinitGuard<AuthSwitchRequest>{ ptr: &auth_switch };

                if (auth_method := AuthMethod.fromString(auth_switch.plugin_name.slice())) {
                    const auth_data = auth_switch.plugin_data.slice();
                    _.@"#auth_plugin" = auth_method;
                    _.@"#auth_data".clearRetainingCapacity();
                    _.@"#auth_data".appendSlice(auth_data)?;
                    _.sendAuthSwitchResponse(auth_method, auth_data)?;
                } else {
                    return error.UnsupportedAuthPlugin;
                }
            },
            else => {
                debug("Unexpected auth packet: 0x{x:0>2}", .{first_byte});
                return error.UnexpectedPacket;
            },
        }
    }

    pub fn handleCommand(*_, comptime Context: type, reader: NewReader(Context), header_length: u24): anyerror!void {
        const request_opt = _.queue.current();
        if (request_opt == null) {
            debug("Received unexpected command response", .{});
            return error.UnexpectedPacket;
        }
        const request = request_opt.?;
        request.ref();
        const _guard_request = DerefGuard<JSMySQLQuery>{ ptr: request };

        debug("handleCommand", .{});
        if (request.isSimple()) {
            return _.handleResultSet(Context, reader, header_length);
        }

        if (statement := request.getStatement()) {
            statement.ref();
            const _guard_statement = DerefGuard<MySQLStatement>{ ptr: statement };
            switch (statement.status) {
                .pending => return error.UnexpectedPacket,
                .parsing => _.handlePreparedStatement(Context, reader, header_length)?,
                .prepared => _.handleResultSet(Context, reader, header_length)?,
                .failed => {
                    defer {
                        _.flushQueue() catch {};
                    }
                    _.@"#flags".is_ready_for_query = true;
                    _.queue.markAsReadyForQuery();
                    _.queue.markCurrentRequestAsFinished(request);
                    _.getJSConnection().onErrorPacket(request, statement.error_response);
                },
            }
        }
    }

    pub fn sendHandshakeResponse(*_): ! {
        debug("sendHandshakeResponse", .{});
        if (plugin := _.@"#auth_plugin") {
            const requires_password = switch (plugin) {
                .caching_sha2_password => false,
                .sha256_password => true,
                .mysql_native_password => false,
            };

            if (requires_password && _.@"#password".len == 0) {
                return error.PasswordRequired;
            }
        }

        let plugin_name: []const u8 = "";
        if (plugin := _.@"#auth_plugin") {
            plugin_name = switch (plugin) {
                .mysql_native_password => "mysql_native_password",
                .caching_sha2_password => "caching_sha2_password",
                .sha256_password => "sha256_password",
            };
        }

        let response = HandshakeResponse41{
            capability_flags: _.@"#capabilities",
            max_packet_size: 0,
            character_set: CharacterSet.default,
            username: Data{ temporary: _.@"#user" },
            database: Data{ temporary: _.@"#database" },
            auth_plugin_name: Data{ temporary: plugin_name },
            auth_response: Data.empty,
            sequence_id: _.@"#sequence_id",
        };
        const _guard_response = DeinitGuard<HandshakeResponse41>{ ptr: &response };

        response.connect_attrs.put(bun.default_allocator, bun.default_allocator.dupe(u8, "_client_name")?, bun.default_allocator.dupe(u8, "Bun")?)?;
        response.connect_attrs.put(bun.default_allocator, bun.default_allocator.dupe(u8, "_client_version")?, bun.default_allocator.dupe(u8, bun.Global.package_json_version_with_revision)?)?;

        let scrambled_buf: [32]u8 = undefined;
        if (plugin := _.@"#auth_plugin") {
            if (_.@"#auth_data".items.len == 0) {
                return error.MissingAuthData;
            }

            response.auth_response = Data{ temporary: plugin.scramble(_.@"#password", _.@"#auth_data".items, &scrambled_buf)? };
        }
        response.capability_flags.reject();
        response.write(_.writer())?;
        _.@"#capabilities" = response.capability_flags;
        _.flushData();
    }

    pub fn sendAuthSwitchResponse(*_, auth_method: AuthMethod, plugin_data: []const u8): anyerror!void {
        let response = AuthSwitchResponse{};
        const _guard_response = DeinitGuard<AuthSwitchResponse>{ ptr: &response };

        let scrambled_buf: [32]u8 = undefined;

        response.auth_response = Data{
            temporary: auth_method.scramble(_.@"#password", plugin_data, &scrambled_buf)?,
        };

        let response_writer = _.writer();
        let packet = response_writer.start(_.@"#sequence_id")?;
        response.write(response_writer)?;
        packet.end()?;
        _.flushData();
    }

    pub fn writer(*_): NewWriter(Writer) {
        const Wrapped = NewWriter(Writer);
        return Wrapped{
            wrapped: Writer{
                connection: _,
            },
        };
    }

    pub fn bufferedReader(*_): NewReader(Reader) {
        const Wrapped = NewReader(Reader);
        return Wrapped{
            wrapped: Reader{
                connection: _,
            },
        };
    }

    fn checkIfPreparedStatementIsDone(*_, statement: *MySQLStatement)  {
        debug("checkIfPreparedStatementIsDone: {d} {d} {d} {d}", .{ statement.columns_received, statement.params_received, statement.columns.len, statement.params.len });
        if (statement.columns_received == statement.columns.len && statement.params_received == statement.params.len) {
            statement.status = .prepared;
            _.@"#flags".waiting_to_prepare = false;
            _.@"#flags".is_ready_for_query = true;
            _.queue.markAsReadyForQuery();
            _.queue.markAsPrepared();
            statement.reset();
            _.queue.advance(_.getJSConnection());
        }
    }

    pub fn handlePreparedStatement(*_, comptime Context: type, reader: NewReader(Context), header_length: u24): anyerror!void {
        debug("handlePreparedStatement", .{});
        const first_byte = reader.int(u8)?;
        reader.skip(-1);

        const request_opt = _.queue.current();
        if (request_opt == null) {
            debug("Unexpected prepared statement packet missing request", .{});
            return error.UnexpectedPacket;
        }
        let request = request_opt.?;
        request.ref();
        const _guard_request = DerefGuard<JSMySQLQuery>{ ptr: request };
        const statement_opt = request.getStatement();
        if (statement_opt == null) {
            debug("Unexpected prepared statement packet missing statement", .{});
            return error.UnexpectedPacket;
        }
        let statement = statement_opt.?;
        statement.ref();
        const _guard_statement = DerefGuard<MySQLStatement>{ ptr: statement };
        if (statement.statement_id > 0) {
            if (!_.@"#capabilities".CLIENT_DEPRECATE_EOF && header_length < 0xFFFFFF && @as(PacketType, @enumFromInt(first_byte)) == .EOF) {
                let eof = EOFPacket{};
                eof.decode(reader)?;
                _.checkIfPreparedStatementIsDone(statement);
                return;
            }
            if (statement.params_received < statement.params.len) {
                let column = ColumnDefinition41{};
                const _guard_column = DeinitGuard<ColumnDefinition41>{ ptr: &column };
                column.decode(reader)?;
                statement.params[statement.params_received] = MySQLStatement.Param{
                    type: column.column_type,
                    flags: column.flags,
                };
                statement.params_received += 1;
            } else if (statement.columns_received < statement.columns.len) {
                statement.columns[statement.columns_received].decode(reader)?;
                statement.columns_received += 1;
            }
            if (_.@"#capabilities".CLIENT_DEPRECATE_EOF) {
                _.checkIfPreparedStatementIsDone(statement);
            }
            return;
        }

        switch (@as(PacketType, @enumFromInt(first_byte))) {
            .OK => {
                let ok = StmtPrepareOKPacket{
                    packet_length: header_length,
                };
                ok.decode(reader)?;

                statement.statement_id = ok.statement_id;

                if (ok.num_params > 0) {
                    statement.params = bun.default_allocator.alloc(MySQLStatement.Param, ok.num_params)?;
                    statement.params_received = 0;
                }

                if (ok.num_columns > 0) {
                    statement.columns = bun.default_allocator.alloc(ColumnDefinition41, ok.num_columns)?;
                    let col_i: usize = 0;
                    while (col_i < statement.columns.len) {
                        statement.columns[col_i] = ColumnDefinition41{};
                        col_i += 1;
                    }
                    statement.columns_received = 0;
                }

                _.checkIfPreparedStatementIsDone(statement);
            },
            .ERROR => {
                debug("handlePreparedStatement ERROR", .{});
                let err_pkt = ErrorPacket{};
                err_pkt.decode(reader)?;
                const _guard_err_pkt = DeinitGuard<ErrorPacket>{ ptr: &err_pkt };
                _.@"#flags".is_ready_for_query = true;
                statement.status = .failed;
                statement.error_response.deinit();
                statement.error_response = err_pkt;
                statement.error_response.error_message = bun.handleOom(Data.create(err_pkt.error_message.slice(), bun.default_allocator));
                _.queue.markAsReadyForQuery();
                _.queue.markCurrentRequestAsFinished(request);
                defer {
                    _.queue.advance(_.getJSConnection());
                }

                _.getJSConnection().onErrorPacket(request, err_pkt);
            },
            else => {
                debug("Unexpected prepared statement packet: 0x{x:0>2}", .{first_byte});
                return error.UnexpectedPacket;
            },
        }
    }

    fn handleResultSetOK(*_, request: *JSMySQLQuery, statement: *MySQLStatement, status_flags: StatusFlags, last_insert_id: u64, affected_rows: u64)  {
        _.@"#status_flags" = status_flags;
        const is_last_result = !status_flags.has(.SERVER_MORE_RESULTS_EXISTS);
        debug("handleResultSetOK: {d} {}", .{ status_flags.toInt(), is_last_result });
        defer {
            _.flushQueue() catch {};
        }
        _.@"#flags".is_ready_for_query = is_last_result;
        if (is_last_result) {
            _.queue.markAsReadyForQuery();
            _.queue.markCurrentRequestAsFinished(request);
        }

        _.getJSConnection().onQueryResult(request, {
            result_count: statement.result_count,
            last_insert_id: last_insert_id,
            affected_rows: affected_rows,
            is_last_result: is_last_result,
        });

        statement.reset();
    }

    fn getJSConnection(*_): *JSMySQLConnection {
        return @fieldParentPtr("#connection", _);
    }

    fn handleResultSet(*_, comptime Context: type, reader: NewReader(Context), header_length: u24): anyerror!void {
        const first_byte = reader.int(u8)?;
        debug("handleResultSet: {x:0>2}", .{first_byte});

        reader.skip(-1);

        const request_opt = _.queue.current();
        if (request_opt == null) {
            debug("Unexpected result set packet", .{});
            return error.UnexpectedPacket;
        }
        let request = request_opt.?;
        request.ref();
        const _guard_request = DerefGuard<JSMySQLQuery>{ ptr: request };
        let ok = OKPacket{
            packet_size: header_length,
        };
        const pkt_type = @as(PacketType, @enumFromInt(first_byte));
        switch (pkt_type) {
            .ERROR => {
                let err_pkt = ErrorPacket{};
                err_pkt.decode(reader)?;
                const _guard_err_pkt = DeinitGuard<ErrorPacket>{ ptr: &err_pkt };
                defer {
                    _.flushQueue() catch {};
                }
                if (statement := request.getStatement()) {
                    statement.reset();
                }

                _.@"#flags".is_ready_for_query = true;
                _.queue.markAsReadyForQuery();
                _.queue.markCurrentRequestAsFinished(request);

                _.getJSConnection().onErrorPacket(request, err_pkt);
            },
            else => {
                const statement_opt = request.getStatement();
                if (statement_opt == null) {
                    debug("Unexpected result set packet", .{});
                    return error.UnexpectedPacket;
                }
                let statement = statement_opt.?;
                statement.ref();
                const _guard_statement = DerefGuard<MySQLStatement>{ ptr: statement };
                if (!statement.execution_flags.header_received) {
                    if (pkt_type == .OK) {
                        ok.decode(reader)?;
                        const _guard_ok = DeinitGuard<OKPacket>{ ptr: &ok };
                        _.handleResultSetOK(request, statement, ok.status_flags, ok.last_insert_id, ok.affected_rows);
                        return;
                    }

                    let header = ResultSetHeader{};
                    header.decode(reader)?;
                    if (header.field_count == 0) {
                        return error.UnexpectedPacket;
                    }
                    if (header.field_count > 4096) {
                        return error.UnexpectedPacket;
                    }
                    if (statement.columns.len != header.field_count) {
                        debug("header field count mismatch: {d} != {d}", .{ statement.columns.len, header.field_count });
                        statement.cached_structure.deinit();
                        statement.cached_structure = CachedStructure{};
                        if (statement.columns.len > 0) {
                            let col_i: usize = 0;
                            while (col_i < statement.columns.len) {
                                statement.columns[col_i].deinit();
                                col_i += 1;
                            }
                            bun.default_allocator.free(statement.columns);
                            statement.columns = empty_columns[0..0];
                        }
                        statement.columns = bun.default_allocator.alloc(ColumnDefinition41, header.field_count)?;
                        let col_i: usize = 0;
                        while (col_i < statement.columns.len) {
                            statement.columns[col_i] = ColumnDefinition41{};
                            col_i += 1;
                        }
                        statement.columns_received = 0;
                    }
                    statement.execution_flags.needs_duplicate_check = true;
                    statement.execution_flags.header_received = true;
                    return;
                } else if (statement.columns_received < statement.columns.len) {
                    statement.columns[statement.columns_received].decode(reader)?;
                    statement.columns_received += 1;
                } else {
                    if (pkt_type == .EOF && header_length < 0xFFFFFF) {
                        if (!_.@"#capabilities".CLIENT_DEPRECATE_EOF) {
                            if (!statement.execution_flags.columns_eof_received) {
                                let eof = EOFPacket{};
                                eof.decode(reader)?;
                                statement.execution_flags.columns_eof_received = true;
                                return;
                            }
                            let eof = EOFPacket{};
                            eof.decode(reader)?;
                            _.handleResultSetOK(request, statement, eof.status_flags, 0, 0);
                            return;
                        }

                        ok.decode(reader)?;
                        const _guard_ok = DeinitGuard<OKPacket>{ ptr: &ok };

                        _.handleResultSetOK(request, statement, ok.status_flags, ok.last_insert_id, ok.affected_rows);
                        return;
                    }

                    const connection = _.getJSConnection();
                    connection.onResultRow(request, statement, Context, reader)?;
                }
            },
        }
    }
}
