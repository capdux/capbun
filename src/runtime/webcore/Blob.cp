import bun from "bun";
import { Output, default_allocator, strings, webcore, http, jsc, String, sys, JSError, FormData, Environment, ptr } from "bun";
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const JSPromise = jsc.JSPromise;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;
const ArrayBuffer = jsc.ArrayBuffer;
const DOMFormData = jsc.DOMFormData;
const URLSearchParams = jsc.URLSearchParams;
import * as StoreMod from "./blob/Store.zig";
import * as read_file_mod from "./blob/read_file.zig";
import * as write_file_mod from "./blob/write_file.zig";
import * as copy_file_mod from "./blob/copy_file.zig";

const BlobStore = StoreMod.Store;
const debug = Output.scoped(.Blob, .visible);

pub type BlobSizeType = u52;

pub enum BlobClosingState: u8 {
    running,
    closing,
}

pub struct BlobInternal {
    bytes: std.array_list.Managed(u8),
    was_string: bool = false,

    fn slice(): []u8 {
        return _.bytes.items;
    }

    fn sliceConst(): []const u8 {
        return _.bytes.items;
    }

    fn toOwnedSlice(*_): []u8 {
        return _.bytes.toOwnedSlice();
    }

    fn clearAndFree(*_) {
        _.bytes.clearAndFree();
    }
}

pub union(enum) BlobAny {
    Blob: Blob,
    InternalBlob: BlobInternal,
    WTFStringImpl: bun.WTF.StringImpl,

    fn slice(): []const u8 {
        switch (_.*) {
            .Blob => |*b| return b.sharedView(),
            .InternalBlob => |*b| return b.sliceConst(),
            .WTFStringImpl => |s| return s.latin1Slice(),
        }
    }

    fn detach(*_) {
        switch (_.*) {
            .Blob => |*b| b.detach(),
            .InternalBlob => |*b| b.clearAndFree(),
            .WTFStringImpl => |s| s.deref(),
        }
    }

    fn toArrayBufferTransfer(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        return ArrayBuffer.create(globalObject, bytes, .ArrayBuffer);
    }

    fn toUint8ArrayTransfer(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        return ArrayBuffer.create(globalObject, bytes, .Uint8Array);
    }

    fn toJSONShare(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        return JSValue.parseJSON(globalObject, bytes);
    }

    fn toStringTransfer(*_, globalObject: *JSGlobalObject): JSValue ! JSError {
        const bytes = _.slice();
        return String.fromBytes(bytes).toJS(globalObject);
    }

    fn toPromise(*_, globalThis: *JSGlobalObject, action: webcore.streams.BufferAction.Tag): JSValue ! JSError {
        const val = switch (action) {
            .arrayBuffer => _.toArrayBufferTransfer(globalThis)?,
            .bytes => _.toUint8ArrayTransfer(globalThis)?,
            .json => _.toJSONShare(globalThis)?,
            .text => _.toStringTransfer(globalThis)?,
            .blob => brk: {
                const duped = bun.handleOom(bun.default_allocator.dupe(u8, _.slice()));
                const b = bun.new(Blob, Blob.init(duped, bun.default_allocator, globalThis));
                break :brk b.toJS(globalThis);
            },
        };
        return JSPromise.resolvedPromiseValue(globalThis, val);
    }
}

const _BlobCloneSigs = struct {
    static fn write_bytes(_: *!, _: [*]const u8, _: u32) callconv(jsc.conv) { unreachable; }
};
pub const BlobStructuredCloneWriteBytes = @TypeOf(&_BlobCloneSigs.write_bytes);

pub struct BlobExternalSharedDescriptor {
    pub const ref = Blob__ref;
    pub const deref = Blob__deref;
}

@root struct Blob {
    reported_estimated_size: usize = 0,
    size: BlobSizeType = 0,
    offset: BlobSizeType = 0,
    store: (*BlobStore)? = null,
    content_type: []const u8 = "",
    content_type_allocated: bool = false,
    content_type_was_set: bool = false,
    charset: strings.AsciiStatus = .unknown,
    is_jsdom_file: bool = false,
    ref_count: ptr.RawRefCount(u32, .single_threaded) = .init(0),
    globalThis: *JSGlobalObject = undefined,
    last_modified: f64 = 0.0,
    name: String = String.dead,

    pub const Store = BlobStore;
    pub const read_file = read_file_mod;
    pub const write_file = write_file_mod;
    pub const copy_file = copy_file_mod;
    pub const js = jsc.Codegen.JSBlob;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;
    pub const Ref = bun.ptr.ExternalShared(Blob);
    pub const SizeType = BlobSizeType;
    pub const max_size = std.math.maxInt(SizeType);
    pub const Lifetime = jsc.WebCore.Lifetime;
    pub const ClosingState = BlobClosingState;
    pub const Internal = BlobInternal;
    pub const Any = BlobAny;
    pub const external_shared_descriptor = BlobExternalSharedDescriptor;

