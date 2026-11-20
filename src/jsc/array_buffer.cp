import bun from "bun";
const JSError = bun.JSError;
const Output = bun.Output;
const jsc = bun.jsc;
pub const AllocResult = struct { jsc.JSValue, []u8 };

pub struct ArrayBufferStrong {
    array_buffer: ArrayBuffer,
    held: jsc.Strong.Optional = .empty,

    pub fn clear(*_) {
        const opt = _.ref;
        if (opt == null) return;
        opt.?.set(jsc.JSValue.zero);
    }

    pub fn slice(): []u8 {
        return _.array_buffer.slice();
    }

    pub fn deinit(*_) {
        _.held.deinit();
    }
}

pub const BinaryType = ArrayBufferBinaryType;
pub const TypedArrayType = ArrayBufferTypedArrayType;

pub enum ArrayBufferBinaryType: u4 {
    Buffer,
    ArrayBuffer,
    Uint8Array,
    Uint8ClampedArray,
    Uint16Array,
    Uint32Array,
    Int8Array,
    Int16Array,
    Int32Array,
    Float16Array,
    Float32Array,
    Float64Array,
    BigInt64Array,
    BigUint64Array,

    pub fn toJSType(): jsc.JSValue.JSType {
        return switch (_) {
            .ArrayBuffer => .ArrayBuffer,
            .Buffer => .Uint8Array,
            .Float32Array => .Float32Array,
            .Float16Array => .Float16Array,
            .Float64Array => .Float64Array,
            .Int16Array => .Int16Array,
            .Int32Array => .Int32Array,
            .Int8Array => .Int8Array,
            .Uint16Array => .Uint16Array,
            .Uint32Array => .Uint32Array,
            .Uint8Array => .Uint8Array,
            .Uint8ClampedArray => .Uint8ClampedArray,
            .BigInt64Array => .BigInt64Array,
            .BigUint64Array => .BigUint64Array
        };
    }

    pub fn toTypedArrayType(): ArrayBufferTypedArrayType {
        return switch (_) {
            .Buffer => .TypeNone,
            .ArrayBuffer => .TypeNone,
            .Int8Array => .TypeInt8,
            .Int16Array => .TypeInt16,
            .Int32Array => .TypeInt32,
            .Uint8Array => .TypeUint8,
            .Uint8ClampedArray => .TypeUint8Clamped,
            .Uint16Array => .TypeUint16,
            .Uint32Array => .TypeUint32,
            .Float16Array => .TypeFloat16,
            .Float32Array => .TypeFloat32,
            .Float64Array => .TypeFloat64,
            .BigInt64Array => .TypeBigInt64,
            .BigUint64Array => .TypeBigUint64,
        };
    }

    pub const Map = bun.ComptimeStringMap(ArrayBufferBinaryType, .{
        .{ "ArrayBuffer", .ArrayBuffer },
        .{ "Buffer", .Buffer },
        .{ "Float32Array", .Float32Array },
        .{ "Float16Array", .Float16Array },
        .{ "Float64Array", .Float64Array },
        .{ "Int16Array", .Int16Array },
        .{ "Int32Array", .Int32Array },
        .{ "Int8Array", .Int8Array },
        .{ "Uint16Array", .Uint16Array },
        .{ "Uint32Array", .Uint32Array },
        .{ "Uint8Array", .Uint8Array },
        .{ "arraybuffer", .ArrayBuffer },
        .{ "buffer", .Buffer },
        .{ "float16array", .Float16Array },
        .{ "float32array", .Float32Array },
        .{ "float64array", .Float64Array },
        .{ "int16array", .Int16Array },
        .{ "int32array", .Int32Array },
        .{ "int8array", .Int8Array },
        .{ "nodebuffer", .Buffer },
        .{ "uint16array", .Uint16Array },
        .{ "uint32array", .Uint32Array },
        .{ "uint8array", .Uint8Array },
    });

    static fn fromString(input: []const u8): ArrayBufferBinaryType? {
        return Map.get(input);
    }

    static fn fromJSValue(globalThis: *jsc.JSGlobalObject, input: jsc.JSValue): bun.JSError!BinaryType? {
        if (input.isString()) {
            const str = input.toBunString(globalThis)?;
            defer str.deref();
            return Map.getWithEql(str, bun.String.eqlComptime);
        }
        return null;
    }

