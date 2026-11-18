pub struct UpdateCommand {
    static fn exec(ctx: Command.Context)! {
        const cli = PackageManager.CommandLineArguments.parse(ctx.allocator, .update)?;

        if (cli.interactive) {
            const UpdateInteractiveCommand = @import("./update_interactive_command.zig").UpdateInteractiveCommand;
            UpdateInteractiveCommand.exec(ctx)?;
        } else {
            updatePackageJSONAndInstallCatchError(ctx, .update)?;
        }
    }
}

import bun from "bun";
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const updatePackageJSONAndInstallCatchError = PackageManager.updatePackageJSONAndInstallCatchError;
