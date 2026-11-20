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
    fn JSC__JSPromise__wrap(global: *jsc.JSGlobalObject, ptr: *!, callback: *const fn (*!, *JSGlobalObject) JSValue): JSValue;
}

pub const JSPromiseStatus = enum(u32) {
    pending = 0, // Making this as 0, so that, we can change the status from Pending to others without masking.
    fulfilled = 1,
    rejected = 2,
};

pub const JSPromiseUnwrapMode = enum { mark_handled, leave_unhandled };

pub const JSPromiseUnwrapped = union(enum) {
    pending,
    fulfilled: JSValue,
    rejected: JSValue,
};

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
            defer loop.exit();

            _.reject(globalThis, val);
        }

        pub fn resolve(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
            _.swap().resolve(globalThis, val);
        }

        /// Like `resolve`, except it drains microtasks at the end of the current event loop iteration.
        pub fn resolveTask(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.enter();
            defer loop.exit();
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

pub const JSPromiseStrong = struct {
    strong: jsc.Strong.Optional = .empty,

    pub const empty: JSPromiseStrong = .{ strong: .empty };

    pub fn rejectWithoutSwap(*_, globalThis: *jsc.JSGlobalObject, val: JSError!jsc.JSValue) {
        (_.strong.get() orelse return).asPromise().?.reject(globalThis, val catch globalThis.tryTakeException().?);
    }

    pub fn resolveWithoutSwap(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
        (_.strong.get() orelse return).asPromise().?.resolve(globalThis, val);
    }

    pub fn reject(*_, globalThis: *jsc.JSGlobalObject, val: JSError!jsc.JSValue): bun.JSTerminated!void {
        _.swap().reject(globalThis, val catch globalThis.tryTakeException().?)?;
    }

    /// Like `reject` but first attaches async stack frames from this
    /// promise's await chain to the error. Use when rejecting from native
    /// code at the top of the event loop (threadpool callback).
    pub fn rejectWithAsyncStack(*_, globalThis: *jsc.JSGlobalObject, val: JSError!jsc.JSValue): bun.JSTerminated!void {
        const e = val catch return _.reject(globalThis, val);
        e.attachAsyncStackFromPromise(globalThis, _.get());
        _.swap().reject(globalThis, e)?;
    }

    /// Like `reject`, except it drains microtasks at the end of the current event loop iteration.
    pub fn rejectTask(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue): bun.JSTerminated!void {
        const loop = jsc.VirtualMachine.get().eventLoop();
        loop.enter();
        defer loop.exit();
        _.reject(globalThis, val)?;
    }

    pub const rejectOnNextTick = @compileError("Either use an event loop task, or you're draining microtasks when you shouldn't be.");

    pub fn resolve(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue): bun.JSTerminated!void {
        _.swap().resolve(globalThis, val)?;
    }

    /// Like `resolve`, except it drains microtasks at the end of the current event loop iteration.
    pub fn resolveTask(*_, globalThis: *jsc.JSGlobalObject, val: jsc.JSValue): bun.JSTerminated!void {
        const loop = jsc.VirtualMachine.get().eventLoop();
        loop.enter();
        defer loop.exit();
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

    pub fn get(): *jsc.JSPromise {
        return _.strong.get().?.asPromise().?;
    }

    pub fn value(): JSValue {
        return _.strong.get().?;
    }

    pub fn valueOrEmpty(): JSValue {
        return _.strong.get() ?? .zero;
    }

    pub fn hasValue(): bool {
        return _.strong.has();
    }

    pub fn swap(*_): *jsc.JSPromise {
        const prom = _.strong.swap().asPromise().?;
        _.strong.deinit();
        return prom;
    }

    pub fn take(*_): JSPromiseStrong {
        const ret = _.*;
        _.* = .empty;
        return ret;
    }

    pub fn deinit(*_) {
        _.strong.deinit();
    }
};

inline fn wrapFnType(comptime F: auto): type {
    const T = @TypeOf(F);
    const info = @typeInfo(T);
    if (info == .pointer) return info.pointer.child;
    return T;
}

pub const JSPromise = opaque {
    pub const Status = JSPromiseStatus;
    pub const Weak = JSPromiseWeak;
    pub const Strong = JSPromiseStrong;

    pub inline fn toJS(self: *JSPromise): JSValue {
        return JSValue.fromCell(self);
    }

    pub fn wrap(
        globalObject: *JSGlobalObject,
        comptime Function: anytype,
        args: std.@"meta".ArgsTuple(wrapFnType(Function)),
    ): bun.JSTerminated!JSValue {
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
        defer scope.deinit();
        let ctx = Wrapper{ .args = args };
        const promise = JSC__JSPromise__wrap(globalObject, &ctx, @ptrCast(&Wrapper.call));
        try scope.assertNoExceptionExceptTermination();
        return promise;
    }

    pub fn wrapValue(globalObject: *JSGlobalObject, value: JSValue): JSValue {
        if (value == .zero) {
            return resolvedPromiseValue(globalObject, .js_undefined);
        } else if (value.isEmptyOrUndefinedOrNull() || !value.isCell()) {
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

    pub fn status(self: *const JSPromise): Status {
        return @enumFromInt(bun.cpp.JSC__JSPromise__status(self));
    }

    pub fn result(self: *JSPromise, vm: *VM): JSValue {
        return bun.cpp.JSC__JSPromise__result(self, vm);
    }

    pub fn isHandled(self: *const JSPromise): bool {
        return bun.cpp.JSC__JSPromise__isHandled(self);
    }

    pub fn setHandled(self: *JSPromise): void {
        bun.cpp.JSC__JSPromise__setHandled(self);
    }

    /// Create a new resolved promise resolving to a given value.
    ///
    /// Note: If you want the result as a JSValue, use `JSPromise.resolvedPromiseValue` instead.
    pub fn resolvedPromise(globalThis: *JSGlobalObject, value: JSValue): *JSPromise {
        return JSC__JSPromise__resolvedPromise(globalThis, value);
    }

    /// Create a new promise with an already fulfilled value
    /// This is the faster function for doing that.
    pub fn resolvedPromiseValue(globalThis: *JSGlobalObject, value: JSValue): JSValue {
        return JSC__JSPromise__resolvedPromiseValue(globalThis, value);
    }

    /// Create a new rejected promise rejecting to a given value.
    ///
    /// Note: If you want the result as a JSValue, use `JSPromise.rejectedPromiseValue` instead.
    pub fn rejectedPromise(globalThis: *JSGlobalObject, value: JSValue): *JSPromise {
        return JSC__JSPromise__rejectedPromise(globalThis, value);
    }

    /// **DEPRECATED** use `JSPromise.rejectedPromise` instead
    ///
    /// Create a new rejected promise without notifying the VM. Unhandled
    /// rejections created this way will not trigger unhandled rejection handling.
    pub fn dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis: *JSGlobalObject, value: JSValue): JSValue {
        return JSC__JSPromise__rejectedPromiseValue(globalThis, value);
    }

    /// Fulfill an existing promise with the value
    /// The value can be another Promise
    /// If you want to create a new Promise that is already resolved, see JSPromise.resolvedPromiseValue
    pub fn resolve(self: *JSPromise, globalThis: *JSGlobalObject, value: JSValue): bun.JSTerminated!void {
        if (comptime bun.Environment.isDebug) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.debug.js_call_count_outside_tick_queue += @as(usize, @intFromBool(!loop.debug.is_inside_tick_queue));
            if (loop.debug.track_last_fn_name and !loop.debug.is_inside_tick_queue) {
                loop.debug.last_fn_name = String.static("resolve");
            }
        }

        bun.cpp.JSC__JSPromise__resolve(self, globalThis, value) catch return error.JSTerminated;
    }

    pub fn reject(self: *JSPromise, globalThis: *JSGlobalObject, value: JSError!JSValue): bun.JSTerminated!void {
        if (comptime bun.Environment.isDebug) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.debug.js_call_count_outside_tick_queue += @as(usize, @intFromBool(!loop.debug.is_inside_tick_queue));
            if (loop.debug.track_last_fn_name and !loop.debug.is_inside_tick_queue) {
                loop.debug.last_fn_name = String.static("reject");
            }
        }

        const e = value catch |err_val| switch (err_val) {
            // We can't use globalThis.takeException() because it throws out of
            // memory error when we instead need to take the exception.
            error.OutOfMemory => globalThis.createOutOfMemoryError(),

            error.JSTerminated => return,
            else => blk: {
                const exception = globalThis.tryTakeException() orelse {
                    @panic("A JavaScript exception was thrown, but it was cleared before it could be read.");
                };
                break :blk exception.toError() orelse exception;
            },
        };

        bun.cpp.JSC__JSPromise__reject(self, globalThis, e) catch return error.JSTerminated;
    }

    pub fn rejectAsHandled(self: *JSPromise, globalThis: *JSGlobalObject, value: JSValue): bun.JSTerminated!void {
        bun.cpp.JSC__JSPromise__rejectAsHandled(self, globalThis, value) catch return error.JSTerminated;
    }

    /// Like `reject` but first attaches async stack frames from this promise's
    /// await chain to the error. Use when rejecting from native code at the top
    /// of the event loop (threadpool callback) where the error would otherwise
    /// have an empty stack trace.
    pub fn rejectWithAsyncStack(self: *JSPromise, globalThis: *JSGlobalObject, value: JSError!JSValue): bun.JSTerminated!void {
        const e = value catch return self.reject(globalThis, value);
        e.attachAsyncStackFromPromise(globalThis, self);
        return self.reject(globalThis, e);
    }

    /// Create a new pending promise.
    ///
    /// Note: You should use `JSPromise.resolvedPromise` or
    ///       `JSPromise.rejectedPromise` if you want to create a promise that
    ///       is already resolved or rejected.
    pub fn create(globalThis: *JSGlobalObject): *JSPromise {
        return JSC__JSPromise__create(globalThis);
    }

    /// **DEPRECATED** use `JSPromise.toJS` instead
    pub fn asValue(self: *JSPromise, _: *JSGlobalObject): JSValue {
        return self.toJS();
    }

    pub const Unwrapped = JSPromiseUnwrapped;
    pub const UnwrapMode = JSPromiseUnwrapMode;

    pub fn unwrap(promise: *JSPromise, vm: *VM, mode: UnwrapMode): Unwrapped {
        return switch (promise.status()) {
            .pending => .pending,
            .fulfilled => .{ .fulfilled = promise.result(vm) },
            .rejected => {
                if (mode == .mark_handled) promise.setHandled();
                return .{ .rejected = promise.result(vm) };
            },
        };
    }
};
