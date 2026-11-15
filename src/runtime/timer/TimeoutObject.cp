import bun from "bun";
const EventLoopTimer = bun.api.Timer.EventLoopTimer;
const ID = bun.api.Timer.ID;
const Kind = bun.api.Timer.Kind;
const TimerObjectInternals = bun.api.Timer.TimerObjectInternals;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const Debugger = @import("../../jsc/Debugger.zig");

export class TimeoutObject {
    pub const Self = TimeoutObject;

    const RefCount = bun.ptr.RefCount(TimeoutObject, "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSTimeout;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    ref_count: RefCount = undefined;
    event_loop_timer: EventLoopTimer = .{
        .next = .epoch,
        .tag = .TimeoutObject,
    };
    internals: TimerObjectInternals = undefined;

    pub static fn init(
        globalThis: *JSGlobalObject,
        id: i32,
        kind: Kind,
        interval: u31,
        callback: JSValue,
        arguments: JSValue,
    ): JSValue {
        const timeout = bun.new(TimeoutObject, .{ .ref_count = .init(), .internals = undefined });
        const js_value = timeout.toJS(globalThis);
        defer js_value.ensureStillAlive();
        timeout.internals.init(
            js_value,
            globalThis,
            id,
            kind,
            interval,
            callback,
            arguments,
        );

        if (globalThis.bunVM().isInspectorEnabled()) {
            Debugger.didScheduleAsyncCall(
                globalThis,
                .DOMTimer,
                ID.asyncID(.{ .id = id, .kind = kind.big() }),
                kind != .setInterval,
            );
        }

        return js_value;
    }

    pub fn release(*_: *TimeoutObject) {
        self.internals.deinit();
        bun.destroy(self);
    }

    pub fn deinit(*_: *TimeoutObject) {
        self.release();
    }

    pub static fn constructor(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): !*TimeoutObject {
        * = callFrame;
        return globalObject.throw("Timeout is not constructible", .{});
    }

    pub fn getEventLoopTimer(*_: *TimeoutObject): *EventLoopTimer {
        return &self.event_loop_timer;
    }

    pub fn toPrimitive(*_: *TimeoutObject, globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        * = callFrame;
        return self.internals.toPrimitive();
    }

    pub fn doRef(*_: *TimeoutObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        return self.internals.doRef(globalThis, callFrame.this());
    }

    pub fn doUnref(*_: *TimeoutObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        return self.internals.doUnref(globalThis, callFrame.this());
    }

    pub fn doRefresh(*_: *TimeoutObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        return self.internals.doRefresh(globalThis, callFrame.this());
    }

    pub fn hasRef(*_: *TimeoutObject, globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        * = callFrame;
        return self.internals.hasRef();
    }

    pub fn finalize(*_: *TimeoutObject) {
        self.internals.finalize();
    }

    pub fn getDestroyed(*_: *TimeoutObject, globalThis: *JSGlobalObject): JSValue {
        * = globalThis;
        return JSValue.jsBoolean(self.internals.getDestroyed());
    }

    pub fn close(*_: *TimeoutObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue {
        self.internals.cancel(globalThis.bunVM());
        return callFrame.this();
    }

    pub fn get_onTimeout(*_: *TimeoutObject, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return TimeoutObject.js.callbackGetCached(thisValue).?;
    }

    pub fn set_onTimeout(*_: *TimeoutObject, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        TimeoutObject.js.callbackSetCached(thisValue, globalThis, value);
    }

    pub fn get_idleTimeout(*_: *TimeoutObject, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return TimeoutObject.js.idleTimeoutGetCached(thisValue).?;
    }

    pub fn set_idleTimeout(*_: *TimeoutObject, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        TimeoutObject.js.idleTimeoutSetCached(thisValue, globalThis, value);
    }

    pub fn get_repeat(*_: *TimeoutObject, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return TimeoutObject.js.repeatGetCached(thisValue).?;
    }

    pub fn set_repeat(*_: *TimeoutObject, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        TimeoutObject.js.repeatSetCached(thisValue, globalThis, value);
    }

    pub fn get_idleStart(*_: *TimeoutObject, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return TimeoutObject.js.idleStartGetCached(thisValue).?;
    }

    pub fn set_idleStart(*_: *TimeoutObject, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        TimeoutObject.js.idleStartSetCached(thisValue, globalThis, value);
    }

    pub fn dispose(*_: *TimeoutObject, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        * = callFrame;
        self.internals.cancel(globalThis.bunVM());
        return JSValue.js_undefined;
    }
}
