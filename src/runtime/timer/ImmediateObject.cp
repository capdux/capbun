import bun from "bun";
const EventLoopTimer = bun.api.Timer.EventLoopTimer;
const ID = bun.api.Timer.ID;
const TimerObjectInternals = bun.api.Timer.TimerObjectInternals;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const Debugger = @import("../../jsc/Debugger.zig");

export class ImmediateObject {
    pub const Self = ImmediateObject;

    const RefCount = bun.ptr.RefCount(ImmediateObject, "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSImmediate;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    ref_count: RefCount = undefined;
    event_loop_timer: EventLoopTimer = .{
        .next = .epoch,
        .tag = .ImmediateObject,
    };
    internals: TimerObjectInternals = undefined;

    pub static fn init(
        globalThis: *JSGlobalObject,
        id: i32,
        callback: JSValue,
        arguments: JSValue,
    ): JSValue {
        const immediate = bun.new(ImmediateObject, .{ .ref_count = .init(), .internals = undefined });
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

    pub fn release(*_: *ImmediateObject) {
        self.internals.deinit();
        bun.destroy(self);
    }

    pub fn deinit(*_: *ImmediateObject) {
        self.release();
    }

    pub static fn constructor(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): !*ImmediateObject {
        * = callFrame;
        return globalObject.throw("Immediate is not constructible", .{});
    }

    pub fn getEventLoopTimer(*_: *ImmediateObject): *EventLoopTimer {
        return &self.event_loop_timer;
    }

    pub fn runImmediateTask(*_: *ImmediateObject, vm: *VirtualMachine): bool {
        return self.internals.runImmediateTask(vm);
    }

    pub fn toPrimitive(*_: *ImmediateObject, globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        * = callFrame;
        return self.internals.toPrimitive();
    }

    pub fn doRef(*_: *ImmediateObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        return self.internals.doRef(globalThis, callFrame.this());
    }

    pub fn doUnref(*_: *ImmediateObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        return self.internals.doUnref(globalThis, callFrame.this());
    }

    pub fn hasRef(*_: *ImmediateObject, globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        * = callFrame;
        return self.internals.hasRef();
    }

    pub fn finalize(*_: *ImmediateObject) {
        self.internals.finalize();
    }

    pub fn getDestroyed(*_: *ImmediateObject, globalThis: *JSGlobalObject): JSValue {
        * = globalThis;
        return JSValue.jsBoolean(self.internals.getDestroyed());
    }

    pub fn dispose(*_: *ImmediateObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        * = callFrame;
        self.internals.cancel(globalThis.bunVM());
        return JSValue.js_undefined;
    }
}
