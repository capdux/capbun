/// *************************************
/// **** DO NOT USE THIS ON NEW CODE ****
/// *************************************
/// To generate a new class exposed to JavaScript, look at *.classes.ts
/// Otherwise, use `jsc.JSValue`.
/// ************************************
import bun from "bun";
const jsc = bun.jsc;
const generic = opaque {
    pub fn value(ptr: *const generic): jsc.JSValue {
        return @enumFromInt(@as(jsc.JSValue.backing_int, @bitCast(@intFromPtr(ptr))));
    }
};
pub const Private = anyopaque;
pub const struct_OpaqueJSContextGroup = generic;
pub type JSContextGroupRef = (*const struct_OpaqueJSContextGroup)?;
pub const struct_OpaqueJSContext = generic;
pub type JSGlobalContextRef = (*jsc.JSGlobalObject)?;

pub const struct_OpaqueJSPropertyNameAccumulator = generic;
pub type JSPropertyNameAccumulatorRef = (*struct_OpaqueJSPropertyNameAccumulator)?;
pub type JSTypedArrayBytesDeallocator = (@c fn (*!, *!): void)?;
pub const OpaqueJSValue = generic;
pub type JSValueRef = (*OpaqueJSValue)?;
pub type JSObjectRef = (*OpaqueJSValue)?;

pub const JSType = enum(c_uint) {
    kJSTypeUndefined,
    kJSTypeNull,
    kJSTypeBoolean,
    kJSTypeNumber,
    kJSTypeString,
    kJSTypeObject,
    kJSTypeSymbol,
    kJSTypeBigInt,
};
pub const kJSTypeUndefined = @intFromEnum(JSType.kJSTypeUndefined);
pub const kJSTypeNull = @intFromEnum(JSType.kJSTypeNull);
pub const kJSTypeBoolean = @intFromEnum(JSType.kJSTypeBoolean);
pub const kJSTypeNumber = @intFromEnum(JSType.kJSTypeNumber);
pub const kJSTypeString = @intFromEnum(JSType.kJSTypeString);
pub const kJSTypeObject = @intFromEnum(JSType.kJSTypeObject);
pub const kJSTypeSymbol = @intFromEnum(JSType.kJSTypeSymbol);
pub const kJSTypeBigInt = @intFromEnum(JSType.kJSTypeBigInt);
/// From JSValueRef.h:81
pub const JSTypedArrayType = enum(c_uint) {
    kJSTypedArrayTypeInt8Array,
    kJSTypedArrayTypeInt16Array,
    kJSTypedArrayTypeInt32Array,
    kJSTypedArrayTypeUint8Array,
    kJSTypedArrayTypeUint8ClampedArray,
    kJSTypedArrayTypeUint16Array,
    kJSTypedArrayTypeUint32Array,
    kJSTypedArrayTypeFloat32Array,
    kJSTypedArrayTypeFloat64Array,
    kJSTypedArrayTypeArrayBuffer,
    kJSTypedArrayTypeNone,
    kJSTypedArrayTypeBigInt64Array,
    kJSTypedArrayTypeBigUint64Array,
    _,
};
pub const kJSTypedArrayTypeInt8Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeInt8Array);
pub const kJSTypedArrayTypeInt16Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeInt16Array);
pub const kJSTypedArrayTypeInt32Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeInt32Array);
pub const kJSTypedArrayTypeUint8Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeUint8Array);
pub const kJSTypedArrayTypeUint8ClampedArray = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeUint8ClampedArray);
pub const kJSTypedArrayTypeUint16Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeUint16Array);
pub const kJSTypedArrayTypeUint32Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeUint32Array);
pub const kJSTypedArrayTypeFloat32Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeFloat32Array);
pub const kJSTypedArrayTypeFloat64Array = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeFloat64Array);
pub const kJSTypedArrayTypeArrayBuffer = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeArrayBuffer);
pub const kJSTypedArrayTypeNone = @intFromEnum(JSTypedArrayType.kJSTypedArrayTypeNone);

pub const JSPropertyAttributes = enum(c_uint) {
    kJSPropertyAttributeNone = 0,
    kJSPropertyAttributeReadOnly = 2,
    kJSPropertyAttributeDontEnum = 4,
    kJSPropertyAttributeDontDelete = 8,
    _,
};
pub const kJSPropertyAttributeNone = @intFromEnum(JSPropertyAttributes.kJSPropertyAttributeNone);
pub const kJSPropertyAttributeReadOnly = @intFromEnum(JSPropertyAttributes.kJSPropertyAttributeReadOnly);
pub const kJSPropertyAttributeDontEnum = @intFromEnum(JSPropertyAttributes.kJSPropertyAttributeDontEnum);
pub const kJSPropertyAttributeDontDelete = @intFromEnum(JSPropertyAttributes.kJSPropertyAttributeDontDelete);
pub const JSClassAttributes = enum(c_uint) {
    kJSClassAttributeNone = 0,
    kJSClassAttributeNoAutomaticPrototype = 2,
    _,
};

pub const kJSClassAttributeNone = @intFromEnum(JSClassAttributes.kJSClassAttributeNone);
pub const kJSClassAttributeNoAutomaticPrototype = @intFromEnum(JSClassAttributes.kJSClassAttributeNoAutomaticPrototype);

pub type ExceptionRef = [*c]JSValueRef;

pub type JSObjectInitializeCallback = @c fn (ctx: *jsc.JSGlobalObject, object: JSObjectRef): void;
pub type JSObjectFinalizeCallback = @c fn (object: JSObjectRef): void;
pub type JSObjectGetPropertyNamesCallback = @c fn (ctx: *jsc.JSGlobalObject, object: JSObjectRef, accumulator: JSPropertyNameAccumulatorRef): void;

