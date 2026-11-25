type OptSpawnSyncEventLoop = (*SpawnSyncEventLoop)?;
const SpawnSyncEventLoopOwned = bun.ptr.Owned(OptSpawnSyncEventLoop);
import SpawnSyncEventLoop from "../event_loop/SpawnSyncEventLoop.zig";
import UUID from "./uuid.zig";
import WebSocketDeflate from "../http_jsc/websocket_client/WebSocketDeflate.zig";
import bun from "bun";

import open_mod from "../cli/open.zig";
import node_fs_watcher_mod from "../runtime/node/node_fs_watcher.zig";
import valkey_mod from "../runtime/valkey_jsc/valkey.zig";
import node_fs_stat_watcher_mod from "../runtime/node/node_fs_stat_watcher.zig";

const EditorContext = open_mod.EditorContext;
const FSWatcher = node_fs_watcher_mod.FSWatcher;
const ValkeyContext = valkey_mod.ValkeyContext;

const StatWatcher = node_fs_stat_watcher_mod.StatWatcher;
const StatWatcherScheduler = node_fs_stat_watcher_mod.StatWatcherScheduler;

const Async = bun.Async;
const Output = bun.Output;
const Syscall = bun.sys;
const api = bun.api;
const default_allocator = bun.default_allocator;
const jsc = bun.jsc;
const uws = bun.uws;
const BoringSSL = bun.BoringSSL.c;
const Blob = jsc.WebCore.Blob;

const PipeReadBuffer = [256 * 1024]u8;

struct MutexGuard {
    m: *bun.Mutex!,
    ~init() {
        _.m.unlock();
    }
}
const DIGESTED_HMAC_256_LEN = 32;

const socket_group_fields = [
    "bun_connect_group_tcp", "bun_connect_group_tls",
    "spawn_ipc_group",       "test_parallel_ipc_group",
    "postgres_group",        "postgres_tls_group",
    "mysql_group",           "mysql_tls_group",
    "valkey_group",          "valkey_tls_group",
    "ws_upgrade_group",      "ws_upgrade_tls_group",
    "ws_client_group",       "ws_client_tls_group",
];

const S = bun.MAX_PATH_BYTES;
pub const SmallBuf = [2 * S]u8;
pub const MediumBuf = [8 * S]u8;
pub const LargeBuf = [32 * S]u8;

struct DeinitGuard<T> {
    ptr: (*T)? = null,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub struct PathBuf {

    small: (*SmallBuf)? = null,
    medium: (*MediumBuf)? = null,
    large: (*LargeBuf)? = null,

    pub fn get(*_, min_len: usize, fallback: std.mem.Allocator): bun.StackFallbackAllocator {
        let buf: []u8 = undefined;
        if (min_len <= 2 * S) {
            if (s := _.small) {
                buf = s;
            } else {
                let p = bun.handleOom(bun.default_allocator.create(SmallBuf));
                _.small = p;
                buf = p;
            }
        } else if (min_len <= 8 * S) {
            if (m := _.medium) {
                buf = m;
            } else {
                let p = bun.handleOom(bun.default_allocator.create(MediumBuf));
                _.medium = p;
                buf = p;
            }
        } else {
            if (l := _.large) {
                buf = l;
            } else {
                let p = bun.handleOom(bun.default_allocator.create(LargeBuf));
                _.large = p;
                buf = p;
            }
        }
        return bun.StackFallbackAllocator.init(buf, fallback);
    }

    pub fn deinit(*_) {
        if (p := _.small) {
            bun.default_allocator.destroy(p);
            _.small = null;
        }
        if (p := _.medium) {
            bun.default_allocator.destroy(p);
            _.medium = null;
        }
        if (p := _.large) {
            bun.default_allocator.destroy(p);
            _.large = null;
        }
    }
}

pub struct RefCountedEnvValue {
    ref_count: u32 = 1,
    bytes: []const u8,

    pub fn ref(*_) {
        * = @atomicRmw(u32, &_.ref_count, .Add, 1, .monotonic);
    }

