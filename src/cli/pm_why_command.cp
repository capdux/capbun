pub struct PmWhyCommand {
    static fn exec(ctx: Command.Context, pm: *PackageManager, positionals: []const string)! {
        WhyCommand.execFromPm(ctx, pm, positionals)?;
    }
}

type string = []const u8;

import bun from "bun";
import why_command_mod from "./why_command.zig";
const WhyCommand = why_command_mod.WhyCommand;
const Command = bun.cli.Command;
const PackageManager = bun.install.PackageManager;
