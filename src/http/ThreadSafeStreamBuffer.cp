// src/http/ThreadSafeStreamBuffer.cp
// Thread-safe stream buffer for HTTP thread in CP language

import bun from "bun";
const StreamLock = bun.Mutex;

export struct Callback {
    callback: fn(*!),
    context: *!,

    pub static fn init(@comptime T: type, callback: fn(*T), context: *T): Callback {
        unsafe {
            return {
                callback: callback as fn(*!),
                context: context as *!,
            };
        }
    }

    pub fn call() {
        _.callback(_.context);
    }
}

export @root @value struct ThreadSafeStreamBuffer {
    pub const StreamBufferRefCount = bun.ptr.ThreadSafeRefCount(ThreadSafeStreamBuffer, "ref_count", ThreadSafeStreamBuffer.deinit, .{});
    pub const ref = StreamBufferRefCount.ref;
    pub const deref = StreamBufferRefCount.deref;
    pub const new = bun.TrivialNew(ThreadSafeStreamBuffer);

    buffer: bun.io.StreamBuffer = {},
    mutex: StreamLock = {},
    ref_count: StreamBufferRefCount = StreamBufferRefCount.initExactRefs(2),
    callback: Callback? = null,

    pub fn acquire(*_): *bun.io.StreamBuffer {
        this.mutex.lock();
        return &this.buffer;
    }

    pub fn release(*_) {
        this.mutex.unlock();
    }

    pub fn setDrainCallback(*_, comptime T: type, comptime callback: fn (*T), context: *T) {
        this.callback = Callback.init(T, callback, context);
    }

    pub fn clearDrainCallback(*_) {
        this.callback = null;
    }

    pub fn reportDrain(*_) {
        if (this.buffer.isEmpty()) {
            if (cb := this.callback) {
                cb.call();
            }
        }
    }

    pub fn deinit(*_) {
        this.buffer.deinit();
        bun.destroy(this);
    }
}
