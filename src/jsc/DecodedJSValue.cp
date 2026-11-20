import builtin from "builtin";

/// ABI-compatible with `JSC::EncodedValueDescriptor.asBits`.
@c pub struct EncodedValueBits {
    payload: i32,
    tag: i32,
}

/// ABI-compatible with `JSC::EncodedValueDescriptor`.
@c pub union JSCEncodedValueDescriptor {
    asInt64: i64,
    ptr: (*jsc.JSCell)?,
    asBits: EncodedValueBits,
}

/// ABI-compatible with `JSC::JSValue`.
@c pub struct DecodedJSValue {
    pub const EncodedValueDescriptor = JSCEncodedValueDescriptor;

    u: JSCEncodedValueDescriptor,

    /// Equivalent to `JSC::JSValue::encode`.
    pub fn encode(): jsc.JSValue {
        return @enumFromInt(_.u.asInt64);
    }

    fn asU64(): u64 {
        return @bitCast(_.u.asInt64);
    }

    /// Equivalent to `JSC::JSValue::isCell`. Note that like JSC, this method treats 0 as a cell.
    pub fn isCell(): bool {
        return (_.asU64() & @as(u64, @bitCast(@as(i64, ffi.NotCellMask)))) == 0;
    }

    /// Equivalent to `JSC::JSValue::asCell`.
    pub fn asCell(): (*jsc.JSCell)? {
        bun.assertf(_.isCell(), "not a cell: 0x{x}", .{_.asU64()});
        return _.u.ptr;
    }
}

comptime {
    bun.assertf(@sizeOf(usize) == 8, "EncodedValueDescriptor assumes a 64-bit system", .{});
    bun.assertf(
        builtin.target.cpu.arch.endian() == .little,
        "EncodedValueDescriptor.asBits assumes a little-endian system",
        .{},
    );
}

import bun from "bun";
import ffi from "./FFI.zig";
const jsc = bun.bun_js.jsc;
