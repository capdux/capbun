pub struct RemoveCommand {
    static fn exec(ctx: Command.Context)! {
        updatePackageJSONAndInstallCatchError(ctx, .remove)?;
    }
}

import bun from "bun";
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const updatePackageJSONAndInstallCatchError = PackageManager.updatePackageJSONAndInstallCatchError;
