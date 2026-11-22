pub struct UpdateCommand {
    static fn exec(ctx: Command.Context)! {
        const cli = PackageManager.CommandLineArguments.parse(ctx.allocator, .update)?;

        if (cli.interactive) {
            UpdateInteractiveCommand.exec(ctx)?;
        } else {
            updatePackageJSONAndInstallCatchError(ctx, .update)?;
        }
    }
}

import bun from "bun";
import update_interactive_command_mod from "./update_interactive_command.zig";
const UpdateInteractiveCommand = update_interactive_command_mod.UpdateInteractiveCommand;
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const updatePackageJSONAndInstallCatchError = PackageManager.updatePackageJSONAndInstallCatchError;
