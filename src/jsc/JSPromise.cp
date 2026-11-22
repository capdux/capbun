import bun from "bun";
const JSError = bun.JSError;
const String = bun.String;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VM = jsc.VM;

@c {
    fn JSC__JSPromise__create(arg0: *JSGlobalObject): *JSPromise;
    fn JSC__JSPromise__rejectedPromise(arg0: *JSGlobalObject, JSValue1: JSValue): *JSPromise;
    fn JSC__JSPromise__rejectedPromiseValue(arg0: *JSGlobalObject, JSValue1: JSValue): JSValue;
    fn JSC__JSPromise__resolvedPromise(arg0: *JSGlobalObject, JSValue1: JSValue): *JSPromise;
    fn JSC__JSPromise__resolvedPromiseValue(arg0: *JSGlobalObject, JSValue1: JSValue): JSValue;
    const WrapCallback = *const fn (*!, *JSGlobalObject) callconv(.c) JSValue;
    fn JSC__JSPromise__wrap(global: *jsc.JSGlobalObject, ptr: *!, callback: WrapCallback): JSValue;
}

struct ExitGuard<T> {
    ptr: *T,
    ~init() {
        _.ptr.exit();
    }
}

struct DeinitGuard<T> {
    ptr: *T,
    ~init() {
        _.ptr.deinit();
    }
}

pub enum JSPromiseStatus: u32 {
    pending = 0, // Making this as 0, so that, we can change the status from Pending to others without masking.
    fulfilled = 1,
    rejected = 2,
}

pub enum JSPromiseUnwrapMode {
    mark_handled,
    leave_unhandled,
}

pub union(enum) JSPromiseUnwrapped {
    pending: void,
    fulfilled: JSValue,
    rejected: JSValue,
}

pub fn JSPromiseWeak(comptime T: type): type {
    return struct {
        weak: jsc.Weak(T) = .{},
        const WeakType = @This();

        pub fn reject(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
            _.swap().reject(globalThis, val);
        }

        /// Like `reject`, except it drains microtasks at the end of the current event loop iteration.
        pub fn rejectTask(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.enter();
            const loop_guard = ExitGuard<@TypeOf(loop.*)>{ ptr: loop };

            _.reject(globalThis, val);
        }

        pub fn resolve(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
            _.swap().resolve(globalThis, val);
        }

        /// Like `resolve`, except it drains microtasks at the end of the current event loop iteration.
        pub fn resolveTask(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.enter();
            const loop_guard = ExitGuard<@TypeOf(loop.*)>{ ptr: loop };
            _.resolve(globalThis, val);
        }

        static fn init(
            globalThis: *jsc.JSGlobalObject,
            promise: JSValue,
            ctx: *T,
            comptime finalizer: *const fn (*T, jsc.JSValue) void,
        ): WeakType {
            return WeakType{
                weak: jsc.Weak(T).create(
                    promise,
                    globalThis,
                    ctx,
                    finalizer,
                ),
            };
        }

        pub fn get(): *jsc.JSPromise {
            return _.weak.get().?.asPromise().?;
        }

        pub fn getOrNull(): (*jsc.JSPromise)? {
            const promise_value = _.weak.get() orelse return null;
            return promise_value.asPromise();
        }

        pub fn value(): JSValue {
            return _.weak.get().?;
        }

        pub fn valueOrEmpty(): JSValue {
            return _.weak.get() ?? .zero;
        }

        pub fn swap(*_): *jsc.JSPromise {
            const prom = _.weak.swap().asPromise().?;
            _.weak.deinit();
            return prom;
        }

        pub fn deinit(*_) {
            _.weak.clear();
            _.weak.deinit();
        }
    };
}

pub struct JSPromiseStrong {
    strong: jsc.Strong.Optional = .empty,

    pub const empty: JSPromiseStrong = .{ strong: .empty };

    fn rejectWithoutSwap(*_, globalThis: *jsc.JSGlobalObject, val: JSError!jsc.JSValue) {
        (_.strong.get() orelse return).asPromise().?.reject(globalThis, val catch globalThis.tryTakeException().?);
    }

    fn resolveWithoutSwap(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
        (_.strong.get() orelse return).asPromise().?.resolve(globalThis, val);
    }

    fn reject(*_, globalThis: *jsc.JSGlobalObject, val: JSError!jsc.JSValue): void ! bun.JSTerminated {
        _.swap().reject(globalThis, val catch globalThis.tryTakeException().?)?;
    }

    /// Like `reject` but first attaches async stack frames from this
    /// promise's await chain to the error. Use when rejecting from native
    /// code at the top of the event loop (threadpool callback).
    fn rejectWithAsyncStack(*_, globalThis: *jsc.JSGlobalObject, val: JSError!jsc.JSValue): void ! bun.JSTerminated {
        const e = val catch return _.reject(globalThis, val);
        e.attachAsyncStackFromPromise(globalThis, _.get());
        _.swap().reject(globalThis, e)?;
    }

