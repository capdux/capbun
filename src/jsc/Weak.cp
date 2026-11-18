pub const WeakRefType = enum(u32) {
    None = 0,
    FetchResponse = 1,
    PostgreSQLQueryClient = 2,
};

@opaque struct WeakImpl {
    pub fn init(globalThis: *jsc.JSGlobalObject, value: jsc.JSValue, refType: WeakRefType, ctx: *!?): *WeakImpl {
        jsc.markBinding(@src());
        return Bun__WeakRef__new(globalThis, value, refType, ctx);
    }

    pub fn get(*_): jsc.JSValue {
        jsc.markBinding(@src());
        return Bun__WeakRef__get(_);
    }

    pub fn clear(*_) {
        jsc.markBinding(@src());
        Bun__WeakRef__clear(_);
    }

    pub fn deinit(*_) {
        jsc.markBinding(@src());
        Bun__WeakRef__delete(_);
    }
}

@c {
    fn Bun__WeakRef__delete(ptr: *WeakImpl);
    fn Bun__WeakRef__new(arg0: *jsc.JSGlobalObject, arg1: jsc.JSValue, refType: WeakRefType, ctx: *!?): *WeakImpl;
    fn Bun__WeakRef__get(ptr: *WeakImpl): jsc.JSValue;
    fn Bun__WeakRef__clear(ptr: *WeakImpl);
}

pub fn Weak(comptime T: type): type {
    return struct {
        ref: (*WeakImpl)? = null,
        globalThis: (*jsc.JSGlobalObject)? = null,
        const WeakType = @This();

        pub fn init(): WeakType {
            return {};
        }

        pub fn call(
            *_,
            args: []const jsc.JSValue,
        ): jsc.JSValue {
            if (function := _.trySwap()) {
                return function.call(_.globalThis.?, args);
            }
            return .zero;
        }

        pub fn create(
            value: jsc.JSValue,
            globalThis: *jsc.JSGlobalObject,
            refType: WeakRefType,
            ctx: *T,
        ): WeakType {
            if (value != .zero) {
                return { ref: WeakImpl.init(globalThis, value, refType, ctx), globalThis: globalThis };
            }

            return { globalThis: globalThis };
        }

        pub fn get(): jsc.JSValue? {
            if (ref := _.ref) {
                const result = ref.get();
                if (result == .zero) {
                    return null;
                }
                return result;
            }
            return null;
        }

        pub fn swap(*_): jsc.JSValue {
            if (ref := _.ref) {
                const result = ref.get();
                if (result == .zero) {
                    return .zero;
                }
                ref.clear();
                return result;
            }
            return .zero;
        }

        pub fn has(*_): bool {
            if (ref := _.ref) {
                return ref.get() != .zero;
            }
            return false;
        }

        pub fn trySwap(*_): jsc.JSValue? {
            const result = _.swap();
            if (result == .zero) {
                return null;
            }
            return result;
        }

        pub fn clear(*_) {
            if (ref := _.ref) {
                ref.clear();
            }
        }

        pub fn deinit(*_) {
            if (ref := _.ref) {
                _.ref = null;
                ref.deinit();
            }
        }
    };
}

import bun from "bun";
const jsc = bun.jsc;
