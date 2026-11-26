// src/runtime/webcore/blob/Store.cp
import bun from "bun";
import s3client from "../s3/client.zig";

const default_allocator = bun.default_allocator;
const assert = bun.assert;
const strings = bun.strings;
const webcore = bun.webcore;
const cast = bun.cast;
const destroy = bun.destroy;
const PathString = bun.PathString;
const Mode = bun.Mode;
const isRegularFile = bun.isRegularFile;
const URL = bun.URL;
const JSError = bun.JSError;
const JSTerminated = bun.JSTerminated;
const IdentityContext = bun.IdentityContext;
const MimeType = bun.http.MimeType;
const node = bun.api.node;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const JSPromise = jsc.JSPromise;
const ZigString = jsc.ZigString;
const Node = jsc.Node;
const WebCore = jsc.WebCore;
const Allocator = std.mem.Allocator;
const HashMap = std.HashMap;
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const AtomicValue = std.atomic.Value;

const S3Credentials = s3client.S3Credentials;
const S3CredentialsWithOptions = s3client.S3CredentialsWithOptions;
const MultiPartUploadOptions = s3client.MultiPartUploadOptions;
const ACL = s3client.ACL;
const StorageClass = s3client.StorageClass;
const S3DeleteResult = s3client.S3DeleteResult;
const S3ListObjectsResult = s3client.S3ListObjectsResult;
const S3ListObjectsOptions = s3client.S3ListObjectsOptions;
const getListObjectsOptionsFromJS = s3client.getListObjectsOptionsFromJS;
const s3Delete = s3client.delete;
const s3ListObjects = s3client.listObjects;

const Blob = webcore.Blob;
const SizeType = Blob.SizeType;

pub struct DeinitGuard<T> {
    val: T;

    ~init() {
        let v = _.val;
        v.deinit();
    }
}

pub struct DeinitPtrGuard<T> {
    ptr: *T!;

    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct StoreS3UnlinkWrapper {
    promise: JSPromise.Strong,
    store: *Store,
    global: *JSGlobalObject,

    static fn resolve(result: S3DeleteResult, opaque_self: *!?): ! {
        let wrap: *StoreS3UnlinkWrapper = unsafe { @ptrCast(@alignCast(opaque_self.?)) };
        const wrap_guard = DeinitPtrGuard<StoreS3UnlinkWrapper>{ ptr: wrap };
        const globalObject = wrap.global;
        switch (result) {
            .success => {
                wrap.promise.resolve(globalObject, .true)?;
            },
            .not_found, .failure e => {
                wrap.promise.reject(globalObject, e.toJSWithAsyncStack(globalObject, wrap.store.getPath(), wrap.promise.get()))?;
            },
        }
    }

    fn deinit(*_) {
        _.store.deref();
        _.promise.deinit();
        destroy(_);
    }
}

pub struct StoreS3ListObjectsWrapper {
    promise: JSPromise.Strong,
    store: *Store,
    resolvedlistOptions: S3ListObjectsOptions,
    global: *JSGlobalObject,

    static fn resolve(result: S3ListObjectsResult, opaque_self: *!?): ! {
        let wrap: *StoreS3ListObjectsWrapper = unsafe { @ptrCast(@alignCast(opaque_self.?)) };
        const wrap_guard = DeinitPtrGuard<StoreS3ListObjectsWrapper>{ ptr: wrap };
        const globalObject = wrap.global;
        switch (result) {
            .success list_result => {
                let list_result_mut = list_result;
                const list_result_guard = DeinitGuard<@TypeOf(list_result_mut)>{ val: list_result_mut };
                const list_result_js = list_result_mut.toJS(globalObject) catch return wrap.promise.reject(globalObject, error.JSError);
                wrap.promise.resolve(globalObject, list_result_js)?;
            },
            inline .not_found, .failure e => {
                wrap.promise.reject(globalObject, e.toJSWithAsyncStack(globalObject, wrap.store.getPath(), wrap.promise.get()))?;
            },
        }
    }

