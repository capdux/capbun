// JSInternalPromise was removed from JavaScriptCore upstream; the new module loader uses regular JSPromise.
// Keep this as a transparent alias so existing Zig callers continue to compile.
import js_promise_mod from "./JSPromise.zig";
pub const JSInternalPromise = js_promise_mod.JSPromise;
