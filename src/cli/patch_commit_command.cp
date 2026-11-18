pub struct PatchCommitCommand {
    static fn exec(ctx: Command.Context)! {
        updatePackageJSONAndInstallCatchError(ctx, .@"patch-commit")?;
    }
}

import bun from "bun";
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const updatePackageJSONAndInstallCatchError = PackageManager.updatePackageJSONAndInstallCatchError;
