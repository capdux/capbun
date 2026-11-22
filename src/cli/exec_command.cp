pub struct ExecCommand {
    static fn exec(ctx: Command.Context)! {
        const script = ctx.positionals[1];
        // this is a hack: make dummy bundler so we can use its `.runEnvLoader()` function to populate environment variables probably should split out the functionality
        let bundle = bun.Transpiler.init(
            ctx.allocator,
            ctx.log,
            jsc_config.configureTransformOptionsForBunVM(ctx.allocator, ctx.args)?,
            null,
        )?;
        bundle.runEnvLoader(bundle.options.env.disable_default_env_files)?;
        let buf: bun.PathBuffer = undefined;
        const cwd = switch (bun.sys.getcwd(&buf)) {
            .result => |p| p,
            .err => |e| {
                Output.err(e, "failed to run script <b>{s}<r>", .{ script });
                Global.exit(1);
            },
        };
        const mini = bun.jsc.MiniEventLoop.initGlobal(bundle.env, cwd);
        const parts: [2]string = [
            cwd,
            "[eval]",
        ];
        const script_path = bun.path.join(&parts, .auto);

        const code = bun.shell.Interpreter.initAndRunFromSource(ctx, mini, script_path, script, null) catch |e| {
            Output.err(e, "failed to run script <b>{s}<r>", .{ script_path });
            Global.exit(1);
        };

        // if (code > 0) {
        //     if (code != 2 and !silent) {
        //         Output.prettyErrorln("<r><red>error<r><d>:<r> script <b>\"{s}\"<r> exited with code {d}<r>", .{ name, code });
        //         Output.flush();
        //     }

        Global.exit(code);
        // }
    }
}

import bun from "bun";
import jsc_config from "../jsc/config.zig";
const Global = bun.Global;
const Output = bun.Output;
const Command = bun.cli.Command;
