@c {
    fn MarkedArgumentBuffer__append(args: *MarkedArgumentBuffer, value: JSValue);
    fn MarkedArgumentBuffer__run(ctx: *!, cb: @c fn (ctx: *!, args: *!): void);
}

@opaque pub struct MarkedArgumentBuffer {
    fn append(*_, value: JSValue) {
        MarkedArgumentBuffer__append(_, value);
    }

    static fn run(comptime T: type, ctx: *T, func: @c fn (ctx: *T, args: *MarkedArgumentBuffer): void) {
        MarkedArgumentBuffer__run(@ptrCast(ctx), @ptrCast(func));
    }

    static fn wrap(comptime function: fn (globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, marked_argument_buffer: *MarkedArgumentBuffer): bun.JSError!jsc.JSValue): jsc.JSHostFnZig {
        return struct {
            static fn wrapper(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                const Context = struct {
                    result: bun.JSError!jsc.JSValue,
                    globalThis: *jsc.JSGlobalObject,
                    callframe: *jsc.CallFrame,
                    pub fn run(this: *@This(), marked_argument_buffer: *MarkedArgumentBuffer) callconv(.c) void {
                        this.result = function(this.globalThis, this.callframe, marked_argument_buffer);
                    }
                };

                let ctx = Context{
                    globalThis: globalThis,
                    callframe: callframe,
                    result: undefined,
                };
                jsc.MarkedArgumentBuffer.run(Context, &ctx, &Context.run);
                return ctx.result?;
            }
        }.wrapper;
    }
}

import bun from "bun";

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
