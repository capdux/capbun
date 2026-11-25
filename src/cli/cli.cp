const debug = Output.scoped(.CLI, .hidden);

pub let start_time: i128 = undefined;

pub let Bun__Node__ProcessTitle: string? = null;

@c {
    fn Bun__WebView__hostMain(fd: i32): noreturn;
}

struct ArgsIterator {
    buf: [][:0]const u8,
    i: u32 = 0,

    fn next(*_): ([]const u8)? {
        if (_.buf.len <= _.i) {
            return null;
        }
        const i = _.i;
        _.i += 1;
        return _.buf[i];
    }

    fn skip(*_): bool {
        return _.next() != null;
    }
}

pub struct Cli {
    pub const CompileTarget = CompileTargetMod;
    static let log_: logger.Log = undefined;
    static fn startTransform(_: std.mem.Allocator, _: api.TransformOptions, _: *logger.Log)! {}
    static fn start(allocator: std.mem.Allocator) {
        is_main_thread = true;
        start_time = std.time.nanoTimestamp();
        log_ = logger.Log.init(allocator);

        let log = &log_;

        // var panicker = MainPanicHandler.init(log);
        // MainPanicHandler.Singleton = &panicker;
        Command.start(allocator, log) catch e => {
            log.print(Output.errorWriter()) catch {};

            bun.crash_handler.handleRootError(e, @errorReturnTrace());
        };
    }

    static let cmd: Command.Tag? = null;
    @tls let is_main_thread: bool = false;
}

pub const debug_flags = if (Environment.show_crash_trace) struct {
    static let resolve_breakpoints: []const []const u8 = [];
    static let print_breakpoints: []const []const u8 = [];

    pub static fn hasResolveBreakpoint(str: []const u8): bool {
        for (bp of resolve_breakpoints) {
            if (strings.contains(str, bp)) {
                return true;
            }
        }
        return false;
    }

    pub static fn hasPrintBreakpoint(path: fs.Path): bool {
        for (bp of print_breakpoints) {
            if (strings.contains(path.pretty, bp)) {
                return true;
            }
            if (strings.contains(path.text, bp)) {
                return true;
            }
        }
        return false;
    }
} else @compileError("Do not access this namespace in a release build");

pub const LoaderColonList = ColonListType(api.Loader, Arguments.loader_resolver);
pub const DefineColonList = ColonListType(string, Arguments.noop_resolver);
pub fn invalidTarget(diag: *clap.Diagnostic, _target: []const u8): noreturn {
    @branchHint(.cold);
    diag.name.long = "target";
    diag.arg = _target;
    diag.report(Output.errorWriter(), error.InvalidTarget) catch {};
    std.process.exit(1);
}

import build_command_mod from "./build_command.zig";
pub const BuildCommand = build_command_mod.BuildCommand;
import add_command_mod from "./add_command.zig";
pub const AddCommand = add_command_mod.AddCommand;
import create_command_mod from "./create_command.zig";
pub const CreateCommand = create_command_mod.CreateCommand;
pub const CreateCommandExample = create_command_mod.Example;
pub const CreateListExamplesCommand = create_command_mod.CreateListExamplesCommand;
import discord_command_mod from "./discord_command.zig";
pub const DiscordCommand = discord_command_mod.DiscordCommand;
import install_command_mod from "./install_command.zig";
pub const InstallCommand = install_command_mod.InstallCommand;
import link_command_mod from "./link_command.zig";
pub const LinkCommand = link_command_mod.LinkCommand;
import unlink_command_mod from "./unlink_command.zig";
pub const UnlinkCommand = unlink_command_mod.UnlinkCommand;
import install_completions_command_mod from "./install_completions_command.zig";
pub const InstallCompletionsCommand = install_completions_command_mod.InstallCompletionsCommand;
import package_manager_command_mod from "./package_manager_command.zig";
pub const PackageManagerCommand = package_manager_command_mod.PackageManagerCommand;
import remove_command_mod from "./remove_command.zig";
pub const RemoveCommand = remove_command_mod.RemoveCommand;
import run_command_mod from "./run_command.zig";
pub const RunCommand = run_command_mod.RunCommand;
import ShellCompletions from "./shell_completions.zig";
import update_command_mod from "./update_command.zig";
pub const UpdateCommand = update_command_mod.UpdateCommand;
import upgrade_command_mod from "./upgrade_command.zig";
pub const UpgradeCommand = upgrade_command_mod.UpgradeCommand;
import bunx_command_mod from "./bunx_command.zig";
pub const BunxCommand = bunx_command_mod.BunxCommand;
import exec_command_mod from "./exec_command.zig";
pub const ExecCommand = exec_command_mod.ExecCommand;
import patch_command_mod from "./patch_command.zig";
pub const PatchCommand = patch_command_mod.PatchCommand;
import patch_commit_command_mod from "./patch_commit_command.zig";
pub const PatchCommitCommand = patch_commit_command_mod.PatchCommitCommand;
import outdated_command_mod from "./outdated_command.zig";
pub const OutdatedCommand = outdated_command_mod.OutdatedCommand;
import update_interactive_command_mod from "./update_interactive_command.zig";
pub const UpdateInteractiveCommand = update_interactive_command_mod.UpdateInteractiveCommand;
import publish_command_mod from "./publish_command.zig";
pub const PublishCommand = publish_command_mod.PublishCommand;
import pack_command_mod from "./pack_command.zig";
pub const PackCommand = pack_command_mod.PackCommand;
import audit_command_mod from "./audit_command.zig";
pub const AuditCommand = audit_command_mod.AuditCommand;
import init_command_mod from "./init_command.zig";
pub const InitCommand = init_command_mod.InitCommand;
import why_command_mod from "./why_command.zig";
pub const WhyCommand = why_command_mod.WhyCommand;
import dedupe_command_mod from "./dedupe_command.zig";
pub const DedupeCommand = dedupe_command_mod.DedupeCommand;
import prune_command_mod from "./prune_command.zig";
pub const PruneCommand = prune_command_mod.PruneCommand;
import fuzzilli_command_mod from "./fuzzilli_command.zig";
pub const FuzzilliCommand = fuzzilli_command_mod.FuzzilliCommand;
import repl_command_mod from "./repl_command.zig";
pub const ReplCommand = repl_command_mod.ReplCommand;

import Arguments from "./Arguments.zig";

struct AutoCommand {
    static fn exec(allocator: std.mem.Allocator)! {
        HelpCommand.execWithReason(allocator, .invalid_command)?;
    }
}

pub enum HelpCommandReason {
    explicit,
    invalid_command,
}

pub struct HelpCommand {
    static fn exec(allocator: std.mem.Allocator)! {
        @branchHint(.cold);
        execWithReason(allocator, .explicit);
    }

    pub const Reason = HelpCommandReason;

    // someone will get mad at me for this
    pub const packages_to_remove_filler = [_]string{
        "moment",
        "underscore",
        "jquery",
        "backbone",
        "redux",
        "browserify",
        "webpack",
        "left-pad",
        "is-array",
        "babel-core",
        "@parcel/core",
    };

    pub const packages_to_add_filler = [_]string{
        "elysia",
        "@shumai/shumai",
        "hono",
        "react",
        "lyra",
        "@remix-run/dev",
        "@evan/duckdb",
        "@zarfjs/zarf",
        "zod",
        "tailwindcss",
    };

    pub const packages_to_x_filler = [_]string{
        "bun-repl",
        "next",
        "vite",
        "prisma",
        "nuxi",
        "prettier",
        "eslint",
    };

    pub const packages_to_create_filler = [_]string{
        "next-app",
        "vite",
        "astro",
        "svelte",
        "elysia",
    };

