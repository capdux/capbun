#include "JSKeyObject.h"
#include "JSKeyObjectPrototype.h"
#include "JSKeyObjectConstructor.h"
#include "DOMIsoSubspaces.h"
#include "ZigGlobalObject.h"
#include "ErrorCode.h"
#include <JavaScriptCore/JSCJSValueInlines.h>
#include <JavaScriptCore/VMTrapsInlines.h>
#include <JavaScriptCore/LazyClassStructureInlines.h>
#include <JavaScriptCore/FunctionPrototype.h>
#include <JavaScriptCore/ObjectPrototype.h>

namespace Bun {

const JSC::ClassInfo JSKeyObject::s_info = { "KeyObject"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSKeyObject) };

void JSKeyObject::finishCreation(JSC::VM& vm, JSC::JSGlobalObject* globalObject)
{
    Base::finishCreation(vm);
    if (m_handle.data()) {
        if (m_handle.type() == WebCore::CryptoKeyType::Secret) {
            m_sizeForGC = m_handle.symmetricKey().size();
        } else if (m_handle.asymmetricKey()) {
            int pkeySize = EVP_PKEY_size(m_handle.asymmetricKey().get());
            if (pkeySize > 0)
                m_sizeForGC = static_cast<size_t>(pkeySize);
        }
        if (m_sizeForGC > 0)
            vm.heap.reportExtraMemoryAllocated(this, m_sizeForGC);
    }
}

template<typename Visitor>
void JSKeyObject::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    JSKeyObject* thisObject = uncheckedDowncast<JSKeyObject>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);
    if (thisObject->m_sizeForGC > 0)
        visitor.reportExtraMemoryVisited(thisObject->m_sizeForGC);
}

DEFINE_VISIT_CHILDREN(JSKeyObject);

void setupKeyObjectClassStructure(JSC::LazyClassStructure::Initializer& init)
{
    auto* prototypeStructure = JSKeyObjectPrototype::createStructure(init.vm, init.global, init.global->objectPrototype());
    auto* prototype = JSKeyObjectPrototype::create(init.vm, init.global, prototypeStructure);

    auto* constructorStructure = JSKeyObjectConstructor::createStructure(init.vm, init.global, init.global->functionPrototype());
    auto* constructor = JSKeyObjectConstructor::create(init.vm, init.global, constructorStructure, prototype);

    auto* structure = JSKeyObject::createStructure(init.vm, init.global, prototype);
    init.setPrototype(prototype);
    init.setStructure(structure);
    init.setConstructor(constructor);
}

} // namespace Bun