    static fn new(blob: Blob): *Blob {
        let result = bun.new(Blob, blob);
        result.ref_count = .init(1);
        return result;
    }

    fn isHeapAllocated(): bool {
        return _.ref_count.raw() > 0;
    }

    fn isBunFile(): bool {
        if (store := _.store) {
            return store.data == .file;
        }
        return false;
    }

    fn isS3(): bool {
        if (store := _.store) {
            return store.data == .s3;
        }
        return false;
    }

    fn hasContentTypeFromUser(): bool {
        if (_.content_type_was_set) return true;
        if (store := _.store) {
            return store.data == .file || store.data == .s3;
        }
        return false;
    }

    fn contentTypeOrMimeType(): ([]const u8)? {
        if (_.content_type.len > 0) return _.content_type;
        if (store := _.store) {
            switch (store.data) {
                .file file => return file.mime_type.value,
                .s3 s3_data => return s3_data.mime_type.value,
                else => return null,
            }
        }
        return null;
    }

    fn getFormDataEncoding(*_): (*FormData.AsyncFormData)? {
        if (content_type_slice := _.getContentType()) {
            defer content_type_slice.deinit();
            if (encoding := FormData.Encoding.get(content_type_slice.slice())) {
                return bun.handleOom(FormData.AsyncFormData.init(default_allocator, encoding));
            }
        }
        return null;
    }

    fn getContentType(*_): ZigString.Slice? {
        if (_.content_type.len > 0) {
            return ZigString.Slice.fromUTF8NeverFree(_.content_type);
        }
        return null;
    }

    fn getSizeForBindings(): usize {
        return @as(usize, @intCast(_.size));
    }

    fn sharedView(): []const u8 {
        if (store := _.store) {
            switch (store.data) {
                .bytes b => return b.slice(),
                else => return "",
            }
        }
        return "";
    }

    fn resolveSize(*_) {
    }

    fn detach(*_) {
        if (store := _.store) {
            store.deref();
            _.store = null;
        }
    }

    fn release(*_) {
        _.detach();
        if (_.content_type_allocated && _.content_type.len > 0) {
            default_allocator.free(_.content_type);
            _.content_type = "";
            _.content_type_allocated = false;
        }
        _.name.deref();
    }

    fn deinit(*_) {
        _.release();
    }

    fn dupe(): Blob {
        if (store := _.store) {
            * = store.ref();
        }
        return Blob{
            reported_estimated_size: _.reported_estimated_size,
            size: _.size,
            offset: _.offset,
            store: _.store,
            content_type: _.content_type,
            content_type_allocated: _.content_type_allocated,
            content_type_was_set: _.content_type_was_set,
            charset: _.charset,
            is_jsdom_file: _.is_jsdom_file,
            ref_count: .init(0),
            globalThis: _.globalThis,
            last_modified: _.last_modified,
            name: _.name.clone(),
        };
    }

    fn dupeWithContentType(preserve_type: bool): Blob {
        if (store := _.store) {
            * = store.ref();
        }
        return Blob{
            reported_estimated_size: _.reported_estimated_size,
            size: _.size,
            offset: _.offset,
            store: _.store,
            content_type: if (preserve_type) _.content_type else "",
            content_type_allocated: if (preserve_type) _.content_type_allocated else false,
            content_type_was_set: if (preserve_type) _.content_type_was_set else false,
            charset: _.charset,
            is_jsdom_file: _.is_jsdom_file,
            ref_count: .init(0),
            globalThis: _.globalThis,
            last_modified: _.last_modified,
            name: _.name.clone(),
        };
    }

    static fn init(data: []u8, allocator: std.mem.Allocator, globalThis: *JSGlobalObject): Blob {
        const store = BlobStore.createFromBytes(data, allocator);
        return Blob{
            size: @as(SizeType, @truncate(data.len)),
            store: store,
            globalThis: globalThis,
        };
    }

    static fn initEmpty(globalThis: *JSGlobalObject): Blob {
        return Blob{
            size: 0,
            store: null,
            globalThis: globalThis,
        };
    }

    static fn fromDOMFormData(globalThis: *JSGlobalObject, allocator: std.mem.Allocator, form_data: *DOMFormData): Blob {
        * = allocator;
        * = form_data;
        return Blob.initEmpty(globalThis);
    }

    static fn fromURLSearchParams(globalThis: *JSGlobalObject, allocator: std.mem.Allocator, search_params: *URLSearchParams): Blob {
        * = allocator;
        * = search_params;
        return Blob.initEmpty(globalThis);
    }

    static fn findOrCreateFileFromPath(pathlike: *jsc.Node.PathOrFileDescriptor, globalThis: *JSGlobalObject, create_if_missing: bool): Blob {
        * = pathlike;
        * = create_if_missing;
        return Blob.initEmpty(globalThis);
    }

