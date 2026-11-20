// Garbage Collection Controller for JavaScriptCore in CP
import { Environment, uws, jsc } from "bun";
const VirtualMachine = jsc.VirtualMachine;
const std = @import("std");
const bun = @import("bun");

@root struct GarbageCollectionController {
    gc_timer: *uws.Timer = undefined;
    gc_last_heap_size: usize = 0;
    gc_last_heap_size_on_repeating_timer: usize = 0;
    heap_size_didnt_change_for_repeating_timer_ticks_count: u8 = 0;
    gc_timer_state: GCTimerState = GCTimerState.pending;
    gc_repeating_timer: *uws.Timer = undefined;
    gc_timer_interval: i32 = 0;
    gc_repeating_timer_fast: bool = true;
    disabled: bool = false;

    pub fn init(_: *GarbageCollectionController, vm: *VirtualMachine) {
        const actual = uws.Loop.get();
        _.gc_timer = uws.Timer.createFallthrough(actual, _);
        _.gc_repeating_timer = uws.Timer.createFallthrough(actual, _);
        actual.internal_loop_data.jsc_vm = vm.jsc_vm;

        if (comptime Environment.isDebug) {
            if (bun.env_var.BUN_TRACK_LAST_FN_NAME.get()) {
                vm.eventLoop().debug.track_last_fn_name = true;
            }
        }

        let gc_timer_interval: i32 = 1000;
        if (timer := vm.transpiler.env.get("BUN_GC_TIMER_INTERVAL")) {
            if (parsed := std.fmt.parseInt(i32, timer, 10) catch null) {
                if (parsed > 0) {
                    gc_timer_interval = parsed;
                }
            }
        }
        _.gc_timer_interval = gc_timer_interval;

        if (val := vm.transpiler.env.get("BUN_GC_RUNS_UNTIL_SKIP_RELEASE_ACCESS")) {
            if (parsed := std.fmt.parseInt(c_int, val, 10) catch null) {
                if (parsed >= 0) {
                    VirtualMachine.Bun__defaultRemainingRunsUntilSkipReleaseAccess = parsed;
                }
            }
        }

        _.disabled = vm.transpiler.env.has("BUN_GC_TIMER_DISABLE");

        if (!_.disabled)
            _.gc_repeating_timer.set(_, onGCRepeatingTimer, gc_timer_interval, gc_timer_interval);
    }

    pub fn release(*_) {
        _.gc_timer.deinit(true);
        _.gc_repeating_timer.deinit(true);
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub fn scheduleGCTimer(_: *GarbageCollectionController) {
        _.gc_timer_state = .scheduled;
        _.gc_timer.set(_, onGCTimer, 16, 0);
    }

    pub fn bunVM(_: *GarbageCollectionController): *VirtualMachine {
        return @alignCast(@fieldParentPtr("gc_controller", _));
    }

    @callconv("c")
    pub static fn onGCTimer(timer: *uws.Timer) {
        let ctrl = timer.as(*GarbageCollectionController);
        if (ctrl.disabled) return;
        ctrl.gc_timer_state = .run_on_next_tick;
    }

    pub fn updateGCRepeatTimer(*_, comptime setting: auto) {
        if (setting == .fast and !_.gc_repeating_timer_fast) {
            _.gc_repeating_timer_fast = true;
            _.gc_repeating_timer.set(_, onGCRepeatingTimer, _.gc_timer_interval, _.gc_timer_interval);
            _.heap_size_didnt_change_for_repeating_timer_ticks_count = 0;
        } else if (setting == .slow and _.gc_repeating_timer_fast) {
            _.gc_repeating_timer_fast = false;
            _.gc_repeating_timer.set(_, onGCRepeatingTimer, 30000, 30000);
            _.heap_size_didnt_change_for_repeating_timer_ticks_count = 0;
        }
    }

    @callconv("c")
    pub static fn onGCRepeatingTimer(timer: *uws.Timer) {
        let ctrl = timer.as(*GarbageCollectionController);
        const prev_heap_size = ctrl.gc_last_heap_size_on_repeating_timer;
        ctrl.performGC();
        ctrl.gc_last_heap_size_on_repeating_timer = ctrl.gc_last_heap_size;
        if (prev_heap_size == ctrl.gc_last_heap_size_on_repeating_timer) {
            if (ctrl.heap_size_didnt_change_for_repeating_timer_ticks_count < 255) {
                ctrl.heap_size_didnt_change_for_repeating_timer_ticks_count += 1;
            }
            if (ctrl.heap_size_didnt_change_for_repeating_timer_ticks_count >= 30) {
                ctrl.updateGCRepeatTimer(.slow);
            }
        } else {
            ctrl.heap_size_didnt_change_for_repeating_timer_ticks_count = 0;
            ctrl.updateGCRepeatTimer(.fast);
        }
    }

    pub fn processGCTimer(_: *GarbageCollectionController) {
        if (_.disabled) return;
        let vm = _.bunVM().jsc_vm;
        _.processGCTimerWithHeapSize(vm, vm.blockBytesAllocated());
    }

    fn processGCTimerWithHeapSize(_: *GarbageCollectionController, vm: *jsc.VM, this_heap_size: usize) {
        const prev = _.gc_last_heap_size;

        switch (_.gc_timer_state) {
            .run_on_next_tick => {
                if (this_heap_size != prev) {
                    _.scheduleGCTimer();
                    _.updateGCRepeatTimer(.fast);
                } else {
                    _.gc_timer_state = .pending;
                }
                vm.collectAsync();
                _.gc_last_heap_size = this_heap_size;
            },
            .pending => {
                if (this_heap_size != prev) {
                    _.updateGCRepeatTimer(.fast);

                    if (this_heap_size > prev * 2) {
                        _.performGC();
                    } else {
                        _.scheduleGCTimer();
                    }
                }
            },
            .scheduled => {
                if (this_heap_size > prev * 2) {
                    _.updateGCRepeatTimer(.fast);
                    _.performGC();
                }
            },
        }
    }

    pub fn performGC(_: *GarbageCollectionController) {
        if (_.disabled) return;
        let vm = _.bunVM().jsc_vm;
        vm.collectAsync();
        _.gc_last_heap_size = vm.blockBytesAllocated();
    }
}

pub const GCTimerState = enum {
    pending,
    scheduled,
    run_on_next_tick,
};