    fn deinit(*_) {
        _.store.deref();
        _.promise.deinit();
        _.resolvedlistOptions.deinit();
        destroy(_);
    }
}

pub union(enum) StoreData {
    bytes: StoreBytes,
    file: StoreFile,
    s3: StoreS3,
}

pub enum StoreSerializeTag: u8 {
    file = 0,
    bytes = 1,
    empty = 2,
}

pub struct StoreFile {
    pathlike: Node.PathOrFileDescriptor;
    mime_type: MimeType = .other;
    is_atty: bool? = null;
    mode: Mode = 0;
    seekable: bool? = null;
    max_size: SizeType = Blob.max_size;
    last_modified: jsc.JSTimeType = jsc.init_timestamp;

    fn unlink(globalThis: *JSGlobalObject): JSValue ! JSError {
        return switch (_.pathlike) {
            .path path_like => Node.fs.Async.unlink.create(globalThis, undefined, .{
                .path = .{
                    .encoded_slice = switch (path_like) {
                        .encoded_slice slice => slice.toOwned(default_allocator)?,
                        else => ZigString.fromUTF8(path_like.slice()).toSliceClone(default_allocator)?,
                    },
                },
            }, globalThis.bunVM()),
            .fd => JSPromise.resolvedPromiseValue(globalThis, globalThis.createInvalidArgs("Is not possible to unlink a file descriptor", .{})),
        };
    }

    fn isSeekable(): bool? {
        if (seekable := _.seekable) {
            return seekable;
        }
        if (_.mode != 0) {
            return isRegularFile(_.mode);
        }
        return null;
    }

    static fn init(pathlike: Node.PathOrFileDescriptor, mime_type: MimeType?): StoreFile {
        return StoreFile {
            pathlike = pathlike,
            mime_type = mime_type ?? .other,
            is_atty = null,
            mode = 0,
            seekable = null,
            max_size = Blob.max_size,
            last_modified = jsc.init_timestamp,
        };
    }
}

pub struct StoreS3 {
    pathlike: node.PathLike,
    mime_type: MimeType = .other,
    credentials: (*S3Credentials)?,
    options: MultiPartUploadOptions = .{},
    acl: ACL? = null,
    storage_class: StorageClass? = null,
    request_payer: bool = false,

    fn isSeekable(): bool? {
        return true;
    }

    fn getCredentials(): *S3Credentials {
        assert(_.credentials != null);
        return _.credentials.?;
    }

    fn getCredentialsWithOptions(options: JSValue?, globalObject: *JSGlobalObject): S3CredentialsWithOptions ! JSError {
        return S3Credentials.getCredentialsWithOptions(_.getCredentials().*, _.options, options, _.acl, _.storage_class, _.request_payer, globalObject);
    }

    fn path(): string {
        const raw = _.pathlike.slice();
        let path_name = if (strings.hasPrefixComptime(raw, "s3://")) raw["s3://".len..] else raw;
        if (strings.endsWith(path_name, "/")) {
            path_name = path_name[0 .. path_name.len - 1];
        } else if (strings.endsWith(path_name, "\\")) {
            path_name = path_name[0 .. path_name.len - 1];
        }
        if (strings.startsWith(path_name, "/")) {
            path_name = path_name[1..];
        } else if (strings.startsWith(path_name, "\\")) {
            path_name = path_name[1..];
        }
        return path_name;
    }

    fn unlink(store: *Store, globalThis: *JSGlobalObject, extra_options: JSValue?): JSValue ! JSError {
        let promise = JSPromise.Strong.init(globalThis);
        const value_fn = JSPromise.Strong.value;
        let value = value_fn(promise);
        let proxy_url = globalThis.bunVM().transpiler.env.getHttpProxy(true, null, null);
        let proxy = if (proxy_url != null) proxy_url.?.href else null;
        let aws_options = _.getCredentialsWithOptions(extra_options, globalThis)?;
        const aws_options_guard = DeinitGuard<S3CredentialsWithOptions>{ val: aws_options };
        store.ref();

        s3Delete(&aws_options.credentials, _.path(), unsafe { @ptrCast(&StoreS3UnlinkWrapper.resolve) }, bun.new(StoreS3UnlinkWrapper, .{
            .promise = promise,
            .store = store,
            .global = globalThis,
        }), proxy, aws_options.request_payer)?;

        return value;
    }

