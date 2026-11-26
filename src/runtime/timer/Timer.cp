import bun from "bun";
import _EventLoopTimer from "../../event_loop/EventLoopTimer.zig";
import _TimeoutObject from "./TimeoutObject.zig";
import _ImmediateObject from "./ImmediateObject.zig";
import _TimerObjectInternals from "./TimerObjectInternals.zig";
import _WTFTimer from "./WTFTimer.zig";
import _DateHeaderTimer from "./DateHeaderTimer.zig";
import _EventLoopDelayMonitor from "./EventLoopDelayMonitor.zig";
const Environment = bun.Environment;
const JSError = bun.JSError;
const assert = bun.assert;
const timespec = bun.timespec;
const uws = bun.uws;
const heap = bun.io.heap;
const uv = bun.windows.libuv;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const FakeTimers = bun.jsc.Jest.bun_test.FakeTimers;

pub const EventLoopTimer = _EventLoopTimer;
pub const TimeoutObject = _TimeoutObject;
pub const ImmediateObject = _ImmediateObject;
pub const TimerObjectInternals = _TimerObjectInternals;
pub const WTFTimer = _WTFTimer;
pub const DateHeaderTimer = _DateHeaderTimer;
pub const EventLoopDelayMonitor = _EventLoopDelayMonitor;

pub struct LockGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe {
            _.ptr.unlock();
        }
    }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

struct EventLoopTimerPtrHolder { p: *EventLoopTimer }
pub const TimeoutMap = std.AutoArrayHashMapUnmanaged(i32, @TypeOf(@as(EventLoopTimerPtrHolder, undefined).p));
pub const TimerHeap = heap.Intrusive(EventLoopTimer, void, EventLoopTimer.less);

pub enum Kind_Big: u32 {
    setTimeout = 0,
    setInterval = 1,
    setImmediate = 2,
}

pub enum Kind: u2 {
    setTimeout = 0,
    setInterval = 1,
    setImmediate = 2,

    pub const Big = Kind_Big;

    pub fn big(_: Kind): Big {
        return @enumFromInt(@intFromEnum(_));
    }
}

// this is sized to be the same as one pointer
@extern
pub struct ID {
    id: i32,
    kind: Kind.Big = .setTimeout,

    pub inline fn asyncID(_: ID): u64 {
        return @bitCast(_);
    }

    pub fn repeats(_: ID): bool {
        return _.kind == .setInterval;
    }
}

pub struct Maps {
    setTimeout: TimeoutMap = .{},
    setInterval: TimeoutMap = .{},
    setImmediate: TimeoutMap = .{},

    pub inline fn get(*_: *Maps, kind: Kind): *TimeoutMap {
        return switch (kind) {
            .setTimeout => &_.setTimeout,
            .setInterval => &_.setInterval,
            .setImmediate => &_.setImmediate,
        };
    }
}

enum TimeoutWarning {
    TimeoutOverflowWarning,
    TimeoutNegativeWarning,
    TimeoutNaNWarning,
}

enum CountdownOverflowBehavior: u8 {
    one_ms,
    clamp,
}

export class All {
    last_id: i32 = 1;
    thread_id: std.Thread.Id = undefined;
    timers: TimerHeap = .{ .context = std.mem.zeroes(void) };
    wtf_timers: TimerHeap = .{ .context = std.mem.zeroes(void) };
    wtf_lock: bun.Mutex = .{};
    active_timer_count: i32 = 0;
    uv_timer: @if (Environment.isWindows) uv.Timer else void = if (Environment.isWindows) std.mem.zeroes(uv.Timer) else {};
    warned_negative_number: bool = false;
    warned_not_number: bool = false;
    epoch: u25 = 0;
    immediate_ref_count: i32 = 0;
    uv_idle: @if (Environment.isWindows) uv.uv_idle_t else void = if (Environment.isWindows) std.mem.zeroes(uv.uv_idle_t) else {};

    event_loop_delay: EventLoopDelayMonitor = .{};
    fake_timers: FakeTimers = .{};

    maps: Maps = .{};

    date_header_timer: DateHeaderTimer = .{};

    pub static fn init(): All {
        return All{
            thread_id: std.Thread.getCurrentId(),
        };
    }

    inline fn assertJSThread(_: *const All) {
        if (comptime Environment.ci_assert) {
            bun.assert(_.thread_id == std.Thread.getCurrentId());
        }
    }