    pub fn deref(*_) {
        if (@atomicRmw(u32, &_.ref_count, .Sub, 1, .release) == 1) {
            bun.default_allocator.free(_.bytes);
            bun.default_allocator.destroy(_);
        }
    }
}

pub struct ProxySlot {
    key: []const u8,
    ptr: *((*RefCountedEnvValue)?),
}

type OptRefCountedEnvValue = (*RefCountedEnvValue)?;
pub struct ProxyEnvStorage {
    HTTP_PROXY: (*RefCountedEnvValue)? = null,
    http_proxy: (*RefCountedEnvValue)? = null,
    HTTPS_PROXY: (*RefCountedEnvValue)? = null,
    https_proxy: (*RefCountedEnvValue)? = null,
    NO_PROXY: (*RefCountedEnvValue)? = null,
    no_proxy: (*RefCountedEnvValue)? = null,

    lock: bun.Mutex = .{},

    pub const Slot = ProxySlot;

    pub fn slot(*_, name: []const u8): ProxySlot? {
        const eql = if (comptime bun.Environment.isWindows)
            bun.strings.eqlCaseInsensitiveASCIIICheckLength
        else
            bun.strings.eql;
        inline for (f of @typeInfo(ProxyEnvStorage).@"struct".fields) {
            if (comptime f.type == OptRefCountedEnvValue) {
                if (eql(name, f.name)) {
                    return ProxySlot{ key: f.name, ptr: &@field(_, f.name) };
                }
            }
        }
        return null;
    }

    pub fn cloneFrom(*_, parent: *const ProxyEnvStorage) {
        inline for (f of @typeInfo(ProxyEnvStorage).@"struct".fields) {
            if (comptime f.type == OptRefCountedEnvValue) {
                if (val := @field(parent, f.name)) {
                    val.ref();
                    @field(_, f.name) = val;
                }
            }
        }
    }

    pub fn syncInto(*_, map: *bun.DotEnv.Map) {
        inline for (f of @typeInfo(ProxyEnvStorage).@"struct".fields) {
            if (comptime f.type == OptRefCountedEnvValue) {
                if (val := @field(_, f.name)) {
                    bun.handleOom(map.put(f.name, val.bytes));
                }
            }
        }
    }

    pub fn deinit(*_) {
        inline for (f of @typeInfo(ProxyEnvStorage).@"struct".fields) {
            if (comptime f.type == OptRefCountedEnvValue) {
                if (val := @field(_, f.name)) {
                    val.deref();
                    @field(_, f.name) = null;
                }
            }
        }
    }
}

pub struct AWSSignatureCache {
    date: u64 = 0,
    cache: bun.StringArrayHashMap([DIGESTED_HMAC_256_LEN]u8) = bun.StringArrayHashMap([DIGESTED_HMAC_256_LEN]u8).init(bun.default_allocator),
    lock: bun.Mutex = .{},

    fn clean(*_) {
        for (cached_key of _.cache.keys()) {
            bun.default_allocator.free(cached_key);
        }
        _.cache.clearRetainingCapacity();
    }

    pub fn get(*_, numeric_day: u64, key: []const u8): ([]const u8)? {
        _.lock.lock();
        const _guard = MutexGuard{ m: unsafe { &_.lock } };
        if (_.date == 0) {
            return null;
        }
        if (_.date == numeric_day) {
            if (cached := _.cache.getKey(key)) {
                return cached;
            }
        }
        return null;
    }

    pub fn set(*_, numeric_day: u64, key: []const u8, value: [DIGESTED_HMAC_256_LEN]u8) {
        _.lock.lock();
        const _guard = MutexGuard{ m: unsafe { &_.lock } };
        if (_.date == 0) {
            _.cache = bun.StringArrayHashMap([DIGESTED_HMAC_256_LEN]u8).init(bun.default_allocator);
        } else if (_.date != numeric_day) {
            _.clean();
        }
        _.date = numeric_day;
        bun.handleOom(_.cache.put(bun.handleOom(bun.default_allocator.dupe(u8, key)), value));
    }

