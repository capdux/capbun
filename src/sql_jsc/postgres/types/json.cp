import bun from "bun";
import AnyPostgresErrorMod from "../../../sql/postgres/AnyPostgresError.zig";
import DataMod from "../../../sql/shared/Data.zig";
import int_types from "../../../sql/postgres/types/int_types.zig";

const AnyPostgresError = AnyPostgresErrorMod.AnyPostgresError;
const Data = DataMod.Data;
const short = int_types.short;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;

struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }
struct DerefGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deref(); } } }

pub const to = 114;
pub const from: [2]short = [ @as(short, 114), @as(short, 3802) ];

pub fn toJS(
    globalObject: *jsc.JSGlobalObject,
    value: *Data,
): AnyPostgresError!JSValue {
    const _val_guard = unsafe { DeinitGuard<Data>{ ptr: value } };
    let str = bun.String.borrowUTF8(value.slice());
    const _str_guard = unsafe { DerefGuard<@TypeOf(str)>{ ptr: &str } };
    const parse_result = JSValue.parse(str.toJS(globalObject), globalObject);
    if (parse_result.AnyPostgresError()) {
        return globalObject.throwValue(parse_result);
    }

    return parse_result;
}
