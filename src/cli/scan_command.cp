import bun from "bun";
import cli from "./cli.zig";
import install from "../install/install.zig";
import security_scanner from "../install/PackageManager/security_scanner.zig";

const Global = bun.Global;
const Output = bun.Output;
const Command = cli.Command;
const PackageManager = install.PackageManager;

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    ptr: T,
    ~init() {
        _.allocator.free(_.ptr);
    }
}

struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct ScanCommand {
    static fn exec(ctx: Command.Context)! {
        const cli_args = PackageManager.CommandLineArguments.parse(ctx.allocator, .scan)?;

        const res = PackageManager.init(ctx, cli_args, .scan) catch |e| {
            if (e == error.MissingPackageJSON) {
                Output.errGeneric("No package.json found. 'bun pm scan' requires a lockfile to analyze dependencies.", .{});
                Output.note("Run \"bun install\" first to generate a lockfile", .{});
                Global.exit(1);
            }
            return e;
        };
        const manager = res[0];
        const cwd_guard = FreeGuard<[]const u8>{ allocator: ctx.allocator, ptr: res[1] };

        execWithManager(ctx, manager, cwd_guard.ptr)?;
    }

    static fn execWithManager(ctx: Command.Context, manager: *PackageManager, original_cwd: []const u8)! {
        if (manager.options.security_scanner == null) {
            Output.prettyErrorln("<r><red>error<r>: no security scanner configured", .{});
            Output.pretty("\nTo use 'bun pm scan', configure a security scanner in bunfig.toml:\n  [install.security]\n  scanner = \"<cyan>package_name<r>\"\n\nSecurity scanners can be npm packages that export a scanner object.\n\n", .{});
            Global.exit(1);
        }

        Output.prettyError(comptime Output.prettyFmt("<r><b>bun pm scan <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", true), .{});
        Output.flush();

        const load_lockfile = manager.lockfile.loadFromCwd(manager, ctx.allocator, ctx.log, true);
        if (load_lockfile == .not_found) {
            Output.errGeneric("Lockfile not found. Run 'bun install' first to generate a lockfile.", .{});
            Global.exit(1);
        }
        if (load_lockfile == .err) {
            Output.errGeneric("Error loading lockfile: {s}", .{ @errorName(load_lockfile.err.value) });
            Global.exit(1);
        }

        const security_scan_results = security_scanner.performSecurityScanForAll(manager, ctx, original_cwd) catch |e| {
            Output.errGeneric("Could not perform security scan (<d>{s}<r>)", .{ @errorName(e) });
            Global.exit(1);
        };

        if (results := security_scan_results) {
            let results_guard = DeinitGuard<@TypeOf(results)>{ val: results };

            security_scanner.printSecurityAdvisories(manager, &results_guard.val);

            if (results_guard.val.hasAdvisories()) {
                Global.exit(1);
            } else {
                Output.pretty("<green>No advisories found<r>\n", .{});
            }
        }

        Global.exit(0);
    }
}
