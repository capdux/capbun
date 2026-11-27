#include "root.h"
#include "StrongRef.h"
#include "BunClientData.h"
#include "ZigGlobalObject.h"
#include <JavaScriptCore/AbstractSlotVisitorInlines.h>
#include <JavaScriptCore/JSCInlines.h>
#include <JavaScriptCore/SlotVisitorInlines.h>

namespace Bun {

namespace JSCastingHelpers = JSC::JSCastingHelpers;

const JSC::ClassInfo StrongRootBlock::s_info = {
    "StrongRootBlock"_s,
    nullptr,
    nullptr,
    nullptr,
    CREATE_METHOD_TABLE(StrongRootBlock)
};

template<typename, JSC::SubspaceAccess mode>
JSC::GCClient::IsoSubspace* StrongRootBlock::subspaceFor(JSC::VM& vm)
{
    if constexpr (mode == JSC::SubspaceAccess::Concurrently)
        return nullptr;
    return WebCore::subspaceForImpl<StrongRootBlock, WebCore::UseCustomHeapCellType::Yes>(
        vm,
        [](auto& spaces) { return spaces.m_clientSubspaceForStrongRootBlock.get(); },
        [](auto& spaces, auto&& space) { spaces.m_clientSubspaceForStrongRootBlock = std::forward<decltype(space)>(space); },
        [](auto& spaces) { return spaces.m_subspaceForStrongRootBlock.get(); },
        [](auto& spaces, auto&& space) { spaces.m_subspaceForStrongRootBlock = std::forward<decltype(space)>(space); },
        [](auto& server) -> JSC::HeapCellType& { return server.m_heapCellTypeForStrongRootBlock; });
}

template JSC::GCClient::IsoSubspace* StrongRootBlock::subspaceFor<StrongRootBlock, JSC::SubspaceAccess::OnMainThread>(JSC::VM&);

JSC::Structure* StrongRootBlock::createStructure(JSC::VM& vm)
{
    return JSC::Structure::create(vm, nullptr, JSC::jsNull(), JSC::TypeInfo(JSC::CellType, StructureFlags), info(), 0, 0);
}

StrongRootBlock* StrongRootBlock::create(JSC::VM& vm, JSC::Structure* structure, WebCore::JSVMClientData* clientData, StrongRootBlockNode* node)
{
    StrongRootBlock* block = new (NotNull, JSC::allocateCell<StrongRootBlock>(vm))
        StrongRootBlock(vm, structure, clientData, node);
    block->finishCreation(vm);
    return block;
}

StrongRootBlock::StrongRootBlock(JSC::VM& vm, JSC::Structure* structure, WebCore::JSVMClientData* clientData, StrongRootBlockNode* node)
    : Base(vm, structure)
    , m_node(node)
{
    node->clientData = clientData;
    node->slots = static_cast<Slot*>(WTF::fastMalloc(sizeof(Slot) * capacity));
    node->freeHead = 0;
    node->usedCount = 0;
    for (uint32_t i = 0; i < capacity; ++i) {
        node->slots[i].value = {};
        node->slots[i].node = node;
        node->slots[i].nextFree = (i + 1 < capacity) ? (i + 1) : invalidIndex;
        node->slots[i].index = i;
    }
}

StrongRootBlock::~StrongRootBlock()
{
    if (!m_node)
        return;
    StrongRootBlockNode* node = m_node;
    m_node = nullptr;
    node->block = nullptr;
    if (node->usedCount == 0) {
        if (node->prev)
            node->prev->next = node->next;
        else if (node->clientData)
            node->clientData->strongRootBlockHead = node->next;
        if (node->next)
            node->next->prev = node->prev;
        WTF::fastFree(node->slots);
        delete node;
    }
}

void StrongRootBlock::destroy(JSC::JSCell* cell)
{
    static_cast<StrongRootBlock*>(cell)->~StrongRootBlock();
}

StrongRootBlock::Slot* StrongRootBlock::allocateSlot()
{
    if (!m_node || m_node->freeHead == invalidIndex)
        return nullptr;
    uint32_t idx = m_node->freeHead;
    Slot* slot = &m_node->slots[idx];
    m_node->freeHead = slot->nextFree;
    slot->nextFree = invalidIndex;
    m_node->usedCount++;
    return slot;
}

void StrongRootBlock::freeSlot(Slot* slot)
{
    StrongRootBlockNode* node = m_node;
    slot->value = {};
    slot->nextFree = node->freeHead;
    node->freeHead = slot->index;
    ASSERT(node->usedCount > 0);
    node->usedCount--;
    if (node->usedCount == 0 && (node->prev != nullptr || node->next != nullptr)) {
        // Keep at most one spare empty block; unlink this one without touching sibling JSCells.
        if (node->prev)
            node->prev->next = node->next;
        else if (node->clientData)
            node->clientData->strongRootBlockHead = node->next;
        if (node->next)
            node->next->prev = node->prev;
        if (node->block)
            node->block->m_node = nullptr;
        WTF::fastFree(node->slots);
        delete node;
    }
}

void StrongRootBlock::addProtectedCounts(JSC::TypeCountSet& typeCounts, size_t& protectedCount) const
{
    if (!m_node || m_node->usedCount == 0)
        return;
    for (unsigned i = 0; i < capacity; ++i) {
        JSC::JSValue val = m_node->slots[i].value;
        if (!val.isEmpty() && val.isCell()) {
            JSC::JSCell* cell = val.asCell();
            if (cell) {
                if (const JSC::ClassInfo* ci = cell->classInfo())
                    typeCounts.add(ci->className);
                else
                    typeCounts.add("Unknown"_s);
                protectedCount++;
            }
        }
    }
}

template<typename Visitor>
void StrongRootBlock::visitChildrenImpl(JSC::JSCell* cell, Visitor& visitor)
{
    auto* thisObject = static_cast<StrongRootBlock*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);
    if (!thisObject->m_node || thisObject->m_node->usedCount == 0)
        return;
    for (unsigned i = 0; i < capacity; ++i) {
        JSC::JSValue val = thisObject->m_node->slots[i].value;
        if (!val.isEmpty())
            visitor.appendUnbarriered(val);
    }
}

