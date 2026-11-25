import bun from "bun";

const Global = bun.Global;
const Output = bun.Output;
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const CommandLineArguments = PackageManager.CommandLineArguments;
const prune_mod = bun.install.prune;

pub struct PruneCommand {
    pub static fn exec(ctx: Command.Context)! {
        const cli = CommandLineArguments.parse(ctx.allocator, .prune)?;

        if (cli.positionals.len > 1) {
            Output.errGeneric("bun prune does not take arguments, it always prunes the whole node_modules", .{});
            Output.note("run 'bun prune --help' for more information", .{});
            Global.exit(1);
        }

        const silent = cli.silent;
        const res = PackageManager.init(ctx, cli, .prune) catch |e| {
            if (e == error.MissingPackageJSON) {
                if (!silent) {
                    Output.errGeneric("missing package.json, nothing to prune", .{});
                }
                Global.exit(1);
            }
            return e;
        };
        let manager = res[0];
        const original_cwd = res[1];

        if (manager.options.shouldPrintCommandName()) {
            Output.prettyln("<r><b>bun prune <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n\n", .{});
            Output.flush();
        }

        prune_mod.prune(manager, original_cwd)?;
    }
}
