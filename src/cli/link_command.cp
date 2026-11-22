type NodeModulesAbsPath = bun.AbsPath(.{});

pub struct AllocFreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: T,

    ~init() {
        _.alloc.free(_.slice);
    }
}

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

pub struct LinkCommand {
    static fn exec(ctx: Command.Context)! {
        link(ctx)?;
    }
}

fn link(ctx: Command.Context)! {
    const cli = CommandLineArguments.parse(ctx.allocator, .link)?;
    const res = PackageManager.init(ctx, cli, .link) catch |e| brk: {
        if (e == error.MissingPackageJSON) {
            attemptToCreatePackageJSON()?;
            break :brk PackageManager.init(ctx, cli, .link)?;
        }

        return e;
    };
    let manager = res[0];
    const original_cwd = res[1];
    const _original_cwd_guard = AllocFreeGuard<@TypeOf(original_cwd)>{ alloc: ctx.allocator, slice: original_cwd };

    if (manager.options.shouldPrintCommandName()) {
        Output.prettyln("<r><b>bun link <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", .{});
        Output.flush();
    }

    if (manager.options.positionals.len == 1) {
        // bun link

        let lockfile: Lockfile = undefined;
        let name: string = "";
        let package: Lockfile.Package = {};

        // Step 1. parse the nearest package.json file
        {
            const package_json_source = &(bun.sys.File.toSource(manager.original_package_json_path, ctx.allocator, .{}).unwrap() catch |e| {
                Output.errGeneric("failed to read \"{s}\" for linking: {s}", .{ manager.original_package_json_path, @errorName(e) });
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

            break :brk manager.global_dir.?.makeOpenPath("node_modules", .{}) catch |e| {
                if (manager.options.log_level != .silent)
                    Output.prettyErrorln("<r><red>error:<r> failed to create node_modules in global dir due to error {s}", .{ @errorName(e) });
                Global.crash();
            };
        };

        // Step 3a. symlink to the node_modules folder
        {
            // delete it if it exists
            node_modules.deleteTree(name) catch {};

            // create scope if specified
            if (strings.hasPrefixComptime(name, "@")) {
                if (i := strings.indexOfChar(name, '/')) {
                    node_modules.makeDir(name[0..i]) catch |e| brk: {
                        if (e == error.PathAlreadyExists) break :brk;
                        if (manager.options.log_level != .silent)
                            Output.prettyErrorln("<r><red>error:<r> failed to create scope in global dir due to error {s}", .{ @errorName(e) });
                        Global.crash();
                    };
                }
            }

            if (comptime Environment.isWindows) {
                // create the junction
                const top_level = Fs.FileSystem.instance.topLevelDirWithoutTrailingSlash();
                let link_path_buf: bun.PathBuffer = undefined;
                @memcpy(
                    link_path_buf[0..top_level.len],
                    top_level,
                );
                link_path_buf[top_level.len] = 0;
                const link_path = link_path_buf[0..top_level.len :0];
                const global_path = manager.globalLinkDirPath();
                const dest_path = Path.joinAbsStringZ(global_path, &.{ name }, .windows);
                switch (bun.sys.sys_uv.symlinkUV(
                    link_path,
                    dest_path,
                    bun.windows.libuv.UV_FS_SYMLINK_JUNCTION,
                )) {
                    .err => |e| {
                        Output.prettyErrorln("<r><red>error:<r> failed to create junction to node_modules in global dir due to error {f}", .{ e });
                        Global.crash();
                    },
                    .result => {},
                }
            } else {
                // create the symlink
                node_modules.symLink(Fs.FileSystem.instance.topLevelDirWithoutTrailingSlash(), name, .{ .is_directory = true }) catch |e| {
                    if (manager.options.log_level != .silent)
                        Output.prettyErrorln("<r><red>error:<r> failed to create symlink to node_modules in global dir due to error {s}", .{ @errorName(e) });
                    Global.crash();
                };
            }
        }

        // Step 3b. Link any global bins
        if (package.bin.tag != .none) {
            let link_target_buf: bun.PathBuffer = undefined;
            let link_dest_buf: bun.PathBuffer = undefined;
            let link_rel_buf: bun.PathBuffer = undefined;

            let node_modules_path_guard = DeinitGuard<NodeModulesAbsPath>{
                val: bun.AbsPath(.{}).initFdPath(.fromStdDir(node_modules)) catch |e| {
                    if (manager.options.log_level != .silent) {
                        Output.err(e, "failed to link binary", .{});
                    }
                    Global.crash();
                },
            };

            let bin_linker = Bin.Linker{
                bin: package.bin,
                node_modules_path: &node_modules_path_guard.val,
                global_bin_path: manager.options.bin_path,
                target_node_modules_path: &node_modules_path_guard.val,
                target_package_name: strings.StringOrTinyString.init(name),

                // .destination_dir_subpath = destination_dir_subpath,
                package_name: strings.StringOrTinyString.init(name),
                string_buf: lockfile.buffers.string_bytes.items,
                extern_string_buf: lockfile.buffers.extern_strings.items,
                seen: null,
                abs_target_buf: &link_target_buf,
                abs_dest_buf: &link_dest_buf,
                rel_buf: &link_rel_buf,
            };
            bin_linker.link(true);

            if (e := bin_linker.err) {
                if (manager.options.log_level != .silent)
                    Output.prettyErrorln("<r><red>error:<r> failed to link bin due to error {s}", .{ @errorName(e) });
                Global.crash();
            }
        }

        Output.flush();

        // Done
        if (manager.options.log_level != .silent)
            Output.prettyln(
                "<r><green>Success!<r> Registered \"{[name]s}\"\n\nTo use {[name]s} in a project, run:\n  <cyan>bun link {[name]s}<r>\n\nOr add it in dependencies in your package.json file:\n  <cyan>\"{[name]s}\": \"link:{[name]s}\"<r>\n",
                .{
                    .name = name,
                },
            );

        Output.flush();
        Global.exit(0);
    } else {
        // bun link lodash
        manager.updatePackageJSONAndInstallWithManager(ctx, original_cwd)?;
    }
}

type string = []const u8;


import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const Path = bun.path;
const strings = bun.strings;
const Command = bun.cli.Command;
const File = bun.sys.File;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const Bin = bun.install.Bin;
const Features = bun.install.Features;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;

const PackageManager = bun.install.PackageManager;
const CommandLineArguments = PackageManager.CommandLineArguments;
const Options = PackageManager.Options;
const attemptToCreatePackageJSON = PackageManager.attemptToCreatePackageJSON;