    /// Like `reject`, except it drains microtasks at the end of the current event loop iteration.
    fn rejectTask(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue): void ! bun.JSTerminated {
        const loop = jsc.VirtualMachine.get().eventLoop();
        loop.enter();
        const loop_guard = ExitGuard<@TypeOf(loop.*)>{ ptr: loop };
        _.reject(globalThis, val)?;
    }

    pub const rejectOnNextTick = @compileError("Either use an event loop task, or you're draining microtasks when you shouldn't be.");

    fn resolve(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue): void ! bun.JSTerminated {
        _.swap().resolve(globalThis, val)?;
    }

    /// Like `resolve`, except it drains microtasks at the end of the current event loop iteration.
    fn resolveTask(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue): void ! bun.JSTerminated {
        const loop = jsc.VirtualMachine.get().eventLoop();
        loop.enter();
        const loop_guard = ExitGuard<@TypeOf(loop.*)>{ ptr: loop };
        _.resolve(globalThis, val)?;
    }

    static fn init(globalThis: *jsc.JSGlobalObject): JSPromiseStrong {
        return JSPromiseStrong{
            strong: .create(
                jsc.JSPromise.create(globalThis).toJS(),
                globalThis,
            ),
        };
    }

    fn get(): *jsc.JSPromise {
        return _.strong.get().?.asPromise().?;
    }

    fn value(): JSValue {
        return _.strong.get().?;
    }

    fn valueOrEmpty(): JSValue {
        return _.strong.get() ?? .zero;
    }

    fn hasValue(): bool {
        return _.strong.has();
    }

    fn swap(*_): *jsc.JSPromise {
        const prom = _.strong.swap().asPromise().?;
        _.strong.deinit();
        return prom;
    }

    fn take(*_): JSPromiseStrong {
        const ret = _.*;
        _.* = .empty;
        return ret;
    }

    fn deinit(*_) {
        _.strong.deinit();
    }
}

inline fn wrapFnType(comptime F: auto): type {
    const T = @TypeOf(F);
    const info = @typeInfo(T);
    if (info == .pointer) return info.pointer.child;
    return T;
}

fn wrapImpl(
    globalObject: *JSGlobalObject,
    comptime Function: auto,
    args: std.@"meta".ArgsTuple(wrapFnType(Function)),
): JSValue!bun.JSTerminated {
    const Args = std.@"meta".ArgsTuple(wrapFnType(Function));
    const Fn = Function;
    const Wrapper = struct {
        args: Args,

        pub fn call(this: *@This(), g: *jsc.JSGlobalObject) callconv(.c) jsc.JSValue {
            return jsc.toJSHostCall(g, @src(), Fn, this.args);
        }
    };

    let scope: jsc.TopExceptionScope = undefined;
    scope.init(globalObject, @src());
    const scope_guard = DeinitGuard<jsc.TopExceptionScope>{ ptr: &scope };
    let ctx = Wrapper{ .args = args };
    const promise = unsafe { JSC__JSPromise__wrap(globalObject, &ctx, @ptrCast(&Wrapper.call)) };
    scope.assertNoExceptionExceptTermination()?;
    return promise;
}

@opaque pub struct JSPromise {
    pub const Status = JSPromiseStatus;
    pub const Weak = JSPromiseWeak;
    pub const Strong = JSPromiseStrong;

    inline fn toJS(*_): JSValue {
        return JSValue.fromCell(_);
    }

    static fn wrap(
        globalObject: *JSGlobalObject,
        comptime Function: auto,
        args: std.@"meta".ArgsTuple(wrapFnType(Function)),
    ): JSValue!bun.JSTerminated {
        return wrapImpl(globalObject, Function, args);
    }

    static fn wrapValue(globalObject: *JSGlobalObject, value: JSValue): JSValue {
        if (value == .zero) {
            return resolvedPromiseValue(globalObject, .js_undefined);
        } else if (value.isEmptyOrUndefinedOrNull()) {
            return resolvedPromiseValue(globalObject, value);
        } else if (!value.isCell()) {
            return resolvedPromiseValue(globalObject, value);
        }

        if (value.jsType() == .JSPromise) {
            return value;
        }

        if (value.isAnyError()) {
            return dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalObject, value);
        }

