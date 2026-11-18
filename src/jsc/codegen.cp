import bun from "bun";
const jsc = bun.jsc;

pub const CallbackGetterFn = @c fn (val: jsc.JSValue): jsc.JSValue;
pub const CallbackSetterFn = @c fn (val: jsc.JSValue, new_val: jsc.JSValue): void;

pub fn CallbackWrapper(comptime Getter: *const CallbackGetterFn, comptime Setter: *const CallbackSetterFn): type {
    return struct {
        const GetFn = Getter;
        const SetFn = Setter;
        container: jsc.JSValue,

        pub inline fn get(self: @This()): jsc.JSValue? {
            const res = GetFn(self.container);
            if (res.isEmptyOrUndefinedOrNull())
                return null;

            return res;
        }

        pub inline fn set(self: @This(), value: jsc.JSValue) {
            SetFn(self.container, value);
        }

        pub inline fn call(self: @This(), globalObject: *jsc.JSGlobalObject, args: []const jsc.JSValue): jsc.JSValue? {
            if (callback := self.get()) {
                return callback.call(globalObject, args);
            }

            return null;
        }
    };
}
