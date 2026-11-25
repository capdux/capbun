#include "config.h"
#include <JavaScriptCore/VM.h>
#include <JavaScriptCore/HeapInlines.h>
#include "JSCTaskScheduler.h"
#include "BunClientData.h"

using Ticket = JSC::DeferredWorkTimer::Ticket;
using Task = JSC::DeferredWorkTimer::Task;
using TicketData = JSC::DeferredWorkTimer::TicketData;

namespace Bun {
using namespace JSC;

extern "C" void Bun__queueJSCDeferredWorkTaskConcurrently(void* bunVM, void* task);
extern "C" void Bun__eventLoop__incrementRefConcurrently(void* bunVM, int delta);

class JSCDeferredWorkTask {
public:
    JSCDeferredWorkTask(WebCore::JSVMClientData* clientData, Ref<TicketData> ticket, Task&& task)
        : clientData(clientData)
        , ticket(WTF::move(ticket))
        , task(WTF::move(task))
    {
    }

    // Not reached through the ticket: a cancelled one's cells may be freed.
    WebCore::JSVMClientData* clientData;
    Ref<TicketData> ticket;
    Task task;
    ~JSCDeferredWorkTask()
    {
    }

    WTF_MAKE_TZONE_ALLOCATED(JSCDeferredWorkTask);
};

static JSC::VM& getVM(Ticket& ticket)
{
    return ticket->scriptExecutionOwner()->vm();
}

void JSCTaskScheduler::onAddPendingWork(WebCore::JSVMClientData* clientData, Ref<TicketData>&& ticket, JSC::DeferredWorkTimer::WorkType kind)
{
    auto& scheduler = clientData->deferredWorkTimer;
    Locker<Lock> holder { scheduler.m_lock };
    if (kind == DeferredWorkTimer::WorkType::ImminentlyScheduled) {
        Bun__eventLoop__incrementRefConcurrently(clientData->bunVM, 1);
        scheduler.m_pendingTicketsKeepingEventLoopAlive.add(WTF::move(ticket));
    } else {
        scheduler.m_pendingTicketsOther.add(WTF::move(ticket));
    }
}
void JSCTaskScheduler::onScheduleWorkSoon(WebCore::JSVMClientData* clientData, Ticket ticket, Task&& task)
{
    auto* job = new JSCDeferredWorkTask(clientData, *ticket, WTF::move(task));
    Bun__queueJSCDeferredWorkTaskConcurrently(clientData->bunVM, job);
}

void JSCTaskScheduler::onCancelPendingWork(WebCore::JSVMClientData* clientData, Ticket ticket)
{
    auto* bunVM = clientData->bunVM;
    auto& scheduler = clientData->deferredWorkTimer;

    Locker<Lock> holder { scheduler.m_lock };
    bool isKeepingEventLoopAlive = scheduler.m_pendingTicketsKeepingEventLoopAlive.removeIf([ticket](auto pendingTicket) {
        return pendingTicket.ptr() == ticket;
    });
    // -- At this point, ticket may be an invalid pointer.

    if (isKeepingEventLoopAlive) {
        holder.unlockEarly();
        Bun__eventLoop__incrementRefConcurrently(bunVM, -1);
    } else {
        scheduler.m_pendingTicketsOther.removeIf([ticket](auto pendingTicket) {
            return pendingTicket.ptr() == ticket;
        });
    }
}

void JSCTaskScheduler::cancelWorkOfDeadRealms(WebCore::JSVMClientData* clientData, JSC::VM& vm)
{
    auto& scheduler = clientData->deferredWorkTimer;
    UncheckedKeyHashSet<JSGlobalObject*> deadRealms;
    {
        Locker<Lock> holder { scheduler.m_lock };
        auto collect = [&](auto& pendingTickets) {
            for (auto& ticket : pendingTickets) {
                if (ticket->isCancelled())
                    continue;
                auto* realm = ticket->target()->globalObject();
                if (!vm.heap.isMarked(realm))
                    deadRealms.add(realm);
            }
        };
        collect(scheduler.m_pendingTicketsKeepingEventLoopAlive);
        collect(scheduler.m_pendingTicketsOther);
    }
    // What ~JSGlobalObject does, too late for a global that is swept lazily. Reaches
    // onCancelPendingWork, which takes m_lock.
    for (auto* realm : deadRealms)
        realm->clearWeakTickets();
}

static void runPendingWork(void* bunVM, Bun::JSCTaskScheduler& scheduler, JSCDeferredWorkTask* job)
{
    Locker<Lock> holder { scheduler.m_lock };
    auto pendingTicket = scheduler.m_pendingTicketsKeepingEventLoopAlive.take(job->ticket);
    if (!pendingTicket) {
        pendingTicket = scheduler.m_pendingTicketsOther.take(job->ticket);
    } else {
        Bun__eventLoop__incrementRefConcurrently(bunVM, -1);
    }
    holder.unlockEarly();

    if (pendingTicket && !pendingTicket->isCancelled()) {
        job->task(job->ticket.ptr());
    }

    delete job;
}

extern "C" void Bun__runDeferredWork(Bun::JSCDeferredWorkTask* job)
{
    runPendingWork(job->clientData->bunVM, job->clientData->deferredWorkTimer, job);
}

}
