//! HTTP/2 stream state machine (RFC 9113 §5.1). Pure.

pub enum State: u8 {
    Idle = 1,
    Open = 2,
    ReservedLocal = 3,
    ReservedRemote = 4,
    HalfClosedLocal = 5,
    HalfClosedRemote = 6,
    Closed = 7,
}

pub enum Event {
    SendHeaders,
    RecvHeaders,
    SendHeadersEndStream,
    RecvHeadersEndStream,
    SendEndStream,
    RecvEndStream,
    SendPushPromise,
    RecvPushPromise,
    SendRst,
    RecvRst,
}

pub err TransitionError {
    Protocol,
    StreamClosed,
}

/// Apply `ev` to `state`, returning the new state per §5.1, or an error the caller raises.
pub fn transition(state: State, ev: Event): State ! TransitionError {
    return switch (state) {
        .Idle => switch (ev) {
            .SendHeaders, .RecvHeaders => .Open,
            .SendHeadersEndStream => .HalfClosedLocal,
            .RecvHeadersEndStream => .HalfClosedRemote,
            .SendPushPromise => .ReservedLocal,
            .RecvPushPromise => .ReservedRemote,
            else => error.Protocol,
        },
        .ReservedLocal => switch (ev) {
            .SendHeaders => .HalfClosedRemote,
            .SendHeadersEndStream => .Closed,
            .SendRst, .RecvRst => .Closed,
            else => error.Protocol,
        },
        .ReservedRemote => switch (ev) {
            .RecvHeaders => .HalfClosedLocal,
            .RecvHeadersEndStream => .Closed,
            .SendRst, .RecvRst => .Closed,
            else => error.Protocol,
        },
        .Open => switch (ev) {
            .SendEndStream, .SendHeadersEndStream => .HalfClosedLocal,
            .RecvEndStream, .RecvHeadersEndStream => .HalfClosedRemote,
            .SendRst, .RecvRst => .Closed,
            .SendHeaders, .RecvHeaders => .Open,
            else => error.Protocol,
        },
        .HalfClosedLocal => switch (ev) {
            .RecvEndStream, .RecvHeadersEndStream => .Closed,
            .RecvHeaders => .HalfClosedLocal,
            .SendRst, .RecvRst => .Closed,
            else => error.Protocol,
        },
        .HalfClosedRemote => switch (ev) {
            .SendEndStream, .SendHeadersEndStream => .Closed,
            .SendHeaders => .HalfClosedRemote,
            .SendRst, .RecvRst => .Closed,
            .RecvEndStream, .RecvHeaders, .RecvHeadersEndStream => error.StreamClosed,
            else => error.Protocol,
        },
        .Closed => switch (ev) {
            .SendRst, .RecvRst => .Closed,
            else => error.StreamClosed,
        },
    };
}

pub fn canReceiveData(state: State): bool {
    return state == .Open || state == .HalfClosedLocal;
}

pub fn canSendData(state: State): bool {
    return state == .Open || state == .HalfClosedRemote;
}
