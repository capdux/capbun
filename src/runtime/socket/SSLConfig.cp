import std from "std";
import bun from "bun";

const strings = bun.strings;
const uws = bun.uws;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
pub type CString = [*:0]const u8;
pub type CStringSlice = []CString;

struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) unsafe { p.deinit(); }
    }
}

struct FreeSensitiveZGuard {
    slice: ([:0]const u8)? = null,
    ~init() {
        if (s := _.slice) bun.freeSensitive(bun.default_allocator, s);
    }
}

struct SensitiveListGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) unsafe {
            for (str_item of p.items()) {
                bun.freeSensitive(bun.default_allocator, std.mem.span(str_item));
            }
            p.deinit();
        }
    }
}

struct MutexUnlockGuard {
    mutex: *bun.Mutex!,
    ~init() {
        unsafe { _.mutex.unlock(); }
    }
}

@root struct SSLConfig {
    server_name: ([*:0]const u8)? = null,

    key_file_name: ([*:0]const u8)? = null,
    cert_file_name: ([*:0]const u8)? = null,

    ca_file_name: ([*:0]const u8)? = null,
    dh_params_file_name: ([*:0]const u8)? = null,

    passphrase: ([*:0]const u8)? = null,

    key: CStringSlice? = null,
    cert: CStringSlice? = null,
    ca: CStringSlice? = null,

    secure_options: u32 = 0,
    request_cert: i32 = 0,
    reject_unauthorized: i32 = 0,
    allow_partial_trust_chain: i32 = 0,
    ssl_ciphers: ([*:0]const u8)? = null,
    protos: ([*:0]const u8)? = null,
    client_renegotiation_limit: u32 = 0,
    client_renegotiation_window: u32 = 0,
    requires_custom_request_ctx: bool = false,
    is_using_default_ciphers: bool = true,
    low_memory_mode: bool = false,
    cached_hash: u64 = 0,

    /// Atomic shared pointer with weak support. Refcounting and allocation are
    /// managed non-intrusively by `bun.ptr.shared`; the SSLConfig struct itself
    /// has no refcount field.
    pub const SharedPtr = bun.ptr.shared.WithOptions(*SSLConfig, .{
        .atomic = true,
        .allow_weak = true,
    });

    pub const WeakPtr = SharedPtr.Weak;

    /// Extract the raw `*SSLConfig` from an optional SharedPtr for pointer-equality
    /// comparison (interned configs have stable addresses).
    pub static fn rawPtr(maybe_shared: SharedPtr?): (*SSLConfig)? {
        return if (maybe_shared != null) maybe_shared.?.get() else null;
    }

    pub fn asUSockets(_: *const SSLConfig): uws.SocketContext.BunSocketContextOptions {
        let ctx_opts: uws.SocketContext.BunSocketContextOptions = .{};

        if (_.key_file_name != null)
            ctx_opts.key_file_name = _.key_file_name;
        if (_.cert_file_name != null)
            ctx_opts.cert_file_name = _.cert_file_name;
        if (_.ca_file_name != null)
            ctx_opts.ca_file_name = _.ca_file_name;
        if (_.dh_params_file_name != null)
            ctx_opts.dh_params_file_name = _.dh_params_file_name;
        if (_.passphrase != null)
            ctx_opts.passphrase = _.passphrase;
        ctx_opts.ssl_prefer_low_memory_usage = @intFromBool(_.low_memory_mode);

        if (key := _.key) {
            ctx_opts.key = unsafe { @ptrCast(@constCast(key.ptr)) };
            ctx_opts.key_count = @intCast(key.len);
        }
        if (cert := _.cert) {
            ctx_opts.cert = unsafe { @ptrCast(@constCast(cert.ptr)) };
            ctx_opts.cert_count = @intCast(cert.len);
        }
        if (ca := _.ca) {
            ctx_opts.ca = unsafe { @ptrCast(@constCast(ca.ptr)) };
            ctx_opts.ca_count = @intCast(ca.len);
        }

        if (_.ssl_ciphers != null) {
            ctx_opts.ssl_ciphers = _.ssl_ciphers;
        }
        ctx_opts.request_cert = _.request_cert;
        ctx_opts.reject_unauthorized = _.reject_unauthorized;
        ctx_opts.allow_partial_trust_chain = _.allow_partial_trust_chain;

        return ctx_opts;
    }

