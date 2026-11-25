//! CP view of `Bun::JSSocketHandlers` (`src/jsc/bindings/JSSocketHandlers.cpp`):
//! a GC-visited internal-fields cell holding a socket context's JS callbacks
//! and its pending `Bun.connect` promise.

import bun from "bun";
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const Strong = jsc.Strong;

@c {
    fn Bun__SocketHandlers__create(global: *JSGlobalObject, callbacks: [*]const JSValue): JSValue;
    fn Bun__SocketHandlers__getField(cell: JSValue, index: u32): JSValue;
    fn Bun__SocketHandlers__setField(global: *JSGlobalObject, cell: JSValue, index: u32, value: JSValue);
    fn Bun__SocketHandlers__setCallbacks(global: *JSGlobalObject, cell: JSValue, callbacks: [*]const JSValue);
}

enum Field: u32 {
    Open = 0,
    Close,
    Data,
    Writable,
    Timeout,
    ConnectError,
    End,
    Error,
    Handshake,
    Session,
    Keylog,
    ServerName,
    AlpnCallback,
    Promise,
}

pub const CALLBACK_COUNT: usize = @intFromEnum(Field.Promise);

@root struct JSSocketHandlers {
    cell: JSValue,

    pub static fn create(global: *JSGlobalObject, callbacks: *const [CALLBACK_COUNT]JSValue): JSSocketHandlers {
        return JSSocketHandlers{
            cell: unsafe { Bun__SocketHandlers__create(global, callbacks) },
        };
    }

    pub fn toJS(): JSValue {
        return _.cell;
    }

    pub fn root(global: *JSGlobalObject): Strong {
        return Strong.create(_.cell, global);
    }

    pub fn onOpen(): JSValue { return _.get(.Open); }
    pub fn onClose(): JSValue { return _.get(.Close); }
    pub fn onData(): JSValue { return _.get(.Data); }
    pub fn onWritable(): JSValue { return _.get(.Writable); }
    pub fn onTimeout(): JSValue { return _.get(.Timeout); }
    pub fn onConnectError(): JSValue { return _.get(.ConnectError); }
    pub fn onEnd(): JSValue { return _.get(.End); }
    pub fn onError(): JSValue { return _.get(.Error); }
    pub fn onHandshake(): JSValue { return _.get(.Handshake); }
    pub fn onSession(): JSValue { return _.get(.Session); }
    pub fn onKeylog(): JSValue { return _.get(.Keylog); }
    pub fn onServerName(): JSValue { return _.get(.ServerName); }
    pub fn onAlpnCallback(): JSValue { return _.get(.AlpnCallback); }

    pub fn setCallbacks(global: *JSGlobalObject, callbacks: *const [CALLBACK_COUNT]JSValue) {
        unsafe { Bun__SocketHandlers__setCallbacks(global, _.cell, callbacks); }
    }

    pub fn clearOnOpen(global: *JSGlobalObject) {
        _.set(global, .Open, .zero);
    }

    pub fn setPromise(global: *JSGlobalObject, promise: JSValue) {
        _.set(global, .Promise, promise);
    }

    pub fn takePromise(global: *JSGlobalObject): JSValue? {
        const promise = _.get(.Promise);
        if (promise == .zero) return null;
        _.set(global, .Promise, .zero);
        return promise;
    }

    fn get(field: Field): JSValue {
        const value = Bun__SocketHandlers__getField(_.cell, @intFromEnum(field));
        if (value.isUndefined()) return .zero;
        return value;
    }

    fn set(global: *JSGlobalObject, field: Field, value: JSValue) {
        const stored = if (value == .zero) JSValue.js_undefined else value;
        Bun__SocketHandlers__setField(global, _.cell, @intFromEnum(field), stored);
    }
}
