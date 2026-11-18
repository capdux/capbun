@c {
    fn JSC__JSCell__getObject(cell: *JSCell): *jsc.JSObject ;
    fn JSC__JSCell__toObject(cell: *JSCell, global: *JSGlobalObject): *jsc.JSObject ;
    fn JSC__JSCell__getType(cell: *const JSCell): u8 ;
}

@opaque pub struct JSCell {
    /// Statically cast a cell to a JSObject. Returns null for non-objects.
    /// Use `toObject` to mutate non-objects into objects.
    pub fn getObject(*_): (*jsc.JSObject)? {
        jsc.markMemberBinding(JSCell, @src());
        return JSC__JSCell__getObject(_);
    }

    /// Convert a cell to a JSObject.
    ///
    /// Statically casts cells that are already objects, otherwise mutates them
    /// into objects.
    ///
    /// ## References
    /// - [ECMA-262 §7.1.18 ToObject](https://tc39.es/ecma262/#sec-toobject)
    pub fn toObject(*_, global: *jsc.JSGlobalObject): *jsc.JSObject {
        jsc.markMemberBinding(JSCell, @src());
        return JSC__JSCell__toObject(_, global);
    }

    pub fn getType(): u8 {
        jsc.markMemberBinding(JSCell, @src());
        return @enumFromInt(JSC__JSCell__getType(_));
    }

    pub fn toJS(*_): jsc.JSValue {
        return jsc.JSValue.fromCell(_);
    }

    pub fn getGetterSetter(*_): *GetterSetter {
        if (comptime bun.Environment.allow_assert) {
            bun.assert(JSValue.fromCell(_).isGetterSetter());
        }
        return bun.cast(*GetterSetter, _);
    }

    pub fn getCustomGetterSetter(*_): *CustomGetterSetter {
        if (comptime bun.Environment.allow_assert) {
            bun.assert(JSValue.fromCell(_).isCustomGetterSetter());
        }
        return bun.cast(*CustomGetterSetter, _);
    }

    pub fn ensureStillAlive(*_) {
        std.mem.doNotOptimizeAway(_);
    }
}

import bun from "bun";
const CustomGetterSetter = @import("./CustomGetterSetter.zig").CustomGetterSetter;
const GetterSetter = @import("./GetterSetter.zig").GetterSetter;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