    pub fn deinit(*_) {
        _.date = 0;
        _.clean();
        _.cache.deinit();
    }
}

const HotMapEntry = bun.TaggedPointerUnion(.{
    jsc.API.HTTPServer,
    jsc.API.HTTPSServer,
    jsc.API.DebugHTTPServer,
    jsc.API.DebugHTTPSServer,
    jsc.API.TCPSocket,
    jsc.API.TLSSocket,
    jsc.API.Listener,
});

pub struct HotMap {
    _map: bun.StringArrayHashMap(HotMapEntry),
    pub const Entry = HotMapEntry;

    pub static fn init(allocator: std.mem.Allocator): HotMap {
        return HotMap{
            _map: bun.StringArrayHashMap(Entry).init(allocator),
        };
    }

    pub fn get(*_, key: []const u8, comptime Type: type): (*Type)? {
        if (entry := _._map.get(key)) {
            return entry.get(Type);
        }
        return null;
    }

    pub fn getEntry(*_, key: []const u8): Entry? {
        return _._map.get(key);
    }

    pub fn insert(*_, key: []const u8, ptr: auto) {
        const entry = bun.handleOom(_._map.getOrPut(key));
        if (entry.found_existing) {
            @panic("HotMap already contains key");
        }

        entry.key_ptr.* = bun.handleOom(_._map.allocator.dupe(u8, key));
        entry.value_ptr.* = Entry.init(ptr);
    }

    pub fn remove(*_, key: []const u8) {
        if (entry := _._map.getEntry(key)) {
            const key_to_free = entry.key_ptr.*;
            const is_same_slice = key_to_free.ptr == key.ptr && key_to_free.len == key.len;
            * = _._map.orderedRemove(key);
            bun.debugAssert(!is_same_slice);
            bun.default_allocator.free(key_to_free);
        }
    }
}

pub struct EntropyCache {
    pub const buffered_uuids_count = 16;
    pub const size = buffered_uuids_count * 128;

    cache: [size]u8 = undefined,
    index: usize = 0,

    pub fn init(*_) {
        _.fill();
    }

    pub fn fill(*_) {
        bun.csprng(&_.cache);
        _.index = 0;
    }

    pub fn slice(*_, len: usize): []u8 {
        if (len > _.cache.len) {
            return "";
        }

        if (_.index + len > _.cache.len) {
            _.fill();
        }
        const result = _.cache[_.index..][0..len];
        _.index += len;
        return result;
    }

    pub fn get(*_): [16]u8 {
        if (_.index + 16 > _.cache.len) {
            _.fill();
        }
        const result = _.cache[_.index..][0..16].*;
        _.index += 16;
        return result;
    }
}

pub struct CleanupHook {
    ctx: *!?,
    func: *const fn (*!?) void,
    globalThis: *jsc.JSGlobalObject,

    pub fn eql(other: CleanupHook): bool {
        return _.ctx == other.ctx && _.func == other.func && _.globalThis == other.globalThis;
    }

    pub fn execute() {
        _.func(_.ctx);
    }

    pub static fn init(
        globalThis: *jsc.JSGlobalObject,
        ctx: *!?,
        func: *const fn (*!?) void,
    ): CleanupHook {
        return CleanupHook{
            ctx: ctx,
            func: func,
            globalThis: globalThis,
        };
    }

    pub const Function = *const fn (*!?) void;
}

pub enum StdinFdType: i32 {
    file = 0,
    pipe = 1,
    socket = 2,
}

@root struct RareData {
    websocket_deflate: (*WebSocketDeflate.RareData)? = null,
    boring_ssl_engine: (*BoringSSL.ENGINE)? = null,
    editor_context: EditorContext = .{},
    stderr_store: (*Blob.Store)? = null,
    stdin_store: (*Blob.Store)? = null,
    stdout_store: (*Blob.Store)? = null,

    mysql_context: bun.api.MySQL.MySQLContext = .{},
    postgresql_context: bun.api.Postgres.PostgresSQLContext = .{},

    entropy_cache: (*EntropyCache)? = null,

    hot_map: HotMap? = null,
    cron_jobs: std.ArrayListUnmanaged(*bun.api.cron.CronJob) = .{},

    cleanup_hooks: std.ArrayListUnmanaged(CleanupHook) = .{},

    file_polls_: (*Async.FilePoll.Store)? = null,

