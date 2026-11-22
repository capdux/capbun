import bun from "bun";
import JSArrayIteratorMod from "./JSArrayIterator.zig";

const JSArrayIterator = JSArrayIteratorMod.JSArrayIterator;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@c {
    fn JSArray__constructArray(global: *JSGlobalObject, ptr: [*]const JSValue, len: usize): JSValue;
    fn JSArray__constructEmptyArray(global: *JSGlobalObject, len: usize): JSValue;
}

@opaque pub struct JSArray {
    // TODO(@paperclover): this can throw
    static fn create(global: *JSGlobalObject, items: []const JSValue): JSValue ! bun.JSError {
        return bun.jsc.fromJSHostCall(global, @src(), JSArray__constructArray, .{ global, items.ptr, items.len });
    }

    static fn createEmpty(global: *JSGlobalObject, len: usize): JSValue ! bun.JSError {
        return bun.jsc.fromJSHostCall(global, @src(), JSArray__constructEmptyArray, .{ global, len });
    }

    fn iterator(*_, global: *JSGlobalObject): JSArrayIterator ! bun.JSError {
        return JSValue.fromCell(_).arrayIterator(global);
    }
}
