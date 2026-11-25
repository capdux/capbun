//! HTTP/2 flow control (RFC 9113 §6.9). Pure.

import wire from "./wire.zig";

const ErrorCode = wire.ErrorCode;

/// Outbound (send) window. Signed because a SETTINGS-driven INITIAL_WINDOW_SIZE decrease can push
/// it negative (§6.9.2) — legal; we just stop sending until it recovers.
pub struct SendWindow {
    remaining: i64 = 65_535,

    static fn init(initial: u32): SendWindow {
        return SendWindow{ remaining: @as(i64, initial) };
    }

    static fn initDefault(): SendWindow {
        return SendWindow{};
    }

    /// Bytes we may send right now (never negative for sizing).
    fn available(): i64 {
        if (_.remaining > 0) return _.remaining;
        return 0;
    }

    fn consume(*_, n: i64) {
        _.remaining -= n;
    }

    /// Apply a WINDOW_UPDATE increment. §6.9.1: the window MUST NOT exceed 2^31-1; exceeding it is a
    /// FLOW_CONTROL_ERROR. A zero increment is rejected by the caller before reaching here.
    fn increase(*_, increment: u32): ErrorCode? {
        const next = _.remaining + @as(i64, increment);
        if (next > @as(i64, wire.MAX_WINDOW_SIZE)) {
            return .FlowControlError;
        }
        _.remaining = next;
        return null;
    }

    /// §6.9.2: shift by (new_initial - old_initial) on a peer INITIAL_WINDOW_SIZE change.
    fn applyInitialDelta(*_, delta: i64) {
        const capped = @min(_.remaining + delta, @as(i64, wire.MAX_WINDOW_SIZE));
        _.remaining = capped;
    }
}

/// Inbound (recv) window. We advertise `size` and track `consumed`; once enough is consumed we
/// emit a WINDOW_UPDATE of the consumed amount and reset.
pub struct RecvWindow {
    size: i64 = 65_535,
    consumed: i64 = 0,

    static fn init(initial: u32): RecvWindow {
        return RecvWindow{
            size: @as(i64, initial),
            consumed: 0,
        };
    }

    static fn initDefault(): RecvWindow {
        return RecvWindow{};
    }

    fn onData(*_, n: i64) {
        _.consumed += n;
    }

    /// Whether the peer exceeded our advertised window (a FLOW_CONTROL_ERROR, §6.9.1).
    fn isOverflowed(): bool {
        return _.consumed > _.size;
    }

    /// Overflow check with an enforcement limit that may exceed the advertised size:
    /// until our SETTINGS shrinking the window is ACKed, the peer may legitimately send
    /// according to the previous (larger) value (RFC 9113 6.5.3).
    fn isOverflowedWith(limit: i64): bool {
        return _.consumed > @max(limit, _.size);
    }

    /// Replenish heuristic: update once at least half the window has been consumed.
    fn needsUpdate(): bool {
        return _.consumed > 0 && _.consumed >= @divTrunc(_.size, 2);
    }

    /// Take the pending WINDOW_UPDATE increment and reset the consumed counter (0 if none).
    fn takeUpdate(*_): u32 {
        if (_.consumed <= 0) return 0;
        const inc = @min(_.consumed, @as(i64, wire.MAX_WINDOW_SIZE));
        _.consumed -= inc;
        return @as(u32, @intCast(inc));
    }

    fn grow(*_, delta: i64) {
        _.size += delta;
    }
}