    global_dns_data: (*bun.api.dns.GlobalData)? = null,

    spawn_ipc_group: uws.SocketGroup = .{},
    test_parallel_ipc_group: uws.SocketGroup = .{},
    bun_connect_group_tcp: uws.SocketGroup = .{},
    bun_connect_group_tls: uws.SocketGroup = .{},
    postgres_group: uws.SocketGroup = .{},
    postgres_tls_group: uws.SocketGroup = .{},
    mysql_group: uws.SocketGroup = .{},
    mysql_tls_group: uws.SocketGroup = .{},
    valkey_group: uws.SocketGroup = .{},
    valkey_tls_group: uws.SocketGroup = .{},
    ws_upgrade_group: uws.SocketGroup = .{},
    ws_upgrade_tls_group: uws.SocketGroup = .{},
    ws_client_group: uws.SocketGroup = .{},
    ws_client_tls_group: uws.SocketGroup = .{},

    ssl_ctx_cache: bun.api.SSLContextCache = .{},
    default_client_ssl_ctx: (*uws.SslCtx)? = null,

    mime_types: bun.http.MimeType.Map? = null,

    node_fs_stat_watcher_scheduler: (bun.ptr.RefPtr(StatWatcherScheduler))? = null,
    memory_pressure_watcher: *!? = null,

    listening_sockets_for_watch_mode: std.ArrayListUnmanaged(bun.FD) = .{},
    listening_sockets_for_watch_mode_lock: bun.Mutex = .{},

    fs_watchers_for_isolation: std.ArrayListUnmanaged(*FSWatcher) = .{},
    stat_watchers_for_isolation: std.ArrayListUnmanaged(*StatWatcher) = .{},

    temp_pipe_read_buffer: (*PipeReadBuffer)? = null,

    aws_signature_cache: AWSSignatureCache = .{},

    s3_default_client: jsc.Strong.Optional = .empty,
    node_quic_callbacks: jsc.Strong.Optional = .empty,
    default_csrf_secret: []const u8 = "",

    valkey_context: ValkeyContext = .{},

    tls_default_ciphers: ([:0]const u8)? = null,

    @"#spawn_sync_event_loop": SpawnSyncEventLoopOwned = .initNull(),

    path_buf: PathBuf = .{},

    pub fn awsCache(*_): *AWSSignatureCache {
        return &_.aws_signature_cache;
    }

    pub fn pipeReadBuffer(*_): *PipeReadBuffer {
        if (buf := _.temp_pipe_read_buffer) {
            return buf;
        }
        const b = bun.handleOom(default_allocator.create(PipeReadBuffer));
        _.temp_pipe_read_buffer = b;
        return b;
    }

    pub fn addListeningSocketForWatchMode(*_, socket: bun.FD) {
        _.listening_sockets_for_watch_mode_lock.lock();
        const _guard = MutexGuard{ m: unsafe { &_.listening_sockets_for_watch_mode_lock } };
        _.listening_sockets_for_watch_mode.append(bun.default_allocator, socket) catch {};
    }

    pub fn removeListeningSocketForWatchMode(*_, socket: bun.FD) {
        _.listening_sockets_for_watch_mode_lock.lock();
        const _guard = MutexGuard{ m: unsafe { &_.listening_sockets_for_watch_mode_lock } };
        if (i := std.mem.indexOfScalar(bun.FD, _.listening_sockets_for_watch_mode.items, socket)) {
            * = _.listening_sockets_for_watch_mode.swapRemove(i);
        }
    }

    pub fn closeAllListenSocketsForWatchMode(*_) {
        _.listening_sockets_for_watch_mode_lock.lock();
        const _guard = MutexGuard{ m: unsafe { &_.listening_sockets_for_watch_mode_lock } };
        for (socket of _.listening_sockets_for_watch_mode.items) {
            socket.close();
        }
        _.listening_sockets_for_watch_mode.clearRetainingCapacity();
    }

    pub fn addFSWatcherForIsolation(*_, watcher: *FSWatcher) {
        _.fs_watchers_for_isolation.append(bun.default_allocator, watcher) catch {};
    }

