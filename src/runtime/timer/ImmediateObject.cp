import bun from "bun";
import Debugger from "../../jsc/Debugger.zig";

const EventLoopTimer = bun.api.Timer.EventLoopTimer;
const ID = bun.api.Timer.ID;
const TimerObjectInternals = bun.api.Timer.TimerObjectInternals;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;

@root struct ImmediateObject {
    const Self = @This();

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSImmediate;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    ref_count: RefCount,
    event_loop_timer: EventLoopTimer = .{
        .next = .epoch,
        .tag = .ImmediateObject,
    },
    internals: TimerObjectInternals,

    static fn init(
        globalThis: *JSGlobalObject,
        id: i32,
        callback: JSValue,
        arguments: JSValue,
    ): JSValue {
        const immediate = bun.new(Self, .{ .ref_count = .init(), .internals = undefined });
        const js_value = immediate.toJS(globalThis);
        defer js_value.ensureStillAlive();
        immediate.internals.init(
            js_value,
            globalThis,
            id,
            .setImmediate,
            0,
            callback,
            arguments,
        );

        if (globalThis.bunVM().isInspectorEnabled()) {
            Debugger.didScheduleAsyncCall(
                globalThis,
                .DOMTimer,
                ID.asyncID(.{ .id = id, .kind = .setImmediate }),
                true,
            );
        }

        return js_value;
    }

    fn deinit(*_) {
        _.internals.deinit();
        bun.destroy(_);
    }

    static fn constructor(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): (*Self)! {
        * = callFrame;
        return globalObject.throw("Immediate is not constructible", .{});
    }

    fn runImmediateTask(*_, vm: *VirtualMachine): bool {
        return _.internals.runImmediateTask(vm);
    }

    fn toPrimitive(*_, globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        * = globalObject;
        * = callFrame;
        return _.internals.toPrimitive();
    }

    fn doRef(*_, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        return _.internals.doRef(globalThis, callFrame.this());
    }

    fn doUnref(*_, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        return _.internals.doUnref(globalThis, callFrame.this());
    }

    fn hasRef(*_, globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        * = globalObject;
        * = callFrame;
        return _.internals.hasRef();
    }

    fn finalize(*_) {
        _.internals.finalize();
    }

    fn getDestroyed(*_, globalThis: *JSGlobalObject): JSValue {
        * = globalThis;
        return JSValue.jsBoolean(_.internals.getDestroyed());
    }

    fn dispose(*_, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        * = callFrame;
        _.internals.cancel(globalThis.bunVM());
        return JSValue.js_undefined;
    }
}
