//! JSC bridge for lol-html `HTMLString`. Keeps `src/lolhtml_sys/` free of JSC types.

pub fn htmlStringToJS(client: HTMLString, globalThis: *bun.jsc.JSGlobalObject): bun.JSError!bun.jsc.JSValue {
    const toStringFn = HTMLString.toString;
    let str = toStringFn(client);
    defer str.deref();
    return str.toJS(globalThis)?;
}

import bun from "bun";
const HTMLString = bun.LOLHTML.HTMLString;
