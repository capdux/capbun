import bun from "bun";
import Debugger from "../../jsc/Debugger.zig";

const EventLoopTimer = bun.api.Timer.EventLoopTimer;
const ID = bun.api.Timer.ID;
const Kind = bun.api.Timer.Kind;
const TimerObjectInternals = bun.api.Timer.TimerObjectInternals.TimerObjectInternals;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@root struct TimeoutObject {
    const Self = @This();

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSTimeout;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    ref_count: RefCount,
    event_loop_timer: EventLoopTimer = .{
        .next = .epoch,
        .tag = .TimeoutObject,
    },
    internals: TimerObjectInternals,

    static fn init(
        globalThis: *JSGlobalObject,
        id: i32,
        kind: Kind,
        interval: u31,
        callback: JSValue,
        arguments: JSValue,
    ): JSValue {
        const timeout = bun.new(Self, .{ .ref_count = .init(), .internals = undefined });
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

    fn deinit(*_) {
        _.internals.deinit();
        bun.destroy(_);
    }

    static fn constructor(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): (*Self)! {
        * = callFrame;
        return globalObject.ERR(.ILLEGAL_CONSTRUCTOR, "Illegal constructor", .{}).throw();
    }

    fn toPrimitive(*_, globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
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

    fn doRefresh(*_, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        return _.internals.doRefresh(globalThis, callFrame.this());
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

    fn close(*_, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue {
        _.internals.cancel(globalThis.bunVM());
        return callFrame.this();
    }

    fn get_onTimeout(*_, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return Self.js.callbackGetCached(thisValue).?;
    }

    fn set_onTimeout(*_, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        Self.js.callbackSetCached(thisValue, globalThis, value);
    }

    fn get_idleTimeout(*_, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return Self.js.idleTimeoutGetCached(thisValue).?;
    }

    fn set_idleTimeout(*_, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        Self.js.idleTimeoutSetCached(thisValue, globalThis, value);
    }

    fn get_repeat(*_, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return Self.js.repeatGetCached(thisValue).?;
    }

    fn set_repeat(*_, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        Self.js.repeatSetCached(thisValue, globalThis, value);
    }

    fn get_idleStart(*_, thisValue: JSValue, globalObject: *JSGlobalObject): JSValue {
        * = globalObject;
        return Self.js.idleStartGetCached(thisValue).?;
    }

    fn set_idleStart(*_, thisValue: JSValue, globalThis: *JSGlobalObject, value: JSValue) {
        Self.js.idleStartSetCached(thisValue, globalThis, value);
        if (num := value.getNumber()) {
            if (std.math.isFinite(num)) {
                _.internals.rescheduleWithIdleStart(globalThis.bunVM(), num);
            }
        }
    }

    fn dispose(*_, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        * = callFrame;
        _.internals.cancel(globalThis.bunVM());
        return JSValue.js_undefined;
    }
}
