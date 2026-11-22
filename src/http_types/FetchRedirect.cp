import bun from "bun";
import fetch_enums_jsc from "../http_jsc/fetch_enums_jsc.zig";

export enum FetchRedirect: u2 {
    follow,
    manual,
    @"error",

    pub const Map = bun.ComptimeStringMap(FetchRedirect, .{
        .{ "follow", .follow },
        .{ "manual", .manual },
        .{ "error", .@"error" },
    });

    pub const toJS = fetch_enums_jsc.fetchRedirectToJS;
}
