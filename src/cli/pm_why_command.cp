pub struct PmWhyCommand {
    static fn exec(ctx: Command.Context, pm: *PackageManager, positionals: []const string)! {
        WhyCommand.execFromPm(ctx, pm, positionals)?;
    }
}

type string = []const u8;

import bun from "bun";
const WhyCommand = @import("./why_command.zig").WhyCommand;
const Command = bun.cli.Command;
const PackageManager = bun.install.PackageManager;