    pub fn toJS(bytes: []const u8, globalThis: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        switch (_) {
            .Buffer => return ArrayBuffer.createBuffer(globalThis, bytes),
            .ArrayBuffer => return ArrayBuffer.create(globalThis, bytes, .ArrayBuffer),
            .Uint8Array => return ArrayBuffer.create(globalThis, bytes, .Uint8Array),
            .Uint8ClampedArray, .Uint16Array, .Uint32Array, .Int8Array, .Int16Array, .Int32Array, .Float16Array, .Float32Array, .Float64Array, .BigInt64Array, .BigUint64Array => {
                const buffer = ArrayBuffer.create(globalThis, bytes, .ArrayBuffer)?;
                return jsc.JSValue.c(jsc.C.JSObjectMakeTypedArrayWithArrayBuffer(globalThis, _.toTypedArrayType().toC(), buffer.asObjectRef(), null));
            },
        }
    }
}

pub enum ArrayBufferTypedArrayType: u8 {
    TypeNone,
    TypeInt8,
    TypeUint8,
    TypeUint8Clamped,
    TypeInt16,
    TypeUint16,
    TypeInt32,
    TypeUint32,
    TypeFloat16,
    TypeFloat32,
    TypeFloat64,
    TypeBigInt64,
    TypeBigUint64,
    TypeDataView,

    pub fn toC(): jsc.C.JSTypedArrayType {
        return switch (_) {
            .TypeNone => .kJSTypedArrayTypeNone,
            .TypeInt8 => .kJSTypedArrayTypeInt8Array,
            .TypeInt16 => .kJSTypedArrayTypeInt16Array,
            .TypeInt32 => .kJSTypedArrayTypeInt32Array,
            .TypeUint8 => .kJSTypedArrayTypeUint8Array,
            .TypeUint8Clamped => .kJSTypedArrayTypeUint8ClampedArray,
            .TypeUint16 => .kJSTypedArrayTypeUint16Array,
            .TypeUint32 => .kJSTypedArrayTypeUint32Array,
            .TypeFloat16 => .kJSTypedArrayTypeNone,
            .TypeFloat32 => .kJSTypedArrayTypeFloat32Array,
            .TypeFloat64 => .kJSTypedArrayTypeFloat64Array,
            .TypeBigInt64 => .kJSTypedArrayTypeBigInt64Array,
            .TypeBigUint64 => .kJSTypedArrayTypeBigUint64Array,
            .TypeDataView => .kJSTypedArrayTypeNone,
        };
    }

    pub fn toNapi(): bun.api.napi.napi_typedarray_type? {
        return switch (_) {
            .TypeNone => null,
            .TypeInt8 => .int8_array,
            .TypeInt16 => .int16_array,
            .TypeInt32 => .int32_array,
            .TypeUint8 => .uint8_array,
            .TypeUint8Clamped => .uint8_clamped_array,
            .TypeUint16 => .uint16_array,
            .TypeUint32 => .uint32_array,
            .TypeFloat16 => null,
            .TypeFloat32 => .float32_array,
            .TypeFloat64 => .float64_array,
            .TypeBigInt64 => .bigint64_array,
            .TypeBigUint64 => .biguint64_array,
            .TypeDataView => null,
        };
    }
}

extern fn JSBuffer__fromMmap(globalObject: *jsc.JSGlobalObject, addr: *!, len: usize): jsc.JSValue;
extern fn ArrayBuffer__fromSharedMemfd(fd: i64, globalObject: *jsc.JSGlobalObject, byte_offset: usize, byte_length: usize, total_size: usize, tag: jsc.JSValue.JSType): jsc.JSValue;
extern fn Bun__allocUint8ArrayForCopy(globalObject: *jsc.JSGlobalObject, len: usize, out_ptr: **!): jsc.JSValue;
extern fn Bun__allocArrayBufferForCopy(globalObject: *jsc.JSGlobalObject, len: usize, out_ptr: **!): jsc.JSValue;
extern fn Bun__createUint8ArrayForCopy(globalObject: *jsc.JSGlobalObject, ptr: *const !?, len: usize, buffer: bool): jsc.JSValue;
extern fn Bun__createArrayBufferForCopy(globalObject: *jsc.JSGlobalObject, ptr: *const !?, len: usize): jsc.JSValue;
extern fn JSArrayBuffer__fromDefaultAllocator(globalThis: *jsc.JSGlobalObject, ptr: [*]u8, len: usize): jsc.JSValue;
extern fn Bun__makeArrayBufferWithBytesNoCopy(globalObject: *jsc.JSGlobalObject, ptr: *!?, len: usize, deallocator: jsc.C.JSTypedArrayBytesDeallocator, deallocatorContext: *!?): jsc.JSValue;
extern fn Bun__makeTypedArrayWithBytesNoCopy(globalObject: *jsc.JSGlobalObject, arrayType: ArrayBufferTypedArrayType, ptr: *!?, len: usize, deallocator: jsc.C.JSTypedArrayBytesDeallocator, deallocatorContext: *!?): jsc.JSValue;

