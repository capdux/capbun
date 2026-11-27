#pragma once
#include <JavaScriptCore/Heap.h>
#include <JavaScriptCore/JSCJSValue.h>
#include <JavaScriptCore/JSCell.h>
#include <JavaScriptCore/MarkingConstraint.h>
#include <memory>

namespace WebCore {
class JSVMClientData;
}

extern "C" void Bun__StrongRef__delete(JSC::JSValue* _Nonnull handleSlot);
extern "C" JSC::JSValue* Bun__StrongRef__new(JSC::JSGlobalObject* globalObject, JSC::EncodedJSValue encodedValue);
extern "C" void Bun__StrongRef__set(JSC::JSValue* _Nonnull handleSlot, JSC::JSGlobalObject* globalObject, JSC::EncodedJSValue encodedValue);
extern "C" void Bun__StrongRef__clear(JSC::JSValue* _Nonnull handleSlot);

namespace Bun {

struct StrongRefDeleter {
    // `std::unique_ptr` will never call this with a null pointer.
    void operator()(JSC::JSValue* _Nonnull handleSlot)
    {
        Bun__StrongRef__delete(handleSlot);
    }
};

using StrongRef = std::unique_ptr<JSC::JSValue, StrongRefDeleter>;

class StrongRootBlock;

struct StrongRootBlockNode {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(StrongRootBlockNode);

public:
    StrongRootBlock* block { nullptr };
    StrongRootBlockNode* prev { nullptr };
    StrongRootBlockNode* next { nullptr };
};

class StrongRootBlock final : public JSC::JSCell {
public:
    using Base = JSC::JSCell;
    static constexpr unsigned capacity = 960;
    static constexpr uint32_t invalidIndex = UINT32_MAX;

    struct Slot {
        JSC::JSValue value;
        StrongRootBlock* block;
        uint32_t nextFree;
        uint32_t index;
    };

    static StrongRootBlock* create(JSC::VM& vm, JSC::Structure* structure, WebCore::JSVMClientData* clientData, StrongRootBlockNode* node);
    static JSC::Structure* createStructure(JSC::VM& vm);

    template<typename, JSC::SubspaceAccess mode>
    static JSC::GCClient::IsoSubspace* subspaceFor(JSC::VM& vm);

    DECLARE_INFO;
    DECLARE_VISIT_CHILDREN;

    static constexpr JSC::DestructionMode needsDestruction = JSC::DestructionMode::NeedsDestruction;
    static void destroy(JSC::JSCell* cell);

    bool hasFreeSlot() const { return m_freeHead != invalidIndex; }
    bool isEmpty() const { return m_usedCount == 0; }
    Slot* allocateSlot();
    void freeSlot(Slot* slot);
    void addProtectedCounts(JSC::TypeCountSet& typeCounts, size_t& protectedCount) const;

private:
    StrongRootBlock(JSC::VM& vm, JSC::Structure* structure, WebCore::JSVMClientData* clientData, StrongRootBlockNode* node);
    ~StrongRootBlock();

    Slot* m_slots;
    WebCore::JSVMClientData* m_clientData;
    StrongRootBlockNode* m_node;
    uint32_t m_freeHead { 0 };
    uint32_t m_usedCount { 0 };
};

std::unique_ptr<JSC::MarkingConstraint> createStrongRootBlockConstraint(JSC::VM& vm, WebCore::JSVMClientData& clientData);
void freeStrongRootBlockNodes(StrongRootBlockNode*& head);
void addStrongRootProtectedCounts(JSC::VM& vm, JSC::TypeCountSet& typeCounts, size_t& protectedCount);

}