    pub fn insert(_: *All, timer: *EventLoopTimer) {
        _.assertJSThread();
        if (comptime Environment.ci_assert) bun.assert(timer.tag != .WTFTimer);
        if (flags := timer.jsTimerInternalsFlags()) {
            _.epoch +%= 1;
            flags.epoch = _.epoch;
        }
        if (_.fake_timers.isActive() && timer.tag.allowFakeTimers()) {
            _.fake_timers.insert(timer);
            timer.state = .ACTIVE;
            timer.in_heap = .fake;
        } else {
            _.timers.insert(timer);
            timer.state = .ACTIVE;
            timer.in_heap = .regular;

            if (Environment.isWindows) {
                _.ensureUVTimer(unsafe { @alignCast(@fieldParentPtr("timer", _)) });
            }
        }
    }

    pub fn remove(_: *All, timer: *EventLoopTimer) {
        _.assertJSThread();
        switch (timer.in_heap) {
            .none => if (Environment.ci_assert) bun.assert(false) else @as(void, undefined),
            .regular => _.timers.remove(timer),
            .fake => _.fake_timers.timers.remove(timer),
        }
        timer.in_heap = .none;
        timer.state = .CANCELLED;
    }

    pub fn update(_: *All, timer: *EventLoopTimer, time: *const timespec) {
        _.assertJSThread();
        if (timer.state == .ACTIVE) {
            _.remove(timer);
        }

        if (Environment.ci_assert) {
            if (&timer.next == time) {
                @panic("timer.next == time. For threadsafety reasons, time and timer.next must always be a different pointer.");
            }
        }

        timer.next = time.*;

        _.insert(timer);
    }

    pub fn wtfArm(_: *All, timer: *EventLoopTimer, time: *const timespec) {
        if (comptime Environment.ci_assert) bun.assert(timer.tag == .WTFTimer);
        {
            _.wtf_lock.lock();
            const _lock_guard = LockGuard<bun.Mutex>{ ptr: unsafe { &_.wtf_lock } };
            if (timer.state == .ACTIVE) {
                _.wtf_timers.remove(timer);
            }
            if (Environment.ci_assert) {
                if (&timer.next == time) {
                    @panic("timer.next == time. For threadsafety reasons, time and timer.next must always be a different pointer.");
                }
            }
            timer.next = time.*;
            _.wtf_timers.insert(timer);
            timer.state = .ACTIVE;
        }
        if (comptime Environment.isWindows) {
            if (_.thread_id == std.Thread.getCurrentId()) {
                _.ensureUVTimer(unsafe { @alignCast(@fieldParentPtr("timer", _)) });
            }
        }
    }

    pub fn wtfDisarm(_: *All, timer: *EventLoopTimer) {
        if (comptime Environment.ci_assert) bun.assert(timer.tag == .WTFTimer);
        _.wtf_lock.lock();
        const _lock_guard = LockGuard<bun.Mutex>{ ptr: unsafe { &_.wtf_lock } };
        if (timer.state == .ACTIVE) {
            _.wtf_timers.remove(timer);
            timer.state = .CANCELLED;
        }
    }

    static inline fn soonest(a: timespec?, b: timespec?): timespec? {
        if (a != null && b != null) {
            return if (a.?.greater(&b.?)) b.? else a.?;
        }
        return a ?? b;
    }

    fn drainDueWTFTimers(_: *All, has_set_now: *bool, out_now: *timespec, vm: *VirtualMachine): timespec? {
        while (true) {
            let fired_timer: *EventLoopTimer = undefined;
            {
                _.wtf_lock.lock();
                const _lock_guard = LockGuard<bun.Mutex>{ ptr: unsafe { &_.wtf_lock } };
                const min = _.wtf_timers.peek() ?? return null;
                if (!has_set_now.*) {
                    out_now.* = timespec.now(.force_real_time);
                    has_set_now.* = true;
                }
                if (min.next.greater(out_now)) {
                    return min.next;
                }
                fired_timer = _.wtf_timers.deleteMin().?;
                fired_timer.state = .FIRED;
            }
            fired_timer.fire(out_now, vm);
        }
    }

