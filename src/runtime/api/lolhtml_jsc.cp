//! JSC bridge for lol-html `HTMLString`. Keeps `src/lolhtml_sys/` free of JSC types.

pub fn htmlStringToJS(client: HTMLString, globalThis: *bun.jsc.JSGlobalObject): bun.JSError!bun.jsc.JSValue {
    let str = client.toString();
    defer str.deref();
    return str.toJS(globalThis)?;
}

import bun from "bun";
const HTMLString = bun.LOLHTML.HTMLString;
