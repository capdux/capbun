import { Socket } from "./Socket.cp";

@c {
    fn us_quic_pending_connect_addrinfo(pc: *PendingConnect): *!;
    fn us_quic_pending_connect_resolved(pc: *PendingConnect): *Socket?;
    fn us_quic_pending_connect_cancel(pc: *PendingConnect);
}

export @opaque class PendingConnect {
    pub fn addrinfo(*_): *! {
        return us_quic_pending_connect_addrinfo(_);
    }
    pub fn resolved(*_): *Socket? {
        return us_quic_pending_connect_resolved(_);
    }
    pub fn cancel(*_) {
        us_quic_pending_connect_cancel(_);
    }
}
