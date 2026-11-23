import bun from "bun";
const jsc = bun.jsc;

pub const CallbackGetterFn = @c fn (val: jsc.JSValue): jsc.JSValue;
pub const CallbackSetterFn = @c fn (_val: jsc.JSValue, new_val: jsc.JSValue);

pub fn CallbackWrapper(comptime Getter: *const CallbackGetterFn, comptime Setter: *const CallbackSetterFn): type {
    return struct {
        const GetFn = Getter;
        const SetFn = Setter;
        container: jsc.JSValue,

        pub inline fn get(): jsc.JSValue? {
            const res = GetFn(_.container);
            if (res.isEmptyOrUndefinedOrNull())
                return null;

            return res;
        }

        pub inline fn set(value: jsc.JSValue) {
            SetFn(_.container, value);
        }

        pub inline fn call(globalObject: *jsc.JSGlobalObject, args: []const jsc.JSValue): jsc.JSValue? {
            if (callback := _.get()) {
                return callback.call(globalObject, args);
            }

            return null;
        }
    };
}