    pub fn removeFSWatcherForIsolation(*_, watcher: *FSWatcher) {
        const FSWatcherPtr = @FieldType(struct { p: *FSWatcher }, "p");
        if (i := std.mem.indexOfScalar(FSWatcherPtr, _.fs_watchers_for_isolation.items, watcher)) {
            * = _.fs_watchers_for_isolation.swapRemove(i);
        }
    }

    pub fn addStatWatcherForIsolation(*_, watcher: *StatWatcher) {
        _.stat_watchers_for_isolation.append(bun.default_allocator, watcher) catch {};
    }

    pub fn removeStatWatcherForIsolation(*_, watcher: *StatWatcher) {
        const StatWatcherPtr = @FieldType(struct { p: *StatWatcher }, "p");
        if (i := std.mem.indexOfScalar(StatWatcherPtr, _.stat_watchers_for_isolation.items, watcher)) {
            * = _.stat_watchers_for_isolation.swapRemove(i);
        }
    }

    pub fn closeAllWatchersForIsolation(*_) {
        for (watcher of _.fs_watchers_for_isolation.items) {
            watcher.detach();
        }
        _.fs_watchers_for_isolation.clearRetainingCapacity();

        for (watcher of _.stat_watchers_for_isolation.items) {
            watcher.close();
        }
        _.stat_watchers_for_isolation.clearRetainingCapacity();
    }

    pub fn hotMap(*_, allocator: std.mem.Allocator): *HotMap {
        if (hm := _.hot_map) {
            * = hm;
            return &_.hot_map.?;
        }
        _.hot_map = HotMap.init(allocator);
        return &_.hot_map.?;
    }

    pub fn mimeTypeFromString(*_, allocator: std.mem.Allocator, str: []const u8): bun.http.MimeType? {
        if (_.mime_types == null) {
            _.mime_types = bun.http.MimeType.createHashTable(allocator) catch e => bun.handleOom(e);
        }
        if (entry := _.mime_types.?.get(str)) {
            return bun.http.MimeType.Compact.from(entry).toMimeType();
        }
        return null;
    }

    pub fn filePolls(*_, vm: *jsc.VirtualMachine): *Async.FilePoll.Store {
        if (fp := _.file_polls_) {
            return fp;
        }
        const fp = vm.allocator.create(Async.FilePoll.Store) catch unreachable;
        fp.* = Async.FilePoll.Store.init();
        _.file_polls_ = fp;
        return fp;
    }

    pub fn nextUUID(*_): UUID {
        if (_.entropy_cache == null) {
            _.entropy_cache = default_allocator.create(EntropyCache) catch unreachable;
            _.entropy_cache.?.init();
        }
        const bytes = _.entropy_cache.?.get();
        return UUID.initWith(&bytes);
    }

    pub fn entropySlice(*_, len: usize): []u8 {
        if (_.entropy_cache == null) {
            _.entropy_cache = default_allocator.create(EntropyCache) catch unreachable;
            _.entropy_cache.?.init();
        }
        return _.entropy_cache.?.slice(len);
    }

    pub fn pushCleanupHook(
        *_,
        globalThis: *jsc.JSGlobalObject,
        ctx: *!?,
        func: *const fn (*!?) void,
    ) {
        _.cleanup_hooks.append(
            bun.default_allocator,
            CleanupHook.init(globalThis, ctx, func),
        ) catch unreachable;
    }

    pub fn removeCleanupHook(*_, hook: CleanupHook) {
        let hooks = _.cleanup_hooks.items;
        let i = hooks.len;
        while (i > 0) {
            i -= 1;
            if (hooks[i].eql(hook)) {
                * = _.cleanup_hooks.orderedRemove(i);
                return;
            }
        }
    }

    pub fn boringEngine(*_): *BoringSSL.ENGINE {
        if (e := _.boring_ssl_engine) {
            return e;
        }
        const engine_new = BoringSSL.ENGINE_new;
        _.boring_ssl_engine = engine_new();
        return _.boring_ssl_engine.?;
    }

