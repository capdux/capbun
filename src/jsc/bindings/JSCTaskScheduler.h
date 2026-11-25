#pragma once

namespace WebCore {
class JSVMClientData;
}

#include <JavaScriptCore/DeferredWorkTimer.h>

namespace Bun {

class JSCTaskScheduler {
public:
    JSCTaskScheduler()
        : m_pendingTicketsKeepingEventLoopAlive()
        , m_pendingTicketsOther()
    {
    }

    static void onAddPendingWork(WebCore::JSVMClientData* clientData, Ref<JSC::DeferredWorkTimer::TicketData>&& ticket, JSC::DeferredWorkTimer::WorkType kind);
    static void onScheduleWorkSoon(WebCore::JSVMClientData* clientData, JSC::DeferredWorkTimer::Ticket ticket, JSC::DeferredWorkTimer::Task&& task);
    static void onCancelPendingWork(WebCore::JSVMClientData* clientData, JSC::DeferredWorkTimer::Ticket ticket);

    // After marking, before the sweep. Only its realm marks a ticket's cells, so the sweep frees
    // those of a realm that died. DeferredWorkTimer::cancelPendingWork(VM&) cancels such tickets
    // in the set it keeps, which is empty here: onAddPendingWork takes them.
    static void cancelWorkOfDeadRealms(WebCore::JSVMClientData* clientData, JSC::VM&);

public:
    Lock m_lock;
    UncheckedKeyHashSet<Ref<JSC::DeferredWorkTimer::TicketData>> m_pendingTicketsKeepingEventLoopAlive;
    UncheckedKeyHashSet<Ref<JSC::DeferredWorkTimer::TicketData>> m_pendingTicketsOther;
};

}
