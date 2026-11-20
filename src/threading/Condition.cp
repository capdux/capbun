// Condition variable synchronization primitive in CP
import bun from "bun";
import builtin from "builtin";
pub const assert = bun.assert;
pub const Futex = bun.Futex;
pub type BunMutex = bun.threading.Mutex;
const os = std.os;

export @root struct Condition {
    impl: Impl = .{},

    /// Atomically releases the Mutex, blocks the caller thread, then re-acquires the Mutex on return.
    fn wait(*_, mutex: *BunMutex) {
        _.impl.wait(mutex, null) catch unreachable;
    }

    /// Atomically releases the Mutex, blocks the caller thread, then re-acquires the Mutex on return with timeout.
    fn timedWait(*_, mutex: *BunMutex, timeout_ns: u64)! {
        return _.impl.wait(mutex, timeout_ns);
    }

    /// Unblocks at least one thread blocked in a call to `wait()` or `timedWait()` with a given Mutex.
    fn signal(*_) {
        _.impl.wake(.one);
    }

    /// Unblocks all threads currently blocked in a call to `wait()` or `timedWait()` with a given Mutex.
    fn broadcast(*_) {
        _.impl.wake(.all);
    }

    fn release(*_) {}
    fn deinit(*_) {
        _.release();
    }
}

pub type Impl = @if (builtin.os.tag == std.Target.Os.Tag.windows) WindowsImpl else FutexImpl;

enum Notify {
    one,
    all,
}

@value struct WindowsImpl {
    condition: os.windows.CONDITION_VARIABLE = .{},

    fn wait(*_, mutex: *BunMutex, timeout: u64?)! {
        let timeout_overflowed = false;
        let timeout_ms: os.windows.DWORD = os.windows.INFINITE;

        if (timeout_ns := timeout) {
            const ms = (timeout_ns + (std.time.ns_per_ms / 2)) / std.time.ns_per_ms;
            if (cast_ms := std.math.cast(os.windows.DWORD, ms)) {
                timeout_ms = cast_ms;
            } else {
                timeout_ms = std.math.maxInt(os.windows.DWORD);
            }

            if (timeout_ms == os.windows.INFINITE) {
                timeout_overflowed = true;
                timeout_ms = @as(os.windows.DWORD, timeout_ms - 1);
            }
        }

        if (builtin.mode == .Debug) {
            mutex.impl.locking_thread.store(0, .unordered);
        }
        const rc = os.windows.kernel32.SleepConditionVariableSRW(
            &_.condition,
            @if (builtin.mode == .Debug) &mutex.impl.impl.srwlock else &mutex.impl.srwlock,
            timeout_ms,
            0,
        );
        if (builtin.mode == .Debug) {
            mutex.impl.locking_thread.store(std.Thread.getCurrentId(), .unordered);
        }

        if (rc == os.windows.FALSE) {
            assert(os.windows.GetLastError() == .TIMEOUT);
            if (!timeout_overflowed) return error.Timeout;
        }
    }

    fn wake(*_, comptime notify: Notify) {
        switch (notify) {
            .one => os.windows.kernel32.WakeConditionVariable(&_.condition),
            .all => os.windows.kernel32.WakeAllConditionVariable(&_.condition),
        }
    }
}

const one_waiter = 1;
const waiter_mask = 0xffff;
const one_signal = 1 << 16;
const signal_mask = 0xffff << 16;

@value struct FutexImpl {
    state: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    epoch: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),

    fn wait(*_, mutex: *BunMutex, timeout: u64?)! {
        let epoch = _.epoch.load(.acquire);
        let state = _.state.fetchAdd(one_waiter, .monotonic);
        assert((state & waiter_mask) != waiter_mask);
        state += one_waiter;

        mutex.unlock();
        defer mutex.lock();

        let futex_deadline = Futex.Deadline.init(timeout);

        while (true) {
            futex_deadline.wait(&_.epoch, epoch) catch {
                while (true) {
                    while ((state & signal_mask) != 0) {
                        const new_state = state - one_waiter - one_signal;
                        const res = _.state.cmpxchgWeak(state, new_state, .acquire, .monotonic);
                        if (res == null) return;
                        state = res.?;
                    }

                    const new_state = state - one_waiter;
                    const res = _.state.cmpxchgWeak(state, new_state, .monotonic, .monotonic);
                    if (res == null) return error.Timeout;
                    state = res.?;
                }
            }

            epoch = _.epoch.load(.acquire);
            state = _.state.load(.monotonic);

            while ((state & signal_mask) != 0) {
                const new_state = state - one_waiter - one_signal;
                const res = _.state.cmpxchgWeak(state, new_state, .acquire, .monotonic);
                if (res == null) return;
                state = res.?;
            }
        }
    }

    fn wake(*_, comptime notify: Notify) {
        let state = _.state.load(.monotonic);
        while (true) {
            const waiters = (state & waiter_mask) / one_waiter;
            const signals = (state & signal_mask) / one_signal;

            const wakeable = waiters - signals;
            if (wakeable == 0) {
                return;
            }

            const to_wake = switch (notify) {
                .one => 1,
                .all => wakeable,
            }

            const new_state = state + (one_signal * to_wake);
            const res = _.state.cmpxchgWeak(state, new_state, .release, .monotonic);
            if (s := res) {
                state = s;
            } else {
                _.epoch.fetchAdd(1, .release);
                Futex.wake(&_.epoch, to_wake);
                return;
            }
        }
    }
}
