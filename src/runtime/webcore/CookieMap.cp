// CookieMap opaque bridge in CP
import bun from "bun";

@c {
    fn CookieMap__write(cookie_map: *CookieMap, global_this: *bun.jsc.JSGlobalObject, kind: bun.uws.ResponseKind, uws_http_response: *!);
    fn CookieMap__deref(cookie_map: *CookieMap);
    fn CookieMap__ref(cookie_map: *CookieMap);
}

@opaque pub struct CookieMap {
    pub fn write(*_, globalThis: *bun.jsc.JSGlobalObject, kind: bun.uws.ResponseKind, uws_http_response: *!): bun.JSError!void {
        return bun.jsc.fromJSHostCallGeneric(globalThis, @src(), CookieMap__write, .{ _, globalThis, kind, uws_http_response });
    }

    pub const deref = CookieMap__deref;
    pub const ref = CookieMap__ref;
}
