import bun from "bun";
const uws = bun.uws;

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

pub fn BodyReaderMixin(comptime Wrap: type, comptime field: []const u8, comptime onBody: auto, comptime onError: auto): type {
    * = Wrap;
    return struct {
        body: std.array_list.Managed(u8),

        static fn init(allocator: std.mem.Allocator): @This() {
            return @This(){ body: std.array_list.Managed(u8).init(allocator) };
        }

        static fn onDataGeneric(mixin: *@This(), r: auto, chunk: []const u8, last: bool) {
            const any_resp = uws.AnyResponse.init(r);
            mixin.onData(any_resp, chunk, last) catch e => {
                if (e == error.OutOfMemory) return mixin.fail(any_resp, "500 Internal Server Error");
                return mixin.fail(any_resp, "400 Bad Request");
            };
        }

        static fn onAborted(mixin: *@This(), _: auto) {
            mixin.body.clearAndFree();
            onError(@fieldParentPtr(field, mixin));
        }

        fn readBody(*_, resp: auto) {
            const ctx = _;
            resp.onData(ctx, @This().onDataGeneric);
            resp.onAborted(ctx, @This().onAborted);
        }

        fn onData(*_, resp: uws.AnyResponse, chunk: []const u8, last: bool)! {
            const ctx = _;
            if (last) {
                let body_guard = DeinitGuard<@TypeOf(ctx.body)>{ val: ctx.body };
                ctx.body = std.array_list.Managed(u8).init(ctx.body.allocator);
                resp.clearOnData();
                if (body_guard.val.items.len > 0) {
                    body_guard.val.appendSlice(chunk)?;
                    onBody(@fieldParentPtr(field, ctx), body_guard.val.items, resp)?;
                } else {
                    onBody(@fieldParentPtr(field, ctx), chunk, resp)?;
                }
            } else {
                ctx.body.appendSlice(chunk)?;
            }
        }

        fn fail(*_, r: uws.AnyResponse, status: string) {
            const ctx = _;
            let body = ctx.body;
            ctx.body = std.array_list.Managed(u8).init(ctx.body.allocator);
            body.deinit();
            r.clearAborted();
            r.clearOnData();
            r.clearOnWritable();

            r.writeStatus(status);
            r.endWithoutBody(false);

            onError(@fieldParentPtr(field, ctx));
        }
    };
}