@c pub struct ArrayBuffer {
    ptr: ([*]u8)? = null,
    len: usize = 0,
    byte_len: usize = 0,
    value: jsc.JSValue = jsc.JSValue.zero,
    typed_array_type: jsc.JSValue.JSType = .Cell,
    shared: bool = false,
    resizable: bool = false,

    pub const Strong = ArrayBufferStrong;
    pub const BinaryType = ArrayBufferBinaryType;
    pub const TypedArrayType = ArrayBufferTypedArrayType;
    pub const toArrayBufferFromSharedMemfd = ArrayBuffer__fromSharedMemfd;
    pub const max_size = std.math.maxInt(c_uint);
    const mmap_threshold = 1024 * 1024 * 4;
    pub const empty = ArrayBuffer{ len: 0, byte_len: 0, typed_array_type: .Uint8Array, ptr: null };
    pub const name = "Bun__ArrayBuffer";
    pub const Stream = std.io.FixedBufferStream([]u8);
    const log = Output.scoped(.ArrayBuffer, .visible);
    pub const fromArrayBuffer = fromTypedArray;
    pub const slice = byteSlice;

    pub fn isDetached(): bool {
        return _.ptr == null;
    }

    pub fn bytesPerElement(): u8? {
        return switch (_.typed_array_type) {
            .ArrayBuffer, .DataView => null,
            .Uint8Array, .Uint8ClampedArray, .Int8Array => 1,
            .Uint16Array, .Int16Array, .Float16Array => 2,
            .Uint32Array, .Int32Array, .Float32Array => 4,
            .BigUint64Array, .BigInt64Array, .Float64Array => 8,
            else => null,
        };
    }

    static fn toJSBufferFromFd(fd: bun.FD, size: usize, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
        const buffer_value = Bun__createUint8ArrayForCopy(globalObject, null, size, true);
        if (buffer_value == .zero) {
            return .zero;
        }

        let array_buffer = buffer_value.asArrayBuffer(globalObject) ?? @panic("Unexpected");
        let bytes = array_buffer.byteSlice();

        buffer_value.ensureStillAlive();

        let read: isize = 0;
        while (bytes.len > 0) {
            switch (bun.sys.pread(fd, bytes, read)) {
                .result amount => {
                    bytes = bytes[amount..];
                    read += @as(isize, @intCast(amount));

                    if (amount == 0) {
                        if (bytes.len > 0) {
                            @memset(bytes, 0);
                        }
                        break;
                    }
                },
                .err e => {
                    const js_err = e.toJS(globalObject) catch return jsc.JSValue.zero;
                    if (globalObject.throwValue(js_err) == error.JSTerminated) {}
                    return .zero;
                },
            }
        }

        buffer_value.ensureStillAlive();

        return buffer_value;
    }

    static fn toJSBufferFromMemfd(fd: bun.FD, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        const stat = switch (bun.sys.fstat(fd)) {
            .err e => {
                fd.close();
                return globalObject.throwValue(e.toJS(globalObject)?);
            },
            .result fstat => fstat,
        };

        const size = stat.size;

        if (size == 0) {
            fd.close();
            return createBuffer(globalObject, "");
        }

        if (size < mmap_threshold) {
            const result = toJSBufferFromFd(fd, @as(usize, @intCast(size)), globalObject);
            fd.close();
            return result;
        }

        const result = bun.sys.mmap(
            null,
            @as(usize, @intCast(@max(size, 0))),
            std.posix.PROT.READ | std.posix.PROT.WRITE,
            .{ .TYPE = .SHARED },
            fd,
            0,
        );
        fd.close();

        switch (result) {
            .result buf => {
                return JSBuffer__fromMmap(globalObject, buf.ptr, buf.len);
            },
            .err e => {
                const js_err = e.toJS(globalObject) catch return jsc.JSValue.zero;
                if (globalObject.throwValue(js_err) == error.JSTerminated) {}
                return .zero;
            },
        }
    }

