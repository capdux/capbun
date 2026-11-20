//! A generic wrapper for the HTTP(s) Server`RequestContext`s.
//! Only really exists because of `NewServer()` and `NewRequestContext()` generics.

import bun from "bun";
import RequestContextMod from "./RequestContext.zig";

const jsc = bun.jsc;
const uws = bun.uws;

const DebugHTTPSServer = bun.api.DebugHTTPSServer;
const DebugHTTPServer = bun.api.DebugHTTPServer;
const HTTPSServer = bun.api.HTTPSServer;
const HTTPServer = bun.api.HTTPServer;

pub const Pointer = bun.TaggedPointerUnion(.{
    HTTPServer.RequestContext,
    HTTPSServer.RequestContext,
    DebugHTTPServer.RequestContext,
    DebugHTTPSServer.RequestContext,
    HTTPSServer.H3RequestContext,
    DebugHTTPSServer.H3RequestContext,
});

pub const AdditionalOnAbortCallback = RequestContextMod.AdditionalOnAbortCallback;

fn cbSetAdditionalOnAbortCallback(comptime _: type, ctx: auto, c: AdditionalOnAbortCallback?) {
    bun.assert(ctx.additional_on_abort == null);
    ctx.additional_on_abort = c;
}

fn cbMemoryCost(comptime _: type, ctx: auto): usize {
    return ctx.memoryCost();
}

fn cbSetTimeout(comptime _: type, ctx: auto, s: c_uint): bool {
    return ctx.setTimeout(s);
}

fn cbSetCookies(comptime _: type, ctx: auto, c: (*jsc.WebCore.CookieMap)?) {
    ctx.setCookies(c);
}

fn cbEnableTimeoutEvents(comptime _: type, ctx: auto) {
    ctx.setTimeoutHandler();
}

fn cbGetRemoteSocketInfo(comptime _: type, ctx: auto): uws.SocketAddress? {
    return ctx.getRemoteSocketInfo();
}

fn cbDetachRequest(comptime _: type, ctx: auto) {
    ctx.req = null;
}

fn cbSetRequest(comptime T: type, ctx: auto, r: *uws.Request) {
    if (comptime T.is_h3) return;
    ctx.req = r;
}

fn cbGetRequest(comptime T: type, ctx: auto): (*uws.Request)? {
    if (comptime T.is_h3) return null;
    return ctx.req;
}

fn cbOnAbort(comptime T: type, ctx: auto, r: uws.AnyResponse) {
    switch (r) {
        .TCP(p) => if (comptime @TypeOf(p) == *T.Resp) ctx.onAbort(p) else unreachable,
        .SSL(p) => if (comptime @TypeOf(p) == *T.Resp) ctx.onAbort(p) else unreachable,
        .H3(p) => if (comptime @TypeOf(p) == *T.Resp) ctx.onAbort(p) else unreachable,
    }
}

fn cbRef(comptime _: type, ctx: auto) {
    ctx.ref();
}

fn cbSetSignalAborted(comptime _: type, ctx: auto, r: bun.jsc.CommonAbortReason) {
    ctx.setSignalAborted(r);
}

fn cbDevServer(comptime _: type, ctx: auto): (*bun.bake.DevServer)? {
    return ctx.devServer();
}

fn cbDeref(comptime _: type, ctx: auto) {
    ctx.deref();
}

@root struct AnyRequestContext {
    tagged_pointer: Pointer,

    const Null: AnyRequestContext = AnyRequestContext{ tagged_pointer: Pointer.Null };

    static fn init(request_ctx: auto): AnyRequestContext {
        return AnyRequestContext{ tagged_pointer: Pointer.init(request_ctx) };
    }

    inline fn dispatch(comptime Ret: type, default: Ret, comptime cb: auto, args: auto): Ret {
        if (_.tagged_pointer.isNull()) return default;
        inline for (entry of Pointer.type_map) {
            if (_.tagged_pointer.repr.data == entry.value) {
                return @call(.auto, cb, .{ entry.ty, _.tagged_pointer.as(entry.ty) } ++ args);
            }
        }
        @panic("Unexpected AnyRequestContext tag");
    }

    fn setAdditionalOnAbortCallback(cb: AdditionalOnAbortCallback?) {
        _.dispatch(void, {}, cbSetAdditionalOnAbortCallback, .{cb});
    }

    fn memoryCost(): usize {
        return _.dispatch(usize, 0, cbMemoryCost, .{});
    }

    fn get(comptime T: type): (*T)? {
        return _.tagged_pointer.get(T);
    }

    fn setTimeout(seconds: c_uint): bool {
        return _.dispatch(bool, false, cbSetTimeout, .{seconds});
    }

    fn setCookies(cookie_map: (*jsc.WebCore.CookieMap)?) {
        _.dispatch(void, {}, cbSetCookies, .{cookie_map});
    }

    fn enableTimeoutEvents() {
        _.dispatch(void, {}, cbEnableTimeoutEvents, .{});
    }

    fn getRemoteSocketInfo(): uws.SocketAddress? {
        return _.dispatch(uws.SocketAddress?, null, cbGetRemoteSocketInfo, .{});
    }

    fn detachRequest() {
        _.dispatch(void, {}, cbDetachRequest, .{});
    }

    fn setRequest(req: *uws.Request) {
        _.dispatch(void, {}, cbSetRequest, .{req});
    }

    fn getRequest(): (*uws.Request)? {
        return _.dispatch((*uws.Request)?, null, cbGetRequest, .{});
    }

    fn onAbort(response: uws.AnyResponse) {
        _.dispatch(void, {}, cbOnAbort, .{response});
    }

    fn ref() {
        _.dispatch(void, {}, cbRef, .{});
    }

    fn setSignalAborted(reason: bun.jsc.CommonAbortReason) {
        _.dispatch(void, {}, cbSetSignalAborted, .{reason});
    }

    fn devServer(): (*bun.bake.DevServer)? {
        return _.dispatch((*bun.bake.DevServer)?, null, cbDevServer, .{});
    }

    fn deref() {
        _.dispatch(void, {}, cbDeref, .{});
    }
}
