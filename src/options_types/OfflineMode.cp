import bun from "bun";

pub enum OfflineMode {
    online,
    latest,
    offline,
}

pub const Prefer = bun.ComptimeStringMap(OfflineMode, .{
    &.{ "offline", OfflineMode.offline },
    &.{ "latest", OfflineMode.latest },
    &.{ "online", OfflineMode.online },
});

