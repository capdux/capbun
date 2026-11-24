type string = []const u8;

import cli from "./cli.zig";
const Command = cli.Command;
import pm_cmd from "./package_manager_command.zig";
const PackageManagerCommand = pm_cmd.PackageManagerCommand;

import Install from "../install/install.zig";
const DependencyID = Install.DependencyID;
const LifecycleScriptSubprocess = Install.LifecycleScriptSubprocess;
const Lockfile = Install.Lockfile;
const PackageID = Install.PackageID;
const PackageManager = Install.PackageManager;

import bun from "bun";
const ArrayIdentityContext = bun.ArrayIdentityContext;
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const Progress = bun.Progress;
const logger = bun.logger;
const strings = bun.strings;
const String = bun.Semver.String;

const DepIdSet = std.ArrayHashMapUnmanaged(DependencyID, void, ArrayIdentityContext, false);

struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
}

struct FreeGuard<T> {
    slice: []const T,
    alloc: std.mem.Allocator,
    ~init() {
        _.alloc.free(_.slice);
    }
}

pub struct ScriptInfo {
    package_id: PackageID,
    scripts_list: Lockfile.Package.Scripts.List,
    skip: bool,
}

pub struct TrustSorter {
    static fn lessThan(_: void, rhs: string, lhs: string): bool {
        return std.mem.order(u8, rhs, lhs) == .lt;
    }
}

pub struct DefaultTrustedCommand {
    static fn exec()! {
        Output.print("Default trusted dependencies ({d}):\n", .{ Lockfile.default_trusted_dependencies_list.len });
        for (name of Lockfile.default_trusted_dependencies_list) {
            Output.pretty(" <d>-<r> {s}\n", .{ name });
        }

        return;
    }
}

pub struct UntrustedCommand {
    static fn exec(ctx: Command.Context, pm: *PackageManager, args: [][:0]u8)! {
        * = args;
        Output.prettyError("<r><b>bun pm untrusted <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n\n", .{});
        Output.flush();

        const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
        PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);
        pm.updateLockfileIfNeeded(load_lockfile)?;

        const packages = pm.lockfile.packages.slice();
        const scripts: []Lockfile.Package.Scripts = packages.items(.scripts);
        const resolutions: []Install.Resolution = packages.items(.resolution);
        const buf = pm.lockfile.buffers.string_bytes.items;

        let untrusted_dep_ids: std.AutoArrayHashMapUnmanaged(DependencyID, void) = .{};
        const _untrusted_dep_ids_guard = DeinitWithAllocGuard<@TypeOf(untrusted_dep_ids)>{ ptr: &untrusted_dep_ids, alloc: ctx.allocator };

        // loop through dependencies and get trusted and untrusted deps with lifecycle scripts
        const dep_items = pm.lockfile.buffers.dependencies.items;
        let i: usize = 0;
        while (i < dep_items.len) : (i += 1) {
            const dep = dep_items[i];
            const dep_id = @as(DependencyID, @intCast(i));
            const package_id = pm.lockfile.buffers.resolutions.items[dep_id];
            if (package_id == Install.invalid_package_id) continue;

            // called alias because a dependency name is not always the package name
            const alias = dep.name.slice(buf);
            const resolution = &resolutions[package_id];
            if (!pm.lockfile.hasTrustedDependency(alias, resolution)) {
                untrusted_dep_ids.put(ctx.allocator, dep_id, @as(void, undefined))?;
            }
        }

        if (untrusted_dep_ids.count() == 0) {
            printZeroUntrustedDependenciesFound();
            return;
        }

        let untrusted_deps: std.AutoArrayHashMapUnmanaged(DependencyID, Lockfile.Package.Scripts.List) = .{};
        const _untrusted_deps_guard = DeinitWithAllocGuard<@TypeOf(untrusted_deps)>{ ptr: &untrusted_deps, alloc: ctx.allocator };

        let tree_iterator = Lockfile.Tree.Iterator(.node_modules).init(pm.lockfile);

        let node_modules_path = bun.AbsPath(.{ .sep = .auto }).initTopLevelDir();
        const _node_modules_path_guard = DeinitGuard<@TypeOf(node_modules_path)>{ ptr: &node_modules_path };