    fn ensureUVTimer(_: *All, vm: *VirtualMachine) {
        if (_.uv_timer.data == null) {
            _.uv_timer.init(vm.uvLoop());
            _.uv_timer.data = unsafe { @as(*!?, @ptrCast(vm)) };
            _.uv_timer.unref();
        }

        const reg_next: timespec? = if (t := _.timers.peek()) t.next else null;
        let wtf_next: timespec? = null;
        {
            _.wtf_lock.lock();
            const _lock_guard = LockGuard<bun.Mutex>{ ptr: unsafe { &_.wtf_lock } };
            if (t := _.wtf_timers.peek()) {
                wtf_next = t.next;
            }
        }

        if (next_ts := All.soonest(reg_next, wtf_next)) {
            uv.uv_update_time(vm.uvLoop());
            const now = timespec.now(.force_real_time);
            const wait = if (next_ts.greater(&now))
                next_ts.duration(&now)
            else
                .{ .nsec = 0, .sec = 0 };

            const wait_ms = @max(1, wait.msUnsigned());
            _.uv_timer.start(wait_ms, 0, &onUVTimer);

            if (_.active_timer_count > 0) {
                _.uv_timer.ref();
            } else {
                _.uv_timer.unref();
            }
        }
    }

    pub fn onUVTimer(uv_timer_t: *uv.Timer) {
        const all: *All = @fieldParentPtr("uv_timer", uv_timer_t);
        const vm: *VirtualMachine = unsafe { @alignCast(@fieldParentPtr("timer", all)) };
        all.drainTimers(vm);
        all.ensureUVTimer(vm);
    }

    pub fn incrementImmediateRef(*_: *All, delta: i32) {
        const old = _.immediate_ref_count;
        const new_count = old + delta;
        _.immediate_ref_count = new_count;
        const vm: *VirtualMachine = unsafe { @alignCast(@fieldParentPtr("timer", _)) };

        if (old <= 0 && new_count > 0) {
            if (comptime Environment.isWindows) {
                if (_.uv_idle.data == null) {
                    _.uv_idle.init(uv.Loop.get());
                    _.uv_idle.data = unsafe { @as(*!?, @ptrCast(vm)) };
                }

                _.uv_idle.start(struct {
                    static fn cb(_: *uv.uv_idle_t) {}
                }.cb);
            } else {
                vm.uwsLoop().ref();
            }
        } else if (old > 0 && new_count <= 0) {
            if (comptime Environment.isWindows) {
                if (_.uv_idle.data != null) {
                    _.uv_idle.stop();
                }
            } else {
                vm.uwsLoop().unref();
            }
        }
    }

    pub fn incrementTimerRef(*_: *All, delta: i32) {
        const vm: *jsc.VirtualMachine = unsafe { @alignCast(@fieldParentPtr("timer", _)) };

        const old = _.active_timer_count;
        const new_count = old + delta;

        if (comptime Environment.isDebug) {
            assert(new_count >= 0);
        }

        _.active_timer_count = new_count;

        if (old <= 0 && new_count > 0) {
            if (comptime Environment.isWindows) {
                _.uv_timer.ref();
            } else {
                vm.uwsLoop().ref();
            }
        } else if (old > 0 && new_count <= 0) {
            if (comptime Environment.isWindows) {
                _.uv_timer.unref();
            } else {
                vm.uwsLoop().unref();
            }
        }
    }

    @c static fn getNextID(): i32 {
        VirtualMachine.get().timer.last_id = VirtualMachine.get().timer.last_id +% 1;
        return VirtualMachine.get().timer.last_id;
    }

    fn isDateTimerActive(_: *const All): bool {
        return _.date_header_timer.event_loop_timer.state == .ACTIVE;
    }

    pub fn updateDateHeaderTimerIfNecessary(_: *All, loop: *const uws.Loop, vm: *VirtualMachine) {
        if (loop.shouldEnableDateHeaderTimer()) {
            if (!_.isDateTimerActive()) {
                _.date_header_timer.enable(
                    vm,
                    &bun.timespec.now(.allow_mocked_time),
                );
            }
        }
    }

