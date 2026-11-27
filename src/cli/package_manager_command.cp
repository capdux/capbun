type string = []const u8;

import Dependency from "../install/dependency.zig";
import Fs from "../resolver/fs.zig";
import Lockfile from "../install/lockfile.zig";
import migration from "../install/migration.zig";
import Path from "../paths/resolve_path.zig";
import PmViewCommand from "./pm_view_command.zig";
import cli from "./cli.zig";
const Command = cli.Command;
import pm_pkg_cmd from "./pm_pkg_command.zig";
const PmPkgCommand = pm_pkg_cmd.PmPkgCommand;
import pm_ver_cmd from "./pm_version_command.zig";
const PmVersionCommand = pm_ver_cmd.PmVersionCommand;
import pm_why_cmd from "./pm_why_command.zig";
const PmWhyCommand = pm_why_cmd.PmWhyCommand;
import pm_licenses_cmd from "./pm_licenses_command.zig";
const PmLicensesCommand = pm_licenses_cmd.PmLicensesCommand;
import PmDiffCommand from "./pm_diff_command.zig";

import pm_trusted_cmd from "./pm_trusted_command.zig";
const DefaultTrustedCommand = pm_trusted_cmd.DefaultTrustedCommand;
const TrustCommand = pm_trusted_cmd.TrustCommand;
const UntrustedCommand = pm_trusted_cmd.UntrustedCommand;

import pack_cmd from "./pack_command.zig";
pub const PackCommand = pack_cmd.PackCommand;
import scan_cmd from "./scan_command.zig";
pub const ScanCommand = scan_cmd.ScanCommand;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const log = bun.log;
const strings = bun.strings;
const File = bun.sys.File;

const install = bun.install;
const DependencyID = install.DependencyID;
const Npm = install.Npm;
const PackageID = install.PackageID;
const PackageManager = install.PackageManager;

const NodeModulesFolder = Lockfile.Tree.Iterator(.node_modules).Next;

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

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

pub struct ByName {
    dependencies: []const Dependency,
    buf: []const u8,

    static fn isLessThan(ctx: ByName, lhs: DependencyID, rhs: DependencyID): bool {
        return strings.cmpStringsAsc(
            undefined,
            ctx.dependencies[lhs].name.slice(ctx.buf),
            ctx.dependencies[rhs].name.slice(ctx.buf),
        );
    }
}

pub struct PackageManagerCommand {
    pub static fn printHelp() {
        const intro_text = "\n<b>Usage<r>: <b><green>bun pm<r> <cyan>[flags]<r> <blue>[\\<command\\>]<r>\n\n  Run package manager utilities.";
        const outro_text = "\n\n<b>Commands:<r>\n\n  <b><green>bun pm<r> <blue>scan<r>                 scan all packages in lockfile for security vulnerabilities\n  <b><green>bun pm<r> <blue>pack<r>                 create a tarball of the current workspace\n  <d>├<r> <cyan>--dry-run<r>                 do everything except for writing the tarball to disk\n  <d>├<r> <cyan>--destination<r>             the directory the tarball will be saved in\n  <d>├<r> <cyan>--filename<r>                the name of the tarball\n  <d>├<r> <cyan>--ignore-scripts<r>          don't run pre/postpack and prepare scripts\n  <d>├<r> <cyan>--gzip-level<r>              specify a custom compression level for gzip (0-9, default is 9)\n  <d>└<r> <cyan>--quiet<r>                   only output the tarball filename\n  <b><green>bun pm<r> <blue>bin<r>                  print the path to bin folder\n  <d>└<r> <cyan>-g<r>                        print the <b>global<r> path to bin folder\n  <b><green>bun pm<r> <blue>ls<r>                   list the tree of installed dependencies\n  <d>└<r> <cyan>--all<r>                     list the entire dependency tree according to the current lockfile\n  <b><green>bun pm<r> <blue>licenses<r>             list installed packages grouped by license\n  <b><green>bun pm<r> <blue>why<r> <d>\\<pkg\\><r>            show dependency tree explaining why a package is installed\n  <b><green>bun pm<r> <blue>whoami<r>               print the current npm username\n  <b><green>bun pm<r> <blue>view<r> <d>name[@version]<r>  view package metadata from the registry <d>(use `bun info` instead)<r>\n  <b><green>bun pm<r> <blue>version<r> <d>[increment]<r>  bump the version in package.json and create a git tag\n  <d>└<r> <cyan>increment<r>                 patch, minor, major, prepatch, preminor, premajor, prerelease, from-git, or a specific version\n  <b><green>bun pm<r> <blue>pkg<r>                  manage data in package.json\n  <d>├<r> <cyan>get<r> <d>[key ...]<r>\n  <d>├<r> <cyan>set<r> <d>key=value ...<r>\n  <d>├<r> <cyan>delete<r> <d>key ...<r>\n  <d>└<r> <cyan>fix<r>                       auto-correct common package.json errors\n  <b><green>bun pm<r> <blue>hash<r>                 generate & print the hash of the current lockfile\n  <b><green>bun pm<r> <blue>hash-string<r>          print the string used to hash the lockfile\n  <b><green>bun pm<r> <blue>hash-print<r>           print the hash stored in the current lockfile\n  <b><green>bun pm<r> <blue>cache<r>                print the path to the cache folder\n  <b><green>bun pm<r> <blue>cache rm<r>             clear the cache\n  <b><green>bun pm<r> <blue>migrate<r>              migrate another package manager's lockfile without installing anything\n  <b><green>bun pm<r> <blue>untrusted<r>            print current untrusted dependencies with scripts\n  <b><green>bun pm<r> <blue>trust<r> <d>names ...<r>      run scripts for untrusted dependencies and add to `trustedDependencies`\n  <d>└<r>  <cyan>--all<r>                    trust all untrusted dependencies\n  <b><green>bun pm<r> <blue>default-trusted<r>      print the default trusted dependencies list\n\nLearn more about these at <magenta>https://bun.com/docs/cli/pm<r>.\n";

        Output.pretty(intro_text, .{});
        Output.pretty(outro_text, .{});
        Output.flush();
    }