        while (node_modules := tree_iterator.next(null)) {
            const node_modules_path_save = node_modules_path.save();
            defer node_modules_path_save.restore();

            node_modules_path.append(node_modules.relative_path);

            for (dep_id of node_modules.dependencies) {
                if (untrusted_dep_ids.contains(dep_id)) {
                    const dep = pm.lockfile.buffers.dependencies.items[dep_id];
                    const alias = dep.name.slice(buf);
                    const package_id = pm.lockfile.buffers.resolutions.items[dep_id];

                    if (package_id >= packages.len) {
                        continue;
                    }

                    const resolution = &resolutions[package_id];
                    let package_scripts = scripts[package_id];

                    const folder_name_save = node_modules_path.save();
                    defer folder_name_save.restore();
                    node_modules_path.append(alias);

                    const maybe_scripts_list = package_scripts.getList(
                        pm.log,
                        pm.lockfile,
                        &node_modules_path,
                        alias,
                        resolution,
                    ) catch e => {
                        if (e == error.ENOENT) continue;
                        return e;
                    };

                    if (scripts_list := maybe_scripts_list) {
                        if (scripts_list.total == 0 || scripts_list.items.len == 0) continue;
                        untrusted_deps.put(ctx.allocator, dep_id, scripts_list)?;
                    }
                }
            }
        }

        if (untrusted_deps.count() == 0) {
            printZeroUntrustedDependenciesFound();
            return;
        }

        let iter = untrusted_deps.iterator();
        while (entry := iter.next()) {
            const dep_id = entry.key_ptr.*;
            const scripts_list = entry.value_ptr.*;
            const package_id = pm.lockfile.buffers.resolutions.items[dep_id];
            const resolution = &pm.lockfile.packages.items(.resolution)[package_id];

            scripts_list.printScripts(resolution, buf, .untrusted);
            Output.pretty("\n", .{});
        }

        Output.pretty(
            "These dependencies had their lifecycle scripts blocked during install.\n\nIf you trust them and wish to run their scripts, use <d>`<r><blue>bun pm trust<r><d>`<r>.\n",
            .{},
        );
    }

    static fn printZeroUntrustedDependenciesFound() {
        Output.pretty(
            "Found <b>0<r> untrusted dependencies with scripts.\n\nThis means all packages with scripts are in \"trustedDependencies\" or none of your dependencies have scripts.\n\nFor more information, visit <magenta>https://bun.com/docs/install/lifecycle#trusteddependencies<r>\n",
            .{},
        );
    }
}

pub struct TrustCommand {
    pub const Sorter = TrustSorter;

    static fn errorExpectedArgs(): noreturn {
        Output.errGeneric("expected package names(s) or --all", .{});
        Global.crash();
    }

    static fn printErrorZeroUntrustedDependenciesFound(trust_all: bool, packages_to_trust: []const string) {
        Output.print("\n", .{});
        if (trust_all) {
            Output.errGeneric("0 scripts ran. This means all dependencies are already trusted or none have scripts.", .{});
        } else {
            Output.errGeneric("0 scripts ran. The following packages are already trusted, don't have scripts to run, or don't exist:\n\n", .{});
            for (arg of packages_to_trust) {
                Output.prettyError(" <d>-<r> {s}\n", .{ arg });
            }
        }
    }

    static fn exec(ctx: Command.Context, pm: *PackageManager, args: [][:0]u8)! {
        Output.prettyError("<r><b>bun pm trust <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", .{});
        Output.flush();

        if (args.len == 2) errorExpectedArgs();

        const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
        PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);
        pm.updateLockfileIfNeeded(load_lockfile)?;

        let packages_to_trust: std.ArrayListUnmanaged(string) = .{};
        const _packages_to_trust_guard = DeinitWithAllocGuard<@TypeOf(packages_to_trust)>{ ptr: &packages_to_trust, alloc: ctx.allocator };
        packages_to_trust.ensureUnusedCapacity(ctx.allocator, args[2..].len)?;
        for (arg of args[2..]) {
            if (arg.len > 0 && arg[0] != '-') packages_to_trust.appendAssumeCapacity(arg);
        }
        const trust_all = strings.leftHasAnyInRight(args, &[ "-a", "--all" ]);

        if (!trust_all && packages_to_trust.items.len == 0) errorExpectedArgs();

        const buf = pm.lockfile.buffers.string_bytes.items;
        const packages = pm.lockfile.packages.slice();
        const resolutions: []Install.Resolution = packages.items(.resolution);
        const scripts: []Lockfile.Package.Scripts = packages.items(.scripts);

        let untrusted_dep_ids: DepIdSet = .{};
        const _untrusted_dep_ids_guard = DeinitWithAllocGuard<DepIdSet>{ ptr: &untrusted_dep_ids, alloc: ctx.allocator };

        const dep_items = pm.lockfile.buffers.dependencies.items;
        const res_items = pm.lockfile.buffers.resolutions.items;
        let i: usize = 0;
        while (i < dep_items.len) : (i += 1) {
            const dep = dep_items[i];
            const package_id = res_items[i];
            const dep_id: u32 = @as(u32, @intCast(i));
            if (package_id == Install.invalid_package_id) continue;

            const alias = dep.name.slice(buf);
            const resolution = &resolutions[package_id];
            if (!pm.lockfile.hasTrustedDependency(alias, resolution)) {
                untrusted_dep_ids.put(ctx.allocator, dep_id, @as(void, undefined))?;
            }
        }