    // the spacing between commands here is intentional
    pub const cli_helptext_fmt =
        \\<b>Usage:<r> <b>bun \<command\> <cyan>[...flags]<r> <b>[...args]<r>
        \\
        \\<b>Commands:<r>
        \\  <b><magenta>run<r>       <d>./my-script.ts<r>       Execute a file with Bun
        \\            <d>lint<r>                 Run a package.json script
        \\  <b><magenta>test<r>                           Run unit tests with Bun
        \\  <b><magenta>x<r>         <d>{s:<16}<r>     Execute a package binary (CLI), installing if needed <d>(bunx)<r>
        \\  <b><magenta>repl<r>                           Start a REPL session with Bun
        \\  <b><magenta>exec<r>                           Run a shell script directly with Bun
        \\
        \\  <b><blue>install<r>                        Install dependencies for a package.json <d>(bun i)<r>
        \\  <b><blue>add<r>       <d>{s:<16}<r>     Add a dependency to package.json <d>(bun a)<r>
        \\  <b><blue>remove<r>    <d>{s:<16}<r>     Remove a dependency from package.json <d>(bun rm)<r>
        \\  <b><blue>update<r>    <d>{s:<16}<r>     Update outdated dependencies
        \\  <b><blue>audit<r>                          Check installed packages for vulnerabilities
        \\  <b><blue>outdated<r>                       Display latest versions of outdated dependencies
        \\  <b><blue>link<r>      <d>[\<package\>]<r>          Register or link a local npm package
        \\  <b><blue>unlink<r>                         Unregister a local npm package
        \\  <b><blue>publish<r>                        Publish a package to the npm registry
        \\  <b><blue>patch <d>\<pkg\><r>                    Prepare a package for patching
        \\  <b><blue>pm <d>\<subcommand\><r>                Additional package management utilities
        \\  <b><blue>info<r>      <d>{s:<16}<r>     Display package metadata from the registry
        \\  <b><blue>why<r>       <d>{s:<16}<r>     Explain why a package is installed
        \\
        \\  <b><yellow>build<r>     <d>./a.ts ./b.jsx<r>       Bundle TypeScript & JavaScript into a single file
        \\
        \\  <b><cyan>init<r>                           Start an empty Bun project from a built-in template
        \\  <b><cyan>create<r>    <d>{s:<16}<r>     Create a new project from a template <d>(bun c)<r>
        \\  <b><cyan>upgrade<r>                        Upgrade to latest version of Bun.
        \\  <b><cyan>feedback<r>  <d>./file1 ./file2<r>      Provide feedback to the Bun team.
        \\
        \\  <d>\<command\><r> <b><cyan>--help<r>               Print help text for command.
        \\
    ;
    const cli_helptext_footer =
        \\
        \\Learn more about Bun:            <magenta>https://bun.com/docs<r>
        \\Join our Discord community:      <blue>https://bun.com/discord<r>
        \\
    ;

    static fn printWithReason(comptime reason: Reason, show_all_flags: bool) {
        let rand_state = std.Random.DefaultPrng.init(@as(u64, i32(@max(std.time.milliTimestamp(), 0))));
        const rand = rand_state.random();

        const package_x_i = rand.uintAtMost(usize, packages_to_x_filler.len - 1);
        const package_add_i = rand.uintAtMost(usize, packages_to_add_filler.len - 1);
        const package_remove_i = rand.uintAtMost(usize, packages_to_remove_filler.len - 1);
        const package_create_i = rand.uintAtMost(usize, packages_to_create_filler.len - 1);

        const arg0: []const u8 = packages_to_x_filler[package_x_i];
        const arg1: []const u8 = packages_to_add_filler[package_add_i];
        const arg2: []const u8 = packages_to_remove_filler[package_remove_i];
        const arg3: []const u8 = packages_to_add_filler[(package_add_i + 1) % packages_to_add_filler.len];
        const arg4: []const u8 = packages_to_add_filler[(package_add_i + 2) % packages_to_add_filler.len];
        const arg5: []const u8 = packages_to_add_filler[(package_add_i + 3) % packages_to_add_filler.len];
        const arg6: []const u8 = packages_to_create_filler[package_create_i];
        const args = .{ arg0, arg1, arg2, arg3, arg4, arg5, arg6 };

        switch (reason) {
            .explicit => {
                if (comptime Environment.isDebug) {
                    if (bun.argv.len == 1) {
                        if (bun.Output.isAIAgent()) {
                            if (event := bun.env_var.npm_lifecycle_event.get()) {
                                if (bun.strings.hasPrefixComptime(event, "bd")) {
                                    // claude gets very confused by the help menu
                                    // let's give claude some self confidence.
                                    Output.println("BUN COMPILED SUCCESSFULLY! 🎉", {});
                                    Global.exit(0);
                                }
                            }
                        }
                    }
                }

                Output.pretty(
                    "<r><b><magenta>Bun<r> is a fast JavaScript runtime, package manager, bundler, and test runner. <d>(" ++
                        Global.package_json_version_with_revision ++
                        ")<r>\n\n" ++
                        cli_helptext_fmt,
                    args,
                );
                if (show_all_flags) {
                    Output.pretty("\n<b>Flags:<r>", {});

                    const flags = Arguments.runtime_params_ ++ Arguments.auto_only_params ++ Arguments.base_params_;
                    clap.simpleHelpBunTopLevel(comptime &flags);
                    Output.pretty("\n\n(more flags in <b>bun install --help<r>, <b>bun test --help<r>, and <b>bun build --help<r>)\n", {});
                }
                Output.pretty(cli_helptext_footer, {});
            },
            .invalid_command => Output.prettyError(
                "<r><red>Uh-oh<r> not sure what to do with that command.\n\n" ++ cli_helptext_fmt,
                args,
            ),
        }

        Output.flush();
    }

    static fn execWithReason(_: std.mem.Allocator, comptime reason: Reason) {
        @branchHint(.cold);
        printWithReason(reason, false);

        if (reason == .invalid_command) {
            Global.exit(1);
        }
        Global.exit(0);
    }
}

pub struct ReservedCommand {
    static fn exec(_: std.mem.Allocator)! {
        @branchHint(.cold);
        let command_name = bun.argv[1];
        for (arg of bun.argv[1..]) {
            if (arg.len > 1 && arg[0] == '-') continue;
            command_name = arg;
            break;
        }
        Output.prettyError(
            "<r><red>Uh-oh<r>. <b><yellow>bun {s}<r> is a subcommand reserved for future use by Bun.\n\nIf you were trying to run a package.json script called {s}, use <b><magenta>bun run {s}<r>.\n",
            .{ command_name, command_name, command_name },
        );
        Output.flush();
        std.process.exit(1);
    }
}

/// This is set `true` during `Command.which()` if argv0 is "node", in which the CLI is going
/// to pretend to be node.js by always choosing RunCommand with a relative filepath.
///
/// Examples of how this differs from bun alone:
/// - `node build`               -> `bun run ./build`
/// - `node scripts/postinstall` -> `bun run ./scripts/postinstall`
pub let pretend_to_be_node = false;

/// This is set `true` during `Command.which()` if argv0 is "bunx"
pub let is_bunx_exe = false;

pub struct Command {
    static fn get(): Context {
        return global_cli_ctx;
    }

    pub const ContextData = ctx_types.ContextData;
    pub const Context = ctx_types.Context;
    pub const DebugOptions = ctx_types.DebugOptions;
    pub const TestOptions = ctx_types.TestOptions;
    pub const RuntimeOptions = ctx_types.RuntimeOptions;
    pub const MacroOptions = ctx_types.MacroOptions;
    pub const HotReload = ctx_types.HotReload;
    pub const Debugger = ctx_types.Debugger;

