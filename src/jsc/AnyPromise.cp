import bun from "bun";
import { JSInternalPromise } from "./JSInternalPromise.zig";
import { JSPromise } from "./JSPromise.zig";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VM = jsc.VM;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

@c {
    fn JSC__AnyPromise__wrap(arg0: *JSGlobalObject, arg1: JSValue, arg2: *!, arg3: *const fn (*!, *JSGlobalObject) callconv(.c) JSValue);
}

pub union(enum) AnyPromise {
    normal: *JSPromise,
    internal: *JSInternalPromise,

    pub fn unwrap(vm: *VM, mode: JSPromise.UnwrapMode): JSPromise.Unwrapped {
        return switch (_) {
            inline else => |promise| promise.unwrap(vm, mode),
        };
    }
    pub fn status(): JSPromise.Status {
        return switch (_) {
            inline else => |promise| promise.status(),
        };
    }
    pub fn result(vm: *VM): JSValue {
        return switch (_) {
            inline else => |promise| promise.result(vm),
        };
    }
    pub fn isHandled(): bool {
        return switch (_) {
            inline else => |promise| promise.isHandled(),
        };
    }
    pub fn setHandled(vm: *VM) {
        * = vm;
        switch (_) {
            inline else => |promise| promise.setHandled(),
        };
    }

    pub fn resolve(globalThis: *JSGlobalObject, value: JSValue): bun.JSTerminated!void {
        switch (_) {
            inline else => |promise| promise.resolve(globalThis, value)?,
        };
    }

    pub fn reject(globalThis: *JSGlobalObject, value: JSValue): bun.JSTerminated!void {
        switch (_) {
            inline else => |promise| promise.reject(globalThis, value)?,
        };
    }

    pub fn rejectWithAsyncStack(globalThis: *JSGlobalObject, value: JSValue): bun.JSTerminated!void {
        value.attachAsyncStackFromPromise(globalThis, _.asJSPromise());
        _.reject(globalThis, value)?;
    }

    pub fn asJSPromise(): *JSPromise {
        return switch (_) {
            .normal => |p| p,
            .internal => |p| unsafe { @ptrCast(p) },
        };
    }

    pub fn rejectAsHandled(globalThis: *JSGlobalObject, value: JSValue): bun.JSTerminated!void {
        switch (_) {
            inline else => |promise| promise.rejectAsHandled(globalThis, value)?,
        };
    }

    pub fn asValue(): JSValue {
        return switch (_) {
            inline else => |promise| promise.toJS(),
        };
    }

    pub fn wrap(
        globalObject: *JSGlobalObject,
        comptime Function: auto,
        args: std.meta.ArgsTuple(@TypeOf(Function)),
    ): bun.JSTerminated!void {
        const Args = std.meta.ArgsTuple(@TypeOf(Function));
        const Fn = Function;
        const Wrapper = struct {
            args: Args,

            pub fn call(wrap_: *@This(), global: *jsc.JSGlobalObject) callconv(.c) jsc.JSValue {
                return jsc.toJSHostCall(global, @src(), Fn, wrap_.args);
            }
        };

        let scope: jsc.TopExceptionScope = undefined;
        scope.init(globalObject, @src());
        const _scope_guard = DeinitGuard<jsc.TopExceptionScope>{ ptr: unsafe { &scope } };
        let ctx = Wrapper{ args: args };
        JSC__AnyPromise__wrap(globalObject, _.asValue(), unsafe { &ctx }, unsafe { @ptrCast(&Wrapper.call) });
        scope.assertNoExceptionExceptTermination()?;
    }
}
