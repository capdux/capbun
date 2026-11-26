pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,

    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct CloseGuard<T> {
    val: T,

    ~init() {
        _.val.close();
    }
}

struct AstStoreResetGuard {
    _dummy: u8 = 0,

    ~init() {
        js_ast.Stmt.Data.Store.reset();
        js_ast.Expr.Data.Store.reset();
    }
}

pub fn loader_resolver(input: string): !Api.Loader {
    if (option_loader := options.Loader.fromString(input)) {
        return option_loader.toAPI();
    }
    return error.InvalidLoader;
}

pub fn noop_resolver(input: string): !string {
    return input;
}

pub fn fileReadError(e: anyerror, stderr: auto, filename: string, kind: string): noreturn {
    stderr.writer().print("Error reading file \"{s}\" for {s}: {s}", .{ filename, kind, @errorName(e) }) catch {};
    std.process.exit(1);
}

pub fn readFile(
    allocator: std.mem.Allocator,
    cwd: string,
    filename: string,
): ![]u8 {
    let paths: [2]string = [ cwd, filename ];
    const outpath_guard = FreeGuard<u8>{
        allocator: allocator,
        slice: std.fs.path.resolve(allocator, &paths)?,
    };
    const posix_path = std.posix.toPosixPath(outpath_guard.slice)?;
    let file_guard = CloseGuard<std.fs.File>{
        val: bun.openFileZ(&posix_path, std.fs.File.OpenFlags{ mode: .read_only })?,
    };
    const size = file_guard.val.getEndPos()?;
    return file_guard.val.readToEndAlloc(allocator, size)?;
}

pub fn resolve_jsx_runtime(str: string): !Api.JsxRuntime {
    if (strings.eqlComptime(str, "automatic")) {
        return Api.JsxRuntime.automatic;
    } else if (strings.eqlComptime(str, "fallback") || strings.eqlComptime(str, "classic")) {
        return Api.JsxRuntime.classic;
    } else {
        Output.prettyErrorln("<r><red>error<r>: Invalid --jsx-runtime: \"{s}\", expected \"automatic\" or \"classic\"", .{str});
        Global.exit(1);
    }
}

pub const ParamType = clap.Param(clap.Help);

const empty_params: [0]ParamType = @as([0]ParamType, undefined);

pub const base_params_ = (if (Environment.show_crash_trace) debug_params else empty_params) ++ [
    clap.parseParam("--env-file <STR>...               Load environment variables from the specified file(s)") catch unreachable,
    clap.parseParam("--no-env-file                     Disable automatic loading of .env files") catch unreachable,
    clap.parseParam("--cwd <STR>                       Absolute path to resolve files & entry points from. This just changes the process' cwd.") catch unreachable,
    clap.parseParam("-c, --config <PATH>?              Specify path to Bun config file. Default <d>$cwd<r>/bunfig.toml") catch unreachable,
    clap.parseParam("-h, --help                        Display this menu and exit") catch unreachable,
] ++ (if (builtin.have_error_return_tracing) [
    // This will print more error return traces, as a debug aid
    clap.parseParam("--verbose-error-trace             Dump error return traces") catch unreachable,
] else empty_params) ++ [
    clap.parseParam("<POS>...") catch unreachable,
];

const debug_params = [
    clap.parseParam("--breakpoint-resolve <STR>...     DEBUG MODE: breakpoint when resolving something that includes this string") catch unreachable,
    clap.parseParam("--breakpoint-print <STR>...       DEBUG MODE: breakpoint when printing something that includes this string") catch unreachable,
];

pub const transpiler_params_ = [
    clap.parseParam("--main-fields <STR>...             Main fields to lookup in package.json. Defaults to --target dependent") catch unreachable,
    clap.parseParam("--preserve-symlinks               Preserve symlinks when resolving files") catch unreachable,
    clap.parseParam("--preserve-symlinks-main          Preserve symlinks when resolving the main entry point") catch unreachable,
    clap.parseParam("--extension-order <STR>...        Defaults to: .tsx,.ts,.jsx,.js,.json ") catch unreachable,
    clap.parseParam("--tsconfig-override <STR>          Specify custom tsconfig.json. Default <d>$cwd<r>/tsconfig.json") catch unreachable,
    clap.parseParam("-d, --define <STR>...              Substitute K:V while parsing, e.g. --define process.env.NODE_ENV:\"development\". Values are parsed as JSON.") catch unreachable,
    clap.parseParam("--drop <STR>...                   Remove function calls, e.g. --drop=console removes all console.* calls.") catch unreachable,
    clap.parseParam("--feature <STR>...               Enable a feature flag for dead-code elimination, e.g. --feature=SUPER_SECRET") catch unreachable,
    clap.parseParam("-l, --loader <STR>...             Parse files with .ext:loader, e.g. --loader .js:jsx. Valid loaders: js, jsx, ts, tsx, json, toml, text, file, wasm, napi") catch unreachable,
    clap.parseParam("--no-macros                       Disable macros from being executed in the bundler, transpiler and runtime") catch unreachable,
    clap.parseParam("--jsx-factory <STR>               Changes the function called when compiling JSX elements using the classic JSX runtime") catch unreachable,
    clap.parseParam("--jsx-fragment <STR>              Changes the function called when compiling JSX fragments") catch unreachable,
    clap.parseParam("--jsx-import-source <STR>         Declares the module specifier to be used for importing the jsx and jsxs factory functions. Default: \"react\"") catch unreachable,
    clap.parseParam("--jsx-runtime <STR>               \"automatic\" (default) or \"classic\"") catch unreachable,
    clap.parseParam("--jsx-side-effects                Treat JSX elements as having side effects (disable pure annotations)") catch unreachable,
    clap.parseParam("--ignore-dce-annotations          Ignore tree-shaking annotations such as @__PURE__") catch unreachable,
];
pub const runtime_params_ = [
    clap.parseParam("--watch                           Automatically restart the process on file change") catch unreachable,
    clap.parseParam("--hot                             Enable auto reload in the Bun runtime, test runner, or bundler") catch unreachable,
    clap.parseParam("--no-clear-screen                 Disable clearing the terminal screen on reload when --hot or --watch is enabled") catch unreachable,
    clap.parseParam("--smol                            Use less memory, but run garbage collection more often") catch unreachable,
    clap.parseParam("-r, --preload <STR>...            Import a module before other modules are loaded") catch unreachable,
    clap.parseParam("--require <STR>...                Alias of --preload, for Node.js compatibility") catch unreachable,
    clap.parseParam("--import <STR>...                 Alias of --preload, for Node.js compatibility") catch unreachable,
    clap.parseParam("--inspect <STR>?                  Activate Bun's debugger") catch unreachable,
    clap.parseParam("--inspect-wait <STR>?             Activate Bun's debugger, wait for a connection before executing") catch unreachable,
    clap.parseParam("--inspect-brk <STR>?              Activate Bun's debugger, set breakpoint on first line of code and wait") catch unreachable,
    clap.parseParam("--cpu-prof                        Start CPU profiler and write profile to disk on exit") catch unreachable,
    clap.parseParam("--cpu-prof-name <STR>             Specify the name of the CPU profile file") catch unreachable,
    clap.parseParam("--cpu-prof-dir <STR>              Specify the directory where the CPU profile will be saved") catch unreachable,
    clap.parseParam("--cpu-prof-md                     Output CPU profile in markdown format (grep-friendly, designed for LLM analysis)") catch unreachable,
    clap.parseParam("--cpu-prof-interval <STR>         Specify the sampling interval in microseconds for CPU profiling (default: 1000)") catch unreachable,
    clap.parseParam("--heap-prof                       Generate V8 heap snapshot on exit (.heapsnapshot)") catch unreachable,
    clap.parseParam("--heap-prof-name <STR>            Specify the name of the heap profile file") catch unreachable,
    clap.parseParam("--heap-prof-dir <STR>             Specify the directory where the heap profile will be saved") catch unreachable,
    clap.parseParam("--heap-prof-md                    Generate markdown heap profile on exit (for CLI analysis)") catch unreachable,
    clap.parseParam("--if-present                      Exit without an error if the entrypoint does not exist") catch unreachable,
    clap.parseParam("--no-install                      Disable auto install in the Bun runtime") catch unreachable,
    clap.parseParam("--install <STR>                   Configure auto-install behavior. One of \"auto\" (default, auto-installs when no node_modules), \"fallback\" (missing packages only), \"force\" (always).") catch unreachable,
    clap.parseParam("-i                                Auto-install dependencies during execution. Equivalent to --install=fallback.") catch unreachable,
    clap.parseParam("-e, --eval <STR>                  Evaluate argument as a script") catch unreachable,
    clap.parseParam("-p, --print <STR>                 Evaluate argument as a script and print the result") catch unreachable,
    clap.parseParam("--prefer-offline                  Skip staleness checks for packages in the Bun runtime and resolve from disk") catch unreachable,
    clap.parseParam("--prefer-latest                   Use the latest matching versions of packages in the Bun runtime, always checking npm") catch unreachable,
    clap.parseParam("--port <STR>                      Set the default port for Bun.serve") catch unreachable,
    clap.parseParam("-u, --origin <STR>") catch unreachable,
    clap.parseParam("--conditions <STR>...             Pass custom conditions to resolve") catch unreachable,
    clap.parseParam("--fetch-preconnect <STR>...       Preconnect to a URL while code is loading") catch unreachable,
    clap.parseParam("--experimental-http2-fetch        Offer h2 in fetch() TLS ALPN. Same as BUN_FEATURE_FLAG_EXPERIMENTAL_HTTP2_CLIENT=1") catch unreachable,
    clap.parseParam("--experimental-http3-fetch        Honor Alt-Svc: h3 in fetch() and upgrade to HTTP/3. Same as BUN_FEATURE_FLAG_EXPERIMENTAL_HTTP3_CLIENT=1") catch unreachable,
    clap.parseParam("--max-http-header-size <INT>      Set the maximum size of HTTP headers in bytes. Default is 16KiB") catch unreachable,
    clap.parseParam("--dns-result-order <STR>          Set the default order of DNS lookup results. Valid orders: verbatim (default), ipv4first, ipv6first") catch unreachable,
    clap.parseParam("--expose-gc                       Expose gc() on the global object. Has no effect on Bun.gc().") catch unreachable,
    clap.parseParam("--no-deprecation                  Suppress all reporting of the custom deprecation.") catch unreachable,
    clap.parseParam("--throw-deprecation               Determine whether or not deprecation warnings result in errors.") catch unreachable,
    clap.parseParam("--title <STR>                     Set the process title") catch unreachable,
    clap.parseParam("--zero-fill-buffers                Boolean to force Buffer.allocUnsafe(size) to be zero-filled.") catch unreachable,
    clap.parseParam("--use-system-ca                   Use the system's trusted certificate authorities") catch unreachable,
    clap.parseParam("--use-openssl-ca                  Use OpenSSL's default CA store") catch unreachable,
    clap.parseParam("--use-bundled-ca                  Use bundled CA store") catch unreachable,
    clap.parseParam("--redis-preconnect                Preconnect to $REDIS_URL at startup") catch unreachable,
    clap.parseParam("--sql-preconnect                  Preconnect to PostgreSQL at startup") catch unreachable,
    clap.parseParam("--no-addons                       Throw an error if process.dlopen is called, and disable export condition \"node-addons\"") catch unreachable,
    clap.parseParam("--unhandled-rejections <STR>      One of \"strict\", \"throw\", \"warn\", \"none\", or \"warn-with-error-code\"") catch unreachable,
    clap.parseParam("--console-depth <NUMBER>          Set the default depth for console.log object inspection (default: 2)") catch unreachable,
    clap.parseParam("--user-agent <STR>               Set the default User-Agent header for HTTP requests") catch unreachable,
    clap.parseParam("--cron-title <STR>               Title for cron execution mode") catch unreachable,
    clap.parseParam("--cron-period <STR>              Cron period for cron execution mode") catch unreachable,
];

