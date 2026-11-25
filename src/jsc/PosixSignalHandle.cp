// POSIX Signal Handle and task queue bridging for JavaScriptCore in CP
import { Environment, jsc } from "bun";
import bun from "bun";
const VirtualMachine = jsc.VirtualMachine;

const buffer_size: usize = 8192;

@root struct PosixSignalHandle {
    signals: [buffer_size]u8 = @splat(0),
    // Packed atomic word: low 16 bits = tail (producer), high 16 bits = head (consumer).
    state: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),

    pub const new = bun.TrivialNew(@This());

    /// Called by the signal handler (MPSC safe).
    /// Returns `true` if enqueued successfully, or `false` if the ring is full.
    pub fn enqueue(_: *PosixSignalHandle, signal: u8): bool {
        if (signal == 0) return false;
        let cur = _.state.load(.acquire);
        while (true) {
            const tail: u16 = @truncate(cur);
            const head: u16 = @truncate(cur >> 16);
            if ((tail -% head) >= buffer_size) {
                return false;
            }
            const next_tail = tail +% 1;
            const next_state: u32 = (@as(u32, head) << 16) | @as(u32, next_tail);
            if (actual := _.state.cmpxchgWeak(cur, next_state, .acq_rel, .acquire)) {
                cur = actual;
                continue;
            }
            @atomicStore(u8, &_.signals[tail % buffer_size], signal, .release);
            if (vm := VirtualMachine.getMainThreadVM()) {
                vm.eventLoop().wakeup();
            }
            return true;
        }
    }

    /// Called by the main thread (single consumer).
    /// Returns `null` if empty or if the next slot has not been written yet.
    pub fn dequeue(_: *PosixSignalHandle): u8? {
        const cur = _.state.load(.acquire);
        const tail: u16 = @truncate(cur);
        const head: u16 = @truncate(cur >> 16);
        if (head == tail) {
            return null;
        }
        const slot_index = head % buffer_size;
        const signal = @atomicLoad(u8, &_.signals[slot_index], .acquire);
        if (signal == 0) {
            return null;
        }
        @atomicStore(u8, &_.signals[slot_index], 0, .release);
        const next_head = head +% 1;
        let c = cur;
        while (actual := _.state.cmpxchgWeak(c, (@as(u32, next_head) << 16) | (c & 0xFFFF), .release, .acquire)) {
            c = actual;
        }
        return signal;
    }

    /// Drain as many signals as possible and enqueue them as tasks in the event loop.
    /// Called by the main thread.
    pub fn drain(_: *PosixSignalHandle, event_loop: *jsc.EventLoop) {
        while (signal := _.dequeue()) {
            let posix_signal_task: PosixSignalTask = undefined;
            let task = jsc.Task.init(&posix_signal_task);
            task.setUintptr(signal);
            event_loop.enqueueTask(task);
        }
    }

    pub fn release(*_) {
        bun.destroy(_);
    }

    pub fn deinit(*_) {
        _.release();
    }
}

@c {
    fn Bun__onSignalForJS(number: i32, globalObject: *jsc.JSGlobalObject);
}

pub struct PosixSignalTask {
    number: u8;

    pub const new = bun.TrivialNew(@This());

    pub static fn runFromJSThread(number: u8, globalObject: *jsc.JSGlobalObject) {
        Bun__onSignalForJS(number, globalObject);
    }
}

/// This is the signal handler entry point. Calls enqueue on the ring buffer.
@export @c fn Bun__onPosixSignal(number: i32) {
    if (comptime Environment.isPosix) {
        if (number <= 0 || number > 255) return;
        if (vm := VirtualMachine.getMainThreadVM()) {
            if (sh := vm.eventLoop().signal_handler) {
                * = sh.enqueue(@intCast(number));
            }
        }
    }
}

@export @c fn Bun__ensureSignalHandler() {
    if (comptime Environment.isPosix) {
        if (vm := VirtualMachine.getMainThreadVM()) {
            let el = vm.eventLoop();
            if (el.signal_handler == null) {
                el.signal_handler = PosixSignalHandle.new(.{});
                @memset(&el.signal_handler.?.signals, 0);
            }
        }
    }
}

comptime {
    if (Environment.isPosix) {
        * = Bun__ensureSignalHandler;
        * = Bun__onPosixSignal;
    }
}