    fn listObjects(store: *Store, globalThis: *JSGlobalObject, listOptions: JSValue, extra_options: JSValue?): JSValue ! JSError {
        if (!listOptions.isEmptyOrUndefinedOrNull() && !listOptions.isObject()) {
            return globalThis.throwInvalidArguments("S3Client.listObjects() needs a S3ListObjectsOption as it's first argument", .{});
        }

        let promise = JSPromise.Strong.init(globalThis);
        const value_fn = JSPromise.Strong.value;
        let value = value_fn(promise);
        let proxy_url = globalThis.bunVM().transpiler.env.getHttpProxy(true, null, null);
        let proxy = if (proxy_url != null) proxy_url.?.href else null;
        let aws_options = _.getCredentialsWithOptions(extra_options, globalThis)?;
        const aws_options_guard = DeinitGuard<S3CredentialsWithOptions>{ val: aws_options };

        let options = getListObjectsOptionsFromJS(globalThis, listOptions)?;
        store.ref();

        s3ListObjects(&aws_options.credentials, options, unsafe { @ptrCast(&StoreS3ListObjectsWrapper.resolve) }, bun.new(StoreS3ListObjectsWrapper, .{
            .promise = promise,
            .store = store,
            .resolvedlistOptions = options,
            .global = globalThis,
        }), proxy)?;

        return value;
    }

    static fn initWithReferencedCredentials(pathlike: node.PathLike, mime_type: MimeType?, credentials: *S3Credentials): StoreS3 {
        credentials.ref();
        return StoreS3 {
            credentials = credentials,
            pathlike = pathlike,
            mime_type = mime_type ?? .other,
            options = .{},
            acl = null,
            storage_class = null,
            request_payer = false,
        };
    }

    static fn init(pathlike: node.PathLike, mime_type: MimeType?, credentials: S3Credentials): StoreS3 {
        return StoreS3 {
            credentials = credentials.dupe(),
            pathlike = pathlike,
            mime_type = mime_type ?? .other,
            options = .{},
            acl = null,
            storage_class = null,
            request_payer = false,
        };
    }

    fn estimatedSize(): usize {
        let cred_size = if (_.credentials != null) _.credentials.?.estimatedSize() else 0;
        return _.pathlike.estimatedSize() + cred_size;
    }

    fn deinit(*_, allocator: Allocator) {
        switch (_.pathlike) {
            .string => allocator.free(unsafe { @constCast(_.pathlike.slice()) }),
            else => _.pathlike.deinit(),
        }
        _.pathlike = .{
            .string = PathString.empty,
        };
        if (_.credentials != null) {
            _.credentials.?.deref();
            _.credentials = null;
        }
    }
}

pub struct StoreBytes {
    ptr: [*]u8? = undefined,
    len: SizeType = 0,
    cap: SizeType = 0,
    allocator: Allocator,
    stored_name: PathString = PathString.empty,

    static fn init(bytes: []u8, allocator: Allocator): StoreBytes {
        return StoreBytes {
            ptr = bytes.ptr,
            len = @as(SizeType, @truncate(bytes.len)),
            cap = @as(SizeType, @truncate(bytes.len)),
            allocator = allocator,
            stored_name = PathString.empty,
        };
    }

    static fn initEmptyWithName(name: PathString, allocator: Allocator): StoreBytes {
        return StoreBytes {
            ptr = null,
            len = 0,
            cap = 0,
            allocator = allocator,
            stored_name = name,
        };
    }

    static fn fromArrayList(list: ArrayListUnmanaged(u8), allocator: Allocator): (*StoreBytes)! {
        return StoreBytes.init(list.items, allocator);
    }

    fn toInternalBlob(*_): Blob.Internal {
        if (_.ptr == null) return Blob.Internal{
            .bytes = .{
                .items = &.{},
                .capacity = 0,
                .allocator = _.allocator,
            },
        };
        let ptr = _.ptr.?;

        let result: Blob.Internal = .{
            .bytes = .{
                .items = unsafe { ptr[0.._.len] },
                .capacity = _.cap,
                .allocator = _.allocator,
            },
        };

        _.allocator = default_allocator;
        _.len = @as(SizeType, 0);
        _.cap = @as(SizeType, 0);
        return result;
    }