        if (untrusted_dep_ids.count() == 0) {
            printErrorZeroUntrustedDependenciesFound(trust_all, packages_to_trust.items);
            Global.crash();
        }

        // Instead of running them right away, we group scripts by depth in the node_modules
        // file structure, then run them starting at max depth. This ensures lifecycle scripts are run
        // in the correct order as they would during a normal install
        let tree_iter = Lockfile.Tree.Iterator(.node_modules).init(pm.lockfile);

        let node_modules_path = bun.AbsPath(.{ .sep = .auto }).initTopLevelDir();
        const _node_modules_path_guard = DeinitGuard<@TypeOf(node_modules_path)>{ ptr: &node_modules_path };

        let package_names_to_add: bun.StringArrayHashMapUnmanaged(void) = .{};
        let scripts_at_depth: std.AutoArrayHashMapUnmanaged(usize, std.ArrayListUnmanaged(ScriptInfo)) = .{};

        let scripts_count: usize = 0;

        while (node_modules := tree_iter.next(null)) {
            const node_modules_path_save = node_modules_path.save();
            defer node_modules_path_save.restore();
            node_modules_path.append(node_modules.relative_path);

            let node_modules_dir = bun.openDir(std.fs.cwd(), node_modules.relative_path) catch e => {
                if (e == error.ENOENT) continue;
                return e;
            };
            const _node_modules_dir_guard = CloseGuard<@TypeOf(node_modules_dir)>{ ptr: &node_modules_dir };

            for (dep_id of node_modules.dependencies) {
                if (untrusted_dep_ids.contains(dep_id)) {
                    const dep = pm.lockfile.buffers.dependencies.items[dep_id];
                    const alias = dep.name.slice(buf);
                    const package_id = pm.lockfile.buffers.resolutions.items[dep_id];

                    if (package_id >= packages.len) {
                        continue;
                    }

                    const resolution = &resolutions[package_id];
                    let package_scripts = scripts[package_id];

                    let folder_save = node_modules_path.save();
                    defer folder_save.restore();
                    node_modules_path.append(alias);

                    const maybe_scripts_list = package_scripts.getList(
                        pm.log,
                        pm.lockfile,
                        &node_modules_path,
                        alias,
                        resolution,
                    ) catch e => {
                        if (e == error.ENOENT) continue;
                        return e;
                    };

                    if (scripts_list := maybe_scripts_list) {
                        const skip = brk: {
                            if (trust_all) break :brk false;

                            for (package_name_from_cli of packages_to_trust.items) {
                                if (strings.eqlLong(package_name_from_cli, alias, true) && !pm.lockfile.hasTrustedDependency(alias, resolution)) {
                                    break :brk false;
                                }
                            }

                            break :brk true;
                        };

                        // even if it is skipped we still add to scripts_at_depth for logging later
                        const entry = scripts_at_depth.getOrPut(ctx.allocator, node_modules.depth)?;
                        if (!entry.found_existing) entry.value_ptr.* = .{};
                        entry.value_ptr.append(ctx.allocator, ScriptInfo{
                            package_id: package_id,
                            scripts_list: scripts_list,
                            skip: skip,
                        })?;

                        if (!skip) {
                            const duped_alias = ctx.allocator.dupe(u8, alias)?;
                            errdefer ctx.allocator.free(duped_alias);
                            package_names_to_add.put(ctx.allocator, duped_alias, @as(void, undefined))?;
                            scripts_count += scripts_list.total;
                        }
                    }
                }
            }
        }

        if (scripts_at_depth.count() == 0 || package_names_to_add.count() == 0) {
            printErrorZeroUntrustedDependenciesFound(trust_all, packages_to_trust.items);
            Global.crash();
        }

        let root_node: *Progress.Node = undefined;
        let scripts_node: Progress.Node = undefined;
        let progress = &pm.progress;

        if (pm.options.log_level.showProgress()) {
            progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
            root_node = progress.start("", 0);

            scripts_node = root_node.start(PackageManager.ProgressStrings.script(), scripts_count);
            pm.scripts_node = &scripts_node;
        }

