type string = []const u8;

import Dependency from "../install/dependency.zig";
import Fs from "../resolver/fs.zig";
import Lockfile from "../install/lockfile.zig";
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

pub struct ByName {
    dependencies: []const Dependency,
    buf: []const u8,

    static fn isLessThan(ctx: ByName, lhs: DependencyID, rhs: DependencyID): bool {
        return strings.cmpStringsAsc(
            .{},
            ctx.dependencies[lhs].name.slice(ctx.buf),
            ctx.dependencies[rhs].name.slice(ctx.buf),
        );
    }
}

pub struct PackageManagerCommand {
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
        defer ctx.allocator.free(cwd);

        const bytes = file.readToEnd(ctx.allocator).unwrap() catch |e| {
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
                subcommand = args[0];
            }
        }

        if (subcommand.len > 0) {
            args = args[1..];
        }

        return subcommand;
    }

    pub static fn exec(ctx: Command.Context, is_direct_whoami: bool)! {
        let args: [][:0]u8 = ctx.args.positionals;
        if (args.len > 0 && strings.eqlComptime(args[0], "pm")) {
            args = args[1..];
        }

        if (args.len > 0 && strings.eqlComptime(args[0], "hash")) {
            if (args.len > 1) {
                const filepath = args[1];
                let file = std.fs.cwd().openFile(filepath, .{}) catch |e| {
                    Output.err(e, "failed to open {s}", .{ filepath });
                    Global.crash();
                };
                defer file.close();
                printHash(ctx, file)?;
                return;
            }
        }


        const cli_args = PackageManager.CommandLineArguments.parse(ctx.allocator, .pm)?;
        const init_res = PackageManager.init(ctx, cli_args, PackageManager.Subcommand.pm) catch |e| {
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
        defer ctx.allocator.free(cwd);

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
            const username = Npm.whoami(ctx.allocator, pm) catch |e| {
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
            const outpath = bun.getFdPath(.fromStdDir(fd), &dir) catch |e| {
                Output.prettyErrorln("{s} getting cache directory", .{ @errorName(e) });
                Global.crash();
            };

            if (pm.options.positionals.len > 1 && strings.eqlComptime(pm.options.positionals[1], "rm")) {
                fd.close();

                let had_err = false;

                std.fs.deleteTreeAbsolute(outpath) catch |e| {
                    Output.err(e, "Could not delete {s}", .{ outpath });
                    had_err = true;
                };
                Output.prettyln("Cleared 'bun install' cache", .{});

                bunx: {
                    const tmp = bun.fs.FileSystem.RealFS.platformTempDir();
                    let tmp_dir_opt = std.fs.openDirAbsolute(tmp, .{ .iterate = true }) catch |e| {
                        Output.err(e, "Could not open {s}", .{ tmp });
                        had_err = true;
                        null
                    };
                    if (tmp_dir_opt == null) break :bunx;
                    let tmp_dir = tmp_dir_opt.?;
                    defer tmp_dir.close();
                    let iter = tmp_dir.iterate();

                    // This is to match 'bunx_command.BunxCommand.exec's logic
                    const prefix = std.fmt.allocPrint(ctx.allocator, "bunx-{d}-", .{
                        if (bun.Environment.isPosix) bun.c.getuid() else bun.windows.userUniqueId(),
                    })?;
                    defer ctx.allocator.free(prefix);

                    let deleted: usize = 0;
                    while (entry := iter.next() catch null) {
                        if (std.mem.startsWith(u8, entry.name, prefix)) {
                            tmp_dir.deleteTree(entry.name) catch |e| {
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
            UntrustedCommand.exec(ctx, pm, args)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "trust")) {
            TrustCommand.exec(ctx, pm, args)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "ls")) {
            const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
            PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

            let lockfile = &load_lockfile.ok.lockfile;
            const resolutions = lockfile.packages.items(.resolution);
            const string_bytes = lockfile.buffers.string_bytes.items;

            Output.flush();

            if (pm.options.all) {
                let directories = Lockfile.Tree.Iterator(.node_modules).init(lockfile).list(ctx.allocator) catch |e| {
                    Output.err(e, "Failed to list dependencies", .{});
                    Global.crash();
                };
                defer directories.deinit();

                if (directories.items.len == 0) {
                    Global.exit(0);
                }

                const first_directory = directories.orderedRemove(0);
                let more_packages = ctx.allocator.alloc(bool, directories.items.len + 1)?;
                defer ctx.allocator.free(more_packages);
                @memset(more_packages, true);
                printNodeModulesFolderStructure(&first_directory, null, 0, &directories, lockfile, more_packages)?;
            } else {
                let tree_iter = Lockfile.Tree.Iterator(.node_modules).init(lockfile);
                const first_directory = tree_iter.next(null) orelse Global.exit(0);

                let cwd_buf: bun.PathBuffer = undefined;
                const path = bun.getcwd(&cwd_buf) catch {
                    Output.prettyErrorln("<r><red>error<r>: Could not get current working directory", .{});
                    Global.exit(1);
                };
                Output.println("{s} node_modules", .{ path });

                const dependencies = lockfile.buffers.dependencies.items;
                const sorted_dependencies = ctx.allocator.alloc(DependencyID, first_directory.dependencies.len)?;
                defer ctx.allocator.free(sorted_dependencies);
                bun.copy(DependencyID, sorted_dependencies, first_directory.dependencies);
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
                        \\<r><red>error<r>: bun.lock already exists
                        \\run with --force to overwrite
                    , .{});
                    Global.exit(1);
                }

                if (bun.sys.existsZ("bun.lockb")) {
                    Output.prettyErrorln(
                        \\<r><red>error<r>: bun.lockb already exists
                        \\run with --force to overwrite
                    , .{});
                    Global.exit(1);
                }
            }

            pm.options.enable.save_lockfile = true;
            pm.options.enable.force_save_lockfile = true;

            const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
            PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

            pm.lockfile.saveToDisk(&load_lockfile, &pm.options);
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "version")) {
            PmVersionCommand.exec(ctx, pm, pm.options.positionals, cwd)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "why")) {
            PmWhyCommand.exec(ctx, pm, pm.options.positionals)?;
            Global.exit(0);
        } else if (strings.eqlComptime(subcommand, "pkg")) {
            PmPkgCommand.exec(ctx, pm, pm.options.positionals, cwd)?;
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
    defer allocator.free(sorted_dependencies);
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
        defer allocator.free(possible_path);

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
