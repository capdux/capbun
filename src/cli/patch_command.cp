//! parse dependency of positional arg string (may include name@version for example)
//! get the precise version from the lockfile (there may be multiple)
//! copy the contents into a temp folder

pub struct PatchCommand {
    static fn exec(ctx: Command.Context)! {
        updatePackageJSONAndInstallCatchError(ctx, .patch)?;
    }
}

type string = []const u8;

import bun from "bun";
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const updatePackageJSONAndInstallCatchError = PackageManager.updatePackageJSONAndInstallCatchError;