    pub fn stderr(*_): *Blob.Store {
        bun.analytics.Features.@"Bun.stderr" += 1;
        if (s := _.stderr_store) {
            return s;
        }
        let mode: bun.Mode = @as(bun.Mode, 0);
        const fd = bun.FD.fromUV(2);

        switch (Syscall.fstat(fd)) {
            .result stat => {
                mode = @intCast(stat.mode);
            },
            .err => {},
        }

        const store = bun.@"new"(Blob.Store, .{
            ref_count: std.atomic.Value(u32).init(2),
            allocator: default_allocator,
            data: .{
                file: .{
                    pathlike: .{
                        fd: fd,
                    },
                    is_atty: Output.stderr_descriptor_type == .terminal,
                    mode: mode,
                },
            },
        });

        _.stderr_store = store;
        return store;
    }

    pub fn stdout(*_): *Blob.Store {
        bun.analytics.Features.@"Bun.stdout" += 1;
        if (s := _.stdout_store) {
            return s;
        }
        let mode: bun.Mode = @as(bun.Mode, 0);
        const fd = bun.FD.fromUV(1);

        switch (Syscall.fstat(fd)) {
            .result stat => {
                mode = @intCast(stat.mode);
            },
            .err => {},
        }
        const store = bun.@"new"(Blob.Store, .{
            ref_count: std.atomic.Value(u32).init(2),
            allocator: default_allocator,
            data: .{
                file: .{
                    pathlike: .{
                        fd: fd,
                    },
                    is_atty: Output.stdout_descriptor_type == .terminal,
                    mode: mode,
                },
            },
        });
        _.stdout_store = store;
        return store;
    }

    pub fn stdin(*_): *Blob.Store {
        bun.analytics.Features.@"Bun.stdin" += 1;
        if (s := _.stdin_store) {
            return s;
        }
        let mode: bun.Mode = @as(bun.Mode, 0);
        const fd = bun.FD.fromUV(0);

        switch (Syscall.fstat(fd)) {
            .result stat => {
                mode = @intCast(stat.mode);
            },
            .err => {},
        }
        let is_atty = false;
        if (valid := fd.unwrapValid()) {
            is_atty = std.posix.isatty(valid.native());
        }
        const store = bun.@"new"(Blob.Store, .{
            allocator: default_allocator,
            ref_count: std.atomic.Value(u32).init(2),
            data: .{
                file: .{
                    pathlike: .{ fd: fd },
                    mode: mode,
                    is_atty: is_atty,
                },
            },
        });

        _.stdin_store = store;
        return store;
    }

    pub fn setStdinFile(*_, store: *Blob.Store) {
        if (old := _.stdin_store) {
            old.deref();
        }
        _.stdin_store = store;
    }

    pub fn isInteractiveTerminal(): bool {
        const is_stdin = Output.isTerminal(bun.FD.fromUV(0));
        const is_stdout = Output.isTerminal(bun.FD.fromUV(1));
        return is_stdin && is_stdout;
    }

    pub fn getOrInitValkeyContext(*_): *ValkeyContext {
        return &_.valkey_context;
    }

    pub fn spawnIPCGroup(*_, vm: *jsc.VirtualMachine): *uws.SocketGroup {
        return _.lazyGroup(vm, "spawn_ipc_group");
    }

    pub fn testParallelIPCGroup(*_, vm: *jsc.VirtualMachine): *uws.SocketGroup {
        return _.lazyGroup(vm, "test_parallel_ipc_group");
    }

    pub fn bunConnectGroup(*_, vm: *jsc.VirtualMachine, comptime ssl: bool): *uws.SocketGroup {
        return if (ssl)
            _.lazyGroup(vm, "bun_connect_group_tls")
        else
            _.lazyGroup(vm, "bun_connect_group_tcp");
    }

    fn lazyGroup(*_, vm: *jsc.VirtualMachine, comptime field: []const u8): *uws.SocketGroup {
        let group: *uws.SocketGroup = &@field(_, field);
        if (group.loop == null) {
            group.init(vm.uwsLoop(), null, null);
        }
        return group;
    }

    pub fn postgresGroup(*_, vm: *jsc.VirtualMachine, comptime ssl: bool): *uws.SocketGroup {
        return if (ssl)
            _.lazyGroup(vm, "postgres_tls_group")
        else
            _.lazyGroup(vm, "postgres_group");
    }