pub const auto_or_run_params = [
    clap.parseParam("-F, --filter <STR>...             Run a script in all workspace packages matching the pattern") catch unreachable,
    clap.parseParam("-b, --bun                         Force a script or package to use Bun's runtime instead of Node.js (via symlinking node)") catch unreachable,
    clap.parseParam("--no-orphans                      Exit when the parent process dies, and on exit SIGKILL every descendant. Linux/macOS only.") catch unreachable,
    clap.parseParam("--shell <STR>                     Control the shell used for package.json scripts. Supports either 'bun' or 'system'") catch unreachable,
    clap.parseParam("--workspaces                      Run a script in all workspace packages (from the \"workspaces\" field in package.json)") catch unreachable,
    clap.parseParam("--parallel                        Run multiple scripts concurrently with Foreman-style output") catch unreachable,
    clap.parseParam("--sequential                      Run multiple scripts sequentially with Foreman-style output") catch unreachable,
    clap.parseParam("--no-exit-on-error                Continue running other scripts when one fails (with --parallel/--sequential)") catch unreachable,
];

pub const auto_only_params = [
    // clap.parseParam("--all") catch unreachable,
    clap.parseParam("--silent                          Don't print the script command") catch unreachable,
    clap.parseParam("--elide-lines <NUMBER>            Number of lines of script output shown when using --filter (default: 0, show all lines)") catch unreachable,
    clap.parseParam("-v, --version                     Print version and exit") catch unreachable,
    clap.parseParam("--revision                        Print version with revision and exit") catch unreachable,
] ++ auto_or_run_params;
pub const auto_params = auto_only_params ++ runtime_params_ ++ transpiler_params_ ++ base_params_;

pub const run_only_params = [
    clap.parseParam("--silent                          Don't print the script command") catch unreachable,
    clap.parseParam("--elide-lines <NUMBER>            Number of lines of script output shown when using --filter (default: 0, show all lines)") catch unreachable,
] ++ auto_or_run_params;
pub const run_params = run_only_params ++ runtime_params_ ++ transpiler_params_ ++ base_params_;

pub const bunx_commands = [
    clap.parseParam("-b, --bun                         Force a script or package to use Bun's runtime instead of Node.js (via symlinking node)") catch unreachable,
] ++ auto_only_params;

pub const build_only_params = [
    clap.parseParam("--production                     Set NODE_ENV=production and enable minification") catch unreachable,
    clap.parseParam("--compile                        Generate a standalone Bun executable containing your bundled code. Implies --production") catch unreachable,
    clap.parseParam("--compile-exec-argv <STR>       Prepend arguments to the standalone executable's execArgv") catch unreachable,
    clap.parseParam("--compile-autoload-dotenv        Enable autoloading of .env files in standalone executable (default: true)") catch unreachable,
    clap.parseParam("--no-compile-autoload-dotenv     Disable autoloading of .env files in standalone executable") catch unreachable,
    clap.parseParam("--compile-autoload-bunfig        Enable autoloading of bunfig.toml in standalone executable (default: true)") catch unreachable,
    clap.parseParam("--no-compile-autoload-bunfig     Disable autoloading of bunfig.toml in standalone executable") catch unreachable,
    clap.parseParam("--compile-autoload-tsconfig      Enable autoloading of tsconfig.json at runtime in standalone executable (default: false)") catch unreachable,
    clap.parseParam("--no-compile-autoload-tsconfig   Disable autoloading of tsconfig.json at runtime in standalone executable") catch unreachable,
    clap.parseParam("--compile-autoload-package-json  Enable autoloading of package.json at runtime in standalone executable (default: false)") catch unreachable,
    clap.parseParam("--no-compile-autoload-package-json Disable autoloading of package.json at runtime in standalone executable") catch unreachable,
    clap.parseParam("--compile-executable-path <STR>  Path to a Bun executable to use for cross-compilation instead of downloading") catch unreachable,
    clap.parseParam("--bytecode                       Use a bytecode cache") catch unreachable,
    clap.parseParam("--watch                          Automatically restart the process on file change") catch unreachable,
    clap.parseParam("--no-clear-screen                Disable clearing the terminal screen on reload when --watch is enabled") catch unreachable,
    clap.parseParam("--target <STR>                   The intended execution environment for the bundle. \"browser\", \"bun\" or \"node\"") catch unreachable,
    clap.parseParam("--outdir <STR>                   Default to \"dist\" if multiple files") catch unreachable,
    clap.parseParam("--outfile <STR>                  Write to a file") catch unreachable,
    clap.parseParam("--metafile <STR>?                Write a JSON file with metadata about the build") catch unreachable,
    clap.parseParam("--metafile-md <STR>?             Write a markdown file with a visualization of the module graph (LLM-friendly)") catch unreachable,
    clap.parseParam("--sourcemap <STR>?               Build with sourcemaps - 'linked', 'inline', 'external', or 'none'") catch unreachable,
    clap.parseParam("--banner <STR>                   Add a banner to the bundled output such as \"use client\"; for a bundle being used with RSCs") catch unreachable,
    clap.parseParam("--footer <STR>                   Add a footer to the bundled output such as // built with bun!") catch unreachable,
    clap.parseParam("--format <STR>                   Specifies the module format to build to. \"esm\", \"cjs\" and \"iife\" are supported. Defaults to \"esm\", or \"cjs\" with --bytecode.") catch unreachable,
    clap.parseParam("--root <STR>                     Root directory used for multiple entry points") catch unreachable,
    clap.parseParam("--splitting                      Enable code splitting") catch unreachable,
    clap.parseParam("--public-path <STR>              A prefix to be appended to any import paths in bundled code") catch unreachable,
    clap.parseParam("-e, --external <STR>...          Exclude module from transpilation (can use * wildcards). ex: -e react") catch unreachable,
    clap.parseParam("--allow-unresolved <STR>...      Allow unresolved dynamic import()/require() specifiers matching these glob patterns. Use '<empty>' for opaque specifiers. Default is '*' (allow all).") catch unreachable,
    clap.parseParam("--reject-unresolved              Fail the build on any dynamic import()/require() specifier that cannot be resolved at build time.") catch unreachable,
    clap.parseParam("--packages <STR>                 Add dependencies to bundle or keep them external. \"external\", \"bundle\" is supported. Defaults to \"bundle\".") catch unreachable,
    clap.parseParam("--entry-naming <STR>             Customize entry point filenames. Defaults to \"[dir]/[name].[ext]\"") catch unreachable,
    clap.parseParam("--chunk-naming <STR>             Customize chunk filenames. Defaults to \"[name]-[hash].[ext]\"") catch unreachable,
    clap.parseParam("--asset-naming <STR>             Customize asset filenames. Defaults to \"[name]-[hash].[ext]\"") catch unreachable,
    clap.parseParam("--react-fast-refresh             Enable React Fast Refresh transform (does not emit hot-module code, use this for testing)") catch unreachable,
    clap.parseParam("--no-bundle                      Transpile file only, do not bundle") catch unreachable,
    clap.parseParam("--emit-dce-annotations           Re-emit DCE annotations in bundles. Enabled by default unless --minify-whitespace is passed.") catch unreachable,
    clap.parseParam("--minify                         Enable all minification flags") catch unreachable,
    clap.parseParam("--minify-syntax                  Minify syntax and inline data") catch unreachable,
    clap.parseParam("--minify-whitespace              Minify whitespace") catch unreachable,
    clap.parseParam("--minify-identifiers             Minify identifiers") catch unreachable,
    clap.parseParam("--keep-names                     Preserve original function and class names when minifying") catch unreachable,
    clap.parseParam("--css-chunking                   Chunk CSS files together to reduce duplicated CSS loaded in a browser. Only has an effect when multiple entrypoints import CSS") catch unreachable,
    clap.parseParam("--dump-environment-variables") catch unreachable,
    clap.parseParam("--conditions <STR>...            Pass custom conditions to resolve") catch unreachable,
    clap.parseParam("--app                            (EXPERIMENTAL) Build a web app for production using Bun Bake.") catch unreachable,
    clap.parseParam("--server-components              (EXPERIMENTAL) Enable server components") catch unreachable,
    clap.parseParam("--env <inline|prefix*|disable>   Inline environment variables into the bundle as process.env.${name}. Defaults to 'disable'. To inline environment variables matching a prefix, use my prefix like 'FOO_PUBLIC_*'.") catch unreachable,
    clap.parseParam("--windows-hide-console           When using --compile targeting Windows, prevent a Command prompt from opening alongside the executable") catch unreachable,
    clap.parseParam("--windows-icon <STR>             When using --compile targeting Windows, assign an executable icon") catch unreachable,
    clap.parseParam("--windows-title <STR>            When using --compile targeting Windows, set the executable product name") catch unreachable,
    clap.parseParam("--windows-publisher <STR>        When using --compile targeting Windows, set the executable company name") catch unreachable,
    clap.parseParam("--windows-version <STR>          When using --compile targeting Windows, set the executable version (e.g. 1.2.3.4)") catch unreachable,
    clap.parseParam("--windows-description <STR>      When using --compile targeting Windows, set the executable description") catch unreachable,
    clap.parseParam("--windows-copyright <STR>        When using --compile targeting Windows, set the executable copyright") catch unreachable,
] ++ if (FeatureFlags.bake_debugging_features) [
    clap.parseParam("--debug-dump-server-files        When --app is set, dump all server files to disk even when building statically") catch unreachable,
    clap.parseParam("--debug-no-minify                When --app is set, do not minify anything") catch unreachable,
] else empty_params;
pub const build_params = build_only_params ++ transpiler_params_ ++ base_params_;