    static let global_cli_ctx: Context = undefined;
    static let context_data: ContextData = undefined;

    pub const init = createContextData;

    /// `ContextData.create` body — kept here because it calls `Arguments.parse`
    /// and reaches into Windows watcher hooks. Aliased onto `ContextData` in
    /// `options_types/Context.zig`.
    static fn createContextData(allocator: std.mem.Allocator, log: *logger.Log, comptime command: Command.Tag): anyerror!Context {
        Cli.cmd = command;
        context_data = {
            args: std.mem.zeroes(api.TransformOptions),
            log: log,
            start_time: start_time,
            allocator: allocator,
        };
        global_cli_ctx = unsafe { @ptrCast(&context_data) };

        if (comptime Command.Tag.uses_global_options.get(command)) {
            global_cli_ctx.args = Arguments.parse(allocator, global_cli_ctx, command)?;
        }

        if (comptime Environment.isWindows) {
            if (global_cli_ctx.debug.hot_reload == .watch) {
                if (!bun.windows.isWatcherChild()) {
                    // this is noreturn
                    bun.windows.becomeWatcherManager(allocator);
                } else {
                    bun.auto_reload_on_crash = true;
                }
            }
        }

        return global_cli_ctx;
    }

    static fn isBunX(argv0: []const u8): bool {
        if (Environment.isWindows) {
            return strings.endsWithComptime(argv0, "bunx.exe") || strings.endsWithComptime(argv0, "bunx");
        }
        return strings.endsWithComptime(argv0, "bunx");
    }

    static fn isNode(argv0: []const u8): bool {
        if (Environment.isWindows) {
            return strings.endsWithComptime(argv0, "node.exe") || strings.endsWithComptime(argv0, "node");
        }
        return strings.endsWithComptime(argv0, "node");
    }

    static fn which(): Tag {
        let args_iter = ArgsIterator{ buf: bun.argv };

        const argv0 = args_iter.next() ?? return .HelpCommand;

        if (isBunX(argv0)) {
            // if we are bunx, but NOT a symlink to bun. when we run `<self> install`, we dont
            // want to recursively run bunx. so this check lets us peek back into bun install.
            if (next := args_iter.next()) {
                if (bun.strings.eqlComptime(next, "add") && bun.feature_flag.BUN_INTERNAL_BUNX_INSTALL.get()) {
                    return .AddCommand;
                } else if (bun.strings.eqlComptime(next, "exec") && bun.feature_flag.BUN_INTERNAL_BUNX_INSTALL.get()) {
                    return .ExecCommand;
                }
            }

            is_bunx_exe = true;
            return .BunxCommand;
        }

        if (isNode(argv0)) {
            clap_streaming.warn_on_unrecognized_flag = false;
            pretend_to_be_node = true;
            return .RunAsNodeCommand;
        }

        let next_arg = ((args_iter.next()) ?? return .AutoCommand);
        while (next_arg.len > 0 && next_arg[0] == '-' && !(next_arg.len > 1 && next_arg[1] == 'e')) {
            const n_opt = args_iter.next();
            if (n_opt == null) return .AutoCommand;
            next_arg = n_opt.?;
        }

        const first_arg_name = next_arg;
        const RootCommandMatcher = strings.ExactSizeMatcher(12);

        const cmd_m = RootCommandMatcher.match(first_arg_name);
        if (cmd_m == RootCommandMatcher.case("init")) return .InitCommand;
        if (cmd_m == RootCommandMatcher.case("build") || cmd_m == RootCommandMatcher.case("bun")) return .BuildCommand;
        if (cmd_m == RootCommandMatcher.case("discord")) return .DiscordCommand;
        if (cmd_m == RootCommandMatcher.case("upgrade")) return .UpgradeCommand;
        if (cmd_m == RootCommandMatcher.case("completions")) return .InstallCompletionsCommand;
        if (cmd_m == RootCommandMatcher.case("getcompletes")) return .GetCompletionsCommand;
        if (cmd_m == RootCommandMatcher.case("link")) return .LinkCommand;
        if (cmd_m == RootCommandMatcher.case("unlink")) return .UnlinkCommand;
        if (cmd_m == RootCommandMatcher.case("x")) return .BunxCommand;
        if (cmd_m == RootCommandMatcher.case("repl")) return .ReplCommand;
        if (cmd_m == RootCommandMatcher.case("i") || cmd_m == RootCommandMatcher.case("install")) {
            for (arg of args_iter.buf) {
                if (arg.len > 0 && (strings.eqlComptime(arg, "-g") || strings.eqlComptime(arg, "--global"))) {
                    return .AddCommand;
                }
            }
            return .InstallCommand;
        }
        if (cmd_m == RootCommandMatcher.case("ci")) return .InstallCommand;
        if (cmd_m == RootCommandMatcher.case("c") || cmd_m == RootCommandMatcher.case("create")) return .CreateCommand;
        if (cmd_m == RootCommandMatcher.case("test")) return .TestCommand;
        if (cmd_m == RootCommandMatcher.case("pm")) return .PackageManagerCommand;
        if (cmd_m == RootCommandMatcher.case("add") || cmd_m == RootCommandMatcher.case("a")) return .AddCommand;
        if (cmd_m == RootCommandMatcher.case("update")) return .UpdateCommand;
        if (cmd_m == RootCommandMatcher.case("patch")) return .PatchCommand;
        if (cmd_m == RootCommandMatcher.case("patch-commit")) return .PatchCommitCommand;
        if (cmd_m == RootCommandMatcher.case("r") || cmd_m == RootCommandMatcher.case("remove") || cmd_m == RootCommandMatcher.case("rm") || cmd_m == RootCommandMatcher.case("uninstall")) return .RemoveCommand;
        if (cmd_m == RootCommandMatcher.case("run")) return .RunCommand;
        if (cmd_m == RootCommandMatcher.case("help")) return .HelpCommand;
        if (cmd_m == RootCommandMatcher.case("exec")) return .ExecCommand;
        if (cmd_m == RootCommandMatcher.case("outdated")) return .OutdatedCommand;
        if (cmd_m == RootCommandMatcher.case("publish")) return .PublishCommand;
        if (cmd_m == RootCommandMatcher.case("audit")) return .AuditCommand;
        if (cmd_m == RootCommandMatcher.case("info")) return .InfoCommand;
        if (cmd_m == RootCommandMatcher.case("deploy") || cmd_m == RootCommandMatcher.case("cloud") || cmd_m == RootCommandMatcher.case("config") || cmd_m == RootCommandMatcher.case("use") || cmd_m == RootCommandMatcher.case("auth") || cmd_m == RootCommandMatcher.case("login") || cmd_m == RootCommandMatcher.case("logout")) return .ReservedCommand;
        if (cmd_m == RootCommandMatcher.case("whoami") || cmd_m == RootCommandMatcher.case("list")) return .PackageManagerCommand;
        if (cmd_m == RootCommandMatcher.case("why")) return .WhyCommand;
        if (cmd_m == RootCommandMatcher.case("dedupe")) return .DedupeCommand;
        if (cmd_m == RootCommandMatcher.case("prune")) return .PruneCommand;
        if (cmd_m == RootCommandMatcher.case("fuzzilli")) return if (bun.Environment.enable_fuzzilli) .FuzzilliCommand else .AutoCommand;
        return .AutoCommand;
    }

    const default_completions_list: [17]string = [
        "build",
        "install",
        "add",
        "run",
        "update",
        "link",
        "unlink",
        "remove",
        "create",
        "bun",
        "upgrade",
        "discord",
        "test",
        "pm",
        "x",
        "repl",
        "info",
    ];