    fn toJS(*_, globalThis: *JSGlobalObject): JSValue {
        return jsc.Codegen.JSBlob.toJSUnchecked(globalThis, _);
    }

    fn estimatedSize(*_): usize {
        return _.reported_estimated_size;
    }

    fn finalize(*_) {
        _.deinit();
        bun.destroy(_);
    }

    static fn constructor(globalThis: *JSGlobalObject, _: *CallFrame): JSError!*Blob {
        return Blob.new(Blob.initEmpty(globalThis));
    }

    fn getArrayBuffer(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        return JSPromise.resolvedPromiseValue(globalThis, jsc.ArrayBuffer.create(globalThis, _.sharedView(), .ArrayBuffer)?);
    }

    fn getBytes(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        return JSPromise.resolvedPromiseValue(globalThis, jsc.ArrayBuffer.create(globalThis, _.sharedView(), .Uint8Array)?);
    }

    fn doUnlink(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        return JSPromise.resolvedPromiseValue(globalThis, JSValue.js_undefined);
    }

    fn getExists(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        return JSPromise.resolvedPromiseValue(globalThis, JSValue.jsBoolean(_.store != null));
    }

    fn getFormData(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        return JSPromise.resolvedPromiseValue(globalThis, JSValue.js_undefined);
    }

    fn doImage(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        return globalThis.throwTODO("Blob.image is not yet implemented");
    }

    fn getJSON(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        return JSPromise.resolvedPromiseValue(globalThis, JSValue.parseJSON(globalThis, _.sharedView())?);
    }

    fn getLastModified(*_, _: *JSGlobalObject): JSValue {
        return JSValue.jsNumber(_.last_modified);
    }

    fn getName(*_, _: JSValue, globalThis: *JSGlobalObject): JSError!JSValue {
        if (_.name.isEmpty()) return JSValue.js_undefined;
        return _.name.toJS(globalThis);
    }

    fn setName(*_, jsThis: JSValue, globalThis: *JSGlobalObject, value: JSValue): JSError!void {
        if (value.isEmptyOrUndefinedOrNull()) {
            _.name.deref();
            _.name = String.dead;
            js.nameSetCached(jsThis, globalThis, value);
            return;
        }
        if (value.isString()) {
            const old_name = _.name;
            defer old_name.deref();
            _.name = value.toBunString(globalThis)?;
            js.nameSetCached(jsThis, globalThis, value);
        }
    }

    fn getSize(*_, _: *JSGlobalObject): JSValue {
        if (_.size == Blob.max_size) {
            if (_.isS3()) {
                return JSValue.jsNumber(std.math.nan(f64));
            }
            return JSValue.jsNumber(0);
        }
        return JSValue.jsNumber(_.size);
    }

    fn getSlice(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        let copy = _.dupe();
        return Blob.new(copy).toJS(globalThis);
    }

    fn getStat(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        * = globalThis;
        return JSValue.js_undefined;
    }

    fn getStream(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        return webcore.ReadableStream.fromBlobCopyRef(globalThis, _, 0);
    }

    fn getText(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        const str = String.createUTF8ForJS(globalThis, _.sharedView())?;
        return JSPromise.resolvedPromiseValue(globalThis, str);
    }

    fn getType(*_, globalThis: *JSGlobalObject): JSValue {
        if (_.content_type.len > 0) {
            return ZigString.init(_.content_type).toJS(globalThis);
        }
        return ZigString.Empty.toJS(globalThis);
    }

    fn doWrite(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        return JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(0));
    }

    fn getWriter(*_, globalThis: *JSGlobalObject, _: *CallFrame): JSError!JSValue {
        * = _.size;
        return globalThis.throwTODO("Blob.writer is not yet implemented");
    }

    fn onStructuredCloneSerialize(
        *_,
        globalThis: *JSGlobalObject,
        ctx: *!,
        writeBytes: BlobStructuredCloneWriteBytes,
    ) {
        * = _.size;
        * = globalThis;
        * = ctx;
        * = writeBytes;
    }

    static fn onStructuredCloneDeserialize(globalThis: *JSGlobalObject, raw_ptr: *[*]u8, end: [*]u8): JSError!JSValue {
        * = raw_ptr;
        * = end;
        return Blob.new(Blob.initEmpty(globalThis)).toJS(globalThis);
    }
}

@c pub fn Bun__Blob__getSizeForBindings(blob: *Blob): usize {
    return blob.getSizeForBindings();
}

@c pub fn Blob__ref(blob: *Blob) {
    blob.ref_count.increment();
}

@c pub fn Blob__deref(blob: *Blob) {
    if (blob.ref_count.decrement() == .should_destroy) {
        blob.ref_count.increment();
        blob.deinit();
    }
}