DEFINE_VISIT_CHILDREN(StrongRootBlock);

class StrongRootBlockConstraint final : public JSC::MarkingConstraint {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(StrongRootBlockConstraint);

public:
    StrongRootBlockConstraint(WebCore::JSVMClientData& clientData)
        : MarkingConstraint("Srb", "StrongRootBlock", JSC::ConstraintVolatility::GreyedByExecution, JSC::ConstraintConcurrency::Sequential, JSC::ConstraintParallelism::Sequential)
        , m_clientData(clientData)
    {
    }

protected:
    void executeImpl(JSC::AbstractSlotVisitor& visitor) override { executeImplImpl(visitor); }
    void executeImpl(JSC::SlotVisitor& visitor) override { executeImplImpl(visitor); }

private:
    template<typename Visitor>
    void executeImplImpl(Visitor& visitor)
    {
        if (m_clientData.strongRootBlockStructure)
            visitor.appendUnbarriered(m_clientData.strongRootBlockStructure);
        for (StrongRootBlockNode* node = m_clientData.strongRootBlockHead; node; node = node->next) {
            if (node->block)
                visitor.appendUnbarriered(node->block);
        }
    }

    WebCore::JSVMClientData& m_clientData;
};

std::unique_ptr<JSC::MarkingConstraint> createStrongRootBlockConstraint(JSC::VM&, WebCore::JSVMClientData& clientData)
{
    return makeUnique<StrongRootBlockConstraint>(clientData);
}

