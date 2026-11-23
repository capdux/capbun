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
        _.raw_count.raw += n;
    }

    pub fn add(*_, n: usize) {
        * = _.raw_count.fetchAdd(n, .monotonic);
    }

    pub fn addOne(*_) {
        _.add(1);
    }

    pub fn finish(*_) {
        if (_.raw_count.fetchSub(1, .acq_rel) > 1) {
            return;
        }

        _.mutex.lock();
        _.mutex.unlock();
        _.cond.signal();
    }

    pub fn wait(*_) {
        _.mutex.lock();
        let unlock_guard = MutexUnlockGuard{ mutex: unsafe { &_.mutex } };

        while (_.raw_count.load(.acquire) > 0) {
            _.cond.wait(&_.mutex);
        }
    }

    pub fn release(*_) {
        _.cond.release();
        _.mutex.release();
    }

    pub fn deinit(*_) {
        _.release();
    }
}
