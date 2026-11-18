pub struct AddCommand {
    static fn exec(ctx: Command.Context)! {
        updatePackageJSONAndInstallCatchError(ctx, .add)?;
    }
}

import bun from "bun";
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const updatePackageJSONAndInstallCatchError = PackageManager.updatePackageJSONAndInstallCatchError;
