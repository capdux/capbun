import bun from "bun";
import fetch_enums_jsc from "../http_jsc/fetch_enums_jsc.zig";

/// https://developer.mozilla.org/en-US/docs/Web/API/Request/mode
export enum FetchRequestMode: u2 {
    @"same-origin",
    @"no-cors",
    cors,
    navigate,

    pub const Map = bun.ComptimeStringMap(FetchRequestMode, .{
        .{ "same-origin", .@"same-origin" },
        .{ "no-cors", .@"no-cors" },
        .{ "cors", .cors },
        .{ "navigate", .navigate },
    });

    pub const toJS = fetch_enums_jsc.fetchRequestModeToJS;
}