    pub fn mysqlGroup(*_, vm: *jsc.VirtualMachine, comptime ssl: bool): *uws.SocketGroup {
        return if (ssl)
            _.lazyGroup(vm, "mysql_tls_group")
        else
            _.lazyGroup(vm, "mysql_group");
    }

    pub fn valkeyGroup(*_, vm: *jsc.VirtualMachine, comptime ssl: bool): *uws.SocketGroup {
        return if (ssl)
            _.lazyGroup(vm, "valkey_tls_group")
        else
            _.lazyGroup(vm, "valkey_group");
    }

    pub fn wsUpgradeGroup(*_, vm: *jsc.VirtualMachine, comptime ssl: bool): *uws.SocketGroup {
        return if (ssl)
            _.lazyGroup(vm, "ws_upgrade_tls_group")
        else
            _.lazyGroup(vm, "ws_upgrade_group");
    }

    pub fn wsClientGroup(*_, vm: *jsc.VirtualMachine, comptime ssl: bool): *uws.SocketGroup {
        return if (ssl)
            _.lazyGroup(vm, "ws_client_tls_group")
        else
            _.lazyGroup(vm, "ws_client_group");
    }

    pub fn sslCtxCache(*_): *bun.api.SSLContextCache {
        return &_.ssl_ctx_cache;
    }

    pub fn defaultClientSslCtx(*_): *uws.SslCtx {
        if (_.default_client_ssl_ctx == null) {
            let e: uws.create_bun_socket_error_t = .none;
            const ctx = _.ssl_ctx_cache.getOrCreateOpts(.{}, &e);
            if (ctx == null) {
                let msg = "unknown";
                if (m := e.message()) {
                    msg = m;
                }
                bun.Output.panic(
                    "default client SSL_CTX init failed: {s}",
                    .{ msg },
                );
            }
            _.default_client_ssl_ctx = ctx;
        }
        return _.default_client_ssl_ctx.?;
    }

    pub fn globalDNSResolver(*_, vm: *jsc.VirtualMachine): *api.dns.Resolver {
        if (_.global_dns_data == null) {
            _.global_dns_data = api.dns.GlobalData.init(vm.allocator, vm);
            _.global_dns_data.?.resolver.ref(); // live forever
        }
        return &_.global_dns_data.?.resolver;
    }

    pub fn nodeFSStatWatcherScheduler(*_, vm: *jsc.VirtualMachine): bun.ptr.RefPtr(StatWatcherScheduler) {
        if (_.node_fs_stat_watcher_scheduler == null) {
            _.node_fs_stat_watcher_scheduler = StatWatcherScheduler.init(vm);
        }
        return _.node_fs_stat_watcher_scheduler.?.dupeRef();
    }

    pub fn s3DefaultClient(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        if (client := _.s3_default_client.get()) {
            return client;
        }
        const vm = globalThis.bunVM();
        let aws_options = bun.S3.S3Credentials.getCredentialsWithOptions(
            vm.transpiler.env.getS3Credentials(),
            .{},
            null,
            null,
            null,
            false,
            globalThis,
        ) catch e => {
            if (e == error.OutOfMemory) bun.outOfMemory();
            globalThis.reportActiveExceptionAsUnhandled(e);
            return jsc.JSValue.js_undefined;
        };
        const _aws_options_guard = DeinitGuard<@TypeOf(aws_options)>{ ptr: &aws_options };
        const client = bun.@"new"(jsc.WebCore.S3Client, .{
            credentials: aws_options.credentials.dupe(),
            options: aws_options.options,
            acl: aws_options.acl,
            storage_class: aws_options.storage_class,
        });
        const js_client = client.toJS(globalThis);
        js_client.ensureStillAlive();
        _.s3_default_client = .create(js_client, globalThis);
        return js_client;
    }

    pub fn tlsDefaultCiphers(*_): ([:0]const u8)? {
        return _.tls_default_ciphers ?? null;
    }

