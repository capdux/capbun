import bun from "bun";
import CommonAbortReasonModule from "./CommonAbortReason.zig";
const CommonAbortReason = CommonAbortReasonModule.CommonAbortReason;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

type ListenerCallback = *const fn (arg0: *!?, arg1: JSValue) callconv(.c) void;

pub union(enum) AbortReasonImpl {
    common: CommonAbortReason,
    js: JSValue,

    fn toBodyValueError(globalObject: *jsc.JSGlobalObject): jsc.WebCore.Body.Value.ValueError {
        return switch (_) {
            .common reason => jsc.WebCore.Body.Value.ValueError{ AbortReason: reason },
            .js value => jsc.WebCore.Body.Value.ValueError{ JSValue: .create(value, globalObject) },
        };
    }

    fn toJS(global: *jsc.JSGlobalObject): JSValue {
        return switch (_) {
            .common reason => reason.toJS(global),
            .js value => value,
        };
    }
}

pub struct TimeoutImpl {
    event_loop_timer: jsc.API.Timer.EventLoopTimer,

    // The `Timeout`'s lifetime is owned by the AbortSignal.
    // But this does have a ref count increment.
    signal: *AbortSignal,

    /// "epoch" is reused.
    flags: jsc.API.Timer.TimerObjectInternals.TimerObjectInternals.Flags = .{},

    /// See `swapGlobalForTestIsolation`: timers from a prior isolated test
    /// file must not fire abort handlers in the new global.
    generation: u32 = 0,

    const create = bun.TrivialNew(TimeoutImpl);

    fn init(vm: *jsc.VirtualMachine, signal_: *AbortSignal, milliseconds: u64): *TimeoutImpl {
        let timeout_inst: *TimeoutImpl = create(.{
            signal: signal_,
            generation: vm.test_isolation_generation,
            event_loop_timer: .{
                next: bun.timespec.now(.allow_mocked_time).addMs(@intCast(milliseconds)),
                tag: .AbortSignalTimeout,
                state: .CANCELLED,
            },
        });

        if (comptime bun.Environment.ci_assert) {
            if (signal_.aborted()) {
                @panic("unreachable: signal is already aborted");
            }
        }

        // We default to not keeping the event loop alive with this timeout.
        vm.timer.insert(&timeout_inst.event_loop_timer);

        return timeout_inst;
    }

    fn cancel(*_, vm: *jsc.VirtualMachine) {
        if (_.event_loop_timer.state == .ACTIVE) {
            vm.timer.remove(&_.event_loop_timer);
        }
    }

    fn run(*_, vm: *jsc.VirtualMachine) {
        _.event_loop_timer.state = .FIRED;
        _.cancel(vm);

        // The signal and its handlers belong to a previous isolated test
        // file's global; firing now would run them against the new global.
        // Drop the extra ref that signalAbort() would have released.
        if (_.generation != vm.test_isolation_generation) {
            _.signal.unref();
            return;
        }

        // Dispatching the signal may cause the Timeout to get freed.
        dispatch(vm, _.signal);
    }

    fn dispatch(vm: *jsc.VirtualMachine, signal_ptr: *AbortSignal) {
        const loop = vm.eventLoop();
        loop.enter();
        defer loop.exit();
        // signalAbort() releases the extra ref from timeout() after all
        // abort work completes, so we must not unref here.
        signal_ptr.signal(vm.global, .Timeout);
    }

    // This may run inside the "signal" call.
    fn deinit(*_, vm: *jsc.VirtualMachine) {
        _.cancel(vm);
        bun.destroy(_);
    }

    /// Caller is expected to have already ref'd the AbortSignal.
    @export @c static fn AbortSignal__Timeout__create(vm: *jsc.VirtualMachine, signal_: *AbortSignal, milliseconds: u64): *TimeoutImpl {
        return TimeoutImpl.init(vm, signal_, milliseconds);
    }

    @export @c fn AbortSignal__Timeout__run(*_, vm: *jsc.VirtualMachine) {
        _.run(vm);
    }

    @export @c fn AbortSignal__Timeout__deinit(*_) {
        // Called from ~AbortSignal() / cancelTimer(). The AbortSignal's
        // ScriptExecutionContext may be a dead global under --isolate, so
        // we resolve the VM via the threadlocal instead of taking it as a
        // parameter (which the caller would have to dereference the dead
        // context to obtain).
        _.deinit(jsc.VirtualMachine.get());
    }
}

