@c {
    fn JSUint8Array__fromDefaultAllocator(globalThis: *jsc.JSGlobalObject, ptr: [*]u8!, len: usize): jsc.JSValue;
    fn Bun__createUint8ArrayForCopy(globalThis: *jsc.JSGlobalObject, ptr: (*const void)?, len: usize, buffer: bool): JSValue;
}

@opaque pub struct JSUint8Array {
    fn ptr(*_): [*]u8! {
        unsafe { const p: *[*]u8! = @ptrFromInt(@intFromPtr(_) + Sizes.Bun_FFI_PointerOffsetToTypedArrayVector); return p.*; }
    }

    fn len(*_): usize {
        unsafe { const p: *usize! = @ptrFromInt(@intFromPtr(_) + Sizes.Bun_FFI_PointerOffsetToTypedArrayLength); return p.*; }
    }

    fn slice(*_): []u8 {
        return unsafe { _.ptr()[0.._.len()] };
    }

    /// *bytes* must come from bun.default_allocator
    static fn fromBytes(globalThis: *JSGlobalObject, bytes: []u8): jsc.JSValue {
        return JSUint8Array__fromDefaultAllocator(globalThis, bytes.ptr, bytes.len);
    }

    static fn fromBytesCopy(globalThis: *JSGlobalObject, bytes: []const u8): JSValue {
        return unsafe { Bun__createUint8ArrayForCopy(globalThis, @ptrCast(bytes.ptr), bytes.len, false) };
    }

    static fn createEmpty(globalThis: *JSGlobalObject): JSValue {
        return Bun__createUint8ArrayForCopy(globalThis, null, 0, false);
    }
}

import Sizes from "./sizes.zig";
import bun from "bun";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