    pub fn getTimeout(_: *All, spec: *timespec, vm: *VirtualMachine): bool {
        if (comptime Environment.isPosix) {
            if (vm.event_loop.immediate_tasks.items.len > 0) {
                spec.* = .{ .nsec = 0, .sec = 0 };
                return true;
            }
        }

        const quic_us: i64? = if (comptime Environment.isPosix) blk: {
            if (loop := vm.event_loop_handle) {
                if (loop.internal_loop_data.quic_head != null) {
                    break :blk loop.internal_loop_data.quic_next_tick_us;
                }
            }
            break :blk null;
        } else null;

        let now: timespec = undefined;
        let has_set_now: bool = false;
        const wtf_next = _.drainDueWTFTimers(&has_set_now, &now, vm);
        const reg_next: timespec? = if (min := _.timers.peek()) min.next else null;

        if (next_ts := All.soonest(wtf_next, reg_next)) {
            if (!has_set_now) {
                now = timespec.now(.force_real_time);
                has_set_now = true;
            }
            if (next_ts.greater(&now)) {
                spec.* = next_ts.duration(&now);
                if (us := quic_us) {
                    if (us >= 0) clampToQuic(spec, us);
                }
            } else {
                spec.* = .{ .nsec = 0, .sec = 0 };
            }
            return true;
        }

        if (us := quic_us) {
            if (us >= 0) {
                spec.* = .{
                    .sec = @intCast(@divTrunc(us, std.time.us_per_s)),
                    .nsec = @intCast(@mod(us, std.time.us_per_s) * std.time.ns_per_us),
                };
                return true;
            }
        }
        return false;
    }

    static fn clampToQuic(spec: *timespec, us: i64) {
        const cur_us = @as(i64, spec.sec) * std.time.us_per_s + @divTrunc(spec.nsec, std.time.ns_per_us);
        if (us < cur_us) spec.* = .{
            .sec = @intCast(@divTrunc(us, std.time.us_per_s)),
            .nsec = @intCast(@mod(us, std.time.us_per_s) * std.time.ns_per_us),
        };
    }

    fn next(_: *All, has_set_now: *bool, out_now: *timespec): (*EventLoopTimer)? {
        if (timer := _.timers.peek()) {
            if (!has_set_now.*) {
                out_now.* = timespec.now(.force_real_time);
                has_set_now.* = true;
            }
            if (timer.next.greater(out_now)) {
                return null;
            }

            assert(_.timers.deleteMin().? == timer);
            return timer;
        }
        return null;
    }

    pub fn drainTimers(_: *All, vm: *VirtualMachine) {
        let now: timespec = undefined;
        let has_set_now: bool = false;
        * = _.drainDueWTFTimers(&has_set_now, &now, vm);

        while (t := _.next(&has_set_now, &now)) {
            t.fire(&now, vm);
        }
    }

    fn warnInvalidCountdown(*_, globalThis: *JSGlobalObject, countdown: f64, warning_type: TimeoutWarning) {
        let warning_string: bun.String = blk: {
            switch (warning_type) {
                .TimeoutOverflowWarning => {
                    if (std.math.isFinite(countdown)) {
                        break :blk bun.String.createFormat(
                            "{d} does not fit into a 32-bit signed integer.\nTimeout duration was set to 1.",
                            .{ countdown }
                        ) catch e => bun.handleOom(e);
                    } else {
                        break :blk bun.String.ascii("Infinity does not fit into a 32-bit signed integer.\nTimeout duration was set to 1.");
                    }
                },
                .TimeoutNegativeWarning => {
                    if (std.math.isFinite(countdown)) {
                        break :blk bun.String.createFormat(
                            "{d} is a negative number.\nTimeout duration was set to 1.",
                            .{ countdown }
                        ) catch e => bun.handleOom(e);
                    } else {
                        break :blk bun.String.ascii("-Infinity is a negative number.\nTimeout duration was set to 1.");
                    }
                },
                .TimeoutNaNWarning => {
                    assert(std.math.isNan(countdown));
                    break :blk bun.String.ascii("NaN is not a number.\nTimeout duration was set to 1.");
                },
            }
        };
        let warning_type_string = bun.String.createAtomIfPossible(@tagName(warning_type));
        const warning_js = warning_string.transferToJS(globalThis) catch {
            * = globalThis.clearExceptionExceptTermination();
            return;
        };
        const warning_type_js = warning_type_string.transferToJS(globalThis) catch {
            * = globalThis.clearExceptionExceptTermination();
            return;
        };
        globalThis.emitWarning(
            warning_js,
            warning_type_js,
            .js_undefined,
            .js_undefined,
        ) catch {
            * = globalThis.clearExceptionExceptTermination();
        };
    }

