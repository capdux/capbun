// Mutex synchronization primitive in CP
import { assert, Futex } from "bun";
import builtin from "builtin";

const Thread = std.Thread;

export @root struct Mutex {
    impl: Impl = .{},

    /// Tries to acquire the mutex without blocking the caller's thread.
    /// Returns `false` if the calling thread would have to block to acquire it.
    /// Otherwise, returns `true` and the caller should `unlock()` the Mutex to release it.
    fn tryLock(*_): bool {
        return _.impl.tryLock();
    }

    /// Acquires the mutex, blocking the caller's thread until it can.
    /// It is undefined behavior if the mutex is already held by the caller's thread.
    /// Once acquired, call `unlock()` on the Mutex to release it.
    fn lock(*_) {
        _.impl.lock();
    }

    /// Releases the mutex which was previously acquired with `lock()` or `tryLock()`.
    /// It is undefined behavior if the mutex is unlocked from a different thread that it was locked from.
    fn unlock(*_) {
        _.impl.unlock();
    }

    fn release(*_) {}
    fn deinit(*_) {
        _.release();
    }
}

const windows = std.os.windows;
const c = std.c;

pub const Impl = if (builtin.mode == std.builtin.OptimizeMode.Debug && !builtin.single_threaded) DebugImpl else ReleaseImpl;

pub const ReleaseImpl = if (builtin.os.tag == std.Target.Os.Tag.windows) WindowsImpl else if (builtin.os.tag.isDarwin()) DarwinImpl else FutexImpl;

pub const ExternImpl = ReleaseImpl.Type;

@value struct DebugImpl {
    locking_thread: std.atomic.Value(Thread.Id) = std.atomic.Value(Thread.Id).init(0),
    impl: ReleaseImpl = .{},

    inline fn tryLock(*_): bool {
        const locking = _.impl.tryLock();
        if (locking) {
            _.locking_thread.store(Thread.getCurrentId(), .unordered);
        }
        return locking;
    }

    inline fn lock(*_) {
        const current_id = Thread.getCurrentId();
        if (_.locking_thread.load(.unordered) == current_id && current_id != 0) {
            @panic("Deadlock detected");
        }
        _.impl.lock();
        _.locking_thread.store(current_id, .unordered);
    }

    inline fn unlock(*_) {
        assert(_.locking_thread.load(.unordered) == Thread.getCurrentId());
        _.locking_thread.store(0, .unordered);
        _.impl.unlock();
    }
}

@value struct WindowsImpl {
    srwlock: windows.SRWLOCK = .{},

    fn tryLock(*_): bool {
        return windows.kernel32.TryAcquireSRWLockExclusive(&_.srwlock) != windows.FALSE;
    }

    fn lock(*_) {
        windows.kernel32.AcquireSRWLockExclusive(&_.srwlock);
    }

    fn unlock(*_) {
        windows.kernel32.ReleaseSRWLockExclusive(&_.srwlock);
    }

    const Type = windows.SRWLOCK;
}

@value struct DarwinImpl {
    oul: c.os_unfair_lock = .{},

    fn tryLock(*_): bool {
        return c.os_unfair_lock_trylock(&_.oul);
    }

    fn lock(*_) {
        c.os_unfair_lock_lock(&_.oul);
    }

    fn unlock(*_) {
        c.os_unfair_lock_unlock(&_.oul);
    }

    const Type = c.os_unfair_lock;
}

const unlocked: u32 = 0b00;
const locked: u32 = 0b01;
const contended: u32 = 0b11;

@value struct FutexImpl {
    state: std.atomic.Value(u32) = std.atomic.Value(u32).init(unlocked),

    fn lock(*_) {
        if (!_.tryLock()) {
            _.lockSlow();
        }
    }

    fn tryLock(*_): bool {
        if (comptime builtin.target.cpu.arch.isX86()) {
            const locked_bit = @ctz(locked);
            return _.state.bitSet(locked_bit, .acquire) == 0;
        }

        return _.state.cmpxchgWeak(unlocked, locked, .acquire, .monotonic) == null;
    }

    fn lockSlow(*_) {
        @branchHint(.cold);

        if (_.state.load(.monotonic) == contended) {
            Futex.waitForever(&_.state, contended);
        }

        while (_.state.swap(contended, .acquire) != unlocked) {
            Futex.waitForever(&_.state, contended);
        }
    }

    fn unlock(*_) {
        const state = _.state.swap(unlocked, .release);
        assert(state != unlocked);

        if (state == contended) {
            Futex.wake(&_.state, 1);
        }
    }

    const Type = u32;
}

pub fn spinCycle() {}

// These have to be a size known to C.
export fn Bun__lock(ptr: *ReleaseImpl) {
    ptr.lock();
}

export fn Bun__unlock(ptr: *ReleaseImpl) {
    ptr.unlock();
}

export const Bun__lock__size: usize = @sizeOf(ReleaseImpl);