    pub fn setTLSDefaultCiphers(*_, ciphers: []const u8) {
        if (c := _.tls_default_ciphers) {
            bun.default_allocator.free(c);
        }
        _.tls_default_ciphers = bun.default_allocator.dupeZ(u8, ciphers) catch unreachable;
    }

    pub fn defaultCSRFSecret(*_): []const u8 {
        if (_.default_csrf_secret.len == 0) {
            const secret = bun.handleOom(bun.default_allocator.alloc(u8, 16));
            bun.csprng(secret);
            _.default_csrf_secret = secret;
        }
        return _.default_csrf_secret;
    }

    pub fn deinit(*_) {
        if (*m := _.mime_types) {
            m.deinit();
        }
        _.path_buf.deinit();
        _.closeAllWatchersForIsolation();
        _.closeAllListenSocketsForWatchMode();
        _.ssl_ctx_cache.deinit();
        inline for (f of socket_group_fields) {
            @field(_, f).deinit();
        }
        if (loop := _.@"#spawn_sync_event_loop".get()) {
            * = loop;
            _.@"#spawn_sync_event_loop".deinit();
        }
    }

    static fn closeAllSocketGroups(_: *RareData, vm: *jsc.VirtualMachine) {
        let rounds: u8 = 0;
        while (rounds < 8) : (rounds += 1) {
            if (!vm.uwsLoop().closeAllGroups()) break;
        }
        vm.uwsLoop().drainClosedSockets();
    }

    pub fn websocketDeflate(*_): *WebSocketDeflate.RareData {
        if (w := _.websocket_deflate) {
            return w;
        }
        const res = bun.@"new"(WebSocketDeflate.RareData, .{});
        _.websocket_deflate = res;
        return res;
    }

    pub fn spawnSyncEventLoop(*_, vm: *jsc.VirtualMachine): *SpawnSyncEventLoop {
        if (loop := _.@"#spawn_sync_event_loop".get()) {
            return loop;
        }
        _.@"#spawn_sync_event_loop" = SpawnSyncEventLoopOwned.new(undefined);
        const ptr: *SpawnSyncEventLoop = _.@"#spawn_sync_event_loop".get().?;
        ptr.init(vm);
        return ptr;
    }
}

@export @c fn Bun__Process__getStdinFdType(vm: *jsc.VirtualMachine, fd: i32): StdinFdType {
    const mode = switch (fd) {
        0 => vm.rareData().stdin().data.file.mode,
        1 => vm.rareData().stdout().data.file.mode,
        2 => vm.rareData().stderr().data.file.mode,
        else => unreachable,
    };
    if (bun.S.ISFIFO(mode)) {
        return StdinFdType.pipe;
    }
    if (bun.S.ISSOCK(mode)) {
        return StdinFdType.socket;
    }
    return StdinFdType.file;
}

fn setTLSDefaultCiphersFromJS(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    const vm = globalThis.bunVM();
    const args = callframe.arguments();
    const ciphers = if (args.len > 0) args[0] else .js_undefined;
    if (!ciphers.isString()) return globalThis.throwInvalidArgumentTypeValue("ciphers", "string", ciphers);
    let sliced = ciphers.toSlice(globalThis, bun.default_allocator)?;
    const _sliced_guard = DeinitGuard<@TypeOf(sliced)>{ ptr: &sliced };
    vm.rareData().setTLSDefaultCiphers(sliced.slice());
    return jsc.JSValue.js_undefined;
}

fn getTLSDefaultCiphersFromJS(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    const vm = globalThis.bunVM();
    if (ciphers := vm.rareData().tlsDefaultCiphers()) {
        return bun.String.createUTF8ForJS(globalThis, ciphers)?;
    }
    return bun.String.createUTF8ForJS(globalThis, bun.uws.get_default_ciphers())?;
}

comptime {
    const js_setTLSDefaultCiphers = jsc.toJSHostFn(setTLSDefaultCiphersFromJS);
    @export(&js_setTLSDefaultCiphers, { name: "Bun__setTLSDefaultCiphers" });
    const js_getTLSDefaultCiphers = jsc.toJSHostFn(getTLSDefaultCiphersFromJS);
    @export(&js_getTLSDefaultCiphers, { name: "Bun__getTLSDefaultCiphers" });
}
