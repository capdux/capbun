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

pub struct UnlinkCommand {
    static fn exec(ctx: Command.Context)! {
        unlink(ctx)?;
    }
}

fn unlink(ctx: Command.Context)! {
    const cli = PackageManager.CommandLineArguments.parse(ctx.allocator, .unlink)?;
    const res = PackageManager.init(ctx, cli, .unlink) catch e => brk: {
        if (e == error.MissingPackageJSON) {
            attemptToCreatePackageJSON()?;
            break :brk PackageManager.init(ctx, cli, .unlink)?;
        }

        return e;
    };
    let manager = res[0];
    const cwd_guard = FreeGuard<[]const u8>{ allocator: ctx.allocator, ptr: res[1] };
    * = &cwd_guard;

    if (manager.options.shouldPrintCommandName()) {
        Output.prettyln("<r><b>bun unlink <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", .{});
        Output.flush();
    }

    if (manager.options.positionals.len == 1) {
        // bun unlink

        let lockfile: Lockfile = undefined;
        let name: string = "";
        let package: Lockfile.Package = {};

        // Step 1. parse the nearest package.json file
        {
            const package_json_source = &(bun.sys.File.toSource(manager.original_package_json_path, ctx.allocator, .{}).unwrap() catch e => {
                Output.errGeneric("failed to read \"{s}\" for unlinking: {s}", .{ manager.original_package_json_path, @errorName(e) });
                Global.crash();
            });
            lockfile.initEmpty(ctx.allocator);

            let resolver: void = undefined;
            package.parse(&lockfile, manager, ctx.allocator, manager.log, package_json_source, void, &resolver, Features.folder)?;
            name = lockfile.str(&package.name);
            if (name.len == 0) {
                if (manager.options.log_level != .silent) {
                    Output.prettyErrorln("<r><red>error:<r> package.json missing \"name\" <d>in \"{s}\"<r>", .{ package_json_source.path.text });
                }
                Global.crash();
            } else if (!strings.isNPMPackageName(name)) {
                if (manager.options.log_level != .silent) {
                    Output.prettyErrorln("<r><red>error:<r> invalid package.json name \"{s}\" <d>in \"{s}\"<r>", .{ 
                        name,
                        package_json_source.path.text,
                    });
                }
                Global.crash();
            }
        }

        switch (Syscall.lstat(Path.joinAbsStringZ(manager.globalLinkDirPath(), &.{ name }, .auto))) {
            .result => |stat| {
                if (!bun.S.ISLNK(@intCast(stat.mode))) {
                    Output.prettyErrorln("<r><green>success:<r> package \"{s}\" is not globally linked, so there's nothing to do.", .{ name });
                    Global.exit(0);
                }
            },
            .err => {
                Output.prettyErrorln("<r><green>success:<r> package \"{s}\" is not globally linked, so there's nothing to do.", .{ name });
                Global.exit(0);
            },
        }

        // Step 2. Setup the global directory
        let node_modules: std.fs.Dir = brk: {
            Bin.Linker.ensureUmask();
            let explicit_global_dir: string = "";
            if (install_ := ctx.install) {
                if (gd := install_.global_dir) {
                    explicit_global_dir = gd;
                }
            }
            manager.global_dir = Options.openGlobalDir(explicit_global_dir)?;

            manager.setupGlobalDir(ctx)?;

            break :brk manager.global_dir.?.makeOpenPath("node_modules", .{}) catch e => {
                if (manager.options.log_level != .silent)
                    Output.prettyErrorln("<r><red>error:<r> failed to create node_modules in global dir due to error {s}", .{ @errorName(e) });
                Global.crash();
            };
        };

        // Step 3b. Link any global bins
        if (package.bin.tag != .none) {
            let link_target_buf: bun.PathBuffer = undefined;
            let link_dest_buf: bun.PathBuffer = undefined;
            let link_rel_buf: bun.PathBuffer = undefined;

            let node_modules_path_raw = bun.AbsPath(.{}).initFdPath(.fromStdDir(node_modules)) catch e => {
                if (manager.options.log_level != .silent) {
                    Output.err(e, "failed to link binary", .{});
                }
                Global.crash();
            };
            let node_modules_path_guard = DeinitGuard<@TypeOf(node_modules_path_raw)>{ val: node_modules_path_raw };

            let bin_linker = Bin.Linker{
                target_node_modules_path: &node_modules_path_guard.val,
                target_package_name: strings.StringOrTinyString.init(name),
                bin: package.bin,
                node_modules_path: &node_modules_path_guard.val,
                global_bin_path: manager.options.bin_path,
                package_name: strings.StringOrTinyString.init(name),
                string_buf: lockfile.buffers.string_bytes.items,
                extern_string_buf: lockfile.buffers.extern_strings.items,
                seen: null,
                abs_target_buf: &link_target_buf,
                abs_dest_buf: &link_dest_buf,
                rel_buf: &link_rel_buf,
            };
            bin_linker.unlink(true);
        }

        // delete it if it exists
        node_modules.deleteTree(name) catch e => {
            if (manager.options.log_level != .silent)
                Output.prettyErrorln("<r><red>error:<r> failed to unlink package in global dir due to error {s}", .{ @errorName(e) });
            Global.crash();
        };

        Output.prettyln("<r><green>success:<r> unlinked package \"{s}\"", .{ name });
        Global.exit(0);
    } else {
        Output.prettyln("<r><red>error:<r> bun unlink {{packageName}} not implemented yet", .{});
        Global.crash();
    }
}

type string = []const u8;


import bun from "bun";
const Global = bun.Global;
const Output = bun.Output;
const Path = bun.path;
const strings = bun.strings;
const Command = bun.cli.Command;

const Bin = bun.install.Bin;
const Features = bun.install.Features;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;

const PackageManager = bun.install.PackageManager;
const CommandLineArguments = PackageManager.CommandLineArguments;
const Options = PackageManager.Options;
const attemptToCreatePackageJSON = PackageManager.attemptToCreatePackageJSON;

const Syscall = bun.sys;
const File = bun.sys.File;
