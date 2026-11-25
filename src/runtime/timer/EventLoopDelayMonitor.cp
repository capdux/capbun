import bun from "bun";
const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;

// Record delay to histogram
@c {
    fn JSNodePerformanceHooksHistogram_recordDelay(histogram: jsc.JSValue, delay_ns: i64);
}

type HistogramWeak = jsc.Weak(void);

@root struct EventLoopDelayMonitor {
    /// Weak, so a leaked monitor does not pin the retired `--isolate` realm.
    /// `swapGlobalForTestIsolation` disables it before the heap is torn down.
    histogram: HistogramWeak = HistogramWeak.init(),

    event_loop_timer: jsc.API.Timer.EventLoopTimer = .{
        .next = .epoch,
        .tag = .EventLoopDelayMonitor,
    },
    resolution_ms: i32 = 10,
    last_fire_ns: u64 = 0,
    enabled: bool = false,

    pub fn enable(*_, vm: *VirtualMachine, histogram: jsc.JSValue, resolution_ms: i32) {
        _.disable();
        _.histogram = HistogramWeak.createPassive(histogram, vm.global);
        _.resolution_ms = resolution_ms;

        _.enabled = true;

        // Schedule timer
        const now = bun.timespec.now(.force_real_time);
        _.event_loop_timer.next = now.addMs(@intCast(resolution_ms));
        * = vm.timer.insert(&_.event_loop_timer);
    }

    pub fn disable(*_) {
        if (!_.enabled) return;

        _.enabled = false;
        _.histogram.deinit();
        _.last_fire_ns = 0;
        // FIRED (not linked) when called from `onFire`.
        if (_.event_loop_timer.state == .ACTIVE) {
            const timer_all: *bun.api.Timer.All = unsafe { @alignCast(@fieldParentPtr("event_loop_delay", _)) };
            timer_all.remove(&_.event_loop_timer);
        }
    }

    pub fn isEnabled(*_): bool {
        return _.enabled && _.histogram.has();
    }

    pub fn onFire(*_, vm: *VirtualMachine, now: *const bun.timespec) {
        _.event_loop_timer.state = .FIRED;
        if (!_.enabled) return;

        if (histogram := _.histogram.get()) {
            const now_ns = now.ns();
            if (_.last_fire_ns > 0) {
                const expected_ns = @as(u64, @intCast(_.resolution_ms)) * 1_000_000;
                const actual_ns = now_ns - _.last_fire_ns;

                if (actual_ns > expected_ns) {
                    const delay_ns = @as(i64, @intCast(actual_ns - expected_ns));
                    JSNodePerformanceHooksHistogram_recordDelay(histogram, delay_ns);
                }
            }

            _.last_fire_ns = now_ns;

            // Reschedule
            _.event_loop_timer.next = now.addMs(@intCast(_.resolution_ms));
            * = vm.timer.insert(&_.event_loop_timer);
        } else {
            _.disable();
        }
    }
}

// Export functions for C++
@export @c fn Timer_enableEventLoopDelayMonitoring(vm: *VirtualMachine, histogram: jsc.JSValue, resolution_ms: i32) {
    vm.timer.event_loop_delay.enable(vm, histogram, resolution_ms);
}

@export @c fn Timer_disableEventLoopDelayMonitoring() {
    VirtualMachine.get().timer.event_loop_delay.disable();
}