void freeStrongRootBlockNodes(StrongRootBlockNode*& head)
{
    StrongRootBlockNode* node = head;
    head = nullptr;
    while (node) {
        StrongRootBlockNode* next = node->next;
        node->prev = nullptr;
        node->next = nullptr;
        node->clientData = nullptr;
        if (node->block == nullptr && node->usedCount == 0) {
            WTF::fastFree(node->slots);
            delete node;
        }
        node = next;
    }
}

void addStrongRootProtectedCounts(JSC::VM& vm, JSC::TypeCountSet& typeCounts, size_t& protectedCount)
{
    if (!vm.clientData)
        return;
    auto& clientData = *static_cast<WebCore::JSVMClientData*>(vm.clientData);
    for (StrongRootBlockNode* node = clientData.strongRootBlockHead; node; node = node->next) {
        if (node->block)
            node->block->addProtectedCounts(typeCounts, protectedCount);
    }
}

} // namespace Bun

extern "C" void Bun__StrongRef__delete(JSC::JSValue* _Nonnull handleSlot)
{
    auto* slot = reinterpret_cast<Bun::StrongRootBlock::Slot*>(handleSlot);
    Bun::StrongRootBlockNode* node = slot->node;
    if (node->block) {
        node->block->freeSlot(slot);
        return;
    }
    slot->value = {};
    slot->nextFree = node->freeHead;
    node->freeHead = slot->index;
    ASSERT(node->usedCount > 0);
    node->usedCount--;
    if (node->usedCount == 0) {
        if (node->clientData) {
            if (node->prev)
                node->prev->next = node->next;
            else
                node->clientData->strongRootBlockHead = node->next;
            if (node->next)
                node->next->prev = node->prev;
        }
        WTF::fastFree(node->slots);
        delete node;
    }
}

extern "C" JSC::JSValue* Bun__StrongRef__new(JSC::JSGlobalObject* globalObject, JSC::EncodedJSValue encodedValue)
{
    auto& vm = globalObject->vm();
    auto& clientData = *static_cast<WebCore::JSVMClientData*>(vm.clientData);

    Bun::StrongRootBlock* targetBlock = nullptr;
    for (auto* node = clientData.strongRootBlockHead; node; node = node->next) {
        if (node->block && node->block->hasFreeSlot()) {
            targetBlock = node->block;
            break;
        }
    }

    if (!targetBlock) {
        if (!clientData.strongRootBlockStructure)
            clientData.strongRootBlockStructure = Bun::StrongRootBlock::createStructure(vm);
        auto* node = new Bun::StrongRootBlockNode();
        targetBlock = Bun::StrongRootBlock::create(vm, clientData.strongRootBlockStructure, &clientData, node);
        node->block = targetBlock;
        node->next = clientData.strongRootBlockHead;
        if (clientData.strongRootBlockHead)
            clientData.strongRootBlockHead->prev = node;
        clientData.strongRootBlockHead = node;
    }

    auto* slot = targetBlock->allocateSlot();
    JSC::JSValue value = JSC::JSValue::decode(encodedValue);
    slot->value = value;
    if (value.isCell())
        vm.writeBarrier(targetBlock, value);
    return &slot->value;
}

extern "C" void Bun__StrongRef__set(JSC::JSValue* _Nonnull handleSlot, JSC::JSGlobalObject* globalObject, JSC::EncodedJSValue encodedValue)
{
    auto* slot = reinterpret_cast<Bun::StrongRootBlock::Slot*>(handleSlot);
    if (!slot->node->block)
        return;
    auto& vm = globalObject->vm();
    JSC::JSValue value = JSC::JSValue::decode(encodedValue);
    slot->value = value;
    if (value.isCell())
        vm.writeBarrier(slot->node->block, value);
}

extern "C" void Bun__StrongRef__clear(JSC::JSValue* _Nonnull handleSlot)
{
    auto* slot = reinterpret_cast<Bun::StrongRootBlock::Slot*>(handleSlot);
    if (!slot->node->block)
        return;
    slot->value = {};
}