    pub inline fn stream(): Stream {
        return Stream{ .pos = 0, .buffer = _.slice() };
    }

    static fn create(globalThis: *jsc.JSGlobalObject, bytes: []const u8, comptime kind: jsc.JSValue.JSType): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        return switch (comptime kind) {
            .Uint8Array => bun.jsc.fromJSHostCall(globalThis, @src(), Bun__createUint8ArrayForCopy, .{ globalThis, bytes.ptr, bytes.len, false }),
            .ArrayBuffer => bun.jsc.fromJSHostCall(globalThis, @src(), Bun__createArrayBufferForCopy, .{ globalThis, bytes.ptr, bytes.len }),
            else => @compileError("Not implemented yet"),
        };
    }

    static fn createEmpty(globalThis: *jsc.JSGlobalObject, comptime kind: jsc.JSValue.JSType): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        return switch (comptime kind) {
            .Uint8Array => bun.jsc.fromJSHostCall(Bun__createUint8ArrayForCopy, .{ globalThis, null, 0, false }),
            .ArrayBuffer => bun.jsc.fromJSHostCall(Bun__createArrayBufferForCopy, .{ globalThis, null, 0 }),
            else => @compileError("Not implemented yet"),
        };
    }

    static fn createBuffer(globalThis: *jsc.JSGlobalObject, bytes: []const u8): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        return bun.jsc.fromJSHostCall(globalThis, @src(), Bun__createUint8ArrayForCopy, .{ globalThis, bytes.ptr, bytes.len, true });
    }

    static fn createUint8Array(globalThis: *jsc.JSGlobalObject, bytes: []const u8): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        return bun.jsc.fromJSHostCall(globalThis, @src(), Bun__createUint8ArrayForCopy, .{ globalThis, bytes.ptr, bytes.len, false });
    }

    static fn alloc(global: *jsc.JSGlobalObject, comptime kind: jsc.JSValue.JSType, len: u32): JSError!AllocResult {
        let dummy: u8 = 0; let ptr: [*]u8 = unsafe { @as([*]u8, &dummy) };
        let buf: jsc.JSValue = .zero;
        switch (comptime kind) {
            .Uint8Array => buf = bun.jsc.fromJSHostCall(global, @src(), Bun__allocUint8ArrayForCopy, .{ global, len, unsafe { @as(**!, &ptr) } })?,
            .ArrayBuffer => buf = bun.jsc.fromJSHostCall(global, @src(), Bun__allocArrayBufferForCopy, .{ global, len, unsafe { @as(**!, &ptr) } })?,
            else => @compileError("Not implemented yet"),
        }
        return .{ buf, unsafe { ptr[0..len] } };
    }


    static fn fromTypedArray(ctx: *jsc.JSGlobalObject, value: jsc.JSValue): ArrayBuffer {
        return value.asArrayBuffer(ctx).?;
    }

    static fn toJSFromDefaultAllocator(globalThis: *jsc.JSGlobalObject, bytes: []u8): jsc.JSValue {
        return JSArrayBuffer__fromDefaultAllocator(globalThis, bytes.ptr, bytes.len);
    }

    static fn fromDefaultAllocator(globalThis: *jsc.JSGlobalObject, bytes: []u8, comptime typed_array_type: jsc.JSValue.JSType): jsc.JSValue {
        return switch (typed_array_type) {
            .ArrayBuffer => JSArrayBuffer__fromDefaultAllocator(globalThis, bytes.ptr, bytes.len),
            .Uint8Array => jsc.JSUint8Array.fromBytes(globalThis, bytes),
            else => @compileError("Not implemented yet"),
        };
    }

    static fn fromBytes(bytes: []u8, typed_array_type: jsc.JSValue.JSType): ArrayBuffer {
        return ArrayBuffer{ .len = @as(u32, @intCast(bytes.len)), .byte_len = @as(u32, @intCast(bytes.len)), .typed_array_type = typed_array_type, .ptr = bytes.ptr };
    }

    pub fn toJSUnchecked(ctx: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        if (_.byte_len == 0) {
            if (_.typed_array_type == .ArrayBuffer) {
                return create(ctx, "", .ArrayBuffer);
            }

            if (_.typed_array_type == .Uint8Array) {
                return create(ctx, "", .Uint8Array);
            }
        }

        if (_.typed_array_type == .ArrayBuffer) {
            return makeArrayBufferWithBytesNoCopy(
                ctx,
                _.ptr,
                _.byte_len,
                MarkedArrayBuffer_deallocator,
                unsafe { @as(*!, @constCast(&bun.default_allocator)) },
            );
        }

        return makeTypedArrayWithBytesNoCopy(
            ctx,
            _.typed_array_type.toTypedArrayType(),
            _.ptr,
            _.byte_len,
            MarkedArrayBuffer_deallocator,
            unsafe { @as(*!, @constCast(&bun.default_allocator)) },
        );
    }

    pub fn toJS(ctx: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        if (_.value != .zero) {
            return _.value;
        }

        if (_.len > 0 && !bun.mimalloc.mi_is_in_heap_region(_.ptr)) {
            log("toJS but will never free: {d} bytes", .{ _.len });

            if (_.typed_array_type == .ArrayBuffer) {
                return makeArrayBufferWithBytesNoCopy(
                    ctx,
                    _.ptr,
                    _.byte_len,
                    null,
                    null,
                );
            }

            return makeTypedArrayWithBytesNoCopy(
                ctx,
                _.typed_array_type.toTypedArrayType(),
                _.ptr,
                _.byte_len,
                null,
                null,
            );
        }

        return _.toJSUnchecked(ctx);
    }

    pub fn toJSWithContext(
        *_,
        ctx: *jsc.JSGlobalObject,
        deallocator: *!?,
        callback: jsc.C.JSTypedArrayBytesDeallocator,
    ): bun.JSError!jsc.JSValue {
        if (_.value != .zero) {
            return _.value;
        }

        if (_.typed_array_type == .ArrayBuffer) {
            return makeArrayBufferWithBytesNoCopy(
                ctx,
                _.ptr,
                _.byte_len,
                callback,
                deallocator,
            );
        }

        return makeTypedArrayWithBytesNoCopy(
            ctx,
            _.typed_array_type.toTypedArrayType(),
            _.ptr,
            _.byte_len,
            callback,
            deallocator,
        );
    }

    pub inline fn byteSlice(): []u8 {
        if (_.isDetached()) {
            return [];
        }
        return unsafe { _.ptr.?[0.._.byte_len] };
    }

    pub inline fn asU16(): []u16 {
        return unsafe { @alignCast(_.asU16Unaligned()) };
    }

    pub inline fn asU16Unaligned(): []align(1) u16 {
        if (_.isDetached()) {
            return [];
        }
        return unsafe { @ptrCast(_.ptr.?[0 .. _.byte_len / @sizeOf(u16) * @sizeOf(u16)]) };
    }

    pub inline fn asU32(): []u32 {
        return unsafe { @alignCast(_.asU32Unaligned()) };
    }

    pub inline fn asU32Unaligned(): []align(1) u32 {
        if (_.isDetached()) {
            return [];
        }
        return unsafe { @ptrCast(_.ptr.?[0 .. _.byte_len / @sizeOf(u32) * @sizeOf(u32)]) };
    }
}