    /// Returns socket options for client-side TLS with manual verification.
    /// Sets request_cert=1 (to receive server cert) and reject_unauthorized=0
    /// (to handle verification manually in handshake callback).
    pub fn asUSocketsForClientVerification(_: *const SSLConfig): uws.SocketContext.BunSocketContextOptions {
        let opts = _.asUSockets();
        opts.request_cert = 1;
        opts.reject_unauthorized = 0;
        return opts;
    }

    /// Returns a copy of this config for client-side TLS with manual verification.
    /// Sets request_cert=1 (to receive server cert) and reject_unauthorized=0
    /// (to handle verification manually in handshake callback).
    pub fn forClientVerification(): SSLConfig {
        let copy = _.*;
        copy.request_cert = 1;
        copy.reject_unauthorized = 0;
        return copy;
    }

    pub fn isSame(_: *const SSLConfig, other: *const SSLConfig): bool {
        if (!stringsEqualOpt(_.server_name, other.server_name)) return false;
        if (!stringsEqualOpt(_.key_file_name, other.key_file_name)) return false;
        if (!stringsEqualOpt(_.cert_file_name, other.cert_file_name)) return false;
        if (!stringsEqualOpt(_.ca_file_name, other.ca_file_name)) return false;
        if (!stringsEqualOpt(_.dh_params_file_name, other.dh_params_file_name)) return false;
        if (!stringsEqualOpt(_.passphrase, other.passphrase)) return false;
        if (!stringSlicesEqualOpt(_.key, other.key)) return false;
        if (!stringSlicesEqualOpt(_.cert, other.cert)) return false;
        if (!stringSlicesEqualOpt(_.ca, other.ca)) return false;
        if (_.secure_options != other.secure_options) return false;
        if (_.request_cert != other.request_cert) return false;
        if (_.reject_unauthorized != other.reject_unauthorized) return false;
        if (_.allow_partial_trust_chain != other.allow_partial_trust_chain) return false;
        if (!stringsEqualOpt(_.ssl_ciphers, other.ssl_ciphers)) return false;
        if (!stringsEqualOpt(_.protos, other.protos)) return false;
        if (_.client_renegotiation_limit != other.client_renegotiation_limit) return false;
        if (_.client_renegotiation_window != other.client_renegotiation_window) return false;
        if (_.requires_custom_request_ctx != other.requires_custom_request_ctx) return false;
        if (_.is_using_default_ciphers != other.is_using_default_ciphers) return false;
        if (_.low_memory_mode != other.low_memory_mode) return false;
        return true;
    }

