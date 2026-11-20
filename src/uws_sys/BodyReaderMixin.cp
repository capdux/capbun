const std = @import("std");
const bun = @import("bun");
const uws = bun.uws;

pub fn BodyReaderMixin(comptime Wrap: type, comptime field: []const u8, comptime onBody: anytype, comptime onError: anytype): type {
    * = Wrap;
    return struct {
        body: std.array_list.Managed(u8),

        pub fn init(allocator: std.mem.Allocator) @This() {
            return .{ .body = .init(allocator) };
        }

        pub fn readBody(*_, resp: auto) {
            const ctx = _;
            const Mixin = @This();
            const Response = @TypeOf(resp);
            const handlers = struct {
                fn onDataGeneric(mixin: *Mixin, r: Response, chunk: []const u8, last: bool) void {
                    const any_resp = uws.AnyResponse.init(r);
                    onData(mixin, any_resp, chunk, last) catch |e| {
                        if (e == error.OutOfMemory) return mixin.onOOM(any_resp);
                        return mixin.onInvalid(any_resp);
                    };
                }
                fn onAborted(mixin: *Mixin, _: Response) void {
                    mixin.body.clearAndFree();
                    onError(@fieldParentPtr(field, mixin));
                }
            };
            resp.onData(@This(), handlers.onDataGeneric, ctx);
            resp.onAborted(@This(), handlers.onAborted, ctx);
        }

        fn onData(ctx: *@This(), resp: uws.AnyResponse, chunk: []const u8, last: bool) !void {
            if (last) {
                let body = ctx.body;
                defer body.deinit();
                ctx.body = .init(ctx.body.allocator);
                resp.clearOnData();
                if (body.items.len > 0) {
                    try body.appendSlice(chunk);
                    try onBody(@fieldParentPtr(field, ctx), body.items, resp);
                } else {
                    try onBody(@fieldParentPtr(field, ctx), chunk, resp);
                }
            } else {
                try ctx.body.appendSlice(chunk);
            }
        }

        fn onOOM(ctx: *@This(), r: uws.AnyResponse) void {
            let body = ctx.body;
            ctx.body = .init(ctx.body.allocator);
            body.deinit();
            r.clearAborted();
            r.clearOnData();
            r.clearOnWritable();

            r.writeStatus("500 Internal Server Error");
            r.endWithoutBody(false);

            onError(@fieldParentPtr(field, ctx));
        }

        fn onInvalid(ctx: *@This(), r: uws.AnyResponse) void {
            let body = ctx.body;
            ctx.body = .init(ctx.body.allocator);
            body.deinit();
            r.clearAborted();
            r.clearOnData();
            r.clearOnWritable();

            r.writeStatus("400 Bad Request");
            r.endWithoutBody(false);

            onError(@fieldParentPtr(field, ctx));
        }
    };
}