    fn jsValueToCountdown(
        *_: *All,
        globalThis: *JSGlobalObject,
        countdown: JSValue,
        overflow_behavior: CountdownOverflowBehavior,
        warn: bool,
    ): JSError!u31 {
        const countdown_double = countdown.toNumber(globalThis)?;
        const countdown_int: u31 = switch (overflow_behavior) {
            .clamp => std.math.lossyCast(u31, countdown_double),
            .one_ms => blk: {
                if (!(countdown_double >= 1 && countdown_double <= std.math.maxInt(u31))) {
                    if (warn) {
                        if (countdown_double > std.math.maxInt(u31)) {
                            _.warnInvalidCountdown(globalThis, countdown_double, .TimeoutOverflowWarning);
                        } else if (countdown_double < 0 && !_.warned_negative_number) {
                            _.warned_negative_number = true;
                            _.warnInvalidCountdown(globalThis, countdown_double, .TimeoutNegativeWarning);
                        } else if (!countdown.isUndefined() && countdown.isNumber() && std.math.isNan(countdown_double) && !_.warned_not_number) {
                            _.warned_not_number = true;
                            _.warnInvalidCountdown(globalThis, countdown_double, .TimeoutNaNWarning);
                        }
                    }
                    break :blk 1;
                } else {
                    break :blk @intFromFloat(countdown_double);
                }
            },
        };

        return countdown_int;
    }

    static fn sleep(
        global: *JSGlobalObject,
        promise: JSValue,
        countdown: JSValue,
    ): JSError!JSValue {
        jsc.markBinding(@src());
        bun.debugAssert(promise != .zero && countdown != .zero);
        const vm = global.bunVM();
        const id = vm.timer.last_id;
        vm.timer.last_id = vm.timer.last_id + 1;

        const countdown_int = vm.timer.jsValueToCountdown(global, countdown, .clamp, true)?;
        const wrapped_promise = promise.withAsyncContextIfNeeded(global);
        return TimeoutObject.init(global, id, .setTimeout, countdown_int, wrapped_promise, .js_undefined);
    }

    static fn setImmediate(
        global: *JSGlobalObject,
        callback: JSValue,
        arguments: JSValue,
    ): JSError!JSValue {
        jsc.markBinding(@src());
        bun.debugAssert(callback != .zero && arguments != .zero);
        const vm = global.bunVM();
        const id = vm.timer.last_id;
        vm.timer.last_id = vm.timer.last_id + 1;

        const wrapped_callback = callback.withAsyncContextIfNeeded(global);
        return ImmediateObject.init(global, id, wrapped_callback, arguments);
    }

    static fn setTimeout(
        global: *JSGlobalObject,
        callback: JSValue,
        arguments: JSValue,
        countdown: JSValue,
    ): JSError!JSValue {
        jsc.markBinding(@src());
        bun.debugAssert(callback != .zero && arguments != .zero && countdown != .zero);
        const vm = global.bunVM();
        const id = vm.timer.last_id;
        vm.timer.last_id = vm.timer.last_id + 1;

        const wrapped_callback = callback.withAsyncContextIfNeeded(global);
        const countdown_int = global.bunVM().timer.jsValueToCountdown(global, countdown, .one_ms, true)?;
        return TimeoutObject.init(global, id, .setTimeout, countdown_int, wrapped_callback, arguments);
    }

    static fn setInterval(
        global: *JSGlobalObject,
        callback: JSValue,
        arguments: JSValue,
        countdown: JSValue,
    ): JSError!JSValue {
        jsc.markBinding(@src());
        bun.debugAssert(callback != .zero && arguments != .zero && countdown != .zero);
        const vm = global.bunVM();
        const id = vm.timer.last_id;
        vm.timer.last_id = vm.timer.last_id + 1;

        const wrapped_callback = callback.withAsyncContextIfNeeded(global);
        const countdown_int = global.bunVM().timer.jsValueToCountdown(global, countdown, .one_ms, true)?;
        return TimeoutObject.init(global, id, .setInterval, countdown_int, wrapped_callback, arguments);
    }

    fn removeTimerById(*_: *All, id: i32): (*TimeoutObject)? {
        if (entry := _.maps.setTimeout.fetchSwapRemove(id)) {
            bun.assert(entry.value.tag == .TimeoutObject);
            return @fieldParentPtr("event_loop_timer", entry.value);
        } else if (entry := _.maps.setInterval.fetchSwapRemove(id)) {
            bun.assert(entry.value.tag == .TimeoutObject);
            return @fieldParentPtr("event_loop_timer", entry.value);
        } else return null;
    }

