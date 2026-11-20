import bun from "bun";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@c {
    fn WebCore__CommonAbortReason__toJS(global: *JSGlobalObject, reason: CommonAbortReason): JSValue;
}

pub enum CommonAbortReason: u8 {
    Timeout = 1,
    UserAbort = 2,
    ConnectionClosed = 3,

    static fn toJS(reason: CommonAbortReason, global: *JSGlobalObject): JSValue {
        return WebCore__CommonAbortReason__toJS(global, reason);
    }
}