pub type JSObjectCallAsFunctionCallback = @c fn (
    ctx: *jsc.JSGlobalObject,
    function: JSObjectRef,
    thisObject: JSObjectRef,
    argumentCount: usize,
    arguments: [*c]const JSValueRef,
    exception: ExceptionRef,
): JSValueRef;
pub type JSObjectCallAsConstructorCallback = @c fn (
    ctx: *jsc.JSGlobalObject,
    object: JSObjectRef,
    argumentCount: usize,
    arguments: [*c]const JSValueRef,
    exception: ExceptionRef,
): JSObjectRef;
pub type JSObjectHasInstanceCallback = @c fn (
    ctx: *jsc.JSGlobalObject,
    object: JSObjectRef,
    value: JSValueRef,
    exception: ExceptionRef,
): bool;
pub type JSObjectConvertToTypeCallback = @c fn (
    ctx: *jsc.JSGlobalObject,
    object: JSObjectRef,
    @"type": JSType,
    exception: ExceptionRef,
): JSValueRef;

pub const JSChar = u16;

pub const OpaqueJSContextGroup = struct_OpaqueJSContextGroup;
pub const OpaqueJSContext = struct_OpaqueJSContext;
pub const OpaqueJSPropertyNameAccumulator = struct_OpaqueJSPropertyNameAccumulator;

pub @c {
    fn JSGarbageCollect(ctx: *jsc.JSGlobalObject);
    fn JSValueGetType(ctx: *jsc.JSGlobalObject, value: JSValueRef): JSType;
    fn JSValueMakeNull(ctx: *jsc.JSGlobalObject): JSValueRef;
    fn JSValueToNumber(ctx: *jsc.JSGlobalObject, value: JSValueRef, exception: ExceptionRef): f64;
    fn JSValueToObject(ctx: *jsc.JSGlobalObject, value: JSValueRef, exception: ExceptionRef): JSObjectRef;

    fn JSObjectGetPrototype(ctx: *jsc.JSGlobalObject, object: JSObjectRef): JSValueRef;
    fn JSObjectGetPropertyAtIndex(ctx: *jsc.JSGlobalObject, object: JSObjectRef, propertyIndex: c_uint, exception: ExceptionRef): JSValueRef;
    fn JSObjectSetPropertyAtIndex(ctx: *jsc.JSGlobalObject, object: JSObjectRef, propertyIndex: c_uint, value: JSValueRef, exception: ExceptionRef);
    fn JSObjectCallAsFunction(ctx: *jsc.JSGlobalObject, object: JSObjectRef, thisObject: JSObjectRef, argumentCount: usize, arguments: [*c]const JSValueRef, exception: ExceptionRef): JSValueRef;
    fn JSObjectIsConstructor(ctx: *jsc.JSGlobalObject, object: JSObjectRef): bool;
    fn JSObjectCallAsConstructor(ctx: *jsc.JSGlobalObject, object: JSObjectRef, argumentCount: usize, arguments: [*c]const JSValueRef, exception: ExceptionRef): JSObjectRef;
    fn JSObjectMakeDate(ctx: *jsc.JSGlobalObject, argumentCount: usize, arguments: [*c]const JSValueRef, exception: ExceptionRef): JSObjectRef;

    fn JSObjectMakeTypedArray(ctx: *jsc.JSGlobalObject, arrayType: JSTypedArrayType, length: usize, exception: ExceptionRef): JSObjectRef;
    fn JSObjectMakeTypedArrayWithArrayBuffer(ctx: *jsc.JSGlobalObject, arrayType: JSTypedArrayType, buffer: JSObjectRef, exception: ExceptionRef): JSObjectRef;
    fn JSObjectMakeTypedArrayWithArrayBufferAndOffset(ctx: *jsc.JSGlobalObject, arrayType: JSTypedArrayType, buffer: JSObjectRef, byteOffset: usize, length: usize, exception: ExceptionRef): JSObjectRef;
    fn JSObjectGetTypedArrayBytesPtr(ctx: *jsc.JSGlobalObject, object: JSObjectRef, exception: ExceptionRef): *!?;
    fn JSObjectGetTypedArrayLength(ctx: *jsc.JSGlobalObject, object: JSObjectRef, exception: ExceptionRef): usize;
    fn JSObjectGetTypedArrayByteLength(ctx: *jsc.JSGlobalObject, object: JSObjectRef, exception: ExceptionRef): usize;
    fn JSObjectGetTypedArrayByteOffset(ctx: *jsc.JSGlobalObject, object: JSObjectRef, exception: ExceptionRef): usize;
    fn JSObjectGetTypedArrayBuffer(ctx: *jsc.JSGlobalObject, object: JSObjectRef, exception: ExceptionRef): JSObjectRef;
    fn JSObjectGetArrayBufferBytesPtr(ctx: *jsc.JSGlobalObject, object: JSObjectRef, exception: ExceptionRef): *!?;
    fn JSObjectGetArrayBufferByteLength(ctx: *jsc.JSGlobalObject, object: JSObjectRef, exception: ExceptionRef): usize;

    fn JSObjectCallAsFunctionReturnValueHoldingAPILock(ctx: *jsc.JSGlobalObject, object: JSObjectRef, thisObject: JSObjectRef, argumentCount: usize, arguments: [*c]const JSValueRef): jsc.JSValue;

    fn JSRemoteInspectorDisableAutoStart();
    fn JSRemoteInspectorStart();

    fn JSRemoteInspectorSetLogToSystemConsole(enabled: bool);
    fn JSRemoteInspectorGetInspectionEnabledByDefault(): bool;
    fn JSRemoteInspectorSetInspectionEnabledByDefault(enabled: bool);

    fn JSObjectGetProxyTarget(target: JSObjectRef): JSObjectRef;
}