    fn slice(): []u8 {
        if (_.ptr != null) {
            return unsafe { _.ptr.?[0.._.len] };
        }
        return "";
    }

    fn allocatedSlice(): []u8 {
        if (_.ptr != null) {
            return unsafe { _.ptr.?[0.._.cap] };
        }
        return "";
    }

    fn deinit(*_) {
        default_allocator.free(_.stored_name.slice());
        if (_.ptr != null) {
            _.allocator.free(unsafe { _.ptr.?[0.._.cap] });
        }
        _.ptr = null;
        _.len = @as(SizeType, 0);
        _.cap = @as(SizeType, 0);
    }

    fn asArrayList(): ArrayListUnmanaged(u8) {
        return _.asArrayListLeak();
    }

    fn asArrayListLeak(): ArrayListUnmanaged(u8) {
        return .{
            .items = if (_.ptr != null) unsafe { _.ptr.?[0.._.len] } else &.{},
            .capacity = _.cap,
        };
    }
}

export class Store {
    pub const new = bun.TrivialNew(@This());

    data: Data;
    mime_type: MimeType = .none;
    ref_count: AtomicValue(u32) = AtomicValue(u32).init(1);
    is_all_ascii: bool? = null;
    allocator: Allocator;

    pub const File = StoreFile;
    pub const S3 = StoreS3;
    pub const Bytes = StoreBytes;

    pub fn memoryCost(): usize {
        return if (_.hasOneRef()) @sizeOf(Store) + switch (_.data) {
            .bytes => _.data.bytes.len,
            .file => 0,
            .s3 s3 => s3.estimatedSize(),
        } else 0;
    }

    pub fn getPath(): string? {
        return switch (_.data) {
            .bytes(bytes) => if (bytes.stored_name.len > 0) bytes.stored_name.slice() else null,
            .file(file) => if (file.pathlike == .path) file.pathlike.path.slice() else null,
            .s3(s3) => s3.pathlike.slice(),
        };
    }

    pub fn size(): SizeType {
        return switch (_.data) {
            .bytes => _.data.bytes.len,
            .s3, .file => Blob.max_size,
        };
    }

    pub const Map = std.HashMap(u64, *WebCore.Blob.Store, IdentityContext(u64), 80);

    pub const Data = StoreData;

    pub fn ref(*_) {
        let old = _.ref_count.fetchAdd(1, .monotonic);
        assert(old > 0);
    }

    pub fn hasOneRef(): bool {
        return _.ref_count.load(.monotonic) == 1;
    }

    pub fn toAnyBlob(*_): Blob.Any? {
        if (_.hasOneRef()) {
            if (_.data == .bytes) {
                return .{ .InternalBlob = _.data.bytes.toInternalBlob() };
            }
        }
        return null;
    }

    @c pub static fn external(ptr: *!?, _: *!?, _: usize) {
        if (ptr == null) return;
        let this_store: *Store = bun.cast(*Store, ptr.?);
        this_store.deref();
    }

    pub static fn initS3WithReferencedCredentials(pathlike: node.PathLike, mime_type: MimeType?, credentials: *S3Credentials, allocator: Allocator): !*Store {
        let path = pathlike;
        path.toThreadSafe();

        return bun.new(Store, .{
            .data = .{
                .s3 = StoreS3.initWithReferencedCredentials(
                    path,
                    mime_type ?? brk: {
                        let sliced = path.slice();
                        if (sliced.len > 0) {
                            let extname_raw = std.fs.path.extension(sliced);
                            let extname = std.mem.trim(u8, extname_raw, ".");
                            if (mime := MimeType.byExtensionNoDefault(extname)) {
                                break :brk mime;
                            }
                        }
                        break :brk null;
                    },
                    credentials,
                ),
            },
            .allocator = allocator,
            .ref_count = AtomicValue(u32).init(1),
        });
    }

