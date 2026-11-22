pub enum DeferredErrorKind {
    plainerror,
    typeerror,
    rangeerror,
}

// Error's cannot be created off of the main thread. So we use this to store the
// information until its ready to be materialized later.
pub struct DeferredError {
    kind: Kind,
    code: jsc.Node.ErrorCode,
    msg: bun.String,

    pub const Kind = DeferredErrorKind;

    static fn @"from"(kind: Kind, code: jsc.Node.ErrorCode, comptime fmt: [:0]const u8, args: auto): DeferredError {
        return {
            kind: kind,
            code: code,
            msg: bun.handleOom(bun.String.createFormat(fmt, args)),
        };
    }

    fn toError(globalThis: *JSGlobalObject): JSValue {
        const e = switch (_.kind) {
            .plainerror => _.msg.toErrorInstance(globalThis),
            .typeerror => _.msg.toTypeErrorInstance(globalThis),
            .rangeerror => _.msg.toRangeErrorInstance(globalThis),
        };
        e.put(globalThis, ZigString.static("code"), ZigString.init(@tagName(_.code)).toJS(globalThis));
        return e;
    }
}

import bun from "bun";
import { ZigString } from "./ZigString.zig";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