    const reject_list: [20]string = default_completions_list ++ [
        "build",
        "completions",
        "help",
    ];

    /// Keep the stack space usage of this function small. This function is
    /// kept alive for the entire duration of the process
    ///
    /// So do not add any path buffers or anything that is large in this
    /// function or that stack space is used up forever.
    static fn start(allocator: std.mem.Allocator, log: *logger.Log)! {
        if (comptime Environment.allow_assert) {
            if (!bun.env_var.MI_VERBOSE.get()) {
                bun.mimalloc.mi_option_set_enabled(.verbose, false);
            }
        }

        // WebView host subprocess entry. Must be before StandaloneModuleGraph,
        // before JSC init, before anything that touches a JS engine. The child
        // runs CFRunLoopRun() as its real main loop — no Bun runtime past this.
        if (comptime Environment.isMac) {
            if (fd_str := bun.env_var.BUN_INTERNAL_WEBVIEW_HOST.get()) {
                const fd = std.fmt.parseInt(u31, fd_str, 10) catch {
                    Output.panic("Invalid BUN_INTERNAL_WEBVIEW_HOST fd: {s}", .{fd_str});
                };
                Bun__WebView__hostMain(fd);
            }
        }

        // bun build --compile entry point
        if (!bun.feature_flag.BUN_BE_BUN.get()) {
            if (graph := bun.StandaloneModuleGraph.fromExecutable(bun.default_allocator)?) {
                let offset_for_passthrough: usize = 0;

                const ctx: Context = brk: {
                    if (graph.compile_exec_argv.len > 0 || bun.bun_options_argc > 0) {
                        const original_argv_len = bun.argv.len;
                        let argv_list = std.array_list.Managed([:0]const u8).fromOwnedSlice(bun.default_allocator, bun.argv);
                        if (graph.compile_exec_argv.len > 0) {
                            bun.appendOptionsEnv(graph.compile_exec_argv, [:0]const u8, &argv_list)?;
                        }

                        // Store the full argv including user arguments
                        const full_argv = argv_list.items;
                        const num_exec_argv_options = full_argv.len -| original_argv_len;

                        // Calculate offset: skip executable name + all exec argv options + BUN_OPTIONS args
                        const num_parsed_options = num_exec_argv_options + bun.bun_options_argc;
                        offset_for_passthrough = if (full_argv.len > 1) 1 + num_parsed_options else @as(usize, 0);

                        // Temporarily set bun.argv to only include executable name + exec_argv options + BUN_OPTIONS args.
                        // This prevents user arguments like --version/--help from being intercepted
                        // by Bun's argument parser (they should be passed through to user code).
                        bun.argv = full_argv[0..@min(1 + num_parsed_options, full_argv.len)];

                        // Handle actual options to parse.
                        const result = Command.init(allocator, log, .AutoCommand)?;

                        // Restore full argv so passthrough calculation works correctly
                        bun.argv = full_argv;

                        break :brk result;
                    }

                    context_data = {
                        args: std.mem.zeroes(api.TransformOptions),
                        log: log,
                        start_time: start_time,
                        allocator: bun.default_allocator,
                    };
                    global_cli_ctx = unsafe { @ptrCast(&context_data) };

                    // If no compile_exec_argv, skip executable name if present
                    offset_for_passthrough = @min(1, bun.argv.len);

                    break :brk global_cli_ctx;
                };

                ctx.args.target = .bun;
                if (ctx.debug.global_cache == .auto)
                    ctx.debug.global_cache = .disable;

                ctx.passthrough = bun.argv[offset_for_passthrough..];

                bun_js.Run.bootStandalone(
                    ctx,
                    graph.entryPoint().name,
                    graph,
                )?;
                return;
            }
        }

        debug("argv: [{f}]", .{bun.fmt.fmtSlice(bun.argv, ", ")});

        const tag = which();

        switch (tag) {
            .DiscordCommand => return DiscordCommand.exec(allocator)?,
            .HelpCommand => return HelpCommand.exec(allocator)?,
            .ReservedCommand => return ReservedCommand.exec(allocator)?,
            .InitCommand => {
                const init_exec = InitCommand.exec;
                return init_exec(allocator, bun.argv[@min(2, bun.argv.len)..])?;
            },
            .InfoCommand => {
                @"bun info"(allocator, log)?;
                return;
            },
            .BuildCommand => {
                const ctx = Command.init(allocator, log, .BuildCommand)?;
                const build_exec = BuildCommand.exec;
                build_exec(ctx, null)?;
            },
            .InstallCompletionsCommand => {
                InstallCompletionsCommand.exec(allocator)?;
                return;
            },
            .InstallCommand => {
                const ctx = Command.init(allocator, log, .InstallCommand)?;

                InstallCommand.exec(ctx)?;
                return;
            },
            .AddCommand => {
                const ctx = Command.init(allocator, log, .AddCommand)?;

                AddCommand.exec(ctx)?;
                return;
            },
            .UpdateCommand => {
                const ctx = Command.init(allocator, log, .UpdateCommand)?;

                UpdateCommand.exec(ctx)?;
                return;
            },
            .PatchCommand => {
                const ctx = Command.init(allocator, log, .PatchCommand)?;

                PatchCommand.exec(ctx)?;
                return;
            },
            .PatchCommitCommand => {
                const ctx = Command.init(allocator, log, .PatchCommitCommand)?;

                PatchCommitCommand.exec(ctx)?;
                return;
            },
            .OutdatedCommand => {
                const ctx = Command.init(allocator, log, .OutdatedCommand)?;

                OutdatedCommand.exec(ctx)?;
                return;
            },
            .UpdateInteractiveCommand => {
                const ctx = Command.init(allocator, log, .UpdateInteractiveCommand)?;

                UpdateInteractiveCommand.exec(ctx)?;
                return;
            },
            .PublishCommand => {
                const ctx = Command.init(allocator, log, .PublishCommand)?;

                PublishCommand.exec(ctx)?;
                return;
            },
            .AuditCommand => {
                const ctx = Command.init(allocator, log, .AuditCommand)?;
                AuditCommand.exec(ctx)?;
            },
            .WhyCommand => {
                const ctx = Command.init(allocator, log, .WhyCommand)?;
                WhyCommand.exec(ctx)?;
                return;
            },
            .DedupeCommand => {
                const ctx = Command.init(allocator, log, .DedupeCommand)?;
                DedupeCommand.exec(ctx)?;
                return;
            },
            .PruneCommand => {
                const ctx = Command.init(allocator, log, .PruneCommand)?;
                PruneCommand.exec(ctx)?;
                return;
            },
            .BunxCommand => {
                const ctx = Command.init(allocator, log, .BunxCommand)?;

                const bunx_exec = BunxCommand.exec;
                bunx_exec(ctx, bun.argv[if (is_bunx_exe) 0 else 1..])?;
                return;
            },
            .ReplCommand => {
                const ctx = Command.init(allocator, log, .RunCommand)?;
                ReplCommand.exec(ctx)?;
                return;
            },
            .RemoveCommand => {
                const ctx = Command.init(allocator, log, .RemoveCommand)?;

                RemoveCommand.exec(ctx)?;
                return;
            },
            .LinkCommand => {
                const ctx = Command.init(allocator, log, .LinkCommand)?;

                LinkCommand.exec(ctx)?;
                return;
            },
            .UnlinkCommand => {
                const ctx = Command.init(allocator, log, .UnlinkCommand)?;

                UnlinkCommand.exec(ctx)?;
                return;
            },
            .PackageManagerCommand => {
                const ctx = Command.init(allocator, log, .PackageManagerCommand)?;

                PackageManagerCommand.exec(ctx)?;
                return;
            },
            .TestCommand => {
                const ctx = Command.init(allocator, log, .TestCommand)?;

                TestCommand.exec(ctx)?;
                return;
            },
            .GetCompletionsCommand => {
                @"bun getcompletes"(allocator, log)?;
                return;
            },
            .CreateCommand => {
                @"bun create"(allocator, log)?;
                return;
            },
            .RunCommand => {
                const ctx = Command.init(allocator, log, .RunCommand)?;
                ctx.args.target = .bun;

                if (ctx.parallel || ctx.sequential) {
                    MultiRun.run(ctx) catch e => {
                        Output.prettyErrorln("<r><red>error<r>: {s}", .{@errorName(e)});
                        Global.exit(1);
                    };
                }

                if (ctx.filters.len > 0 || ctx.workspaces) {
                    FilterRun.runScriptsWithFilter(ctx) catch e => {
                        Output.prettyErrorln("<r><red>error<r>: {s}", .{@errorName(e)});
                        Global.exit(1);
                    };
                }

                if (ctx.positionals.len > 0) {
                    const run_exec = RunCommand.exec;
                    if (run_exec(ctx, { bin_dirs_only: false, log_errors: true, allow_fast_run_for_extensions: false })?) {
                        return;
                    }

                    Global.exit(1);
                }
            },
            .RunAsNodeCommand => {
                const ctx = Command.init(allocator, log, .RunAsNodeCommand)?;
                bun.assert(pretend_to_be_node);
                RunCommand.execAsIfNode(ctx)?;
            },
            .UpgradeCommand => {
                const ctx = Command.init(allocator, log, .UpgradeCommand)?;
                UpgradeCommand.exec(ctx)?;
                return;
            },
            .AutoCommand => {
                const ctx = Command.init(allocator, log, .AutoCommand) catch e => {
                    if (e == error.MissingEntryPoint) {
                        HelpCommand.execWithReason(allocator, .explicit);
                        return;
                    }
                    return e;
                };
                ctx.args.target = .bun;

                if (ctx.parallel || ctx.sequential) {
                    MultiRun.run(ctx) catch e => {
                        Output.prettyErrorln("<r><red>error<r>: {s}", .{@errorName(e)});
                        Global.exit(1);
                    };
                }

                if (ctx.filters.len > 0 || ctx.workspaces) {
                    FilterRun.runScriptsWithFilter(ctx) catch e => {
                        Output.prettyErrorln("<r><red>error<r>: {s}", .{@errorName(e)});
                        Global.exit(1);
                    };
                }

                if (ctx.runtime_options.eval.script.len > 0) {
                    return @"bun --eval --print"(ctx)?;
                }

                const extension: []const u8 = if (ctx.args.entry_points.len > 0)
                    std.fs.path.extension(ctx.args.entry_points[0])
                else
                    "";
                // KEYWORDS: open file argv argv0
                if (ctx.args.entry_points.len == 1) {
                    if (strings.eqlComptime(extension, ".lockb")) {
                        return @"bun ./bun.lockb"(ctx)?;
                    }
                }

                if (ctx.positionals.len > 0) {
                    if (ctx.filters.len > 0) {
                        Output.prettyln("<r><yellow>warn<r>: Filters are ignored for auto command", {});
                    }
                    const run_exec = RunCommand.exec;
                    if (run_exec(ctx, { bin_dirs_only: true, log_errors: !ctx.runtime_options.if_present, allow_fast_run_for_extensions: true })?) {
                        return;
                    }
                    return;
                }

                Output.flush();
                HelpCommand.exec(allocator)?;
            },
            .ExecCommand => {
                const ctx = Command.init(allocator, log, .ExecCommand)?;

                if (ctx.positionals.len > 1) {
                    ExecCommand.exec(ctx)?;
                } else Tag.printHelp(.ExecCommand, true);
            },
            .FuzzilliCommand => {
                if (bun.Environment.enable_fuzzilli) {
                    const ctx = Command.init(allocator, log, .FuzzilliCommand)?;
                    FuzzilliCommand.exec(ctx)?;
                    return;
                } else {
                    return error.UnrecognizedCommand;
                }
            },
        }
    }