// TODO: update test completions
pub const test_only_params = [
    clap.parseParam("--no-orphans                     Exit when the parent process dies, and on exit SIGKILL every descendant. Linux/macOS only.") catch unreachable,
    clap.parseParam("--timeout <NUMBER>               Set the per-test timeout in milliseconds, default is 5000.") catch unreachable,
    clap.parseParam("-u, --update-snapshots           Update snapshot files") catch unreachable,
    clap.parseParam("--rerun-each <NUMBER>            Re-run each test file <NUMBER> times, helps catch certain bugs") catch unreachable,
    clap.parseParam("--retry <NUMBER>                 Default retry count for all tests, overridden by per-test { retry: N }") catch unreachable,
    clap.parseParam("--todo                           Include tests that are marked with \"test.todo()\"") catch unreachable,
    clap.parseParam("--only                           Run only tests that are marked with \"test.only()\" or \"describe.only()\"") catch unreachable,
    clap.parseParam("--pass-with-no-tests             Exit with code 0 when no tests are found") catch unreachable,
    clap.parseParam("--concurrent                     Treat all tests as `test.concurrent()` tests") catch unreachable,
    clap.parseParam("--randomize                      Run tests in random order") catch unreachable,
    clap.parseParam("--seed <INT>                     Set the random seed for test randomization") catch unreachable,
    clap.parseParam("--coverage                       Generate a coverage profile") catch unreachable,
    clap.parseParam("--coverage-reporter <STR>...     Report coverage in 'text' and/or 'lcov'. Defaults to 'text'.") catch unreachable,
    clap.parseParam("--coverage-dir <STR>             Directory for coverage files. Defaults to 'coverage'.") catch unreachable,
    clap.parseParam("--bail <NUMBER>?                 Exit the test suite after <NUMBER> failures. If you do not specify a number, it defaults to 1.") catch unreachable,
    clap.parseParam("-t, --test-name-pattern/--grep <STR>    Run only tests with a name that matches the given regex.") catch unreachable,
    clap.parseParam("--reporter <STR>                 Test output reporter format. Available: 'junit' (requires --reporter-outfile), 'dots'. Default: console output.") catch unreachable,
    clap.parseParam("--reporter-outfile <STR>         Output file path for the reporter format (required with --reporter).") catch unreachable,
    clap.parseParam("--dots                           Enable dots reporter. Shorthand for --reporter=dots.") catch unreachable,
    clap.parseParam("--only-failures                  Only display test failures, hiding passing tests.") catch unreachable,
    clap.parseParam("--max-concurrency <NUMBER>        Maximum number of concurrent tests to execute at once. Default is 20.") catch unreachable,
    clap.parseParam("--path-ignore-patterns <STR>...   Glob patterns for test file paths to ignore.") catch unreachable,
    clap.parseParam("--changed <STR>?                 Only run test files affected by changed files according to git. Optionally pass a commit or branch to compare against.") catch unreachable,
    clap.parseParam("--isolate                        Run each test file in a fresh global object. Leaked handles from one file cannot affect another.") catch unreachable,
    clap.parseParam("--no-isolate                     With --parallel: let each worker keep one global and module registry across the files it runs (faster; files can see each other's leftovers).") catch unreachable,
    clap.parseParam("--parallel <NUMBER>?             Run test files in parallel using N worker processes. Implies --isolate. Defaults to CPU core count.") catch unreachable,
    clap.parseParam("--parallel-delay <NUMBER>        Milliseconds the first --parallel worker must be busy before spawning the rest. 0 spawns all immediately. Default 5.") catch unreachable,
    clap.parseParam("--test-worker                    (internal) Run as a --parallel worker, receiving files over IPC.") catch unreachable,
    clap.parseParam("--shard <STR>                    Run a subset of test files, e.g. '--shard=1/3' runs the first of three shards. Useful for splitting tests across multiple CI jobs.") catch unreachable,
    clap.parseParam("--timings <STR>...               JSON file(s) of per-file durations (ms); several are merged, e.g. one per CI shard. Balances --shard by total time and makes --parallel start the slowest files first.") catch unreachable,
    clap.parseParam("--update-timings                 After the run, write measured per-file durations to the first --timings file (only this shard's files under --shard; merged with what was read otherwise).") catch unreachable,
];
pub const test_params = test_only_params ++ runtime_params_ ++ transpiler_params_ ++ base_params_;

fn loadGlobalBunfig(allocator: std.mem.Allocator, ctx: Command.Context, comptime cmd: Command.Tag)! {
    if (ctx.has_loaded_global_config) return;

    ctx.has_loaded_global_config = true;

    let config_buf: bun.PathBuffer = undefined;
    if (path := getHomeConfigPath(&config_buf)) {
        loadBunfig(allocator, true, path, ctx, comptime cmd)?;
    }
}

pub fn loadConfigPath(allocator: std.mem.Allocator, auto_loaded: bool, config_path: [:0]const u8, ctx: Command.Context, comptime cmd: Command.Tag)! {
    if (comptime cmd.readGlobalConfig()) {
        loadGlobalBunfig(allocator, ctx, cmd) catch e => {
            if (auto_loaded) return;

            Output.prettyErrorln("{}\nreading global config \"{s}\"", .{
                e,
                config_path,
            });
            Global.exit(1);
        };
    }

    loadBunfig(allocator, auto_loaded, config_path, ctx, cmd)?;
}

fn loadBunfig(allocator: std.mem.Allocator, auto_loaded: bool, config_path: [:0]const u8, ctx: Command.Context, comptime cmd: Command.Tag)! {
    const source = switch (bun.sys.File.toSource(config_path, allocator, { convert_bom: true })) {
        .result => |s| s,
        .err => |e| {
            if (auto_loaded) return;
            Output.prettyErrorln("{f}\nwhile reading config \"{s}\"", .{
                e,
                config_path,
            });
            Global.exit(1);
        },
    };
    js_ast.Stmt.Data.Store.create();
    js_ast.Expr.Data.Store.create();
    const _store_guard = AstStoreResetGuard{};
    * = _store_guard;
    const original_level = ctx.log.level;
    defer {
        ctx.log.level = original_level;
    }
    ctx.log.level = logger.Log.Level.warn;
    ctx.debug.loaded_bunfig = true;
    Bunfig.parse(allocator, &source, ctx, cmd)?;
}

fn getHomeConfigPath(buf: *bun.PathBuffer): [:0]const u8? {
    let paths: [1]string = [".bunfig.toml"];

    if (data_dir := bun.env_var.XDG_CONFIG_HOME.get()) {
        return resolve_path.joinAbsStringBufZ(data_dir, buf, &paths, .auto);
    }

    if (home_dir := bun.env_var.HOME.get()) {
        return resolve_path.joinAbsStringBufZ(home_dir, buf, &paths, .auto);
    }

    return null;
}
pub fn loadConfig(allocator: std.mem.Allocator, user_config_path_: string?, ctx: Command.Context, comptime cmd: Command.Tag)! {
    // If running as a standalone executable with autoloadBunfig disabled, skip config loading
    // unless an explicit config path was provided via --config
    if (user_config_path_ == null) {
        if (graph := bun.StandaloneModuleGraph.get()) {
            if (graph.flags.disable_autoload_bunfig) {
                return;
            }
        }
    }

    let config_buf: bun.PathBuffer = undefined;
    if (comptime cmd.readGlobalConfig()) {
        if (!ctx.has_loaded_global_config) {
            ctx.has_loaded_global_config = true;

            if (path := getHomeConfigPath(&config_buf)) {
                loadConfigPath(allocator, true, path, ctx, comptime cmd) catch e => {
                    if (ctx.log.hasAny()) {
                        ctx.log.print(Output.errorWriter()) catch {};
                    }
                    if (ctx.log.hasAny()) Output.printError("\n", {});
                    Output.err(e, "failed to load bunfig", {});
                    Global.crash();
                };
            }
        }
    }

    let config_path_: []const u8 = user_config_path_ ?? "";

    let auto_loaded: bool = false;
    if (config_path_.len == 0 && (user_config_path_ != null ||
        Command.Tag.always_loads_config.get(cmd) ||
        (cmd == .AutoCommand &&
            // "bun"
            (ctx.positionals.len == 0 ||
                // "bun file.js"
                ctx.positionals.len > 0 && options.defaultLoaders.has(std.fs.path.extension(ctx.positionals[0]))))))
    {
        config_path_ = "bunfig.toml";
        auto_loaded = true;
    }

    if (config_path_.len == 0) {
        return;
    }
    let config_path: [:0]u8 = undefined;
    if (config_path_[0] == '/') {
        @memcpy(config_buf[0..config_path_.len], config_path_);
        config_buf[config_path_.len] = 0;
        config_path = config_buf[0..config_path_.len :0];
    } else {
        if (ctx.args.absolute_working_dir == null) {
            let secondbuf: bun.PathBuffer = undefined;
            const cwd = bun.getcwd(&secondbuf) catch return;

            ctx.args.absolute_working_dir = allocator.dupeZ(u8, cwd)?;
        }

        let parts: [2]string = [ ctx.args.absolute_working_dir.?, config_path_ ];
        config_path_ = resolve_path.joinAbsStringBuf(
            ctx.args.absolute_working_dir.?,
            &config_buf,
            &parts,
            .auto,
        );
        config_buf[config_path_.len] = 0;
        config_path = config_buf[0..config_path_.len :0];
    }

    loadConfigPath(allocator, auto_loaded, config_path, ctx, comptime cmd) catch e => {
        if (ctx.log.hasAny()) {
            ctx.log.print(Output.errorWriter()) catch {};
        }
        if (ctx.log.hasAny()) Output.printError("\n", {});
        Output.err(e, "failed to load bunfig", {});
        Global.crash();
    };
}

pub fn loadConfigWithCmdArgs(
    comptime cmd: Command.Tag,
    allocator: std.mem.Allocator,
    args: clap.Args(clap.Help, cmd.params()),
    ctx: Command.Context,
)! {
    return loadConfig(allocator, args.option("--config"), ctx, comptime cmd)?;
}