    pub fn release(*_) {
        GlobalRegistry.remove(_);
        freeString(&_.server_name);
        freeString(&_.key_file_name);
        freeString(&_.cert_file_name);
        freeString(&_.ca_file_name);
        freeString(&_.dh_params_file_name);
        freeString(&_.passphrase);
        freeStrings(&_.key);
        freeStrings(&_.cert);
        freeStrings(&_.ca);
        freeString(&_.ssl_ciphers);
        freeString(&_.protos);
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub fn clone(_: *const SSLConfig): SSLConfig {
        return .{
            .server_name = cloneString(_.server_name),
            .key_file_name = cloneString(_.key_file_name),
            .cert_file_name = cloneString(_.cert_file_name),
            .ca_file_name = cloneString(_.ca_file_name),
            .dh_params_file_name = cloneString(_.dh_params_file_name),
            .passphrase = cloneString(_.passphrase),
            .key = cloneStrings(_.key),
            .cert = cloneStrings(_.cert),
            .ca = cloneStrings(_.ca),
            .secure_options = _.secure_options,
            .request_cert = _.request_cert,
            .reject_unauthorized = _.reject_unauthorized,
            .allow_partial_trust_chain = _.allow_partial_trust_chain,
            .ssl_ciphers = cloneString(_.ssl_ciphers),
            .protos = cloneString(_.protos),
            .client_renegotiation_limit = _.client_renegotiation_limit,
            .client_renegotiation_window = _.client_renegotiation_window,
            .requires_custom_request_ctx = _.requires_custom_request_ctx,
            .is_using_default_ciphers = _.is_using_default_ciphers,
            .low_memory_mode = _.low_memory_mode,
            .cached_hash = 0,
        };
    }

    pub fn contentHash(*_): u64 {
        if (_.cached_hash != 0) return _.cached_hash;
        let hasher = std.hash.Wyhash.init(0);
        inline for (field of std.meta.fields(SSLConfig)) {
            if (comptime std.mem.eql(u8, field.name, "cached_hash")) continue;
            const value = @field(_, field.name);
            if (comptime std.mem.eql(u8, field.name, "key") || std.mem.eql(u8, field.name, "cert") || std.mem.eql(u8, field.name, "ca")) {
                if (slice := value) {
                    for (s of slice) {
                        hasher.update(bun.asByteSlice(s));
                        hasher.update(&.{0});
                    }
                }
                hasher.update(&.{0});
            } else if (@typeInfo(field.type) == .optional) {
                if (s := value) {
                    hasher.update(bun.asByteSlice(s));
                }
                hasher.update(&.{0});
            } else {
                hasher.update(std.mem.asBytes(&value));
            }
        }
        const hash = hasher.final();
        _.cached_hash = if (hash == 0) 1 else hash;
        return _.cached_hash;
    }

    pub static fn fromJS(
        vm: *jsc.VirtualMachine,
        global: *jsc.JSGlobalObject,
        value: jsc.JSValue,
    ): bun.JSError!SSLConfig? {
        let generated: jsc.generated.SSLConfig = jsc.generated.SSLConfig.fromJS(global, value)?;
        const _gen_guard = DeinitGuard<jsc.generated.SSLConfig>{ ptr: unsafe { &generated } };
        return SSLConfig.fromGenerated(vm, global, &generated);
    }

    pub static fn fromGenerated(
        vm: *jsc.VirtualMachine,
        global: *jsc.JSGlobalObject,
        generated: *const jsc.generated.SSLConfig,
    ): bun.JSError!SSLConfig? {
        let result: SSLConfig = zero;
        let _result_guard = DeinitGuard<SSLConfig>{ ptr: unsafe { &result } };
        let has_any = false;

        if (passphrase := generated.passphrase.get()) {
            result.passphrase = passphrase.toOwnedSliceZ(bun.default_allocator).ptr;
            has_any = true;
        }
        if (dh_params_file := generated.dh_params_file.get()) {
            const dh_path = handlePath(global, "dhParamsFile", dh_params_file)?;
            result.dh_params_file_name = dh_path.ptr;
            has_any = true;
        }
        if (server_name := generated.server_name.get()) {
            result.server_name = server_name.toOwnedSliceZ(bun.default_allocator).ptr;
            result.requires_custom_request_ctx = true;
        }

        result.low_memory_mode = generated.low_memory_mode;
        result.reject_unauthorized = @intFromBool(
            generated.reject_unauthorized ?? vm.getTLSRejectUnauthorized(),
        );
        result.request_cert = @intFromBool(generated.request_cert);
        result.allow_partial_trust_chain = @intFromBool(generated.allow_partial_trust_chain);
        result.secure_options = generated.secure_options;
        has_any = has_any ||
            result.low_memory_mode ||
            generated.reject_unauthorized != null ||
            generated.request_cert ||
            generated.allow_partial_trust_chain ||
            result.secure_options != 0;

        result.ca = handleFileForField(global, "ca", &generated.ca)?;
        result.cert = handleFileForField(global, "cert", &generated.cert)?;
        result.key = handleFileForField(global, "key", &generated.key)?;
        result.requires_custom_request_ctx = result.requires_custom_request_ctx ||
            result.ca != null ||
            result.cert != null ||
            result.key != null ||
            result.allow_partial_trust_chain != 0;

        if (key_file := generated.key_file.get()) {
            const key_path = handlePath(global, "keyFile", key_file)?;
            result.key_file_name = key_path.ptr;
            result.requires_custom_request_ctx = true;
        }
        if (cert_file := generated.cert_file.get()) {
            const cert_path = handlePath(global, "certFile", cert_file)?;
            result.cert_file_name = cert_path.ptr;
            result.requires_custom_request_ctx = true;
        }
        if (ca_file := generated.ca_file.get()) {
            const ca_path = handlePath(global, "caFile", ca_file)?;
            result.ca_file_name = ca_path.ptr;
            result.requires_custom_request_ctx = true;
        }

        const protocols: ([*:0]const u8)? = switch (generated.alpn_protocols) {
            .none => null,
            .string => |*val| val.get().toOwnedSliceZ(bun.default_allocator).ptr,
            .buffer => |*val| ({
                const buffer: jsc.ArrayBuffer = val.get().asArrayBuffer();
                const duped = bun.default_allocator.dupeZ(u8, buffer.byteSlice())?;
                break duped.ptr;
            }),
        };
        if (some_protocols := protocols) {
            result.protos = some_protocols;
            result.requires_custom_request_ctx = true;
        }
        if (ciphers := generated.ciphers.get()) {
            result.ssl_ciphers = ciphers.toOwnedSliceZ(bun.default_allocator).ptr;
            result.is_using_default_ciphers = false;
            result.requires_custom_request_ctx = true;
        } else if (default_ciphers := vm.rareData().tlsDefaultCiphers()) {
            result.ssl_ciphers = bun.handleOom(bun.default_allocator.dupeZ(u8, default_ciphers)).ptr;
            result.is_using_default_ciphers = false;
            result.requires_custom_request_ctx = true;
        }

        result.client_renegotiation_limit = generated.client_renegotiation_limit;
        result.client_renegotiation_window = generated.client_renegotiation_window;
        has_any = has_any ||
            result.requires_custom_request_ctx ||
            result.client_renegotiation_limit != 0 ||
            generated.client_renegotiation_window != 0;

        if (has_any) {
            _result_guard.ptr = null;
            return result;
        }
        return null;
    }

    pub fn takeProtos(*_): ([]const u8)? {
        defer _.protos = null;
        if (_.protos == null) return null;
        const protos = _.protos.?;
        return bun.handleOom(bun.memory.dropSentinel(protos, bun.default_allocator));
    }

    pub fn takeServerName(*_): ([]const u8)? {
        defer _.server_name = null;
        if (_.server_name == null) return null;
        const server_name = _.server_name.?;
        return bun.handleOom(bun.memory.dropSentinel(server_name, bun.default_allocator));
    }
}

pub const zero = SSLConfig{};

fn stringsEqualOpt(first: ([*:0]const u8)?, second: ([*:0]const u8)?): bool {
    if (a := first) {
        if (second == null) return false;
        const b = second.?;
        return stringsEqual(a, b);
    } else {
        return second == null;
    }
}

fn stringSlicesEqualOpt(first: CStringSlice?, second: CStringSlice?): bool {
    if (slice1 := first) {
        if (second == null) return false;
        const slice2 = second.?;
        if (slice1.len != slice2.len) return false;
        let i: usize = 0;
        while (i < slice1.len) : (i += 1) {
            if (!stringsEqual(slice1[i], slice2[i])) return false;
        }
        return true;
    } else {
        return second == null;
    }
}

fn stringsEqual(a: [*:0]const u8, b: [*:0]const u8): bool {
    const lhs = bun.asByteSlice(a);
    const rhs = bun.asByteSlice(b);
    return strings.eqlLong(lhs, rhs, true);
}

type OptCStringSlice = CStringSlice?;
type OptCString = ([*:0]const u8)?;

fn freeStrings(slice: *OptCStringSlice) {
    if (slice.* == null) return;
    const inner = slice.*.?;
    for (str_item of inner) {
        bun.freeSensitive(bun.default_allocator, std.mem.span(str_item));
    }
    bun.default_allocator.free(inner);
    slice.* = null;
}

fn freeString(str_ptr: *OptCString) {
    if (str_ptr.* == null) return;
    const inner = str_ptr.*.?;
    bun.freeSensitive(bun.default_allocator, std.mem.span(inner));
    str_ptr.* = null;
}

fn cloneStrings(slice: CStringSlice?): CStringSlice? {
    if (slice == null) return null;
    const inner = slice.?;
    let result = bun.handleOom(bun.default_allocator.alloc(CString, inner.len));
    let i: usize = 0;
    while (i < inner.len) : (i += 1) {
        result[i] = bun.handleOom(bun.default_allocator.dupeZ(u8, std.mem.span(inner[i])));
    }
    return result;
}

fn cloneString(str_opt: ([*:0]const u8)?): ([*:0]const u8)? {
    if (str_opt == null) return null;
    return bun.handleOom(bun.default_allocator.dupeZ(u8, std.mem.span(str_opt.?)));
}

struct _GlobalRegistryMapContext {
    pub fn hash(key: *SSLConfig): u32 {
        const _ctx = _;
        * = _ctx;
        return @truncate(key.contentHash());
    }
    pub fn eql(a: *SSLConfig, b: *SSLConfig, _: usize): bool {
        const _ctx = _;
        * = _ctx;
        return a.isSame(b);
    }
}

pub struct GlobalRegistry {
    const MapContext = _GlobalRegistryMapContext;