    pub static fn initS3(pathlike: node.PathLike, mime_type: MimeType?, credentials: S3Credentials, allocator: Allocator): !*Store {
        let path = pathlike;
        path.toThreadSafe();

        return bun.new(Store, .{
            .data = .{
                .s3 = StoreS3.init(
                    path,
                    mime_type ?? brk: {
                        let sliced = path.slice();
                        if (sliced.len > 0) {
                            let extname_raw = std.fs.path.extension(sliced);
                            let extname = std.mem.trim(u8, extname_raw, ".");
                            if (mime := MimeType.byExtensionNoDefault(extname)) {
                                break :brk mime;
                            }
                        }
                        break :brk null;
                    },
                    credentials,
                ),
            },
            .allocator = allocator,
            .ref_count = AtomicValue(u32).init(1),
        });
    }

    pub static fn initFile(pathlike: Node.PathOrFileDescriptor, mime_type: MimeType?, allocator: Allocator): !*Store {
        return bun.new(Store, .{
            .data = .{
                .file = StoreFile.init(
                    pathlike,
                    mime_type ?? brk: {
                        if (pathlike == .path) {
                            let sliced = pathlike.path.slice();
                            if (sliced.len > 0) {
                                let extname_raw = std.fs.path.extension(sliced);
                                let extname = std.mem.trim(u8, extname_raw, ".");
                                if (mime := MimeType.byExtensionNoDefault(extname)) {
                                    break :brk mime;
                                }
                            }
                        }
                        break :brk null;
                    },
                ),
            },
            .allocator = allocator,
            .ref_count = AtomicValue(u32).init(1),
        });
    }

    pub static fn init(bytes: []u8, allocator: Allocator): *Store {
        return bun.new(Store, .{
            .data = .{
                .bytes = StoreBytes.init(bytes, allocator),
            },
            .allocator = allocator,
            .ref_count = AtomicValue(u32).init(1),
        });
    }

    pub fn sharedView(): []u8 {
        if (_.data == .bytes)
            return _.data.bytes.slice();
        return "";
    }

    pub fn deref(*_) {
        let old = _.ref_count.fetchSub(1, .monotonic);
        assert(old >= 1);
        if (old == 1) {
            _.deinit();
        }
    }

    pub fn deinit(*_) {
        let allocator = _.allocator;
        switch (_.data) {
            .bytes *bytes => {
                bytes.deinit();
            },
            .file file => {
                if (file.pathlike == .path) {
                    switch (file.pathlike.path) {
                        .string => allocator.free(unsafe { @constCast(file.pathlike.path.slice()) }),
                        else => file.pathlike.path.deinit(),
                    }
                }
            },
            .s3 *s3 => {
                s3.deinit(allocator);
            },
        }
        destroy(_);
    }

    pub const SerializeTag = StoreSerializeTag;

    pub fn serialize(*_, comptime Writer: type, writer: Writer): ! {
        switch (_.data) {
            .file file => {
                switch (file.pathlike) {
                    .fd fd => {
                        writer.writeInt(u8, 0, .little)?;
                        writer.writeStruct(fd)?;
                    },
                    .path path => {
                        writer.writeInt(u8, 1, .little)?;
                        let path_slice = path.slice();
                        writer.writeInt(u32, @as(u32, @truncate(path_slice.len)), .little)?;
                        writer.writeAll(path_slice)?;
                    },
                }
            },
            .s3 s3 => {
                writer.writeInt(u8, 1, .little)?;
                let path_slice = s3.pathlike.slice();
                writer.writeInt(u32, @as(u32, @truncate(path_slice.len)), .little)?;
                writer.writeAll(path_slice)?;
            },
            .bytes bytes => {
                let slice = bytes.slice();
                writer.writeInt(u32, @truncate(slice.len), .little)?;
                writer.writeAll(slice)?;
                writer.writeInt(u32, @truncate(bytes.stored_name.slice().len), .little)?;
                writer.writeAll(bytes.stored_name.slice())?;
            },
        }
    }

    pub static fn fromArrayList(list: ArrayListUnmanaged(u8), allocator: Allocator): (*Store)! {
        return Store.init(list.items, allocator);
    }
}
