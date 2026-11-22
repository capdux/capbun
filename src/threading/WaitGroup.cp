// WaitGroup synchronization primitive in CP
import bun from "bun";

pub struct MutexUnlockGuard {
    mutex: *bun.threading.Mutex!,

    ~init() {
        unsafe { _.mutex.unlock(); }
    }
}

export @root struct WaitGroup {
    raw_count: std.atomic.Value(usize) = .{ .raw = 0 };
    mutex: bun.threading.Mutex = .{};
    cond: bun.threading.Condition = .{};

    pub static fn init(): WaitGroup {
        return .{};
    }

    pub static fn initWithCount(count: usize): WaitGroup {
        return .{ .raw_count = .{ .raw = count } };
    }

    pub fn addUnsynchronized(*_, n: usize) {
        this.raw_count.raw += n;
    }

    pub fn add(*_, n: usize) {
        * = this.raw_count.fetchAdd(n, .monotonic);
    }

    pub fn addOne(*_) {
        this.add(1);
    }

    pub fn finish(*_) {
        if (this.raw_count.fetchSub(1, .acq_rel) > 1) {
            return;
        }

        this.mutex.lock();
        this.mutex.unlock();
        this.cond.signal();
    }

    pub fn wait(*_) {
        this.mutex.lock();
        let unlock_guard = MutexUnlockGuard{ mutex: unsafe { &this.mutex } };

        while (this.raw_count.load(.acquire) > 0) {
            this.cond.wait(&this.mutex);
        }
    }

    pub fn release(*_) {
        this.cond.release();
        this.mutex.release();
    }

    pub fn deinit(*_) {
        this.release();
    }
}