    static let mutex: bun.Mutex = .{};
    static let configs: std.ArrayHashMapUnmanaged(*SSLConfig, SSLConfig.WeakPtr, MapContext, true) = .empty;

    pub static fn intern(config: SSLConfig): SSLConfig.SharedPtr {
        let new_shared = SSLConfig.SharedPtr.new(config);
        const new_ptr = new_shared.get();

        let dispose_new: SSLConfig.SharedPtr? = null;
        let dispose_old_weak: SSLConfig.WeakPtr? = null;
        let _dispose_new_guard = DeinitGuard<SSLConfig.SharedPtr>{};
        let _dispose_old_guard = DeinitGuard<SSLConfig.WeakPtr>{};

        mutex.lock();
        const _mutex_guard = MutexUnlockGuard{ mutex: unsafe { &mutex } };

        const gop = bun.handleOom(configs.getOrPutContext(bun.default_allocator, new_ptr, .{}));
        if (gop.found_existing) {
            if (existing_shared := gop.value_ptr.upgrade()) {
                dispose_new = new_shared;
                _dispose_new_guard.ptr = unsafe { &dispose_new.? };
                return existing_shared;
            }
            dispose_old_weak = gop.value_ptr.*;
            _dispose_old_guard.ptr = unsafe { &dispose_old_weak.? };
            gop.key_ptr.* = new_ptr;
        }
        gop.value_ptr.* = new_shared.cloneWeak();
        return new_shared;
    }