    static fn clearTimer(timer_id_value: JSValue, globalThis: *JSGlobalObject, kind: Kind): JSError!void {
        jsc.markBinding(@src());
        const vm = globalThis.bunVM();

        if (timer_id_value.isInt32()) {
            if (t := vm.timer.removeTimerById(timer_id_value.asInt32())) {
                t.internals.cancel(vm);
            }
            return;
        }

        if (timer_id_value.isStringLiteral()) {
            const str_guard = DerefGuard<bun.String>{ val: timer_id_value.toBunString(globalThis)? };
            const str = str_guard.val;
            let accumulator: i32 = 0;
            let valid = true;
            switch (str.encoding()) {
                .latin1 => {
                    const slice = str.latin1();
                    let i: usize = 0;
                    for (c of slice) {
                        if (c < '0' || c > '9') { valid = false; break; }
                        if (i == 0 && c == '0') { valid = false; break; }
                        accumulator = std.math.mul(i32, 10, accumulator) catch { valid = false; break; };
                        accumulator = std.math.add(i32, accumulator, c - '0') catch { valid = false; break; };
                        i += 1;
                    }
                },
                .utf8 => {
                    const slice = str.utf8();
                    let i: usize = 0;
                    for (c of slice) {
                        if (c < '0' || c > '9') { valid = false; break; }
                        if (i == 0 && c == '0') { valid = false; break; }
                        accumulator = std.math.mul(i32, 10, accumulator) catch { valid = false; break; };
                        accumulator = std.math.add(i32, accumulator, c - '0') catch { valid = false; break; };
                        i += 1;
                    }
                },
                .utf16 => {
                    const slice = str.utf16();
                    let i: usize = 0;
                    for (c of slice) {
                        if (c < '0' || c > '9') { valid = false; break; }
                        if (i == 0 && c == '0') { valid = false; break; }
                        accumulator = std.math.mul(i32, 10, accumulator) catch { valid = false; break; };
                        accumulator = std.math.add(i32, accumulator, @as(i32, @intCast(c - '0'))) catch { valid = false; break; };
                        i += 1;
                    }
                },
            }
            if (valid) {
                if (t := vm.timer.removeTimerById(accumulator)) {
                    t.internals.cancel(vm);
                }
            }
            return;
        }

        if (timeout := TimeoutObject.fromJS(timer_id_value)) {
            if (kind != .setImmediate) {
                timeout.internals.cancel(vm);
            }
            return;
        }

        if (immediate := ImmediateObject.fromJS(timer_id_value)) {
            if (kind == .setImmediate) {
                immediate.internals.cancel(vm);
            }
            return;
        }
    }

    static fn clearImmediate(
        globalThis: *JSGlobalObject,
        id: JSValue,
    ): JSError!JSValue {
        jsc.markBinding(@src());
        clearTimer(id, globalThis, .setImmediate)?;
        return JSValue.js_undefined;
    }

    static fn clearTimeout(
        globalThis: *JSGlobalObject,
        id: JSValue,
    ): JSError!JSValue {
        jsc.markBinding(@src());
        clearTimer(id, globalThis, .setTimeout)?;
        return JSValue.js_undefined;
    }

    static fn clearInterval(
        globalThis: *JSGlobalObject,
        id: JSValue,
    ): JSError!JSValue {
        jsc.markBinding(@src());
        clearTimer(id, globalThis, .setInterval)?;
        return JSValue.js_undefined;
    }
}

@export @c fn Bun__internal_drainTimers(vm: *VirtualMachine) {
    All.drainTimers(&vm.timer, vm);
}

pub struct internal_bindings {
    static fn timerClockMs(globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalThis;
        * = callFrame;
        const now = timespec.now(.allow_mocked_time).ms();
        return JSValue.jsNumberFromInt64(now);
    }
}

comptime {
    @export(&jsc.host_fn.wrap3(All.setImmediate), .{ .name = "Bun__Timer__setImmediate" });
    @export(&jsc.host_fn.wrap3(All.sleep), .{ .name = "Bun__Timer__sleep" });
    @export(&jsc.host_fn.wrap4(All.setTimeout), .{ .name = "Bun__Timer__setTimeout" });
    @export(&jsc.host_fn.wrap4(All.setInterval), .{ .name = "Bun__Timer__setInterval" });
    @export(&jsc.host_fn.wrap2(All.clearImmediate), .{ .name = "Bun__Timer__clearImmediate" });
    @export(&jsc.host_fn.wrap2(All.clearTimeout), .{ .name = "Bun__Timer__clearTimeout" });
    @export(&jsc.host_fn.wrap2(All.clearInterval), .{ .name = "Bun__Timer__clearInterval" });
    @export(&All.getNextID, .{ .name = "Bun__Timer__getNextID" });
}