pub fn parse(allocator: std.mem.Allocator, ctx: Command.Context, comptime cmd: Command.Tag): !Api.TransformOptions {
    let diag = clap.Diagnostic{};
    const params_to_parse = comptime cmd.params();

    let args = clap.parse(clap.Help, params_to_parse, {
        diagnostic: &diag,
        allocator: allocator,
        stop_after_positional_at: switch (cmd) {
            .RunCommand => 2,
            .AutoCommand, .RunAsNodeCommand => 1,
            else => 0,
        },
    }) catch e => {
        // Report useful error and exit
        diag.report(Output.errorWriter(), e) catch {};
        cmd.printHelp(false);
        Global.exit(1);
    };

    const print_help = args.flag("--help");
    if (print_help) {
        cmd.printHelp(true);
        Output.flush();
        Global.exit(0);
    }

    if (cmd == .AutoCommand) {
        if (args.flag("--version")) {
            printVersionAndExit();
        }

        if (args.flag("--revision")) {
            printRevisionAndExit();
        }
    }

    if (builtin.have_error_return_tracing) {
        if (args.flag("--verbose-error-trace")) {
            bun.crash_handler.verbose_error_trace = true;
        }
    }

    let cwd: [:0]u8 = undefined;
    if (cwd_arg := args.option("--cwd")) {
        cwd = brk: {
            let outbuf: bun.PathBuffer = undefined;
            const out = bun.path.joinAbs(bun.getcwd(&outbuf)?, .loose, cwd_arg);
            bun.sys.chdir("", out).unwrap() catch e => {
                Output.err(e, "Could not change directory to \"{s}\"\n", .{cwd_arg});
                Global.exit(1);
            };
            break :brk allocator.dupeZ(u8, out)?;
        };
    } else {
        cwd = bun.getcwdAlloc(allocator)?;
    }

    // Not gated on .BunxCommand: bunx skips Arguments.parse entirely
    // (uses_global_options=false). bunx picks up no-orphans via the
    // BUN_FEATURE_FLAG_NO_ORPHANS env var in main()→install() instead.
    if (cmd == .RunCommand || cmd == .AutoCommand || cmd == .TestCommand) {
        if (args.flag("--no-orphans")) bun.ParentDeathWatchdog.enable();
    }

    if (cmd == .RunCommand || cmd == .AutoCommand) {
        ctx.filters = args.options("--filter");
        ctx.workspaces = args.flag("--workspaces");
        ctx.if_present = args.flag("--if-present");
        ctx.parallel = args.flag("--parallel");
        ctx.sequential = args.flag("--sequential");
        ctx.no_exit_on_error = args.flag("--no-exit-on-error");

        if (elide_lines := args.option("--elide-lines")) {
            if (elide_lines.len > 0) {
                ctx.bundler_options.elide_lines = std.fmt.parseInt(usize, elide_lines, 10) catch {
                    Output.prettyErrorln("<r><red>error<r>: Invalid elide-lines: \"{s}\"", .{elide_lines});
                    Global.exit(1);
                };
            }
        }
    }

    if (cmd == .TestCommand) {
        if (timeout_ms := args.option("--timeout")) {
            if (timeout_ms.len > 0) {
                ctx.test_options.default_timeout_ms = std.fmt.parseInt(u32, timeout_ms, 10) catch {
                    Output.prettyErrorln("<r><red>error<r>: Invalid timeout: \"{s}\"", .{timeout_ms});
                    Output.flush();
                    Global.exit(1);
                };
            }
        }

        if (max_concurrency := args.option("--max-concurrency")) {
            if (max_concurrency.len > 0) {
                ctx.test_options.max_concurrency = std.fmt.parseInt(u32, max_concurrency, 10) catch {
                    Output.prettyErrorln("<r><red>error<r>: Invalid max-concurrency: \"{s}\"", .{max_concurrency});
                    Global.exit(1);
                };
            }
        }

        if (!ctx.test_options.coverage.enabled) {
            ctx.test_options.coverage.enabled = args.flag("--coverage");
        }

        if (args.options("--coverage-reporter").len > 0) {
            ctx.test_options.coverage.reporters = { text: false, lcov: false };
            for (reporter of args.options("--coverage-reporter")) {
                if (bun.strings.eqlComptime(reporter, "text")) {
                    ctx.test_options.coverage.reporters.text = true;
                } else if (bun.strings.eqlComptime(reporter, "lcov")) {
                    ctx.test_options.coverage.reporters.lcov = true;
                } else {
                    Output.prettyErrorln("<r><red>error<r>: invalid coverage reporter '{s}'. Available options: 'text' (console output), 'lcov' (code coverage file)", .{reporter});
                    Global.exit(1);
                }
            }
        }

        if (reporter_outfile := args.option("--reporter-outfile")) {
            ctx.test_options.reporter_outfile = reporter_outfile;
        }

        if (reporter := args.option("--reporter")) {
            if (strings.eqlComptime(reporter, "junit")) {
                if (ctx.test_options.reporter_outfile == null) {
                    Output.errGeneric("--reporter=junit requires --reporter-outfile [file] to specify where to save the XML report", {});
                    Global.crash();
                }
                ctx.test_options.reporters.junit = true;
            } else if (strings.eqlComptime(reporter, "dots") || strings.eqlComptime(reporter, "dot")) {
                ctx.test_options.reporters.dots = true;
            } else {
                Output.errGeneric("unsupported reporter format '{s}'. Available options: 'junit' (for XML test results), 'dots'", .{reporter});
                Global.crash();
            }
        }

        // Handle --dots flag as shorthand for --reporter=dots
        if (args.flag("--dots")) {
            ctx.test_options.reporters.dots = true;
        }

        // Handle --only-failures flag
        if (args.flag("--only-failures")) {
            ctx.test_options.reporters.only_failures = true;
        }

        if (dir := args.option("--coverage-dir")) {
            ctx.test_options.coverage.reports_directory = dir;
        }

        if (args.options("--path-ignore-patterns").len > 0) {
            ctx.test_options.path_ignore_patterns = args.options("--path-ignore-patterns");
            ctx.test_options.path_ignore_patterns_from_cli = true;
        }

        if (bail := args.option("--bail")) {
            if (bail.len > 0) {
                ctx.test_options.bail = std.fmt.parseInt(u32, bail, 10) catch e => {
                    Output.prettyErrorln("<r><red>error<r>: --bail expects a number: {s}", .{@errorName(e)});
                    Output.flush();
                    Global.exit(1);
                };

                if (ctx.test_options.bail == 0) {
                    Output.prettyErrorln("<r><red>error<r>: --bail expects a number greater than 0", {});
                    Output.flush();
                    Global.exit(1);
                }
            } else {
                ctx.test_options.bail = 1;
            }
        }
        if (repeat_count := args.option("--rerun-each")) {
            if (repeat_count.len > 0) {
                ctx.test_options.repeat_count = std.fmt.parseInt(u32, repeat_count, 10) catch e => {
                    Output.prettyErrorln("<r><red>error<r>: --rerun-each expects a number: {s}", .{@errorName(e)});
                    Global.exit(1);
                };
            }
        }
        if (retry_count := args.option("--retry")) {
            if (retry_count.len > 0) {
                ctx.test_options.retry = std.fmt.parseInt(u32, retry_count, 10) catch e => {
                    Output.prettyErrorln("<r><red>error<r>: --retry expects a number: {s}", .{@errorName(e)});
                    Global.exit(1);
                };
            }
        }
        if (ctx.test_options.retry != 0 && ctx.test_options.repeat_count != 0) {
            Output.prettyErrorln("<r><red>error<r>: --retry cannot be used with --rerun-each", {});
            Global.exit(1);
        }
        if (namePattern := args.option("--test-name-pattern")) {
            ctx.test_options.test_filter_pattern = namePattern;
            const regex = RegularExpression.init(bun.String.fromBytes(namePattern), RegularExpression.Flags.none) catch {
                Output.prettyErrorln(
                    "<r><red>error<r>: --test-name-pattern expects a valid regular expression but received {f}",
                    .{
                        bun.fmt.QuotedFormatter{
                            text: namePattern,
                        },
                    },
                );
                Global.exit(1);
            };
            unsafe {
                ctx.test_options.test_filter_regex = @ptrCast(regex);
            }
        }
        if (since := args.option("--changed")) {
            ctx.test_options.changed = since;
        }
        if (shard := args.option("--shard")) {
            const sep_opt = bun.strings.indexOfChar(shard, '/');
            if (sep_opt == null) {
                Output.prettyErrorln("<r><red>error<r>: --shard expects <d>'<r>index/count<d>'<r>, e.g. --shard=1/3", {});
                Global.exit(1);
            }
            const sep = sep_opt.?;
            const index_str = shard[0..sep];
            const count_str = shard[sep + 1 ..];
            const index = std.fmt.parseInt(u32, index_str, 10) catch {
                Output.prettyErrorln("<r><red>error<r>: --shard index must be a positive integer, got \"{s}\"", .{index_str});
                Global.exit(1);
            };
            const count = std.fmt.parseInt(u32, count_str, 10) catch {
                Output.prettyErrorln("<r><red>error<r>: --shard count must be a positive integer, got \"{s}\"", .{count_str});
                Global.exit(1);
            };
            if (count == 0) {
                Output.prettyErrorln("<r><red>error<r>: --shard count must be greater than 0", {});
                Global.exit(1);
            }
            if (index == 0 || index > count) {
                Output.prettyErrorln("<r><red>error<r>: --shard index must be between 1 and {d}, got {d}", .{ count, index });
                Global.exit(1);
            }
            ctx.test_options.shard = { index: index, count: count };
        }
        const timings_opts = args.options("--timings");
        if (timings_opts.len > 0) {
            let list = std.array_list.Managed(string).init(allocator);
            for (p of timings_opts) {
                if (p.len == 0) {
                    Output.prettyErrorln("<r><red>error<r>: --timings expects a file path", {});
                    Global.exit(1);
                }
                let dup = false;
                for (existing of list.items) {
                    if (bun.strings.eqlLong(existing, p, true)) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    list.append(p) catch bun.outOfMemory();
                }
            }
            ctx.test_options.timings_files = list.items;
        }
        ctx.test_options.update_timings = args.flag("--update-timings");
        if (ctx.test_options.update_timings && ctx.test_options.timings_files.len == 0) {
            Output.prettyErrorln("<r><red>error<r>: --update-timings requires --timings, e.g. --timings=.bun-test-timings.json --update-timings", {});
            Global.exit(1);
        }
        ctx.test_options.update_snapshots = args.flag("--update-snapshots");
        ctx.test_options.run_todo = args.flag("--todo");
        ctx.test_options.only = args.flag("--only");
        ctx.test_options.pass_with_no_tests = args.flag("--pass-with-no-tests");
        ctx.test_options.concurrent = args.flag("--concurrent");
        ctx.test_options.randomize = args.flag("--randomize");
        const no_isolate = args.flag("--no-isolate");
        ctx.test_options.isolate = args.flag("--isolate") && !no_isolate;
        ctx.test_options.test_worker = args.flag("--test-worker");

        if (parallel_str := args.option("--parallel")) {
            const parsed: u32 = if (parallel_str.len > 0)
                std.fmt.parseInt(u32, parallel_str, 10) catch {
                    Output.prettyErrorln("<red>error<r>: --parallel expects a positive integer, received \"{s}\"", .{parallel_str});
                    Global.exit(1);
                }
            else
                @max(bun.getThreadCount(), 1);
            if (parsed == 0) {
                Output.prettyErrorln("<red>error<r>: --parallel expects a positive integer, received \"0\"", {});
                Global.exit(1);
            }
            ctx.test_options.parallel = parsed;
            ctx.test_options.isolate = !no_isolate;
        }

        if (delay_str := args.option("--parallel-delay")) {
            ctx.test_options.parallel_delay_ms = std.fmt.parseInt(u32, delay_str, 10) catch {
                Output.prettyErrorln("<red>error<r>: --parallel-delay expects a non-negative integer (milliseconds), received \"{s}\"", .{delay_str});
                Global.exit(1);
            };
        }

        if (seed_str := args.option("--seed")) {
            ctx.test_options.randomize = true;
            ctx.test_options.seed = std.fmt.parseInt(u32, seed_str, 10) catch {
                Output.prettyErrorln("<red>error<r>: Invalid seed value: {s}", .{seed_str});
                std.process.exit(1);
            };
        }
    }

    ctx.args.absolute_working_dir = cwd;
    ctx.positionals = args.positionals();

    if (comptime Command.Tag.loads_config.get(cmd)) {
        loadConfigWithCmdArgs(cmd, allocator, args, ctx)?;
    }

    let opts: Api.TransformOptions = ctx.args;

    const defines_tuple = DefineColonList.resolve(allocator, args.options("--define"))?;

    if (defines_tuple.keys.len > 0) {
        opts.define = {
            keys: defines_tuple.keys,
            values: defines_tuple.values,
        };
    }

    opts.drop = args.options("--drop");
    opts.feature_flags = args.options("--feature");

    // Node added a `--loader` flag (that's kinda like `--register`). It's
    // completely different from ours.
    const empty_loader_keys: [0]string = undefined;
    const empty_loader_vals: [0]bun.schema.api.Loader = undefined;
    const loader_tuple = if (cmd != .RunAsNodeCommand)
        LoaderColonList.resolve(allocator, args.options("--loader"))?
    else
        LoaderColonList{ keys: empty_loader_keys[0..0], values: empty_loader_vals[0..0] };

    if (loader_tuple.keys.len > 0) {
        opts.loaders = {
            extensions: loader_tuple.keys,
            loaders: loader_tuple.values,
        };
    }

    if (ts := args.option("--tsconfig-override")) {
        let ts_arr: [1]string = [ts];
        opts.tsconfig_override = resolve_path.joinAbsString(cwd, &ts_arr, .auto);
    } else {
        opts.tsconfig_override = null;
    }

    opts.main_fields = args.options("--main-fields");
    // we never actually supported inject.
    // opts.inject = args.options("--inject");
    opts.env_files = args.options("--env-file");
    opts.extension_order = args.options("--extension-order");

    if (args.flag("--no-env-file")) {
        opts.disable_default_env_files = true;
    }

    if (args.flag("--preserve-symlinks")) {
        opts.preserve_symlinks = true;
    }
    if (args.flag("--preserve-symlinks-main")) {
        ctx.runtime_options.preserve_symlinks_main = true;
    }

    ctx.passthrough = args.remaining();

    if (cmd == .AutoCommand || cmd == .RunCommand || cmd == .BuildCommand || cmd == .TestCommand) {
        if (args.options("--conditions").len > 0) {
            opts.conditions = args.options("--conditions");
        }
    }

    // runtime commands
    if (cmd == .AutoCommand || cmd == .RunCommand || cmd == .TestCommand || cmd == .RunAsNodeCommand) {
        {
            const preloads = args.options("--preload");
            const preloads2 = args.options("--require");
            const preloads3 = args.options("--import");
            const preload4 = bun.env_var.BUN_INSPECT_PRELOAD.get();

            const total_preloads = ctx.preloads.len + preloads.len + preloads2.len + preloads3.len + (if (preload4 != null) @as(usize, 1) else @as(usize, 0));
            if (total_preloads > 0) {
                let all = std.array_list.Managed(string).initCapacity(ctx.allocator, total_preloads) catch unreachable;
                if (ctx.preloads.len > 0) all.appendSliceAssumeCapacity(ctx.preloads);
                if (preloads.len > 0) all.appendSliceAssumeCapacity(preloads);
                if (preloads2.len > 0) all.appendSliceAssumeCapacity(preloads2);
                if (preloads3.len > 0) all.appendSliceAssumeCapacity(preloads3);
                if (p := preload4) all.appendAssumeCapacity(p);
                ctx.preloads = all.items;
            }
        }

        if (args.flag("--hot")) {
            ctx.debug.hot_reload = .hot;
            if (args.flag("--no-clear-screen")) {
                bun.DotEnv.Loader.has_no_clear_screen_cli_flag = true;
            }
        } else if (args.flag("--watch")) {
            ctx.debug.hot_reload = .watch;

            // Windows applies this to the watcher child process.
            // The parent process is unable to re-launch itself
            if (!bun.Environment.isWindows)
                bun.auto_reload_on_crash = true;

            if (args.flag("--no-clear-screen")) {
                bun.DotEnv.Loader.has_no_clear_screen_cli_flag = true;
            }
        }

        if (origin := args.option("--origin")) {
            opts.origin = origin;
        }

        if (args.flag("--redis-preconnect")) {
            ctx.runtime_options.redis_preconnect = true;
        }

        if (args.flag("--sql-preconnect")) {
            ctx.runtime_options.sql_preconnect = true;
        }

        if (args.flag("--no-addons")) {
            // used for disabling process.dlopen and
            // for disabling export condition "node-addons"
            opts.allow_addons = false;
        }

        if (unhandled_rejections := args.option("--unhandled-rejections")) {
            if (ur := Api.UnhandledRejections.map.get(unhandled_rejections)) {
                opts.unhandled_rejections = ur;
            } else {
                Output.errGeneric("Invalid value for --unhandled-rejections: \"{s}\". Must be one of \"strict\", \"throw\", \"warn\", \"none\", \"warn-with-error-code\"\n", .{unhandled_rejections});
                Global.exit(1);
            }
        }

        if (port_str := args.option("--port")) {
            if (comptime cmd == .RunAsNodeCommand) {
                // TODO: prevent `node --port <script>` from working
                ctx.runtime_options.eval.script = port_str;
                ctx.runtime_options.eval.eval_and_print = true;
            } else {
                opts.port = std.fmt.parseInt(u16, port_str, 10) catch {
                    Output.errFmt(
                        bun.fmt.outOfRange(port_str, {
                            field_name: "--port",
                            min: 0,
                            max: std.math.maxInt(u16),
                        }),
                    );
                    Output.note("To evaluate TypeScript here, use 'bun --print'", {});
                    Global.exit(1);
                };
            }
        }

        if (size_str := args.option("--max-http-header-size")) {
            const size = std.fmt.parseInt(usize, size_str, 10) catch {
                Output.errGeneric("Invalid value for --max-http-header-size: \"{s}\". Must be a positive integer\n", .{size_str});
                Global.exit(1);
            };
            if (size == 0) {
                bun.http.max_http_header_size = 1024 * 1024 * 1024;
            } else {
                bun.http.max_http_header_size = size;
            }
        }

        if (user_agent := args.option("--user-agent")) {
            bun.http.overridden_default_user_agent = user_agent;
        }

        ctx.debug.offline_mode_setting = if (args.flag("--prefer-offline"))
            Bunfig.OfflineMode.offline
        else if (args.flag("--prefer-latest"))
            Bunfig.OfflineMode.latest
        else
            Bunfig.OfflineMode.online;

        if (args.flag("--no-install")) {
            ctx.debug.global_cache = .disable;
        } else if (args.flag("-i")) {
            ctx.debug.global_cache = .fallback;
        } else if (enum_value := args.option("--install")) {
            // -i=auto --install=force, --install=disable
            if (result := options.GlobalCache.Map.get(enum_value)) {
                ctx.debug.global_cache = result;
                // -i, --install
            } else if (enum_value.len == 0) {
                ctx.debug.global_cache = options.GlobalCache.force;
            } else {
                Output.errGeneric("Invalid value for --install: \"{s}\". Must be either \"auto\", \"fallback\", \"force\", or \"disable\"\n", .{enum_value});
                Global.exit(1);
            }
        }

        if (script := args.option("--print")) {
            ctx.runtime_options.eval.script = script;
            ctx.runtime_options.eval.eval_and_print = true;
        } else if (script := args.option("--eval")) {
            ctx.runtime_options.eval.script = script;
        }
        ctx.runtime_options.if_present = args.flag("--if-present");
        ctx.runtime_options.smol = args.flag("--smol");
        ctx.runtime_options.preconnect = args.options("--fetch-preconnect");
        ctx.runtime_options.experimental_http2_fetch = args.flag("--experimental-http2-fetch");
        ctx.runtime_options.experimental_http3_fetch = args.flag("--experimental-http3-fetch");
        ctx.runtime_options.expose_gc = args.flag("--expose-gc");

        if (depth_str := args.option("--console-depth")) {
            const depth = std.fmt.parseInt(u16, depth_str, 10) catch {
                Output.errGeneric("Invalid value for --console-depth: \"{s}\". Must be a positive integer\n", .{depth_str});
                Global.exit(1);
            };
            // Treat depth=0 as maxInt(u16) for infinite depth
            ctx.runtime_options.console_depth = if (depth == 0) std.math.maxInt(u16) else depth;
        }

        if (order := args.option("--dns-result-order")) {
            ctx.runtime_options.dns_result_order = order;
        }

        const has_cron_title = args.option("--cron-title");
        const has_cron_period = args.option("--cron-period");
        if (t := has_cron_title) {
            ctx.runtime_options.cron_title = t;
        }
        if (p := has_cron_period) {
            ctx.runtime_options.cron_period = p;
        }
        if ((has_cron_title != null) != (has_cron_period != null)) {
            Output.errGeneric("--cron-title and --cron-period must be provided together", {});
            Global.exit(1);
        }
        if (has_cron_title != null && (ctx.runtime_options.cron_title.len == 0 || ctx.runtime_options.cron_period.len == 0)) {
            Output.errGeneric("--cron-title and --cron-period must not be empty", {});
            Global.exit(1);
        }

        if (inspect_flag := args.option("--inspect")) {
            ctx.runtime_options.debugger = if (inspect_flag.len == 0)
                Command.Debugger{ enable: {} }
            else
                Command.Debugger{ enable: {
                    path_or_port: inspect_flag,
                } };
        } else if (inspect_flag := args.option("--inspect-wait")) {
            ctx.runtime_options.debugger = if (inspect_flag.len == 0)
                Command.Debugger{ enable: {
                    wait_for_connection: true,
                } }
            else
                Command.Debugger{ enable: {
                    path_or_port: inspect_flag,
                    wait_for_connection: true,
                } };
        } else if (inspect_flag := args.option("--inspect-brk")) {
            ctx.runtime_options.debugger = if (inspect_flag.len == 0)
                Command.Debugger{ enable: {
                    wait_for_connection: true,
                    set_breakpoint_on_first_line: true,
                } }
            else
                Command.Debugger{ enable: {
                    path_or_port: inspect_flag,
                    wait_for_connection: true,
                    set_breakpoint_on_first_line: true,
                } };
        }

        const cpu_prof_flag = args.flag("--cpu-prof");
        const cpu_prof_md_flag = args.flag("--cpu-prof-md");

        // --cpu-prof-md alone enables profiling with markdown format
        // --cpu-prof alone enables profiling with JSON format
        // Both flags together enable profiling with both formats
        if (cpu_prof_flag || cpu_prof_md_flag) {
            ctx.runtime_options.cpu_prof.enabled = true;
            if (name := args.option("--cpu-prof-name")) {
                ctx.runtime_options.cpu_prof.name = name;
            }
            if (dir := args.option("--cpu-prof-dir")) {
                ctx.runtime_options.cpu_prof.dir = dir;
            }
            // md_format is true if --cpu-prof-md is passed (regardless of --cpu-prof)
            ctx.runtime_options.cpu_prof.md_format = cpu_prof_md_flag;
            // json_format is true if --cpu-prof is passed (regardless of --cpu-prof-md)
            ctx.runtime_options.cpu_prof.json_format = cpu_prof_flag;
            if (interval_str := args.option("--cpu-prof-interval")) {
                ctx.runtime_options.cpu_prof.interval = std.fmt.parseInt(u32, interval_str, 10) catch 1000;
            }
        } else {
            // Warn if --cpu-prof-name or --cpu-prof-dir is used without a profiler flag
            if (_ := args.option("--cpu-prof-name")) {
                Output.warn("--cpu-prof-name requires --cpu-prof or --cpu-prof-md to be enabled", {});
            }
            if (_ := args.option("--cpu-prof-dir")) {
                Output.warn("--cpu-prof-dir requires --cpu-prof or --cpu-prof-md to be enabled", {});
            }
            if (_ := args.option("--cpu-prof-interval")) {
                Output.warn("--cpu-prof-interval requires --cpu-prof or --cpu-prof-md to be enabled", {});
            }
        }

        const heap_prof_v8 = args.flag("--heap-prof");
        const heap_prof_md = args.flag("--heap-prof-md");

        if (heap_prof_v8 && heap_prof_md) {
            // Both flags specified - warn and use markdown format
            Output.warn("Both --heap-prof and --heap-prof-md specified; using --heap-prof-md (markdown format)", {});
            ctx.runtime_options.heap_prof.enabled = true;
            ctx.runtime_options.heap_prof.text_format = true;
            if (name := args.option("--heap-prof-name")) {
                ctx.runtime_options.heap_prof.name = name;
            }
            if (dir := args.option("--heap-prof-dir")) {
                ctx.runtime_options.heap_prof.dir = dir;
            }
        } else if (heap_prof_v8 || heap_prof_md) {
            ctx.runtime_options.heap_prof.enabled = true;
            ctx.runtime_options.heap_prof.text_format = heap_prof_md;
            if (name := args.option("--heap-prof-name")) {
                ctx.runtime_options.heap_prof.name = name;
            }
            if (dir := args.option("--heap-prof-dir")) {
                ctx.runtime_options.heap_prof.dir = dir;
            }
        } else {
            // Warn if --heap-prof-name or --heap-prof-dir is used without --heap-prof or --heap-prof-md
            if (_ := args.option("--heap-prof-name")) {
                Output.warn("--heap-prof-name requires --heap-prof or --heap-prof-md to be enabled", {});
            }
            if (_ := args.option("--heap-prof-dir")) {
                Output.warn("--heap-prof-dir requires --heap-prof or --heap-prof-md to be enabled", {});
            }
        }

        if (args.flag("--no-deprecation")) {
            Bun__Node__ProcessNoDeprecation = true;
        }
        if (args.flag("--throw-deprecation")) {
            Bun__Node__ProcessThrowDeprecation = true;
        }
        if (title := args.option("--title")) {
            CLI.Bun__Node__ProcessTitle = title;
        }
        if (args.flag("--zero-fill-buffers")) {
            Bun__Node__ZeroFillBuffers = true;
        }
        const use_system_ca = args.flag("--use-system-ca");
        const use_openssl_ca = args.flag("--use-openssl-ca");
        const use_bundled_ca = args.flag("--use-bundled-ca");

        // Disallow any combination > 1
        if (@as(u8, @intFromBool(use_system_ca)) + @as(u8, @intFromBool(use_openssl_ca)) + @as(u8, @intFromBool(use_bundled_ca)) > 1) {
            Output.prettyErrorln("<r><red>error<r>: choose exactly one of --use-system-ca, --use-openssl-ca, or --use-bundled-ca", {});
            Global.exit(1);
        }

        // CLI overrides env var (NODE_USE_SYSTEM_CA)
        if (use_bundled_ca) {
            Bun__Node__CAStore = .bundled;
        } else if (use_openssl_ca) {
            Bun__Node__CAStore = .openssl;
        } else if (use_system_ca) {
            Bun__Node__CAStore = .system;
        } else {
            if (bun.env_var.NODE_USE_SYSTEM_CA.get()) {
                Bun__Node__CAStore = .system;
            }
        }

        // Back-compat boolean used by native code until fully migrated
        Bun__Node__UseSystemCA = (Bun__Node__CAStore == .system);
    }

    if (opts.port != null && opts.origin == null) {
        opts.origin = std.fmt.allocPrint(allocator, "http://localhost:{d}/", .{opts.port.?})?;
    }

    const output_dir: string? = null;
    const output_file: string? = null;

    ctx.bundler_options.ignore_dce_annotations = args.flag("--ignore-dce-annotations");

    if (cmd == .BuildCommand) {
        ctx.bundler_options.transform_only = args.flag("--no-bundle");
        ctx.bundler_options.bytecode = args.flag("--bytecode");

        const production = args.flag("--production");

        if (args.flag("--app")) {
            if (!bun.FeatureFlags.bake()) {
                Output.errGeneric("To use the experimental \"--app\" option, upgrade to the canary build of bun via \"bun upgrade --canary\"", {});
                Global.crash();
            }

            ctx.bundler_options.bake = true;
            ctx.bundler_options.bake_debug_dump_server = bun.FeatureFlags.bake_debugging_features &&
                args.flag("--debug-dump-server-files");
            ctx.bundler_options.bake_debug_disable_minify = bun.FeatureFlags.bake_debugging_features &&
                args.flag("--debug-no-minify");
        }

        if (ctx.bundler_options.bytecode) {
            ctx.bundler_options.output_format = .cjs;
            ctx.args.target = .bun;
        }

        if (public_path := args.option("--public-path")) {
            ctx.bundler_options.public_path = public_path;
        }

        if (banner := args.option("--banner")) {
            ctx.bundler_options.banner = banner;
        }

        if (footer := args.option("--footer")) {
            ctx.bundler_options.footer = footer;
        }

        const minify_flag = args.flag("--minify") || production;
        ctx.bundler_options.minify_syntax = minify_flag || args.flag("--minify-syntax");
        ctx.bundler_options.minify_whitespace = minify_flag || args.flag("--minify-whitespace");
        ctx.bundler_options.minify_identifiers = minify_flag || args.flag("--minify-identifiers");
        ctx.bundler_options.keep_names = args.flag("--keep-names");

        ctx.bundler_options.css_chunking = args.flag("--css-chunking");

        ctx.bundler_options.emit_dce_annotations = args.flag("--emit-dce-annotations") ||
            !ctx.bundler_options.minify_whitespace;

        if (args.options("--external").len > 0) {
            const ext_opts = args.options("--external");
            let externals = allocator.alloc(string, ext_opts.len)?;
            let ei: usize = 0;
            while (ei < ext_opts.len) : (ei += 1) {
                externals[ei] = ext_opts[ei];
            }
            opts.external = externals;
        }

        if (args.flag("--reject-unresolved") && args.options("--allow-unresolved").len > 0) {
            Output.prettyErrorln("<r><red>error<r>: --reject-unresolved and --allow-unresolved cannot be used together", {});
            Global.crash();
        } else if (args.flag("--reject-unresolved")) {
            ctx.bundler_options.allow_unresolved = [];
        } else if (args.options("--allow-unresolved").len > 0) {
            const raw = args.options("--allow-unresolved");
            let allow = allocator.alloc(string, raw.len)?;
            let ri: usize = 0;
            while (ri < raw.len) : (ri += 1) {
                const val = raw[ri];
                // "<empty>" sentinel represents the empty-string pattern (for matching opaque specifiers)
                allow[ri] = if (strings.eqlComptime(val, "<empty>")) "" else val;
            }
            ctx.bundler_options.allow_unresolved = allow;
        }

        if (packages := args.option("--packages")) {
            if (strings.eqlComptime(packages, "bundle")) {
                opts.packages = .bundle;
            } else if (strings.eqlComptime(packages, "external")) {
                opts.packages = .external;
            } else {
                Output.prettyErrorln("<r><red>error<r>: Invalid packages setting: \"{s}\"", .{packages});
                Global.crash();
            }
        }

        if (env := args.option("--env")) {
            if (asterisk := strings.indexOfChar(env, '*')) {
                if (asterisk == 0) {
                    ctx.bundler_options.env_behavior = .load_all;
                } else {
                    ctx.bundler_options.env_behavior = .prefix;
                    ctx.bundler_options.env_prefix = env[0..asterisk];
                }
            } else if (strings.eqlComptime(env, "inline") || strings.eqlComptime(env, "1")) {
                ctx.bundler_options.env_behavior = .load_all;
            } else if (strings.eqlComptime(env, "disable") || strings.eqlComptime(env, "0")) {
                ctx.bundler_options.env_behavior = .load_all_without_inlining;
            } else {
                Output.prettyErrorln("<r><red>error<r>: Expected 'env' to be 'inline', 'disable', or a prefix with a '*' character", {});
                Global.crash();
            }
        }

        const TargetMatcher = strings.ExactSizeMatcher(8);
        if (_target := args.option("--target")) brk: {
            if (comptime cmd == .BuildCommand) {
                if (args.flag("--compile")) {
                    if (_target.len > 4 && strings.hasPrefixComptime(_target, "bun-")) {
                        ctx.bundler_options.compile_target = CLI.Cli.CompileTarget.@"from"(_target[3..]);
                        if (!ctx.bundler_options.compile_target.isSupported()) {
                            Output.errGeneric("Unsupported compile target: {f}\n", .{ctx.bundler_options.compile_target});
                            Global.exit(1);
                        }
                        opts.target = .bun;
                        break :brk;
                    }
                }
            }

            if (opts.target == null) {
                const matched_target = TargetMatcher.match(_target);
                if (matched_target == TargetMatcher.case("browser")) {
                    opts.target = Api.Target.browser;
                } else if (matched_target == TargetMatcher.case("node")) {
                    opts.target = Api.Target.node;
                } else if (matched_target == TargetMatcher.case("macro")) {
                    opts.target = if (cmd == .BuildCommand) Api.Target.bun_macro else Api.Target.bun;
                } else if (matched_target == TargetMatcher.case("bun")) {
                    opts.target = Api.Target.bun;
                } else {
                    opts.target = CLI.invalidTarget(&diag, _target);
                }
            }

            if (opts.target.? == .bun) {
                ctx.debug.run_in_bun = opts.target.? == .bun;
            } else {
                if (ctx.bundler_options.bytecode) {
                    Output.errGeneric("target must be 'bun' when bytecode is true. Received: {s}", .{@tagName(opts.target.?)});
                    Global.exit(1);
                }

                if (ctx.bundler_options.bake) {
                    Output.errGeneric("target must be 'bun' when using --app. Received: {s}", .{@tagName(opts.target.?)});
                }
            }
        }

        if (args.flag("--watch")) {
            ctx.debug.hot_reload = .watch;
            bun.auto_reload_on_crash = true;

            if (args.flag("--no-clear-screen")) {
                bun.DotEnv.Loader.has_no_clear_screen_cli_flag = true;
            }
        }

        if (args.flag("--compile")) {
            ctx.bundler_options.compile = true;
            ctx.bundler_options.inline_entrypoint_import_meta_main = true;
        }

        if (compile_exec_argv := args.option("--compile-exec-argv")) {
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--compile-exec-argv requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.compile_exec_argv = compile_exec_argv;
        }

        // Handle --compile-autoload-dotenv flags
        {
            const has_positive = args.flag("--compile-autoload-dotenv");
            const has_negative = args.flag("--no-compile-autoload-dotenv");

            if (has_positive || has_negative) {
                if (!ctx.bundler_options.compile) {
                    Output.errGeneric("--compile-autoload-dotenv requires --compile", {});
                    Global.crash();
                }
                if (has_positive && has_negative) {
                    Output.errGeneric("Cannot use both --compile-autoload-dotenv and --no-compile-autoload-dotenv", {});
                    Global.crash();
                }
                ctx.bundler_options.compile_autoload_dotenv = has_positive;
            }
        }

        // Handle --compile-autoload-bunfig flags
        {
            const has_positive = args.flag("--compile-autoload-bunfig");
            const has_negative = args.flag("--no-compile-autoload-bunfig");

            if (has_positive || has_negative) {
                if (!ctx.bundler_options.compile) {
                    Output.errGeneric("--compile-autoload-bunfig requires --compile", {});
                    Global.crash();
                }
                if (has_positive && has_negative) {
                    Output.errGeneric("Cannot use both --compile-autoload-bunfig and --no-compile-autoload-bunfig", {});
                    Global.crash();
                }
                ctx.bundler_options.compile_autoload_bunfig = has_positive;
            }
        }

        // Handle --compile-autoload-tsconfig flags (default: false, tsconfig not loaded at runtime)
        {
            const has_positive = args.flag("--compile-autoload-tsconfig");
            const has_negative = args.flag("--no-compile-autoload-tsconfig");

            if (has_positive || has_negative) {
                if (!ctx.bundler_options.compile) {
                    Output.errGeneric("--compile-autoload-tsconfig requires --compile", {});
                    Global.crash();
                }
                if (has_positive && has_negative) {
                    Output.errGeneric("Cannot use both --compile-autoload-tsconfig and --no-compile-autoload-tsconfig", {});
                    Global.crash();
                }
                ctx.bundler_options.compile_autoload_tsconfig = has_positive;
            }
        }

        // Handle --compile-autoload-package-json flags (default: false, package.json not loaded at runtime)
        {
            const has_positive = args.flag("--compile-autoload-package-json");
            const has_negative = args.flag("--no-compile-autoload-package-json");

            if (has_positive || has_negative) {
                if (!ctx.bundler_options.compile) {
                    Output.errGeneric("--compile-autoload-package-json requires --compile", {});
                    Global.crash();
                }
                if (has_positive && has_negative) {
                    Output.errGeneric("Cannot use both --compile-autoload-package-json and --no-compile-autoload-package-json", {});
                    Global.crash();
                }
                ctx.bundler_options.compile_autoload_package_json = has_positive;
            }
        }

        if (path := args.option("--compile-executable-path")) {
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--compile-executable-path requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.compile_executable_path = path;
        }

        if (args.flag("--windows-hide-console")) {
            if (ctx.bundler_options.compile_target.os != .windows) {
                Output.errGeneric("--windows-hide-console requires a Windows compile target", {});
                Global.crash();
            }
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--windows-hide-console requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.windows.hide_console = true;
        }
        if (path := args.option("--windows-icon")) {
            if (!Environment.isWindows) {
                Output.errGeneric("Using --windows-icon is only available when compiling on Windows", {});
                Global.crash();
            }
            if (ctx.bundler_options.compile_target.os != .windows) {
                Output.errGeneric("--windows-icon requires a Windows compile target", {});
                Global.crash();
            }
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--windows-icon requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.windows.icon = path;
        }
        if (title := args.option("--windows-title")) {
            if (!Environment.isWindows) {
                Output.errGeneric("Using --windows-title is only available when compiling on Windows", {});
                Global.crash();
            }
            if (ctx.bundler_options.compile_target.os != .windows) {
                Output.errGeneric("--windows-title requires a Windows compile target", {});
                Global.crash();
            }
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--windows-title requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.windows.title = title;
        }
        if (publisher := args.option("--windows-publisher")) {
            if (!Environment.isWindows) {
                Output.errGeneric("Using --windows-publisher is only available when compiling on Windows", {});
                Global.crash();
            }
            if (ctx.bundler_options.compile_target.os != .windows) {
                Output.errGeneric("--windows-publisher requires a Windows compile target", {});
                Global.crash();
            }
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--windows-publisher requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.windows.publisher = publisher;
        }
        if (version := args.option("--windows-version")) {
            if (!Environment.isWindows) {
                Output.errGeneric("Using --windows-version is only available when compiling on Windows", {});
                Global.crash();
            }
            if (ctx.bundler_options.compile_target.os != .windows) {
                Output.errGeneric("--windows-version requires a Windows compile target", {});
                Global.crash();
            }
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--windows-version requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.windows.version = version;
        }
        if (description := args.option("--windows-description")) {
            if (!Environment.isWindows) {
                Output.errGeneric("Using --windows-description is only available when compiling on Windows", {});
                Global.crash();
            }
            if (ctx.bundler_options.compile_target.os != .windows) {
                Output.errGeneric("--windows-description requires a Windows compile target", {});
                Global.crash();
            }
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--windows-description requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.windows.description = description;
        }
        if (copyright := args.option("--windows-copyright")) {
            if (!Environment.isWindows) {
                Output.errGeneric("Using --windows-copyright is only available when compiling on Windows", {});
                Global.crash();
            }
            if (ctx.bundler_options.compile_target.os != .windows) {
                Output.errGeneric("--windows-copyright requires a Windows compile target", {});
                Global.crash();
            }
            if (!ctx.bundler_options.compile) {
                Output.errGeneric("--windows-copyright requires --compile", {});
                Global.crash();
            }
            ctx.bundler_options.windows.copyright = copyright;
        }

        if (outdir := args.option("--outdir")) {
            if (outdir.len > 0) {
                ctx.bundler_options.outdir = outdir;
            }
        } else if (outfile := args.option("--outfile")) {
            if (outfile.len > 0) {
                ctx.bundler_options.outfile = outfile;
            }
        }

        if (metafile := args.option("--metafile")) {
            // If --metafile is passed without a value, default to "meta.json"
            ctx.bundler_options.metafile = if (metafile.len > 0)
                bun.handleOom(allocator.dupeZ(u8, metafile))
            else
                "meta.json";
        }

        if (metafile_md := args.option("--metafile-md")) {
            // If --metafile-md is passed without a value, default to "meta.md"
            ctx.bundler_options.metafile_md = if (metafile_md.len > 0)
                bun.handleOom(allocator.dupeZ(u8, metafile_md))
            else
                "meta.md";
        }

        if (root_dir := args.option("--root")) {
            if (root_dir.len > 0) {
                ctx.bundler_options.root_dir = root_dir;
            }
        }

        if (format_str := args.option("--format")) {
            const format_opt = options.Format.fromString(format_str);
            if (format_opt == null) {
                Output.errGeneric("Invalid format - must be esm, cjs, or iife", {});
                Global.crash();
            }
            const format = format_opt.?;

            switch (format) {
                .internal_bake_dev => {
                    bun.Output.warn("--format={s} is for debugging only, and may experience breaking changes at any moment", .{format_str});
                    bun.Output.flush();
                },
                .cjs => {
                    if (ctx.args.target == null) {
                        ctx.args.target = .node;
                    }
                },
                else => {},
            }

            ctx.bundler_options.output_format = format;
            if (ctx.bundler_options.bytecode) {
                if (format != .cjs && format != .esm) {
                    Output.errGeneric("format must be 'cjs' or 'esm' when bytecode is true.", {});
                    Global.exit(1);
                }
                // ESM bytecode requires --compile because module_info (import/export metadata)
                // is only available in compiled binaries. Without it, JSC must parse the file
                // twice (once for module analysis, once for bytecode), which is a deopt.
                if (format == .esm && !ctx.bundler_options.compile) {
                    Output.errGeneric("ESM bytecode requires --compile. Use --format=cjs for bytecode without --compile.", {});
                    Global.exit(1);
                }
            }
        }

        if (args.flag("--splitting")) {
            ctx.bundler_options.code_splitting = true;
        }

        if (entry_naming := args.option("--entry-naming")) {
            if (u := options.findUnterminatedPlaceholder(entry_naming)) {
                bun.Output.errGeneric("{s}: unterminated \"{s}\" placeholder (missing \"]\") at position {d}", .{ "--entry-naming", u.tail, u.pos });
                Global.exit(1);
            }
            ctx.bundler_options.entry_naming = strings.concat(allocator, &.{ "./", bun.strings.removeLeadingDotSlash(entry_naming) })?;
        }

        if (chunk_naming := args.option("--chunk-naming")) {
            if (u := options.findUnterminatedPlaceholder(chunk_naming)) {
                bun.Output.errGeneric("{s}: unterminated \"{s}\" placeholder (missing \"]\") at position {d}", .{ "--chunk-naming", u.tail, u.pos });
                Global.exit(1);
            }
            ctx.bundler_options.chunk_naming = strings.concat(allocator, &.{ "./", bun.strings.removeLeadingDotSlash(chunk_naming) })?;
        }

        if (asset_naming := args.option("--asset-naming")) {
            if (u := options.findUnterminatedPlaceholder(asset_naming)) {
                bun.Output.errGeneric("{s}: unterminated \"{s}\" placeholder (missing \"]\") at position {d}", .{ "--asset-naming", u.tail, u.pos });
                Global.exit(1);
            }
            ctx.bundler_options.asset_naming = strings.concat(allocator, &.{ "./", bun.strings.removeLeadingDotSlash(asset_naming) })?;
        }

        if (args.flag("--server-components")) {
            ctx.bundler_options.server_components = true;
            if (target := opts.target) {
                if (!bun.options.Target.@"from"(target).isServerSide()) {
                    bun.Output.errGeneric("Cannot use client-side --target={s} with --server-components", .{@tagName(target)});
                    Global.crash();
                } else {
                    opts.target = .bun;
                }
            }
        }

        if (args.flag("--react-fast-refresh")) {
            ctx.bundler_options.react_fast_refresh = true;
        }

        if (setting := args.option("--sourcemap")) {
            if (setting.len == 0) {
                // In the future, Bun is going to make this default to .linked
                opts.source_map = .linked;
            } else if (strings.eqlComptime(setting, "inline")) {
                opts.source_map = .@"inline";
            } else if (strings.eqlComptime(setting, "none")) {
                opts.source_map = .none;
            } else if (strings.eqlComptime(setting, "external")) {
                opts.source_map = .external;
            } else if (strings.eqlComptime(setting, "linked")) {
                opts.source_map = .linked;
            } else {
                Output.prettyErrorln("<r><red>error<r>: Invalid sourcemap setting: \"{s}\"", .{setting});
                Global.crash();
            }

            // when using --compile, only `external` works, as we do not
            // look at the source map comment. so after we validate the
            // user's choice was in the list, we secretly override it
            if (ctx.bundler_options.compile) {
                opts.source_map = .external;
            }
        }
    }

    if (opts.entry_points.len == 0) {
        let entry_points = ctx.positionals;

        switch (comptime cmd) {
            .BuildCommand => {
                if (entry_points.len > 0 && (strings.eqlComptime(
                    entry_points[0],
                    "build",
                ) || strings.eqlComptime(entry_points[0], "bun"))) {
                    let out_entry = entry_points[1..];
                    let epi: usize = 0;
                    while (epi < entry_points.len) : (epi += 1) {
                        if (entry_points[epi].len > 0) {
                            out_entry = out_entry[epi..];
                            break;
                        }
                    }
                    entry_points = out_entry;
                }
            },
            .RunCommand => {
                if (entry_points.len > 0 && (strings.eqlComptime(
                    entry_points[0],
                    "run",
                ) || strings.eqlComptime(
                    entry_points[0],
                    "r",
                ))) {
                    entry_points = entry_points[1..];
                }
            },
            else => {},
        }

        opts.entry_points = entry_points;
    }

    const jsx_factory = args.option("--jsx-factory");
    const jsx_fragment = args.option("--jsx-fragment");
    const jsx_import_source = args.option("--jsx-import-source");
    const jsx_runtime = args.option("--jsx-runtime");
    const jsx_side_effects = args.flag("--jsx-side-effects");

    if (cmd == .AutoCommand || cmd == .RunCommand) {
        // "run.silent" in bunfig.toml
        if (args.flag("--silent")) {
            ctx.debug.silent = true;
        }

        if (elide_lines := args.option("--elide-lines")) {
            if (elide_lines.len > 0) {
                ctx.bundler_options.elide_lines = std.fmt.parseInt(usize, elide_lines, 10) catch {
                    Output.prettyErrorln("<r><red>error<r>: Invalid elide-lines: \"{s}\"", .{elide_lines});
                    Global.exit(1);
                };
            }
        }

        if (define := opts.define) {
            if (define.keys.len > 0)
                bun.jsc.RuntimeTranspilerCache.is_disabled = true;
        }
    }

    if (cmd == .RunCommand || cmd == .AutoCommand || cmd == .BunxCommand) {
        // "run.bun" in bunfig.toml
        if (args.flag("--bun")) {
            ctx.debug.run_in_bun = true;
        }
    }

    opts.resolve = Api.ResolveMode.lazy;

    if (jsx_factory != null ||
        jsx_fragment != null ||
        jsx_import_source != null ||
        jsx_runtime != null)
    {
        let default_factory = "".*;
        let default_fragment = "".*;
        let default_import_source = "".*;
        if (opts.jsx == null) {
            let resolved_runtime = Api.JsxRuntime.automatic;
            if (runtime := jsx_runtime) {
                resolved_runtime = resolve_jsx_runtime(runtime)?;
            }
            opts.jsx = Api.Jsx{
                factory: (jsx_factory ?? &default_factory),
                fragment: (jsx_fragment ?? &default_fragment),
                import_source: (jsx_import_source ?? &default_import_source),
                runtime: resolved_runtime,
                development: false,
                side_effects: jsx_side_effects,
            };
        } else {
            let resolved_runtime = opts.jsx.?.runtime;
            if (runtime := jsx_runtime) {
                resolved_runtime = resolve_jsx_runtime(runtime)?;
            }
            opts.jsx = Api.Jsx{
                factory: (jsx_factory ?? opts.jsx.?.factory),
                fragment: (jsx_fragment ?? opts.jsx.?.fragment),
                import_source: (jsx_import_source ?? opts.jsx.?.import_source),
                runtime: resolved_runtime,
                development: false,
                side_effects: jsx_side_effects,
            };
        }
    }

    if (cmd == .BuildCommand) {
        if (opts.entry_points.len == 0 && !ctx.bundler_options.bake) {
            Output.prettyln("<r><b>bun build <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>", {});
            Output.pretty("<r><red>error: Missing entrypoints. What would you like to bundle?<r>\n\n", {});
            Output.flush();
            Output.pretty("Usage:\n  <d>$<r> <b><green>bun build<r> \\<entrypoint\\> [...\\<entrypoints\\>] <cyan>[...flags]<r>  \n", {});
            Output.pretty("\nTo see full documentation:\n  <d>$<r> <b><green>bun build<r> --help\n", {});
            Output.flush();
            Global.exit(1);
        }

        if (args.flag("--production")) {
            let any_html = false;
            for (entry_point of opts.entry_points) {
                if (strings.hasSuffixComptime(entry_point, ".html")) {
                    any_html = true;
                    break;
                }
            }
            if (any_html) {
                ctx.bundler_options.css_chunking = true;
            }

            ctx.bundler_options.production = true;
        }
    }

    if (log_level := opts.log_level) {
        logger.Log.default_log_level = switch (log_level) {
            .debug => logger.Log.Level.debug,
            .err => logger.Log.Level.err,
            .warn => logger.Log.Level.warn,
            else => logger.Log.Level.err,
        };
        ctx.log.level = logger.Log.default_log_level;
    }

    if (args.flag("--no-macros")) {
        ctx.debug.macros = { disable: undefined };
    }

    opts.output_dir = output_dir;
    if (output_file != null)
        ctx.debug.output_file = output_file.?;

    if (cmd == .RunCommand || cmd == .AutoCommand) {
        if (shell := args.option("--shell")) {
            if (strings.eqlComptime(shell, "bun")) {
                ctx.debug.use_system_shell = false;
            } else if (strings.eqlComptime(shell, "system")) {
                ctx.debug.use_system_shell = true;
            } else {
                Output.errGeneric("Expected --shell to be one of 'bun' or 'system'. Received: \"{s}\"", .{shell});
                Global.exit(1);
            }
        }
    }

    if (Environment.show_crash_trace) {
        debug_flags.resolve_breakpoints = args.options("--breakpoint-resolve");
        debug_flags.print_breakpoints = args.options("--breakpoint-print");
    }

    return opts;
}

export let Bun__Node__ZeroFillBuffers = false;
export let Bun__Node__ProcessNoDeprecation = false;
export let Bun__Node__ProcessThrowDeprecation = false;

pub enum BunCAStore: u8 { bundled, openssl, system }
pub export let Bun__Node__CAStore: BunCAStore = .bundled;
pub export let Bun__Node__UseSystemCA = false;

type string = []const u8;

import builtin from "builtin";

import bun from "bun";
const Bunfig = bun.Bunfig;
const Environment = bun.Environment;
const FeatureFlags = bun.FeatureFlags;
const Global = bun.Global;
const OOM = bun.OOM;
const Output = bun.Output;
const clap = bun.clap;
const js_ast = bun.ast;
const logger = bun.logger;
const options = bun.options;
const resolve_path = bun.path;
const strings = bun.strings;
const Api = bun.schema.api;
const RegularExpression = bun.jsc.RegularExpression;

const CLI = bun.cli;
const Command = CLI.Command;
const DefineColonList = CLI.DefineColonList;
const LoaderColonList = CLI.LoaderColonList;
const debug_flags = CLI.debug_flags;
const printRevisionAndExit = CLI.printRevisionAndExit;
const printVersionAndExit = CLI.printVersionAndExit;