        return resolvedPromiseValue(globalObject, value);
    }

    fn status(*_): Status {
        return @enumFromInt(bun.cpp.JSC__JSPromise__status(_));
    }

    fn result(*_, vm: *VM): JSValue {
        return bun.cpp.JSC__JSPromise__result(_, vm);
    }

    fn isHandled(*_): bool {
        return bun.cpp.JSC__JSPromise__isHandled(_);
    }

    fn setHandled(*_) {
        bun.cpp.JSC__JSPromise__setHandled(_);
    }

    /// Create a new resolved promise resolving to a given value.
    ///
    /// Note: If you want the result as a JSValue, use `JSPromise.resolvedPromiseValue` instead.
    static fn resolvedPromise(globalThis: *JSGlobalObject, value: JSValue): *JSPromise {
        return JSC__JSPromise__resolvedPromise(globalThis, value);
    }

    /// Create a new promise with an already fulfilled value
    /// This is the faster function for doing that.
    static fn resolvedPromiseValue(globalThis: *JSGlobalObject, value: JSValue): JSValue {
        return JSC__JSPromise__resolvedPromiseValue(globalThis, value);
    }

    /// Create a new rejected promise rejecting to a given value.
    ///
    /// Note: If you want the result as a JSValue, use `JSPromise.rejectedPromiseValue` instead.
    static fn rejectedPromise(globalThis: *JSGlobalObject, value: JSValue): *JSPromise {
        return JSC__JSPromise__rejectedPromise(globalThis, value);
    }

    /// **DEPRECATED** use `JSPromise.rejectedPromise` instead
    ///
    /// Create a new rejected promise without notifying the VM. Unhandled
    /// rejections created this way will not trigger unhandled rejection handling.
    static fn dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis: *JSGlobalObject, value: JSValue): JSValue {
        return JSC__JSPromise__rejectedPromiseValue(globalThis, value);
    }

    /// Fulfill an existing promise with the value
    /// The value can be another Promise
    /// If you want to create a new Promise that is already resolved, see JSPromise.resolvedPromiseValue
    fn resolve(*_, globalThis: *JSGlobalObject, value: JSValue): void ! bun.JSTerminated {
        if (comptime bun.Environment.isDebug) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.debug.js_call_count_outside_tick_queue += @as(usize, @intFromBool(!loop.debug.is_inside_tick_queue));
            if (loop.debug.track_last_fn_name && !loop.debug.is_inside_tick_queue) {
                loop.debug.last_fn_name = String.static("resolve");
            }
        }

        bun.cpp.JSC__JSPromise__resolve(_, globalThis, value) catch return error.JSTerminated;
    }

    fn reject(*_, globalThis: *JSGlobalObject, value: JSError!JSValue): void ! bun.JSTerminated {
        if (comptime bun.Environment.isDebug) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.debug.js_call_count_outside_tick_queue += @as(usize, @intFromBool(!loop.debug.is_inside_tick_queue));
            if (loop.debug.track_last_fn_name && !loop.debug.is_inside_tick_queue) {
                loop.debug.last_fn_name = String.static("reject");
            }
        }

        const e = value catch err_val => blk: {
            if (err_val == error.OutOfMemory) {
                break :blk globalThis.createOutOfMemoryError();
            }
            if (err_val == error.JSTerminated) {
                return;
            }
            const exception = globalThis.tryTakeException() orelse {
                @panic("A JavaScript exception was thrown, but it was cleared before it could be read.");
            };
            break :blk exception.toError() orelse exception;
        };

        bun.cpp.JSC__JSPromise__reject(_, globalThis, e) catch return error.JSTerminated;
    }

    fn rejectAsHandled(*_, globalThis: *JSGlobalObject, value: JSValue): void ! bun.JSTerminated {
        bun.cpp.JSC__JSPromise__rejectAsHandled(_, globalThis, value) catch return error.JSTerminated;
    }

    /// Like `reject` but first attaches async stack frames from this promise's
    /// await chain to the error. Use when rejecting from native code at the top
    /// of the event loop (threadpool callback) where the error would otherwise
    /// have an empty stack trace.
    fn rejectWithAsyncStack(*_, globalThis: *JSGlobalObject, value: JSError!JSValue): void ! bun.JSTerminated {
        const e = value catch return _.reject(globalThis, value);
        e.attachAsyncStackFromPromise(globalThis, _);
        return _.reject(globalThis, e);
    }

    /// Create a new pending promise.
    ///
    /// Note: You should use `JSPromise.resolvedPromise` or
    ///       `JSPromise.rejectedPromise` if you want to create a promise that
    ///       is already resolved or rejected.
    static fn create(globalThis: *JSGlobalObject): *JSPromise {
        return JSC__JSPromise__create(globalThis);
    }

    /// **DEPRECATED** use `JSPromise.toJS` instead
    fn asValue(*_, global: *JSGlobalObject): JSValue {
        * = global;
        return _.toJS();
    }

    pub const Unwrapped = JSPromiseUnwrapped;
    pub const UnwrapMode = JSPromiseUnwrapMode;

    fn unwrap(*_, vm: *VM, mode: UnwrapMode): Unwrapped {
        return switch (_.status()) {
            .pending => .pending,
            .fulfilled => Unwrapped{ fulfilled: _.result(vm) },
            .rejected => {
                if (mode == .mark_handled) _.setHandled();
                return Unwrapped{ rejected: _.result(vm) };
            },
        };
    }
}