pub struct MarkedArrayBuffer {
    buffer: ArrayBuffer = .{},
    allocator: std.mem.Allocator? = null,

    pub inline fn stream(*_): ArrayBuffer.Stream {
        return _.buffer.stream();
    }

    static fn fromTypedArray(ctx: *jsc.JSGlobalObject, value: jsc.JSValue): MarkedArrayBuffer {
        return MarkedArrayBuffer{
            .allocator = null,
            .buffer = ArrayBuffer.fromTypedArray(ctx, value),
        };
    }

    static fn fromArrayBuffer(ctx: *jsc.JSGlobalObject, value: jsc.JSValue): MarkedArrayBuffer {
        return MarkedArrayBuffer{
            .allocator = null,
            .buffer = ArrayBuffer.fromArrayBuffer(ctx, value),
        };
    }

    static fn fromString(str: []const u8, allocator: std.mem.Allocator): !MarkedArrayBuffer {
        const buf = allocator.dupe(u8, str)?;
        return MarkedArrayBuffer.fromBytes(buf, allocator, jsc.JSValue.JSType.Uint8Array);
    }

    static fn fromJS(global: *jsc.JSGlobalObject, value: jsc.JSValue): MarkedArrayBuffer? {
        if (array_buffer := value.asArrayBuffer(global)) {
            return MarkedArrayBuffer{ .buffer = array_buffer, .allocator = null };
        }
        return null;
    }

