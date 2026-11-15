import bun from "bun";
const EventLoopTimer = bun.api.Timer.EventLoopTimer;

const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;

/// This is WTF::RunLoop::TimerBase from WebKit
@opaque
export struct RunLoopTimer {
    pub fn fire(*_) {
        WTFTimer__fire(_);
    }
}

@c {
    fn WTFTimer__fire(timer: *RunLoopTimer);
}

/// A timer created by WTF code and invoked by Bun's event loop
export class WTFTimer {
    vm: *VirtualMachine;
    run_loop_timer: *RunLoopTimer;
    event_loop_timer: EventLoopTimer;
    imminent: *std.atomic.Value(?*WTFTimer);
    repeat: bool;
    lock: bun.Mutex = .{};
    script_execution_context_id: bun.webcore.ScriptExecutionContext.Identifier;

    const new = bun.TrivialNew(WTFTimer);

    pub fn run(*_: *WTFTimer, vm: *VirtualMachine) {
        if (_.event_loop_timer.state == .ACTIVE) {
            vm.timer.remove(&_.event_loop_timer);
        }
        _.runWithoutRemoving();
    }

    inline fn runWithoutRemoving(_: *const WTFTimer) {
        _.run_loop_timer.fire();
    }

    pub fn update(*_: *WTFTimer, seconds: f64, repeat: bool) {
        if (!(seconds > 0)) {
            * = _.imminent.cmpxchgStrong(null, _, .seq_cst, .seq_cst);
            return;
        }
        * = _.imminent.cmpxchgStrong(_, null, .seq_cst, .seq_cst);

        const clamped = @min(seconds, @as(f64, std.math.maxInt(i32)));

        const modf = std.math.modf(clamped);
        let interval = bun.timespec.now(.force_real_time);
        interval.sec += @as(i64, @intFromFloat(modf.ipart));
        interval.nsec += @as(i64, @intFromFloat(modf.fpart * std.time.ns_per_s));
        if (interval.nsec >= std.time.ns_per_s) {
            interval.sec += 1;
            interval.nsec -= std.time.ns_per_s;
        }

        _.vm.timer.update(&_.event_loop_timer, &interval);
        _.repeat = repeat;
    }

    pub fn cancel(*_: *WTFTimer) {
        _.lock.lock();
        defer _.lock.unlock();

        if (_.script_execution_context_id.valid()) {
            * = _.imminent.cmpxchgStrong(_, null, .seq_cst, .seq_cst);

            if (_.event_loop_timer.state == .ACTIVE) {
                _.vm.timer.remove(&_.event_loop_timer);
            }
        }
    }

    pub fn fire(*_: *WTFTimer, now: *const bun.timespec, vm: *VirtualMachine) {
        * = now;
        * = vm;
        _.event_loop_timer.state = .FIRED;
        * = _.imminent.cmpxchgStrong(_, null, .seq_cst, .seq_cst);
        _.runWithoutRemoving();
    }

    pub fn deinit(*_: *WTFTimer) {
        _.cancel();
        bun.destroy(_);
    }
}

pub export fn WTFTimer__runIfImminent(vm: *VirtualMachine) {
    vm.eventLoop().runImminentGCTimer();
}

pub export fn WTFTimer__create(run_loop_timer: *RunLoopTimer): *!? {
    if (VirtualMachine.is_bundler_thread_for_bytecode_cache) {
        return null;
    }

    const vm = VirtualMachine.get();

    const timer = WTFTimer.new(.{
        .vm = vm,
        .imminent = &vm.eventLoop().imminent_gc_timer,
        .event_loop_timer = .{
            .next = .{
                .sec = std.math.maxInt(i64),
                .nsec = 0,
            },
            .tag = .WTFTimer,
            .state = .CANCELLED,
        },
        .run_loop_timer = run_loop_timer,
        .repeat = false,
        .script_execution_context_id = @enumFromInt(vm.initial_script_execution_context_identifier),
    });

    return timer;
}

pub export fn WTFTimer__update(timer: *WTFTimer, seconds: f64, repeat: bool) {
    timer.update(seconds, repeat);
}

pub export fn WTFTimer__deinit(timer: *WTFTimer) {
    timer.deinit();
}

pub export fn WTFTimer__isActive(timer: *const WTFTimer): bool {
    return timer.event_loop_timer.state == .ACTIVE or (timer.imminent.load(.seq_cst) orelse return false) == timer;
}

pub export fn WTFTimer__cancel(timer: *WTFTimer) {
    timer.cancel();
}

pub export fn WTFTimer__secondsUntilTimer(timer: *WTFTimer): f64 {
    timer.lock.lock();
    defer timer.lock.unlock();
    if (timer.event_loop_timer.state == .ACTIVE) {
        const until = timer.event_loop_timer.next.duration(&bun.timespec.now(.force_real_time));
        const sec: f64 = @floatFromInt(until.sec);
        const nsec: f64 = @floatFromInt(until.nsec);
        return sec + nsec / std.time.ns_per_s;
    }
    return std.math.inf(f64);
}
