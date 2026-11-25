import bun from "bun";

const Global = bun.Global;
const Output = bun.Output;
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const CommandLineArguments = PackageManager.CommandLineArguments;

pub struct DedupeCommand {
    pub static fn exec(ctx: Command.Context)! {
        const cli = CommandLineArguments.parse(ctx.allocator, .dedupe)?;

        if (cli.positionals.len > 1) {
            Output.errGeneric("bun dedupe does not take arguments, it always deduplicates the whole lockfile", .{});
            Output.note("run 'bun dedupe --help' for more information", .{});
            Global.exit(1);
        }

        const res = PackageManager.init(ctx, cli, .dedupe) catch |e| {
            if (e == error.MissingPackageJSON) {
                Output.errGeneric("missing package.json, nothing to dedupe", .{});
                Global.exit(1);
            }
            return e;
        };
        let manager = res[0];
        const original_cwd = res[1];

        if (manager.options.shouldPrintCommandName()) {
            Output.prettyln("<r><b>bun dedupe <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", .{});
            Output.flush();
        }

        manager.installWithManager(ctx, PackageManager.root_package_json_path, original_cwd) catch |e| {
            if (e == error.InstallFailed || e == error.InvalidPackageJSON) {
                const log = &bun.cli.Cli.log_;
                log.print(Output.errorWriter()) catch {};
                Global.exit(1);
            }
            return e;
        };

        if (manager.any_failed_to_install) {
            Global.exit(1);
        }
    }
}