    static fn remove(config: *SSLConfig) {
        mutex.lock();
        const _mutex_guard = MutexUnlockGuard{ mutex: unsafe { &mutex } };
        if (configs.count() == 0) return;
        const idx_opt = configs.getIndexContext(config, .{});
        if (idx_opt == null) return;
        const idx = idx_opt.?;
        if (configs.keys()[idx] != config) return;
        let weak = configs.values()[idx];
        configs.swapRemoveAt(idx);
        weak.deinit();
    }
}

fn readFromBlob(
    global: *jsc.JSGlobalObject,
    blob: *bun.webcore.Blob,
): ![:0]const u8 {
    if (blob.store == null) return error.NullStore;
    const store = blob.store.?;
    const file = switch (store.data) {
        .file => |f| f,
        else => return error.NotAFile,
    };
    let fs: jsc.Node.fs.NodeFS = .{};
    const maybe = fs.readFileWithOptions(
        .{ .path = file.pathlike },
        .sync,
        .null_terminated,
    );
    const result = switch (maybe) {
        .result => |res| res,
        .err => |err| return global.throwValue(err.toJS(global)?),
    };
    if (result.null_terminated.len == 0) return error.EmptyFile;
    return result.null_terminated;
}

fn handlePath(
    global: *jsc.JSGlobalObject,
    comptime field: []const u8,
    str_impl: bun.string.WTFStringImpl,
): bun.JSError![:0]const u8 {
    const name = str_impl.toOwnedSliceZ(bun.default_allocator);
    let _name_guard = FreeSensitiveZGuard{ slice: name };
    if (std.posix.system.access(name, std.posix.F_OK) != 0) {
        return global.throwInvalidArguments(
            std.fmt.comptimePrint("Unable to access {s} path", .{field}),
            .{},
        );
    }
    _name_guard.slice = null;
    return name;
}

fn handleFileForField(
    global: *jsc.JSGlobalObject,
    comptime field: []const u8,
    file: *const jsc.generated.SSLConfigFile,
): bun.JSError!([][*:0]const u8)? {
    return handleFile(global, file) catch e => {
        if (e == error.JSError) return error.JSError;
        if (e == error.OutOfMemory) return error.OutOfMemory;
        if (e == error.JSTerminated) return error.JSTerminated;
        if (e == error.EmptyFile) {
            return global.throwInvalidArguments(
                std.fmt.comptimePrint("TLSOptions.{s} is an empty file", .{field}),
                .{},
            );
        }
        return global.throwInvalidArguments(
            std.fmt.comptimePrint(
                "TLSOptions.{s} is not a valid BunFile (non-BunFile `Blob`s are not supported)",
                .{field},
            ),
            .{},
        );
    };
}

pub union(enum) SingleFileInput {
    string: bun.string.WTFStringImpl,
    buffer: *jsc.JSCArrayBuffer,
    file: *bun.webcore.Blob,
}

fn handleFile(
    global: *jsc.JSGlobalObject,
    file: *const jsc.generated.SSLConfigFile,
): !([][*:0]const u8)? {
    const single = handleSingleFile(global, switch (file.*) {
        .none => return null,
        .string => |*val| SingleFileInput{ .string = val.get() },
        .buffer => |*val| SingleFileInput{ .buffer = val.get() },
        .file => |*val| SingleFileInput{ .file = val.get() },
        .array => |*list| return handleFileArray(global, list.items())?,
    })?;
    let _single_guard = FreeSensitiveZGuard{ slice: single };
    let result = bun.default_allocator.alloc([*:0]const u8, 1)?;
    result[0] = single;
    _single_guard.slice = null;
    return result;
}

fn handleFileArray(
    global: *jsc.JSGlobalObject,
    elements: []const jsc.generated.SSLConfigSingleFile,
): !([][*:0]const u8)? {
    if (elements.len == 0) return null;
    let result: bun.collections.ArrayListDefault(CString) = .initCapacity(elements.len)?;
    let _result_guard = SensitiveListGuard<bun.collections.ArrayListDefault(CString)>{ ptr: unsafe { &result } };
    for (elem of elements) {
        result.appendAssumeCapacity(handleSingleFile(global, switch (elem) {
            .string => |*val| SingleFileInput{ .string = val.get() },
            .buffer => |*val| SingleFileInput{ .buffer = val.get() },
            .file => |*val| SingleFileInput{ .file = val.get() },
        })?);
    }
    const owned = result.toOwnedSlice()?;
    _result_guard.ptr = null;
    return owned;
}

fn handleSingleFile(
    global: *jsc.JSGlobalObject,
    file: SingleFileInput,
): ![:0]const u8 {
    return switch (file) {
        .string => |s| s.toOwnedSliceZ(bun.default_allocator),
        .buffer => |jsc_buffer| ({
            const buffer: jsc.ArrayBuffer = jsc_buffer.asArrayBuffer();
            break bun.default_allocator.dupeZ(u8, buffer.byteSlice())?;
        }),
        .file => |blob| readFromBlob(global, blob)?,
    };
}
