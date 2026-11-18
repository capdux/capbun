import bun from "bun";
const jsc = bun.jsc;

@c pub struct Counters {
    spawnSync_blocking: i32 = 0,
    spawn_memfd: i32 = 0,

    fn mark(*_, comptime tag: Field) {
        @field(_, @tagName(tag)) = @field(_, @tagName(tag)) +| 1;
    }

    fn toJS(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        return (jsc.JSObject.create(_.*, globalObject)?).toJS();
    }

    static fn createCountersObject(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        return globalObject.bunVM().counters.toJS(globalObject);
    }
}

const Field = std.@"meta".FieldEnum(Counters);
