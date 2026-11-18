@c {
    fn Bun__JSArray__getContiguousVector(value: JSValue, length: *u32): ([*]const JSValue)? ;
    fn Bun__JSArray__contiguousVectorIsStillValid(value: JSValue, vector: [*]const JSValue, length: u32): bool ;
}

pub struct JSArrayIterator {
    i: u32 = 0,
    len: u32 = 0,
    array: JSValue,
    global: *JSGlobalObject,
    /// Direct pointer into the JSArray butterfly when the array has Int32 or
    /// Contiguous storage and a sane prototype chain. Holes are encoded as 0.
    fast: ([*]const JSValue)? = null,

    static fn init(value: JSValue, global: *JSGlobalObject): bun.JSError!JSArrayIterator {
        let length: u32 = 0;
        if (elements := Bun__JSArray__getContiguousVector(value, &length)) {
            return {
                array: value,
                global: global,
                len: length,
                fast: elements,
            };
        }
        return {
            array: value,
            global: global,
            len: @truncate(value.getLength(global)?),
        };
    }

    pub fn next(*_): bun.JSError!JSValue? {
        if (!(_.i < _.len)) {
            return null;
        }
        const i = _.i;
        _.i += 1;
        if (elements := _.fast) {
            if (Bun__JSArray__contiguousVectorIsStillValid(_.array, elements, _.len)) {
                const val = unsafe { elements[i] };
                return if (val == .zero) .js_undefined else val;
            }
            _.fast = null;
        }
        return JSObject.getIndex(_.array, _.global, i)?;
    }
}

import bun from "bun";
const JSObject = @import("./JSObject.zig").JSObject;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