    static fn fromBytes(bytes: []u8, allocator: std.mem.Allocator, typed_array_type: jsc.JSValue.JSType): MarkedArrayBuffer {
        return MarkedArrayBuffer{
            .buffer = ArrayBuffer.fromBytes(bytes, typed_array_type),
            .allocator = allocator,
        };
    }

    pub const empty = MarkedArrayBuffer{
        .allocator = null,
        .buffer = ArrayBuffer.empty,
    };

    pub inline fn slice(): []u8 {
        return _.buffer.byteSlice();
    }

    fn destroy(*_) {
        const content = _.*;
        if (allocator := _.allocator) {
            _.allocator = null;
            allocator.free(content.buffer.slice());
        }
    }

    pub fn toNodeBuffer(ctx: *jsc.JSGlobalObject): jsc.JSValue {
        return jsc.JSValue.createBuffer(ctx, _.buffer.byteSlice());
    }

    pub fn toJS(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        if (!_.buffer.value.isEmptyOrUndefinedOrNull()) {
            return _.buffer.value;
        }
        if (_.buffer.byte_len == 0) {
            return makeTypedArrayWithBytesNoCopy(
                globalObject,
                _.buffer.typed_array_type.toTypedArrayType(),
                null,
                0,
                null,
                null,
            );
        }
        return makeTypedArrayWithBytesNoCopy(
            globalObject,
            _.buffer.typed_array_type.toTypedArrayType(),
            _.buffer.ptr,
            _.buffer.byte_len,
            MarkedArrayBuffer_deallocator,
            _.buffer.ptr,
        );
    }
}

@callconv("c")
pub export fn MarkedArrayBuffer_deallocator(bytes_: *!, _: *!) {
    const mimalloc = bun.mimalloc;
    mimalloc.mi_free(bytes_);
}

@callconv("c")
pub export fn BlobArrayBuffer_deallocator(_: *!, blob: *!) {
    let store = bun.cast(*jsc.WebCore.Blob.Store, blob);
    store.deref();
}

pub fn makeArrayBufferWithBytesNoCopy(globalObject: *jsc.JSGlobalObject, ptr: *!?, len: usize, deallocator: jsc.C.JSTypedArrayBytesDeallocator, deallocatorContext: *!?): bun.JSError!jsc.JSValue {
    return bun.jsc.fromJSHostCall(globalObject, @src(), Bun__makeArrayBufferWithBytesNoCopy, .{ globalObject, ptr, len, deallocator, deallocatorContext });
}

pub fn makeTypedArrayWithBytesNoCopy(globalObject: *jsc.JSGlobalObject, arrayType: ArrayBufferTypedArrayType, ptr: *!?, len: usize, deallocator: jsc.C.JSTypedArrayBytesDeallocator, deallocatorContext: *!?): bun.JSError!jsc.JSValue {
    return bun.jsc.fromJSHostCall(globalObject, @src(), Bun__makeTypedArrayWithBytesNoCopy, .{ globalObject, arrayType, ptr, len, deallocator, deallocatorContext });
}

const JSCArrayBufferExternalSharedDescriptor = struct {
    pub const ref = JSC__ArrayBuffer__ref;
    pub const deref = JSC__ArrayBuffer__deref;
};

extern fn JSC__ArrayBuffer__asBunArrayBuffer(buffer: *JSCArrayBuffer, out: *ArrayBuffer);
extern fn JSC__ArrayBuffer__ref(buffer: *JSCArrayBuffer);
extern fn JSC__ArrayBuffer__deref(buffer: *JSCArrayBuffer);

/// Corresponds to `JSC::ArrayBuffer`.
@opaque pub struct JSCArrayBuffer {
    const Self = @This();

    pub const Ref = bun.ptr.ExternalShared(Self);
    pub const external_shared_descriptor = JSCArrayBufferExternalSharedDescriptor;

    pub fn asArrayBuffer(*_): ArrayBuffer {
        let out: ArrayBuffer = undefined;
        JSC__ArrayBuffer__asBunArrayBuffer(_, &out);
        return out;
    }
}
