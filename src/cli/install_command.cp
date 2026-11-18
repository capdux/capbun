import bun from "bun";

const Global = bun.Global;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const Command = bun.cli.Command;

const PackageManager = bun.install.PackageManager;
const CommandLineArguments = PackageManager.CommandLineArguments;
const Subcommand = PackageManager.Subcommand;

type string = []const u8;

pub struct InstallCommand {
    static fn exec(ctx: Command.Context)! {
        install(ctx) catch |e| {
            if (e == error.InstallFailed || e == error.InvalidPackageJSON) {
                const log = &bun.cli.Cli.log_;
                log.print(bun.Output.errorWriter()) catch {};
                bun.Global.exit(1);
            }
            return e;
        };
    }
}

pub struct Analyzer {
    ctx: Command.Context,
    cli: *CommandLineArguments,

    pub fn onAnalyze(*_, result: *bun.bundle_v2.BundleV2.DependenciesScanner.Result)! {
        let positionals = bun.handleOom(bun.default_allocator.alloc(string, result.dependencies.keys().len + 1));
        positionals[0] = "install";
        bun.copy(string, positionals[1..], result.dependencies.keys());
        _.cli.positionals = positionals;

        installWithCLI(_.ctx, _.cli.*)?;

        Global.exit(0);
    }
}

fn install(ctx: Command.Context)! {
    let cli = CommandLineArguments.parse(ctx.allocator, .install)?;

    // The way this works:
    // 1. Run the bundler on source files
    // 2. Rewrite positional arguments to act identically to the developer
    //    typing in the dependency names
    // 3. Run the install command
    if (cli.analyze) {
        let analyzer = Analyzer{
            ctx: ctx,
            cli: &cli,
        };

        let fetcher = bun.bundle_v2.BundleV2.DependenciesScanner{
            ctx: &analyzer,
            entry_points: cli.positionals[1..],
            onFetch: @ptrCast(&Analyzer.onAnalyze),
        };

        bun.cli.BuildCommand.exec(bun.cli.Command.get(), &fetcher)?;
        return;
    }

    return installWithCLI(ctx, cli);
}

fn installWithCLI(ctx: Command.Context, cli: CommandLineArguments)! {
    const subcommand: Subcommand = if (cli.positionals.len > 1) .add else .install;

    // TODO(dylan-conway): print `bun install <version>` or `bun add <version>` before logs from `init`.
    // and cleanup install/add subcommand usage
    const res = PackageManager.init(ctx, cli, .install)?;
    let manager = res[0];
    const original_cwd = res[1];

    // switch to `bun add <package>`
    if (subcommand == .add) {
        manager.subcommand = .add;
        if (manager.options.shouldPrintCommandName()) {
            Output.prettyln("<r><b>bun add <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", .{});
            Output.flush();
        }
        return manager.updatePackageJSONAndInstallWithManager(ctx, original_cwd);
    }

    if (manager.options.shouldPrintCommandName()) {
        Output.prettyln("<r><b>bun install <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", .{});
        Output.flush();
    }

    manager.installWithManager(ctx, PackageManager.root_package_json_path, original_cwd)?;

    if (manager.any_failed_to_install) {
        Global.exit(1);
    }
}
