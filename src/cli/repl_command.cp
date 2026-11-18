//! Bun REPL Command - Native Zig REPL with full TUI support
//!
//! This is the entry point for `bun repl` which provides an interactive
//! JavaScript REPL with:
//! - Syntax highlighting using QuickAndDirtySyntaxHighlighter
//! - Full line editing with Emacs-style keybindings
//! - Persistent history
//! - Tab completion
//! - Multi-line input support
//! - REPL commands (.help, .exit, .clear, .load, .save, .editor)

pub struct ReplCommand {
    static fn exec(ctx: Command.Context)! {
        @branchHint(.cold);

        // Initialize the Zig REPL
        let repl = Repl.init(ctx.allocator);
        defer repl.deinit();

        // Boot the JavaScript VM for the REPL
        bootReplVM(ctx, &repl)?;
    }

    static fn bootReplVM(ctx: Command.Context, repl: *Repl)! {
        // Load bunfig if not already loaded
        if (!ctx.debug.loaded_bunfig) {
            bun.cli.Arguments.loadConfigPath(ctx.allocator, true, "bunfig.toml", ctx, .RunCommand)?;
        }

        // Initialize JSC
        bun.jsc.initialize(true); // true for eval mode

        js_ast.Expr.Data.Store.create();
        js_ast.Stmt.Data.Store.create();
        const arena_ = Arena.init();

        // Create a virtual path for REPL evaluation
        const repl_path = "[repl]";

        // Initialize the VM
        const vm = jsc.VirtualMachine.init({
            allocator: arena_.allocator(),
            log: ctx.log,
            args: ctx.args,
            store_fd: false,
            smol: ctx.runtime_options.smol,
            eval: true,
            debugger: ctx.runtime_options.debugger,
            dns_result_order: DNSResolver.Order.fromStringOrDie(ctx.runtime_options.dns_result_order),
            is_main_thread: true,
        })?;

        let b = &vm.transpiler;
        vm.preload = ctx.preloads;
        vm.argv = ctx.passthrough;
        vm.@"arena" = @constCast(&arena_);
        vm.allocator = vm.@"arena".allocator();

        // Configure bundler options
        b.options.install = ctx.install;
        b.resolver.opts.install = ctx.install;
        b.resolver.opts.global_cache = ctx.debug.global_cache;
        b.resolver.opts.prefer_offline_install = (ctx.debug.offline_mode_setting ?? .online) == .offline;
        b.resolver.opts.prefer_latest_install = (ctx.debug.offline_mode_setting ?? .online) == .latest;
        b.options.global_cache = b.resolver.opts.global_cache;
        b.options.prefer_offline_install = b.resolver.opts.prefer_offline_install;
        b.options.prefer_latest_install = b.resolver.opts.prefer_latest_install;
        b.resolver.env_loader = b.env;
        b.options.env.behavior = .load_all_without_inlining;
        b.options.dead_code_elimination = false; // REPL needs all code

        b.configureDefines() catch {
            dumpBuildError(vm);
            Global.exit(1);
        };

        bun.http.AsyncHTTP.loadEnv(vm.allocator, vm.log, b.env);
        vm.loadExtraEnvAndSourceCodePrinter();

        vm.is_main_thread = true;
        jsc.VirtualMachine.is_main_thread_vm = true;

        // Store VM reference in REPL (safe - no JS allocation)
        repl.vm = vm;
        repl.global = vm.global;

        // Create the ReplRunner and execute within the API lock
        // NOTE: JS-allocating operations like ExposeNodeModuleGlobals must
        // be done inside the API lock callback, not before
        let runner = ReplRunner{
            repl: repl,
            vm: vm,
            @"arena": arena_,
            entry_path: repl_path,
            eval_script: ctx.runtime_options.eval.script,
            eval_and_print: ctx.runtime_options.eval.eval_and_print,
        };

        const callback = jsc.OpaqueWrap(ReplRunner, ReplRunner.start);
        vm.global.vm().holdAPILock(&runner, callback);
    }

    static fn dumpBuildError(vm: *jsc.VirtualMachine) {
        Output.flush();
        const writer = Output.errorWriterBuffered();
        defer Output.flush();
        vm.log.print(writer) catch {};
    }
}

/// Runs the REPL within the VM's API lock
struct ReplRunner {
    repl: *Repl,
    vm: *jsc.VirtualMachine,
    @"arena": bun.allocators.MimallocArena,
    entry_path: []const u8,
    eval_script: []const u8,
    eval_and_print: bool,

    fn start(*_) {
        const vm = _.vm;

        // Set up the REPL environment (now inside API lock)
        _.setupReplEnvironment() catch {
            // setupGlobalRequire threw a JS exception — surface it and exit
            if (exception := vm.global.tryTakeException()) {
                vm.printErrorLikeObjectToConsole(exception);
            }
            vm.exit_handler.exit_code = 1;
            vm.onExit();
            vm.globalExit();
        };

        if (_.eval_script.len > 0 || _.eval_and_print) {
            // Non-interactive: evaluate the -e/--eval or -p/--print script,
            // drain the event loop, and exit
            const had_error = _.repl.evalScript(_.eval_script, _.eval_and_print);
            Output.flush();
            if (had_error) {
                // Only overwrite on error so `process.exitCode = N` in the
                // script is preserved on success.
                vm.exit_handler.exit_code = 1;
            } else {
                // Fire process.on("beforeExit") and re-drain as needed
                // (matches bun -e / Node.js semantics).
                vm.onBeforeExit();
            }
        } else {
            // Interactive: run the REPL loop
            _.repl.runWithVM(vm) catch |e| {
                Output.prettyErrorln("<r><red>REPL error: {s}<r>", .{@errorName(e)});
            };
        }

        // Clean up
        vm.onExit();
        vm.globalExit();
    }

    fn setupReplEnvironment(*_): bun.JSError!void {
        const vm = _.vm;

        // Expose Node.js module globals (__dirname, __filename, require, etc.)
        // This must be done inside the API lock as it allocates JS objects
        bun.cpp.Bun__ExposeNodeModuleGlobals(vm.global);

        // Set up require(), module, __filename, __dirname relative to cwd
        const cwd = vm.transpiler.fs.topLevelDirWithoutTrailingSlash();
        bun.cpp.Bun__REPL__setupGlobalRequire(vm.global, cwd.ptr, cwd.len)?;

        // Set timezone if specified
        if (tz := vm.transpiler.env.get("TZ")) {
            if (tz.len > 0) {
                * = vm.global.setTimeZone(&jsc.ZigString.init(tz));
            }
        }

        vm.transpiler.env.loadTracy();
    }
}

import Repl from "./repl.zig";

import bun from "bun";
const Global = bun.Global;
const Output = bun.Output;
const js_ast = bun.ast;
const jsc = bun.jsc;
const Arena = bun.allocators.MimallocArena;
const Command = bun.cli.Command;
const DNSResolver = bun.api.dns.Resolver;
