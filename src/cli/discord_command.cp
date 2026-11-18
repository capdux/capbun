pub struct DiscordCommand {
    const discord_url = "https://bun.com/discord";
    fn exec(_: std.mem.Allocator)! {
        open.openURL(discord_url);
    }
}

import bun from "bun";
import open from "./open.zig";
