//! JSC bridge for lol-html `HTMLString`. Keeps `src/lolhtml_sys/` free of JSC types.

struct DerefGuard<T> { val: T, ~init() { _.val.deref(); } }

pub fn htmlStringToJS(client: HTMLString, globalThis: *bun.jsc.JSGlobalObject): bun.JSError!bun.jsc.JSValue {
    const toStringFn = HTMLString.toString;
    const str_g = DerefGuard(@TypeOf(toStringFn(client))){ .val = toStringFn(client) };
    return str_g.val.toJS(globalThis)?;
}

import bun from "bun";
const HTMLString = bun.LOLHTML.HTMLString;