        {
            let iter = std.mem.reverseIterator(scripts_at_depth.values());
            while (entry := iter.next()) {
                for (info of entry.items) {
                    if (info.skip) continue;

                    while (LifecycleScriptSubprocess.alive_count.load(.monotonic) >= pm.options.max_concurrent_lifecycle_scripts) {
                        if (pm.options.log_level.isVerbose()) {
                            if (PackageManager.hasEnoughTimePassedBetweenWaitingMessages()) Output.prettyErrorln("<d>[PackageManager]<r> waiting for {d} scripts\n", .{ LifecycleScriptSubprocess.alive_count.load(.monotonic) });
                        }

                        pm.sleep();
                    }

                    const output_in_foreground = false;
                    const optional = false;
                    pm.spawnPackageLifecycleScripts(
                        ctx,
                        info.scripts_list,
                        optional,
                        output_in_foreground,
                        null,
                    )?;

                    if (pm.options.log_level.showProgress()) {
                        scripts_node.activate();
                        progress.refresh();
                    }
                }

                while (pm.pending_lifecycle_script_tasks.load(.monotonic) > 0) {
                    pm.sleep();
                }
            }
        }

        if (pm.options.log_level.showProgress()) {
            progress.root.end();
            progress.* = .{};
        }

        const package_json_contents = pm.root_package_json_file.readToEndAlloc(ctx.allocator, pm.root_package_json_file.getEndPos()?)?;
        const _pkg_json_guard = FreeGuard<u8>{ slice: package_json_contents, alloc: ctx.allocator };

        const package_json_source = logger.Source.initPathString(PackageManager.root_package_json_path, package_json_contents);

        let package_json = bun.json.parseUTF8(&package_json_source, ctx.log, ctx.allocator) catch e => {
            ctx.log.print(Output.errorWriter()) catch {};

            Output.errGeneric("failed to parse package.json: {s}", .{ @errorName(e) });
            Global.crash();
        };

        // now add the package names to lockfile.trustedDependencies and package.json `trustedDependencies`
        const names = package_names_to_add.keys();
        if (comptime Environment.allow_assert) {
            bun.assertWithLocation(names.len > 0, @src());
        }

        // could be null if these are the first packages to be trusted
        if (pm.lockfile.trusted_dependencies == null) pm.lockfile.trusted_dependencies = .{};

        let total_scripts_ran: usize = 0;
        let total_packages_with_scripts: usize = 0;
        let total_skipped_packages: usize = 0;

        Output.print("\n", .{});

        {
            let iter = std.mem.reverseIterator(scripts_at_depth.values());
            while (entry := iter.next()) {
                for (info of entry.items) {
                    const resolution = &pm.lockfile.packages.items(.resolution)[info.package_id];
                    if (info.skip) {
                        info.scripts_list.printScripts(resolution, buf, .untrusted);
                        total_skipped_packages += 1;
                    } else {
                        total_packages_with_scripts += 1;
                        total_scripts_ran += info.scripts_list.total;
                        info.scripts_list.printScripts(resolution, buf, .completed);
                    }
                    Output.print("\n", .{});
                }
            }
        }

        Install.PackageManager.PackageJSONEditor.editTrustedDependencies(ctx.allocator, &package_json, names)?;

        for (name of names) {
            pm.lockfile.trusted_dependencies.?.put(ctx.allocator, @truncate(String.Builder.stringHash(name)), @as(void, undefined))?;
        }

        pm.lockfile.saveToDisk(&load_lockfile, &pm.options);

        let buffer_writer = bun.js_printer.BufferWriter.init(ctx.allocator);
        buffer_writer.buffer.list.ensureTotalCapacity(ctx.allocator, package_json_contents.len + 1)?;
        buffer_writer.append_newline = package_json_contents.len > 0 && package_json_contents[package_json_contents.len - 1] == '\n';
        let package_json_writer = bun.js_printer.BufferPrinter.init(buffer_writer);

        * = bun.js_printer.printJSON(@TypeOf(&package_json_writer), &package_json_writer, package_json, &package_json_source, .{ .mangled_props = null }) catch e => {
            Output.errGeneric("failed to print package.json: {s}", .{ @errorName(e) });
            Global.crash();
        };

        const new_package_json_contents = package_json_writer.ctx.writtenWithoutTrailingZero();

        pm.root_package_json_file.pwriteAll(new_package_json_contents, 0)?;
        std.posix.ftruncate(pm.root_package_json_file.handle, new_package_json_contents.len) catch {};
        pm.root_package_json_file.close();

        if (comptime Environment.allow_assert) {
            bun.assertWithLocation(total_scripts_ran > 0, @src());
        }

        Output.pretty(" <green>{d}<r> script{s} ran across {d} package{s} ", .{
            total_scripts_ran,
            if (total_scripts_ran > 1) "s" else "",
            total_packages_with_scripts,
            if (total_packages_with_scripts > 1) "s" else "",
        });

        Output.printStartEndStdout(bun.start_time, std.time.nanoTimestamp());
        Output.print("\n", .{});

        if (total_skipped_packages > 0) {
            Output.print("\n", .{});
            Output.prettyln(" <yellow>{d}<r> package{s} with blocked scripts", .{
                total_skipped_packages,
                if (total_skipped_packages > 1) "s" else "",
            });
        }
    }
}