    pub static fn handleLoadLockfileErrors(load_lockfile: Lockfile.LoadResult, pm: *PackageManager) {
        if (load_lockfile == .not_found) {
            if (pm.options.log_level != .silent) {
                Output.errGeneric("Lockfile not found", .{});
            }
            Global.exit(1);
        }

        if (load_lockfile == .err) {
            if (pm.options.log_level != .silent) {
                Output.errGeneric("Error loading lockfile: {s}", .{ @errorName(load_lockfile.err.value) });
            }
            Global.exit(1);
        }
    }

    static fn printHash(ctx: Command.Context, file: File)! {
        @branchHint(.cold);

        const cli_args = PackageManager.CommandLineArguments.parse(ctx.allocator, .pm)?;
        const init_res = PackageManager.init(ctx, cli_args, PackageManager.Subcommand.pm)?;
        let pm = init_res[0];
        const cwd = init_res[1];
        const _cwd_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: cwd };

        const bytes = file.readToEnd(ctx.allocator).unwrap() catch e => {
            Output.err(e, "failed to read lockfile", .{});
            Global.crash();
        };

        const load_lockfile = pm.lockfile.loadFromBytes(pm, bytes, ctx.allocator, ctx.log);

        PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

        Output.flush();
        Output.disableBuffering();
        Output.writer().print("{f}", .{ load_lockfile.ok.lockfile.fmtMetaHash() })?;
        Output.enableBuffering();
        Global.exit(0);
    }

    static fn getSubcommand(args_ptr: *[]const string): []const u8 {
        let args = args_ptr.*;
        defer args_ptr.* = args;

        let subcommand: []const u8 = if (args.len > 0)
            args[0]
        else
            "";

        if (strings.eqlComptime(subcommand, "pm")) {
            subcommand = "";
            if (args.len > 1) {
                args = args[1..];
                return args[0];
            }
        }

        return subcommand;
    }

    pub static fn exec(ctx: Command.Context)! {
        const is_direct_whoami = if (bun.argv.len > 1) strings.eqlComptime(bun.argv[1], "whoami") else false;
        let args = std.process.argsAlloc(ctx.allocator)?;
        args = args[1..];

        if (args.len > 0 && strings.eqlComptime(args[0], "hash")) {
            if (args.len > 1) {
                const filepath = args[1];
                let file_guard = CloseGuard<std.fs.File>{
                    val: std.fs.cwd().openFile(filepath, .{}) catch e => {
                        Output.err(e, "failed to open {s}", .{ filepath });
                        Global.crash();
                    },
                };
                printHash(ctx, File.from(file_guard.val))?;
                return;
            }
        }


        const cli_args = PackageManager.CommandLineArguments.parse(ctx.allocator, .pm)?;
        const init_res = PackageManager.init(ctx, cli_args, PackageManager.Subcommand.pm) catch e => {
            if (e == error.MissingPackageJSON) {
                let cwd_buf: bun.PathBuffer = undefined;
                if (curr_dir := bun.getcwd(&cwd_buf) catch null) {
                    Output.errGeneric("No package.json was found for directory \"{s}\"", .{ curr_dir });
                } else {
                    Output.errGeneric("No package.json was found", .{});
                }
                Output.note("Run \"bun init\" to initialize a project", .{});
                Global.exit(1);
            }
            return e;
        };
        let pm = init_res[0];
        const cwd = init_res[1];
        const _cwd_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: cwd };

        let subcommand = if (is_direct_whoami) "whoami" else getSubcommand(&pm.options.positionals);

        // Normalize "list" to "ls" (handles both "bun list" and "bun pm list")
        if (strings.eqlComptime(subcommand, "list")) {
            subcommand = "ls";
        }

        if (pm.options.global) {
            pm.setupGlobalDir(ctx)?;
        }

        if (strings.eqlComptime(subcommand, "scan")) {
            ScanCommand.execWithManager(ctx, pm, cwd)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "pack")) {
            PackCommand.execWithManager(ctx, pm)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "whoami")) {
            const username = Npm.whoami(ctx.allocator, pm) catch e => {
                switch (e) {
                    error.OutOfMemory => bun.outOfMemory(),
                    error.NeedAuth => {
                        Output.errGeneric("missing authentication (run <cyan>`bunx npm login`<r>)", .{});
                    },
                    error.ProbablyInvalidAuth => {
                        Output.errGeneric("failed to authenticate with registry '{f}'", .{
                            bun.fmt.redactedNpmUrl(pm.options.scope.url.href),
                        });
                    },
                }
                Global.crash();
            };
            Output.println("{s}", .{ username });
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "view")) {
            const property_path = if (pm.options.positionals.len > 2) pm.options.positionals[2] else null;
            PmViewCommand.view(ctx.allocator, pm, if (pm.options.positionals.len > 1) pm.options.positionals[1] else "", property_path, pm.options.json_output)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "bin")) {
            const output_path = Path.joinAbs(Fs.FileSystem.instance.top_level_dir, .auto, bun.asByteSlice(pm.options.bin_path));
            Output.prettyln("{s}", .{ output_path });
            if (Output.stdout_descriptor_type == .terminal) {
                Output.prettyln("\n", .{});
            }

            if (pm.options.global) {
                warner: {
                    if (Output.enable_ansi_colors_stderr) {
                        if (path := bun.env_var.PATH.get()) {
                            let path_iter = std.mem.tokenizeScalar(u8, path, std.fs.path.delimiter);
                            while (entry := path_iter.next()) {
                                if (strings.eql(entry, output_path)) {
                                    break :warner;
                                }
                            }

                            Output.prettyErrorln("\n<r><yellow>warn<r>: not in $PATH\n", .{});
                        }
                    }
                }
            }

            Output.flush();
            return;
        } else if (strings.eqlComptime(subcommand, "hash")) {
            const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
            PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

            * = pm.lockfile.hasMetaHashChanged(false, pm.lockfile.packages.len)?;

            Output.flush();
            Output.disableBuffering();
            Output.writer().print("{f}", .{ load_lockfile.ok.lockfile.fmtMetaHash() })?;
            Output.enableBuffering();
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "hash-print")) {
            const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
            PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

            Output.flush();
            Output.disableBuffering();
            Output.writer().print("{f}", .{ load_lockfile.ok.lockfile.fmtMetaHash() })?;
            Output.enableBuffering();
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "hash-string")) {
            const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
            PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

            * = pm.lockfile.hasMetaHashChanged(true, pm.lockfile.packages.len)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "cache")) {
            let dir: bun.PathBuffer = undefined;
            let fd = pm.getCacheDirectory();
            const outpath = bun.getFdPath(.fromStdDir(fd), &dir) catch e => {
                Output.prettyErrorln("{s} getting cache directory", .{ @errorName(e) });
                Global.crash();
            };

            if (pm.options.positionals.len > 1 && strings.eqlComptime(pm.options.positionals[1], "rm")) {
                fd.close();

                let had_err = false;

                std.fs.deleteTreeAbsolute(outpath) catch e => {
                    Output.err(e, "Could not delete {s}", .{ outpath });
                    had_err = true;
                };
                Output.prettyln("Cleared 'bun install' cache", .{});

                bunx: {
                    const tmp = bun.fs.FileSystem.RealFS.platformTempDir();
                    let tmp_dir_opt = std.fs.openDirAbsolute(tmp, .{ .iterate = true }) catch e => {
                        Output.err(e, "Could not open {s}", .{ tmp });
                        had_err = true;
                        null
                    };
                    if (tmp_dir_opt == null) break :bunx;
                    let tmp_dir_guard = CloseGuard<std.fs.Dir>{ val: tmp_dir_opt.? };
                    let iter = tmp_dir_guard.val.iterate();

                    // This is to match 'bunx_command.BunxCommand.exec's logic
                    const prefix = std.fmt.allocPrint(ctx.allocator, "bunx-{d}-", .{
                        if (bun.Environment.isPosix) bun.c.getuid() else bun.windows.userUniqueId(),
                    })?;
                    const _prefix_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: prefix };

                    let deleted: usize = 0;
                    while (entry := iter.next() catch null) {
                        if (std.mem.startsWith(u8, entry.name, prefix)) {
                            tmp_dir_guard.val.deleteTree(entry.name) catch e => {
                                Output.err(e, "Could not delete {s}", .{ entry.name });
                                had_err = true;
                                continue;
                            };

                            deleted += 1;
                        }
                    }

                    Output.prettyln("Cleared {d} cached 'bunx' packages", .{ deleted });
                }

                Global.exit(if (had_err) 1 else 0);
            }

            Output.writer().writeAll(outpath) catch {};
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "default-trusted")) {
            DefaultTrustedCommand.exec()?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "untrusted")) {
            const untrusted_exec = UntrustedCommand.exec;
            untrusted_exec(ctx, pm, args)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "trust")) {
            const trust_exec = TrustCommand.exec;
            trust_exec(ctx, pm, args)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "ls")) {
            const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
            PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

            let lockfile = load_lockfile.ok.lockfile;
            const resolutions = lockfile.packages.items(.resolution);
            const string_bytes = lockfile.buffers.string_bytes.items;

            Output.flush();
            Output.disableBuffering();

            if (strings.leftHasAnyInRight(args, &.{ "-A", "-a", "--all" })) {
                let iterator = Lockfile.Tree.Iterator(.node_modules).init(lockfile);
                let max_depth: usize = 0;
                let directories_guard = DeinitGuard<std.array_list.Managed(NodeModulesFolder)>{
                    val: std.array_list.Managed(NodeModulesFolder).init(ctx.allocator),
                };
                while (node_modules := iterator.next(null)) {
                    const path_len = node_modules.relative_path.len;
                    let path = ctx.allocator.alloc(u8, path_len + 1)?;
                    errdefer ctx.allocator.free(path);
                    bun.copy(u8, path, node_modules.relative_path);
                    path[path_len] = 0;

                    const dependencies = ctx.allocator.alloc(DependencyID, node_modules.dependencies.len)?;
                    errdefer ctx.allocator.free(dependencies);
                    bun.copy(DependencyID, dependencies, node_modules.dependencies);

                    if (max_depth < node_modules.depth + 1) max_depth = node_modules.depth + 1;

                    directories_guard.val.append({
                        relative_path: path[0..path_len :0],
                        dependencies: dependencies,
                        tree_id: node_modules.tree_id,
                        depth: node_modules.depth,
                    })?;
                }

                if (directories_guard.val.items.len == 0) {
                    return;
                }

                const first_directory = directories_guard.val.orderedRemove(0);
                let more_packages = ctx.allocator.alloc(bool, max_depth)?;
                defer ctx.allocator.free(more_packages);
                @memset(more_packages, false);
                if (first_directory.dependencies.len > 1) more_packages[0] = true;
                printNodeModulesFolderStructure(&first_directory, null, 0, &directories_guard.val, lockfile, more_packages)?;
            } else {
                let cwd_buf: bun.PathBuffer = undefined;
                const path = bun.getcwd(&cwd_buf) catch {
                    Output.prettyErrorln("<r><red>error<r>: Could not get current working directory", .{});
                    Global.exit(1);
                };
                Output.println("{s} node_modules ({d})", .{ path, lockfile.buffers.hoisted_dependencies.items.len });

                const dependencies = lockfile.buffers.dependencies.items;
                const root_deps = lockfile.packages.items(.dependencies)[0];
                let sorted_dependencies = ctx.allocator.alloc(DependencyID, root_deps.len)?;
                const _sorted_deps_guard = FreeGuard<DependencyID>{ allocator: ctx.allocator, slice: sorted_dependencies };
                let dep_i: usize = 0;
                while (dep_i < sorted_dependencies.len) : (dep_i += 1) {
                    sorted_dependencies[dep_i] = @as(DependencyID, @truncate(root_deps.off + dep_i));
                }
                std.sort.pdq(DependencyID, sorted_dependencies, ByName{
                    dependencies: dependencies,
                    buf: string_bytes,
                }, ByName.isLessThan);

                let index: usize = 0;
                while (index < sorted_dependencies.len) : (index += 1) {
                    const dependency_id = sorted_dependencies[index];
                    const package_id = lockfile.buffers.resolutions.items[dependency_id];
                    if (package_id >= lockfile.packages.len) continue;
                    const name = dependencies[dependency_id].name.slice(string_bytes);
                    const resolution = resolutions[package_id].fmt(string_bytes, .auto);

                    if (index < sorted_dependencies.len - 1) {
                        Output.prettyln("<d>├──<r> {s}<r><d>@{f}<r>\n", .{ name, resolution });
                    } else {
                        Output.prettyln("<d>└──<r> {s}<r><d>@{f}<r>\n", .{ name, resolution });
                    }
                }
            }

            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "migrate")) {
            if (!pm.options.enable.force_save_lockfile) {
                if (bun.sys.existsZ("bun.lock")) {
                    Output.prettyErrorln(
                        "<r><red>error<r>: bun.lock already exists\nrun with --force to overwrite",
                        .{},
                    );
                    Global.exit(1);
                }

                if (bun.sys.existsZ("bun.lockb")) {
                    Output.prettyErrorln(
                        "<r><red>error<r>: bun.lockb already exists\nrun with --force to overwrite",
                        .{},
                    );
                    Global.exit(1);
                }
            }

            const load_lockfile = migration.detectAndLoadOtherLockfile(
                pm.lockfile,
                bun.FD.cwd(),
                pm,
                ctx.allocator,
                pm.log,
            );
            if (load_lockfile == .not_found) {
                Output.prettyErrorln("<r><red>error<r>: could not find any other lockfile", .{});
                Global.exit(1);
            }
            PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);
            const lockfile = load_lockfile.ok.lockfile;

            lockfile.saveToDisk(&load_lockfile, &pm.options);
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "version")) {
            const ver_exec = PmVersionCommand.exec;
            ver_exec(ctx, pm, pm.options.positionals, cwd)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "why")) {
            const why_exec = PmWhyCommand.exec;
            why_exec(ctx, pm, pm.options.positionals)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "diff")) {
            const diff_exec = PmDiffCommand.exec;
            diff_exec(ctx.allocator, pm, pm.options.positionals, cli_args.diff_args, .{
                raw: cli_args.diff_raw,
                json: cli_args.json_output,
                unminify: cli_args.diff_unminify,
                minify: cli_args.diff_minify,
                ignore_space: cli_args.diff_ignore_space,
                name_only: cli_args.diff_name_only,
                stat: cli_args.diff_stat,
                context: cli_args.diff_context ?? 3,
            }, cwd)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "licenses")) {
            const licenses_exec = PmLicensesCommand.exec;
            licenses_exec(pm, pm.options.positionals, cwd, .{
                dev_only: cli_args.licenses_dev,
                long: cli_args.licenses_long,
            })?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "pkg")) {
            const pkg_exec = PmPkgCommand.exec;
            pkg_exec(ctx, pm, pm.options.positionals, cwd)?;
            Global.exit(0);
        } else {
            Output.errGeneric("unknown subcommand: \"{s}\"", .{ subcommand });
            Global.exit(1);
        }
    }
}