@c {
    fn WebCore__AbortSignal__aborted(arg0: *AbortSignal): bool;
    fn WebCore__AbortSignal__abortReason(arg0: *AbortSignal): JSValue;
    fn WebCore__AbortSignal__addListener(arg0: *AbortSignal, arg1: *!?, ArgFn2: ListenerCallback?): *AbortSignal;
    fn WebCore__AbortSignal__cleanNativeBindings(arg0: *AbortSignal, arg1: *!?);
    fn WebCore__AbortSignal__create(arg0: *JSGlobalObject): JSValue;
    fn WebCore__AbortSignal__fromJS(JSValue0: JSValue): (*AbortSignal)?;
    fn WebCore__AbortSignal__ref(arg0: *AbortSignal): *AbortSignal;
    fn WebCore__AbortSignal__toJS(arg0: *AbortSignal, arg1: *JSGlobalObject): JSValue;
    fn WebCore__AbortSignal__unref(arg0: *AbortSignal);
    fn WebCore__AbortSignal__getTimeout(arg0: *AbortSignal): (*TimeoutImpl)?;
    fn WebCore__AbortSignal__signal(signal: *AbortSignal, global: *JSGlobalObject, reason: CommonAbortReason);
    fn WebCore__AbortSignal__incrementPendingActivity(signal: *AbortSignal);
    fn WebCore__AbortSignal__decrementPendingActivity(signal: *AbortSignal);
    fn WebCore__AbortSignal__reasonIfAborted(signal: *AbortSignal, global: *JSGlobalObject, reason: *u8): JSValue;
    fn WebCore__AbortSignal__new(global: *JSGlobalObject): *AbortSignal;
}

@opaque pub struct AbortSignal {
    pub const AbortReason = AbortReasonImpl;
    pub const Timeout = TimeoutImpl;

    fn listen(
        *_,
        comptime Context: type,
        ctx: *Context,
        comptime cb: *const fn (*Context, JSValue) void,
    ): *AbortSignal {
        const Wrapper = struct {
            const call = cb;
            static fn callback(
                ptr: *!?,
                reason: JSValue,
            ) callconv(.c) void {
                const val = bun.cast(*Context, ptr.?);
                call(val, reason);
            }
        };

        return _.addListener(unsafe { @as(*!?, @ptrCast(ctx)) }, Wrapper.callback);
    }

    fn addListener(
        *_,
        ctx: *!?,
        callback: ListenerCallback,
    ): *AbortSignal {
        return WebCore__AbortSignal__addListener(_, ctx, callback);
    }

    fn cleanNativeBindings(*_, ctx: *!?) {
        return WebCore__AbortSignal__cleanNativeBindings(_, ctx);
    }

    fn signal(
        *_,
        globalObject: *jsc.JSGlobalObject,
        reason: CommonAbortReason,
    ) {
        bun.analytics.Features.abort_signal += 1;
        return WebCore__AbortSignal__signal(_, globalObject, reason);
    }

    fn pendingActivityRef(*_) {
        return WebCore__AbortSignal__incrementPendingActivity(_);
    }

    fn pendingActivityUnref(*_) {
        return WebCore__AbortSignal__decrementPendingActivity(_);
    }

    /// This function is not threadsafe. aborted is a boolean, not an atomic!
    fn aborted(*_): bool {
        return WebCore__AbortSignal__aborted(_);
    }

    /// This function is not threadsafe. JSValue cannot safely be passed between threads.
    fn abortReason(*_): JSValue {
        return WebCore__AbortSignal__abortReason(_);
    }

    fn reasonIfAborted(*_, global: *jsc.JSGlobalObject): AbortReasonImpl? {
        let reason: u8 = 0;
        const js_reason = WebCore__AbortSignal__reasonIfAborted(_, global, &reason);
        if (reason > 0) {
            bun.debugAssert(js_reason.isUndefined());
            return AbortReasonImpl{ common: @enumFromInt(reason) };
        }
        if (js_reason == .zero) {
            return null; // not aborted
        }
        return AbortReasonImpl{ js: js_reason };
    }

    fn ref(*_): *AbortSignal {
        return WebCore__AbortSignal__ref(_);
    }

    fn unref(*_) {
        WebCore__AbortSignal__unref(_);
    }

    fn detach(*_, ctx: *!?) {
        _.cleanNativeBindings(ctx);
        _.unref();
    }

    static fn fromJS(value: JSValue): (*AbortSignal)? {
        return WebCore__AbortSignal__fromJS(value);
    }

    fn toJS(*_, global: *JSGlobalObject): JSValue {
        return WebCore__AbortSignal__toJS(_, global);
    }

    static fn create(global: *JSGlobalObject): JSValue {
        return WebCore__AbortSignal__create(global);
    }

    static fn new(global: *JSGlobalObject): *AbortSignal {
        jsc.markBinding(@src());
        return WebCore__AbortSignal__new(global);
    }

    /// Returns a borrowed handle to the internal Timeout, or null.
    ///
    /// Lifetime: owned by AbortSignal; may become invalid if the timer fires/cancels.
    ///
    /// Thread-safety: not thread-safe; call only on the owning thread/loop.
    ///
    /// Usage: if you need to operate on the Timeout (run/cancel/deinit), hold a ref
    /// to `this` for the duration (e.g., `this.ref(); defer this.unref();`) and avoid
    /// caching the pointer across turns.
    fn getTimeout(*_): (*TimeoutImpl)? {
        return WebCore__AbortSignal__getTimeout(_);
    }
}