    pub const Tag = CommandTagMod.Tag;

    static fn tagParams(comptime cmd: Tag): []const Arguments.ParamType {
        return comptime &switch (cmd) {
            .AutoCommand => Arguments.auto_params,
            .RunCommand, .RunAsNodeCommand => Arguments.run_params,
            .BuildCommand => Arguments.build_params,
            .TestCommand => Arguments.test_params,
            .BunxCommand => Arguments.run_params,
            else => Arguments.base_params_ ++ Arguments.runtime_params_ ++ Arguments.transpiler_params_,
        };
    }

    static fn tagPrintHelp(comptime cmd: Tag, show_all_flags: bool) {
        switch (cmd) {

            // the output of --help uses the following syntax highlighting
            // template: <b>Usage<r>: <b><green>bun <command><r> <cyan>[flags]<r> <blue>[arguments]<r>
            // use [foo] for multiple arguments or flags for foo.
            // use <bar> to emphasize 'bar'

            // these commands do not use Context
            // .DiscordCommand => return try DiscordCommand.exec(allocator),
            // .HelpCommand => return try HelpCommand.exec(allocator),
            // .ReservedCommand => return try ReservedCommand.exec(allocator),

            // these commands are implemented in install.zig
            // Command.Tag.InstallCommand => {},
            // Command.Tag.AddCommand => {},
            // Command.Tag.RemoveCommand => {},
            // Command.Tag.UpdateCommand => {},
            // Command.Tag.PackageManagerCommand => {},
            // Command.Tag.LinkCommand => {},
            // Command.Tag.UnlinkCommand => {},

            // fall back to HelpCommand.printWithReason
            .AutoCommand => {
                HelpCommand.printWithReason(.explicit, show_all_flags);
            },
            .RunCommand, .RunAsNodeCommand => {
                RunCommand_.printHelp(null);
            },

            .InitCommand => {
                const intro_text =
                    \\<b>Usage<r>: <b><green>bun init<r> <cyan>[flags]<r> <blue>[\<folder\>]<r>
                    \\  Initialize a Bun project in the current directory.
                    \\  Creates a package.json, tsconfig.json, and bunfig.toml if they don't exist.
                    \\
                    \\<b>Flags<r>:
                    \\      <cyan>--help<r>             Print this menu
                    \\  <cyan>-y, --yes<r>              Accept all default options
                    \\  <cyan>-m, --minimal<r>          Only initialize type definitions
                    \\  <cyan>-r, --react<r>            Initialize a React project
                    \\      <cyan>--react=tailwind<r>   Initialize a React project with TailwindCSS
                    \\      <cyan>--react=shadcn<r>     Initialize a React project with @shadcn/ui and TailwindCSS
                    \\
                    \\<b>Examples:<r>
                    \\  <b><green>bun init<r>
                    \\  <b><green>bun init<r> <cyan>--yes<r>
                    \\  <b><green>bun init<r> <cyan>--react<r>
                    \\  <b><green>bun init<r> <cyan>--react=tailwind<r> <blue>my-app<r>
                ;

                Output.pretty(intro_text ++ "\n", {});
                Output.flush();
            },

            .BunxCommand => {
                Output.prettyErrorln(
                    "<b>Usage<r>: <b><green>bunx<r> <cyan>[flags]<r> <blue>\\<package\\><r><d>\\<@version\\><r> [flags and arguments for the package]<r>\nExecute an npm package executable (CLI), automatically installing into a global shared cache if not installed in node_modules.\n\nFlags:\n  <cyan>--bun<r>                  Force the command to run with Bun instead of Node.js\n  <cyan>-p, --package <blue>\\<package\\><r>    Specify package to install when binary name differs from package name\n  <cyan>--no-install<r>           Skip installation if package is not already installed\n  <cyan>--verbose<r>              Enable verbose output during installation\n  <cyan>--silent<r>               Suppress output during installation\n\nExamples<d>:<r>\n  <b><green>bunx<r> <blue>prisma<r> migrate<r>\n  <b><green>bunx<r> <blue>prettier<r> foo.js<r>\n  <b><green>bunx<r> <cyan>-p @angular/cli<r> <blue>ng<r> new my-app\n  <b><green>bunx<r> <cyan>--bun<r> <blue>vite<r> dev foo.js<r>\n",
                    .{},
                );
            },
            .BuildCommand => {
                const intro_text =
                    \\<b>Usage<r>:
                    \\  Transpile and bundle one or more files.
                    \\  <b><green>bun build<r> <cyan>[flags]<r> <blue>\<entrypoint\><r>
                ;

                const outro_text =
                    \\<b>Examples:<r>
                    \\  <d>Frontend web apps:<r>
                    \\  <b><green>bun build<r> <cyan>--outfile=bundle.js<r> <blue>./src/index.ts<r>
                    \\  <b><green>bun build<r> <cyan>--minify --splitting --outdir=out<r> <blue>./index.jsx ./lib/worker.ts<r>
                    \\
                    \\  <d>Bundle code to be run in Bun (reduces server startup time)<r>
                    \\  <b><green>bun build<r> <cyan>--target=bun --outfile=server.js<r> <blue>./server.ts<r>
                    \\
                    \\  <d>Creating a standalone executable (see https://bun.com/docs/bundler/executables)<r>
                    \\  <b><green>bun build<r> <cyan>--compile --outfile=my-app<r> <blue>./cli.ts<r>
                    \\
                    \\A full list of flags is available at <magenta>https://bun.com/docs/bundler<r>
                    \\
                ;

                Output.pretty(intro_text ++ "\n\n", {});
                Output.flush();
                Output.pretty("<b>Flags:<r>", {});
                Output.flush();
                clap.simpleHelp(&Arguments.build_only_params);
                Output.pretty("\n\n" ++ outro_text, {});
                Output.flush();
            },
            .TestCommand => {
                const intro_text =
                    \\<b>Usage<r>: <b><green>bun test<r> <cyan>[flags]<r> <blue>[\<patterns\>]<r>
                    \\  Run all matching test files and print the results to stdout
                ;
                const outro_text =
                    \\<b>Examples:<r>
                    \\  <d>Run all test files<r>
                    \\  <b><green>bun test<r>
                    \\
                    \\  <d>Run all test files with "foo" or "bar" in the file name<r>
                    \\  <b><green>bun test<r> <blue>foo bar<r>
                    \\
                    \\  <d>Run all test files, only including tests whose names includes "baz"<r>
                    \\  <b><green>bun test<r> <cyan>--test-name-pattern<r> <blue>baz<r>
                    \\
                    \\Full documentation is available at <magenta>https://bun.com/docs/cli/test<r>
                    \\
                ;

                Output.pretty(intro_text, {});
                Output.flush();
                Output.pretty("\n\n<b>Flags:<r>", {});
                Output.flush();
                clap.simpleHelp(&Arguments.test_only_params);
                Output.pretty("\n\n", {});
                Output.pretty(outro_text, {});
                Output.flush();
            },
            .CreateCommand => {
                const intro_text =
                    \\<b>Usage<r><d>:<r>
                    \\  <b><green>bun create<r> <magenta>\<MyReactComponent.(jsx|tsx)\><r>
                    \\  <b><green>bun create<r> <magenta>\<template\><r> <cyan>[...flags]<r> <blue>dest<r>
                    \\  <b><green>bun create<r> <magenta>\<github-org/repo\><r> <cyan>[...flags]<r> <blue>dest<r>
                    \\
                    \\<b>Environment variables<r><d>:<r>
                    \\  <cyan>GITHUB_TOKEN<r>         <d>Supply a token to download code from GitHub with a higher rate limit<r>
                    \\  <cyan>GITHUB_API_DOMAIN<r>    <d>Configure custom/enterprise GitHub domain. Default "api.github.com"<r>
                    \\  <cyan>NPM_CLIENT<r>           <d>Absolute path to the npm client executable<r>
                    \\  <cyan>BUN_CREATE_DIR<r>       <d>Custom path for global templates (default: $HOME/.bun-create)<r>
                ;

                const outro_text =
                    \\<b>React Component Projects<r><d>:<r>
                    \\  • Turn an existing React component into a complete frontend dev environment
                    \\  • Automatically starts a hot-reloading dev server
                    \\  • Auto-detects & configures TailwindCSS and shadcn/ui
                    \\
                    \\  <b><magenta>bun create \<MyReactComponent.(jsx|tsx)\><r>
                    \\
                    \\<b>Templates<r><d>:<r>
                    \\  • NPM: Runs <b><magenta>bunx create-\<template\><r> with given arguments
                    \\  • GitHub: Downloads repository contents as template
                    \\  • Local: Uses templates from $HOME/.bun-create/\<name\> or ./.bun-create/\<name\>
                    \\
                    \\Learn more: <magenta>https://bun.com/docs/cli/bun-create<r>
                    \\
                ;

                Output.pretty(intro_text, {});
                Output.pretty("\n\n", {});
                Output.pretty(outro_text, {});
                Output.flush();
            },
            .HelpCommand => {
                HelpCommand.printWithReason(.explicit);
            },
            .UpgradeCommand => {
                const intro_text =
                    \\<b>Usage<r>: <b><green>bun upgrade<r> <cyan>[flags]<r>
                    \\  Upgrade Bun
                ;
                const outro_text = "<b>Examples:<r>\n  <d>Install the latest {s} version<r>\n  <b><green>bun upgrade<r>\n\n  <d>{s}<r>\n  <b><green>bun upgrade<r> <cyan>--{s}<r>\n\nFull documentation is available at <magenta>https://bun.com/docs/installation#upgrading<r>\n";

                const args = if (Environment.is_canary)
                    .{ "canary", "Switch from the canary version back to the latest stable release", "stable" }
                else
                    .{ "stable", "Install the most recent canary version of Bun", "canary" };

                Output.pretty(intro_text, {});
                Output.pretty("\n\n", {});
                Output.flush();
                Output.pretty(outro_text, args);
                Output.flush();
            },
            .ReplCommand => {
                const intro_text =
                    \\<b>Usage<r>: <b><green>bun repl<r> <cyan>[flags]<r>
                    \\  Open a Bun REPL
                    \\
                ;

                Output.pretty(intro_text, {});
                Output.flush();
            },

            .GetCompletionsCommand => {
                Output.pretty("<b>Usage<r>: <b><green>bun getcompletes<r>", {});
                Output.flush();
            },
            .InstallCompletionsCommand => {
                Output.pretty("<b>Usage<r>: <b><green>bun completions<r>", {});
                Output.flush();
            },
            .PatchCommand => {
                Install.PackageManager.CommandLineArguments.printHelp(.patch);
            },
            .PatchCommitCommand => {
                Install.PackageManager.CommandLineArguments.printHelp(.@"patch-commit");
            },
            .ExecCommand => {
                Output.pretty(
                    "<b>Usage: bun exec <r><cyan>\\<script\\><r>\n\nExecute a shell script directly from Bun.\n\n<b><red>Note<r>: If executing this from a shell, make sure to escape the string!\n\n<b>Examples<d>:<r>\n  <b>bun exec \"echo hi\"<r>\n  <b>bun exec \"echo \\\"hey friends\\\"!\"<r>\n",
                    {},
                );
                Output.flush();
            },
            .OutdatedCommand, .UpdateInteractiveCommand, .PublishCommand, .AuditCommand => {
                Install.PackageManager.CommandLineArguments.printHelp(switch (cmd) {
                    .OutdatedCommand => .outdated,
                    .UpdateInteractiveCommand => .update,
                    .PublishCommand => .publish,
                    .AuditCommand => .audit,
                });
            },
            .InfoCommand => {
                const intro_text =
                    \\<b>Usage<r>: <b><green>bun info<r> <cyan>[flags]<r> <blue>\<package\><r><d>\<@version\><r> <blue>[property path]<r>
                    \\  Display package metadata from the registry.
                    \\
                    \\<b>Examples:<r>
                    \\  <d>View basic information about a package<r>
                    \\  <b><green>bun info<r> <blue>react<r>
                    \\
                    \\  <d>View specific version<r>
                    \\  <b><green>bun info<r> <blue>react@18.0.0<r>
                    \\
                    \\  <d>View specific property<r>
                    \\  <b><green>bun info<r> <blue>react<r> version
                    \\  <b><green>bun info<r> <blue>react<r> dependencies
                    \\  <b><green>bun info<r> <blue>react<r> versions
                    \\
                    \\Full documentation is available at <magenta>https://bun.com/docs/cli/info<r>
                    \\
                ;

                Output.pretty(intro_text, {});
                Output.flush();
            },
            .WhyCommand => {
                const intro_text =
                    \\<b>Usage<r>: <b><green>bun why<r> <cyan>[flags]<r> <blue>\<package\><r><d>\<@version\><r> <blue>[property path]<r>
                    \\Explain why a package is installed
                    \\
                    \\<b>Arguments:<r>
                    \\  <blue>\<package\><r>     <d>The package name to explain (supports glob patterns like '@org/*')<r>
                    \\
                    \\<b>Options:<r>
                    \\  <cyan>--top<r>         <d>Show only the top dependency tree instead of nested ones<r>
                    \\  <cyan>--depth<r> <blue>\<NUM\><r> <d>Maximum depth of the dependency tree to display<r>
                    \\
                    \\<b>Examples:<r>
                    \\  <d>$<r> <b><green>bun why<r> <blue>react<r>
                    \\  <d>$<r> <b><green>bun why<r> <blue>"@types/*"<r> <cyan>--depth<r> <blue>2<r>
                    \\  <d>$<r> <b><green>bun why<r> <blue>"*-lodash"<r> <cyan>--top<r>
                    \\
                    \\Full documentation is available at <magenta>https://bun.com/docs/cli/why<r>
                    \\
                ;

                Output.pretty(intro_text, {});
                Output.flush();
            },
            .DedupeCommand => PackageManager.CommandLineArguments.printHelp(.dedupe),
            .PruneCommand => PackageManager.CommandLineArguments.printHelp(.prune),
            else => {
                HelpCommand.printWithReason(.explicit, false);
            },
        }
    }

    static fn @"bun --eval --print"(ctx: Context)! {
        const trigger = bun.pathLiteral("/[eval]");
        const entry_point_buf_len = bun.MAX_PATH_BYTES + trigger.len;
        let entry_point_buf: [entry_point_buf_len]u8 = undefined;
        const cwd = std.posix.getcwd(&entry_point_buf)?;
        @memcpy(entry_point_buf[cwd.len..][0..trigger.len], trigger);
        const concat_parts = [ ctx.positionals, ctx.passthrough ];
        ctx.passthrough = std.mem.concat(ctx.allocator, string, &concat_parts)?;
        bun_js.Run.boot(ctx, entry_point_buf[0 .. cwd.len + trigger.len], null)?;
    }

    static fn @"bun ./bun.lockb"(ctx: Context)! {
        for (arg of bun.argv) {
            if (strings.eqlComptime(arg, "--hash")) {
                let path_buf: bun.PathBuffer = undefined;
                @memcpy(path_buf[0..ctx.args.entry_points[0].len], ctx.args.entry_points[0]);
                path_buf[ctx.args.entry_points[0].len] = 0;
                const lockfile_path = path_buf[0..ctx.args.entry_points[0].len :0];
                const file = File.open(lockfile_path, bun.O.RDONLY, 0).unwrap() catch e => {
                    Output.err(e, "failed to open lockfile", {});
                    Global.crash();
                };
                PackageManagerCommand.printHash(ctx, file)?;
                return;
            }
        }

        Install.Lockfile.Printer.print(
            ctx.allocator,
            ctx.log,
            ctx.args.entry_points[0],
            .yarn,
        )?;
    }

    static fn @"bun getcompletes"(allocator: std.mem.Allocator, log: *logger.Log)! {
        const ctx = Command.init(allocator, log, .GetCompletionsCommand)?;
        let filter = ctx.positionals;

        let i: usize = 0;
        while (i < filter.len) : (i += 1) {
            const item = filter[i];
            if (strings.eqlComptime(item, "getcompletes")) {
                if (i + 1 < filter.len) {
                    filter = filter[i + 1 ..];
                } else {
                    filter = [];
                }

                break;
            }
        }
        const biggest_list_len = AddCompletions.biggest_list;
        let prefilled_completions: [biggest_list_len]string = undefined;
        let completions = ShellCompletions{};

        if (filter.len == 0) {
            completions = RunCommand.completions(ctx, default_completions_list[0..], reject_list[0..], .all)?;
        } else if (strings.eqlComptime(filter[0], "s")) {
            completions = RunCommand.completions(ctx, null, reject_list[0..], .script)?;
        } else if (strings.eqlComptime(filter[0], "i")) {
            completions = RunCommand.completions(ctx, default_completions_list[0..], reject_list[0..], .script_exclude)?;
        } else if (strings.eqlComptime(filter[0], "b")) {
            completions = RunCommand.completions(ctx, null, reject_list[0..], .bin)?;
        } else if (strings.eqlComptime(filter[0], "r")) {
            completions = RunCommand.completions(ctx, null, reject_list[0..], .all)?;
        } else if (strings.eqlComptime(filter[0], "g")) {
            completions = RunCommand.completions(ctx, null, reject_list[0..], .all_plus_bun_js)?;
        } else if (strings.eqlComptime(filter[0], "j")) {
            completions = RunCommand.completions(ctx, null, reject_list[0..], .bun_js)?;
        } else if (strings.eqlComptime(filter[0], "z")) {
            completions = RunCommand.completions(ctx, null, reject_list[0..], .script_and_descriptions)?;
        } else if (strings.eqlComptime(filter[0], "a")) {
            const FirstLetter = AddCompletions.FirstLetter;

            outer: {
                if (filter.len > 1 && filter[1].len > 0) {
                    const first_letter: FirstLetter = switch (filter[1][0]) {
                        'a' => FirstLetter.a,
                        'b' => FirstLetter.b,
                        'c' => FirstLetter.c,
                        'd' => FirstLetter.d,
                        'e' => FirstLetter.e,
                        'f' => FirstLetter.f,
                        'g' => FirstLetter.g,
                        'h' => FirstLetter.h,
                        'i' => FirstLetter.i,
                        'j' => FirstLetter.j,
                        'k' => FirstLetter.k,
                        'l' => FirstLetter.l,
                        'm' => FirstLetter.m,
                        'n' => FirstLetter.n,
                        'o' => FirstLetter.o,
                        'p' => FirstLetter.p,
                        'q' => FirstLetter.q,
                        'r' => FirstLetter.r,
                        's' => FirstLetter.s,
                        't' => FirstLetter.t,
                        'u' => FirstLetter.u,
                        'v' => FirstLetter.v,
                        'w' => FirstLetter.w,
                        'x' => FirstLetter.x,
                        'y' => FirstLetter.y,
                        'z' => FirstLetter.z,
                        else => break :outer,
                    };
                    bun.handleOom(AddCompletions.init(bun.default_allocator));
                    const results = AddCompletions.getPackages(first_letter);

                    let prefilled_i: usize = 0;
                    for (cur of results) {
                        if (cur.len == 0 || !strings.hasPrefix(cur, filter[1])) continue;
                        prefilled_completions[prefilled_i] = cur;
                        prefilled_i += 1;
                        if (prefilled_i >= prefilled_completions.len) break;
                    }
                    completions.commands = prefilled_completions[0..prefilled_i];
                }
            }
        }
        completions.print();
    }

    static fn @"bun create"(allocator: std.mem.Allocator, log: *logger.Log)! {
        // These are templates from the legacy `bun create`
        // most of them aren't useful but these few are kinda nice.
        const HardcodedNonBunXList = bun.ComptimeStringMap(void, .{
            .{"elysia"},
            .{"elysia-buchta"},
            .{"stric"},
        });

        // Create command wraps bunx
        const ctx = Command.init(allocator, log, .CreateCommand)?;

        let args = std.process.argsAlloc(allocator)?;

        if (args.len <= 2) {
            Command.Tag.printHelp(.CreateCommand, false);
            Global.exit(1);
            return;
        }

        let template_name_start: usize = 0;
        let positionals: [2]string = .{ "", "" };
        let positional_i: usize = 0;

        let dash_dash_bun = false;
        let print_help = false;
        if (args.len > 2) {
            const remainder = args[1..];
            let remainder_i: usize = 0;
            while (remainder_i < remainder.len && positional_i < positionals.len) : (remainder_i += 1) {
                const slice = std.mem.trim(u8, bun.asByteSlice(remainder[remainder_i]), " \t\n");
                if (slice.len > 0) {
                    if (!strings.hasPrefixComptime(slice, "--")) {
                        if (positional_i == 1) {
                            template_name_start = remainder_i + 2;
                        }
                        positionals[positional_i] = slice;
                        positional_i += 1;
                    }
                    if (slice[0] == '-') {
                        if (strings.eqlComptime(slice, "--bun")) {
                            dash_dash_bun = true;
                        } else if (strings.eqlComptime(slice, "--help") || strings.eqlComptime(slice, "-h")) {
                            print_help = true;
                        }
                    }
                }
            }
        }

        if (print_help ||
            // "bun create --"
            // "bun create -abc --"
            positional_i == 0 ||
            positionals[1].len == 0)
        {
            Command.Tag.printHelp(.CreateCommand, true);
            Global.exit(0);
            return;
        }

        const template_name = positionals[1];

        // if template_name is "react"
        // print message telling user to use "bun create vite" instead
        if (strings.eqlComptime(template_name, "react")) {
            Output.prettyErrorln(
                "The \"react\" template has been deprecated.\nIt is recommended to use \"react-app\" or \"vite\" instead.\n\nTo create a project using Create React App, run\n\n  <d>bun create react-app<r>\n\nTo create a React project using Vite, run\n\n  <d>bun create vite<r>\n\nThen select \"React\" from the list of frameworks.\n",
                {},
            );
            Global.exit(1);
            return;
        }

        // if template_name is "next"
        // print message telling user to use "bun create next-app" instead
        if (strings.eqlComptime(template_name, "next")) {
            Output.prettyErrorln(
                "<yellow>warn: No template <b>create-next<r> found.\nTo create a project with the official Next.js scaffolding tool, run\n  <b>bun create next-app <cyan>[destination]<r>",
                {},
            );
            Global.exit(1);
            return;
        }

        const create_command_info = CreateCommand.extractInfo(ctx)?;
        const template = create_command_info.template;
        const example_tag = create_command_info.example_tag;

        const use_bunx = !HardcodedNonBunXList.has(template_name) &&
            (!strings.containsComptime(template_name, "/") ||
                strings.startsWithChar(template_name, '@')) &&
            example_tag != CreateCommandExample.Tag.local_folder;

        if (use_bunx) {
            let bunx_args = allocator.alloc([:0]const u8, 2 + args.len - template_name_start + @intFromBool(dash_dash_bun))?;
            bunx_args[0] = "bunx";
            if (dash_dash_bun) {
                bunx_args[1] = "--bun";
            }
            bunx_args[1 + @as(usize, @intFromBool(dash_dash_bun))] = BunxCommand.addCreatePrefix(allocator, template_name)?;
            let dst = bunx_args[2 + @as(usize, @intFromBool(dash_dash_bun)) ..];
            const srcs = args[template_name_start..];
            let di: usize = 0;
            while (di < dst.len) : (di += 1) {
                dst[di] = srcs[di];
            }

            const bunx_exec = BunxCommand.exec;
            bunx_exec(ctx, bunx_args)?;
            return;
        }

        const create_exec = CreateCommand.exec;
        create_exec(ctx, example_tag, template)?;
    }

    static fn @"bun info"(allocator: std.mem.Allocator, log: *logger.Log)! {
        // Parse arguments manually since the standard flow doesn't work for standalone commands
        const cli = PackageManager.CommandLineArguments.parse(allocator, .info)?;
        const ctx = Command.init(allocator, log, .InfoCommand)?;
        const pm_res = PackageManager.init(
            ctx,
            cli,
            PackageManager.Subcommand.info,
        )?;
        const pm = pm_res[0];

        // Handle arguments correctly for standalone info command
        let package_name: []const u8 = "";
        let property_path: ([]const u8)? = null;

        // Find non-flag arguments starting from argv[2] (after "bun info")
        let arg_idx: usize = 2;
        let found_package = false;

        while (arg_idx < bun.argv.len) : (arg_idx += 1) {
            const arg = bun.argv[arg_idx];

            // Skip flags
            if (arg.len > 0 && arg[0] == '-') {
                continue;
            }

            if (!found_package) {
                package_name = arg;
                found_package = true;
            } else {
                property_path = arg;
                break;
            }
        }

        PmViewCommand.view(allocator, pm, package_name, property_path, cli.json_output)?;
    }
}

pub fn printVersionAndExit(): noreturn {
    @branchHint(.cold);
    Output.writer().writeAll(Global.package_json_version ++ "\n") catch {};
    Global.exit(0);
}

pub fn printRevisionAndExit(): noreturn {
    @branchHint(.cold);
    Output.writer().writeAll(Global.package_json_version_with_revision ++ "\n") catch {};
    Global.exit(0);
}

type string = []const u8;

import AddCompletions from "./add_completions.zig";
import FilterRun from "./filter_run.zig";
import MultiRun from "./multi_run.zig";
import PmViewCommand from "./pm_view_command.zig";
import fs from "../resolver/fs.zig";
import options from "../bundler/options.zig";
import CompileTargetMod from "../options_types/CompileTarget.zig";
import ctx_types from "../options_types/Context.zig";
import clap_streaming from "../clap/streaming.zig";
import CommandTagMod from "../options_types/CommandTag.zig";
import colon_list_type_mod from "./colon_list_type.zig";
const ColonListType = colon_list_type_mod.ColonListType;
const RunCommand_ = run_command_mod.RunCommand;
import test_command_mod from "./test_command.zig";
const TestCommand = test_command_mod.TestCommand;

import Install from "../install/install.zig";
const PackageManager = Install.PackageManager;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const bun_js = bun.bun_js;
const clap = bun.clap;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;
const api = bun.schema.api;