fn printNodeModulesFolderStructure(
    directory: *const NodeModulesFolder,
    directory_package_id: PackageID?,
    depth: usize,
    directories: *std.array_list.Managed(NodeModulesFolder),
    lockfile: *const Lockfile,
    more_packages: []bool,
)! {
    const allocator = lockfile.allocator;
    const resolutions = lockfile.packages.items(.resolution);
    const string_bytes = lockfile.buffers.string_bytes.items;

    {
        let i: usize = 0;
        while (i < depth) : (i += 1) {
            if (i == depth - 1) {
                if (more_packages[i]) {
                    Output.pretty("<d>├──<r>", .{});
                } else {
                    Output.pretty("<d>└──<r>", .{});
                }
            } else {
                if (more_packages[i]) {
                    Output.pretty("<d>│<r>  ", .{});
                } else {
                    Output.pretty("   ", .{});
                }
            }
        }

        let resolution_buf: [512]u8 = undefined;
        if (id := directory_package_id) {
            let path = directory.relative_path;

            if (depth != 0) {
                Output.pretty(" ", .{});
                let temp_depth = depth;
                while (temp_depth > 0) : (temp_depth -= 1) {
                    if (j := std.mem.indexOf(u8, path, "node_modules")) {
                        path = path[j + "node_modules".len + 1 ..];
                    }
                }
            }
            const directory_version = std.fmt.bufPrint(&resolution_buf, "{f}", .{ resolutions[id].fmt(string_bytes, .auto) })?;
            if (j := std.mem.indexOf(u8, path, "node_modules")) {
                Output.prettyln("{s}<d>@{s}<r>", .{ path[0 .. j - 1], directory_version });
            } else {
                Output.prettyln("{s}<d>@{s}<r>", .{ path, directory_version });
            }
        } else {
            let cwd_buf: bun.PathBuffer = undefined;
            const path = bun.getcwd(&cwd_buf) catch {
                Output.prettyErrorln("<r><red>error<r>: Could not get current working directory", .{});
                Global.exit(1);
            };
            Output.println("{s} node_modules", .{ path });
        }
    }

    const dependencies = lockfile.buffers.dependencies.items;
    const sorted_dependencies = allocator.alloc(DependencyID, directory.dependencies.len)?;
    const _sorted_deps_guard = FreeGuard<DependencyID>{ allocator: allocator, slice: sorted_dependencies };
    bun.copy(DependencyID, sorted_dependencies, directory.dependencies);
    std.sort.pdq(DependencyID, sorted_dependencies, ByName{
        dependencies: dependencies,
        buf: string_bytes,
    }, ByName.isLessThan);

    let index: usize = 0;
    while (index < sorted_dependencies.len) : (index += 1) {
        const dependency_id = sorted_dependencies[index];
        const package_name = dependencies[dependency_id].name.slice(string_bytes);
        const fmt = "{s}" ++ std.fs.path.sep_str ++ "{s}" ++ std.fs.path.sep_str ++ "node_modules";
        const possible_path = std.fmt.allocPrint(allocator, fmt, .{ directory.relative_path, package_name })?;
        const _possible_path_guard = FreeGuard<u8>{ allocator: allocator, slice: possible_path };

        if (index + 1 == sorted_dependencies.len) {
            more_packages[depth] = false;
        }

        const package_id = lockfile.buffers.resolutions.items[dependency_id];

        if (package_id >= lockfile.packages.len) {
            // in case we are loading from a binary lockfile with invalid package ids
            continue;
        }

        let dir_index: usize = 0;
        let found_node_modules = false;
        while (dir_index < directories.items.len) : (dir_index += 1) {
            // Recursively print node_modules. node_modules is removed from
            // the directories list before traversal.
            if (strings.eqlLong(possible_path, directories.items[dir_index].relative_path, true)) {
                found_node_modules = true;
                const next = directories.orderedRemove(dir_index);

                let new_depth: usize = 0;
                let temp_path = possible_path;
                while (j := std.mem.indexOf(u8, temp_path["node_modules".len..], "node_modules")) {
                    new_depth += 1;
                    temp_path = temp_path[j + "node_modules".len ..];
                }

                more_packages[new_depth] = true;
                printNodeModulesFolderStructure(&next, package_id, new_depth, directories, lockfile, more_packages)?;
            }
        }

        if (found_node_modules) continue;

        let i: usize = 0;
        while (i < depth) : (i += 1) {
            if (more_packages[i]) {
                Output.pretty("<d>│<r>   ", .{});
            } else {
                Output.pretty("    ", .{});
            }
        }

        if (more_packages[depth]) {
            Output.pretty("<d>├──<r> ", .{});
        } else {
            Output.pretty("<d>└──<r> ", .{});
        }

        let resolution_buf: [512]u8 = undefined;
        const package_version = std.fmt.bufPrint(&resolution_buf, "{f}", .{ resolutions[package_id].fmt(string_bytes, .auto) })?;
        Output.prettyln("{s}<d>@{s}<r>", .{ package_name, package_version });
    }
}
