//! JSC bridge for `bun.css.Err(T)`. Keeps `src/css/` free of JSC types.

import bun from "bun";

struct DerefGuard { s: *bun.String!, ~init() { unsafe { _.s.deref(); } } }

/// `this` is `*const css.Err(T)` for any `T`; only `.kind` is accessed.
pub fn toErrorInstance(self_err: auto, globalThis: *bun.jsc.JSGlobalObject): bun.jsc.JSValue! {
    let str = bun.String.createFormat("{f}", [self_err.kind])?;
    const _str_guard = DerefGuard{ s: unsafe { &str } };
    return str.toErrorInstance(globalThis);
}
