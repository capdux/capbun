#include "root.h"
#include "headers-handwritten.h"
#include "ExceptionOr.h"
#include "MessagePort.h"
#include "SerializedScriptValue.h"
#include "JSDOMExceptionHandling.h"
#include "ZigGlobalObject.h"
#include "JSBuffer.h"
#include <JavaScriptCore/JSArray.h>
#include <JavaScriptCore/JSTypedArrays.h>
#include <JavaScriptCore/PropertyNameArray.h>
#include <wtf/HashSet.h>

using namespace JSC;
using namespace WebCore;

// Must be synced with bindings.zig's JSValue.SerializedScriptValue.External
struct SerializedValueSlice {
    const uint8_t* bytes;
    size_t size;
    WebCore::SerializedScriptValue* value; // NOLINT
};

enum class SerializedFlags : uint8_t {
    None = 0,
    ForCrossProcessTransfer = 1 << 0,
    ForStorage = 1 << 1,
};

/// Returns a "slice" that also contains a pointer to the SerializedScriptValue. Must be freed by the caller
extern "C" SerializedValueSlice Bun__serializeJSValue(JSGlobalObject* globalObject, EncodedJSValue encodedValue, const SerializedFlags flags)
{
    auto& vm = JSC::getVM(globalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSValue value = JSValue::decode(encodedValue);

    Vector<JSC::Strong<JSC::JSObject>> transferList;
    Vector<RefPtr<MessagePort>> dummyPorts;
    auto forStorage = (static_cast<uint8_t>(flags) & static_cast<uint8_t>(SerializedFlags::ForStorage)) ? SerializationForStorage::Yes : SerializationForStorage::No;
    auto context = SerializationContext::Default;
    auto forTransferEnum = (static_cast<uint8_t>(flags) & static_cast<uint8_t>(SerializedFlags::ForCrossProcessTransfer)) ? SerializationForCrossProcessTransfer::Yes : SerializationForCrossProcessTransfer::No;
    ExceptionOr<Ref<SerializedScriptValue>> serialized = SerializedScriptValue::create(*globalObject, value, WTF::move(transferList), dummyPorts, forStorage, context, forTransferEnum);

    EXCEPTION_ASSERT(!!scope.exception() == serialized.hasException());
    if (serialized.hasException()) {
        WebCore::propagateException(*globalObject, scope, serialized.releaseException());
        RELEASE_AND_RETURN(scope, { 0 });
    }

    auto serializedValue = serialized.releaseReturnValue();

    const Vector<uint8_t>& bytes = serializedValue->wireBytes();

    return {
        bytes.begin(),
        bytes.size(),
        &serializedValue.leakRef(),
    };
}

extern "C" void Bun__SerializedScriptSlice__free(SerializedScriptValue* value)
{
    // Use deref() instead of delete to properly handle CHECK_REF_COUNTED_LIFECYCLE.
    // The value was leaked via leakRef() which leaves refcount at 1, so deref() will delete it.
    value->deref();
}

extern "C" EncodedJSValue Bun__JSValue__deserialize(JSGlobalObject* globalObject, const uint8_t* bytes, size_t size)
{
    Vector<uint8_t> vector(std::span { bytes, size });
    /// ?! did i just give ownership of these bytes to JSC?
    auto scriptValue = SerializedScriptValue::createFromWireBytes(WTF::move(vector));
    return JSValue::encode(scriptValue->deserialize(*globalObject, globalObject));
}

namespace {

void collectUint8ArrayBufferFlags(
    JSGlobalObject* globalObject,
    JSValue val,
    WTF::HashSet<JSObject*>& visited,
    Vector<uint8_t>& flags,
    bool& hasBuffer)
{
    if (!val.isObject())
        return;
    JSObject* obj = asObject(val);
    if (!visited.add(obj).isNewEntry)
        return;

    if (dynamicDowncast<JSUint8Array>(obj)) {
        bool isBuf = JSBuffer__isBuffer(globalObject, JSValue::encode(val));
        if (isBuf)
            hasBuffer = true;
        flags.append(isBuf ? 1 : 0);
        return;
    }

    auto& vm = JSC::getVM(globalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (auto* arr = dynamicDowncast<JSArray>(obj)) {
        unsigned len = arr->length();
        for (unsigned i = 0; i < len; ++i) {
            JSValue elem = arr->getIndex(globalObject, i);
            if (scope.exception()) {
                (void)scope.tryClearException();
                return;
            }
            collectUint8ArrayBufferFlags(globalObject, elem, visited, flags, hasBuffer);
        }
        return;
    }

    PropertyNameArrayBuilder names(vm, PropertyNameMode::Strings, PrivateSymbolMode::Exclude);
    JSObject::getOwnPropertyNames(obj, globalObject, names, DontEnumPropertiesMode::Exclude);
    if (scope.exception()) {
        (void)scope.tryClearException();
        return;
    }
    for (const auto& name : names) {
        JSValue prop = obj->get(globalObject, name);
        if (scope.exception()) {
            (void)scope.tryClearException();
            return;
        }
        collectUint8ArrayBufferFlags(globalObject, prop, visited, flags, hasBuffer);
    }
}

void restoreUint8ArrayBuffers(
    JSGlobalObject* globalObject,
    JSValue val,
    WTF::HashSet<JSObject*>& visited,
    const uint8_t* flags,
    size_t flagsLen,
    size_t& idx,
    JSValue bufferProto)
{
    if (!val.isObject() || idx >= flagsLen)
        return;
    JSObject* obj = asObject(val);
    if (!visited.add(obj).isNewEntry)
        return;

    auto& vm = JSC::getVM(globalObject);

    if (auto* u8 = dynamicDowncast<JSUint8Array>(obj)) {
        if (flags[idx++] != 0)
            u8->setPrototypeDirect(vm, bufferProto);
        return;
    }

    auto scope = DECLARE_THROW_SCOPE(vm);

    if (auto* arr = dynamicDowncast<JSArray>(obj)) {
        unsigned len = arr->length();
        for (unsigned i = 0; i < len; ++i) {
            JSValue elem = arr->getIndex(globalObject, i);
            if (scope.exception()) {
                (void)scope.tryClearException();
                return;
            }
            restoreUint8ArrayBuffers(globalObject, elem, visited, flags, flagsLen, idx, bufferProto);
        }
        return;
    }

    PropertyNameArrayBuilder names(vm, PropertyNameMode::Strings, PrivateSymbolMode::Exclude);
    JSObject::getOwnPropertyNames(obj, globalObject, names, DontEnumPropertiesMode::Exclude);
    if (scope.exception()) {
        (void)scope.tryClearException();
        return;
    }
    for (const auto& name : names) {
        JSValue prop = obj->get(globalObject, name);
        if (scope.exception()) {
            (void)scope.tryClearException();
            return;
        }
        restoreUint8ArrayBuffers(globalObject, prop, visited, flags, flagsLen, idx, bufferProto);
    }
}

} // namespace

extern "C" EncodedJSValue Bun__wrapIPCBufferEnvelope(JSGlobalObject* globalObject, EncodedJSValue encodedValue)
{
    JSValue value = JSValue::decode(encodedValue);
    WTF::HashSet<JSObject*> visited;
    Vector<uint8_t> flags;
    bool hasBuffer = false;
    collectUint8ArrayBufferFlags(globalObject, value, visited, flags, hasBuffer);
    if (!hasBuffer)
        return JSValue::encode(jsNull());

    auto* zigGlobal = uncheckedDowncast<Zig::GlobalObject>(globalObject);
    JSUint8Array* flagsArr = JSUint8Array::create(globalObject, zigGlobal->typedArrayStructure(TypeUint8, false), flags.size());
    if (!flagsArr)
        return JSValue::encode(jsNull());
    memcpy(flagsArr->typedVector(), flags.begin(), flags.size());

    MarkedArgumentBuffer args;
    args.append(value);
    args.append(flagsArr);
    JSArray* envelope = constructArray(globalObject, static_cast<ArrayAllocationProfile*>(nullptr), args);
    return JSValue::encode(envelope);
}

extern "C" EncodedJSValue Bun__unwrapIPCBufferEnvelope(JSGlobalObject* globalObject, EncodedJSValue encodedEnvelope)
{
    JSValue envVal = JSValue::decode(encodedEnvelope);
    auto* arr = dynamicDowncast<JSArray>(envVal);
    if (!arr || arr->length() < 2)
        return encodedEnvelope;

    auto& vm = JSC::getVM(globalObject);
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSValue msg = arr->getIndex(globalObject, 0);
    if (scope.exception()) {
        (void)scope.tryClearException();
        return encodedEnvelope;
    }
    JSValue flagsVal = arr->getIndex(globalObject, 1);
    if (scope.exception()) {
        (void)scope.tryClearException();
        return JSValue::encode(msg);
    }
    auto* flagsArr = dynamicDowncast<JSUint8Array>(flagsVal);
    if (!flagsArr)
        return JSValue::encode(msg);

    auto* zigGlobal = uncheckedDowncast<Zig::GlobalObject>(globalObject);
    JSValue bufferProto = zigGlobal->JSBufferPrototype();
    WTF::HashSet<JSObject*> visited;
    size_t idx = 0;
    restoreUint8ArrayBuffers(globalObject, msg, visited, flagsArr->typedVector(), flagsArr->byteLength(), idx, bufferProto);
    return JSValue::encode(msg);
}
