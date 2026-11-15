// src/http/h3_client/PendingConnect.cp
// DNS-pending QUIC connect in CP language

import bun from "bun";
const uws = bun.uws;
const quic = uws.quic;

import { ClientSession } from "./ClientSession.cp";

let resolved_mutex: bun.Mutex = {};
let resolved_head: (*PendingConnect)? = null;

export @root struct PendingConnect {
    session: *ClientSession,
    pc: *quic.PendingConnect,
    loop_ptr: *uws.Loop,
    next: (*PendingConnect)? = null,

    static fn register(session: *ClientSession, pc: *quic.PendingConnect, l: *uws.Loop) {
        session.ref();
        const item = bun.new(PendingConnect, { session: session, pc: pc, loop_ptr: l });
        bun.dns.internal.registerQuic(unsafe { @ptrCast(@alignCast(pc.addrinfo())) }, item);
    }

    fn loop(): *uws.Loop {
        return _.loop_ptr;
    }

    fn onDNSResolved(*_) {
        defer {
            _.session.deref();
            bun.destroy(_);
        }
        if (_.session.closed || _.session.pending.items.len == 0) {
            _.pc.cancel();
            if (!_.session.closed) {
                failSession(_.session, error.Aborted);
            }
            return;
        }
        if (qs := _.pc.resolved()) {
            _.session.qsocket = qs;
            qs.ext(ClientSession).* = _.session;
        } else {
            failSession(_.session, error.DNSResolutionFailed);
            return;
        }
    }

    fn onDNSResolvedThreadsafe(*_) {
        resolved_mutex.lock();
        _.next = resolved_head;
        resolved_head = _;
        resolved_mutex.unlock();
        _.loop_ptr.wakeup();
    }

    static fn drainResolved() {
        resolved_mutex.lock();
        let head = resolved_head;
        resolved_head = null;
        resolved_mutex.unlock();
        while (pc := head) {
            const next = pc.next;
            pc.onDNSResolved();
            head = next;
        }
    }

    static fn failSession(session: *ClientSession, e: anyerror) {
        session.closed = true;
        if (ctx := bun.http.H3.ClientContext.get()) {
            ctx.unregister(session);
        }
        while (session.pending.items.len > 0) {
            const stream = session.pending.items[0];
            const cl = stream.client;
            session.detach(stream);
            if (cl_ := cl) {
                cl_.failFromH2(e);
            }
        }
        * = bun.http.H3.live_sessions.fetchSub(1, .monotonic);
        session.deref();
    }
}
