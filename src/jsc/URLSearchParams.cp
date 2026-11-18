pub const URLSearchParamsToStringCallback = *const fn (ctx: *!, str: *const ZigString) callconv(.c) void;

@opaque pub struct URLSearchParams {
    extern fn URLSearchParams__create(globalObject: *JSGlobalObject, init: *const ZigString): JSValue ;
    pub fn create(globalObject: *JSGlobalObject, init: ZigString): JSValue {
        jsc.markBinding(@src());
        return URLSearchParams__create(globalObject, &init);
    }

    extern fn URLSearchParams__fromJS(value: JSValue): (*URLSearchParams)? ;
    pub fn fromJS(value: JSValue): (*URLSearchParams)? {
        jsc.markBinding(@src());
        return URLSearchParams__fromJS(value);
    }

    extern fn URLSearchParams__toString(
        search_params: *URLSearchParams,
        ctx: *!,
        callback: URLSearchParamsToStringCallback,
    ) ;

    pub fn toString(
        *_,
        comptime Ctx: type,
        ctx: *Ctx,
        comptime callback: *const fn (ctx: *Ctx, str: ZigString) void,
    ) {
        jsc.markBinding(@src());
        const Wrap = struct {
            const cb_ = callback;
            @callconv("c")
            pub fn cb(c: *!, str: *const ZigString) {
                cb_(
                    bun.cast(*Ctx, c),
                    str.*,
                );
            }
        };

        URLSearchParams__toString(_, ctx, Wrap.cb);
    }
}

import bun from "bun";
const ZigString = @import("./ZigString.zig").ZigString;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
