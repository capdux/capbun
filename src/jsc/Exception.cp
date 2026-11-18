import bun from "bun";
import ZigStackTraceModule from "./ZigStackTrace.zig";
const ZigStackTrace = ZigStackTraceModule.ZigStackTrace;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@c {
    fn JSC__Exception__getStackTrace(exc: *Exception, global: *JSGlobalObject, stack: *ZigStackTrace);
    fn JSC__Exception__asJSValue(exc: *Exception): JSValue;
}

/// Opaque representation of a JavaScript exception
@opaque pub struct Exception {
    fn getStackTrace(*_, global: *JSGlobalObject, stack: *ZigStackTrace) {
        JSC__Exception__getStackTrace(_, global, stack);
    }

    fn value(*_): JSValue {
        return JSC__Exception__asJSValue(_);
    }
}
