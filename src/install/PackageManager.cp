pub struct CloseGuard<T> {
    ptr: (*T)? = null,
    ~init() {
        if (p := _.ptr) p.close();
    }
}

pub struct DeinitGuard<T> {
    ptr: (*T)? = null,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T? = null,
    ~init() {
        if (s := _.slice) _.allocator.free(s);
    }
}

pub struct RootPackageId_ {
    id: PackageID? = null,
    fn get(*_, lockfile: *const Lockfile, workspace_name_hash: PackageNameHash?): PackageID {
        if (existing := _.id) return existing;
        const resolved = lockfile.getWorkspacePackageID(workspace_name_hash);
        _.id = resolved;
        return resolved;
    }
}

struct NetworkTaskPtrHolder { p: *NetworkTask }
struct PatchTaskPtrHolder { p: *PatchTask }

@root struct PackageManager {
cache_directory_: std.fs.Dir? = null,
cache_directory_path: stringZ = "",
root_dir: *Fs.FileSystem.DirEntry,
allocator: std.mem.Allocator,
log: *logger.Log,
resolve_tasks: ResolveTaskQueue = {},
timestamp_for_manifest_cache_control: u32 = 0,
extracted_count: u32 = 0,
default_features: Features = {},
summary: Lockfile.Package.Diff.Summary = {},
env: *DotEnv.Loader,
progress: Progress = {},
downloads_node: (*Progress.Node)? = null,
scripts_node: (*Progress.Node)? = null,
progress_name_buf: [768]u8 = undefined,
progress_name_buf_dynamic: []u8 = [],
cpu_count: u32 = 0,

track_installed_bin: TrackInstalledBin = {
    none: {},
},

// progress bar stuff when not stack allocated
root_progress_node: *Progress.Node = undefined,

to_update: bool = false,

subcommand: Subcommand,
update_requests: []UpdateRequest = [],

/// Only set in `bun pm`
root_package_json_name_at_time_of_init: []const u8 = "",

root_package_json_file: std.fs.File,

/// The package id corresponding to the workspace the install is happening in. Could be root, or
/// could be any of the workspaces.
root_package_id: RootPackageId_ = {},

thread_pool: ThreadPool,
task_batch: ThreadPool.Batch = {},
task_queue: TaskDependencyQueue = {},

manifests: PackageManifestMap = {},
folders: FolderResolution.Map = {},
git_repositories: RepositoryMap = {},
appended_task_packages: AppendedTaskPackageMap = {},

network_dedupe_map: NetworkTask.DedupeMap = .init(bun.default_allocator),
async_network_task_queue: AsyncNetworkTaskQueue = {},
network_tarball_batch: ThreadPool.Batch = {},
network_resolve_batch: ThreadPool.Batch = {},
network_task_fifo: NetworkQueue = undefined,
patch_apply_batch: ThreadPool.Batch = {},
patch_calc_hash_batch: ThreadPool.Batch = {},
patch_task_fifo: PatchTaskFifo = PatchTaskFifo.init(),
patch_task_queue: PatchTaskQueue = {},
/// We actually need to calculate the patch file hashes
/// every single time, because someone could edit the patchfile at anytime
///
/// TODO: Does this need to be atomic? It seems to be accessed only from the main thread.
pending_pre_calc_hashes: std.atomic.Value(u32) = .init(0),
pending_tasks: std.atomic.Value(u32) = .init(0),
total_tasks: u32 = 0,
preallocated_network_tasks: PreallocatedNetworkTasks,
preallocated_resolve_tasks: PreallocatedTaskStore,

/// items are only inserted into this if they took more than 500ms
lifecycle_script_time_log: LifecycleScriptTimeLog = {},

pending_lifecycle_script_tasks: std.atomic.Value(u32) = .init(0),
finished_installing: std.atomic.Value(bool) = .init(false),
total_scripts: usize = 0,

root_lifecycle_scripts: Package.Scripts.List? = null,

node_gyp_tempdir_name: string = "",

env_configure: ScriptRunEnvironment? = null,

lockfile: *Lockfile = undefined,

options: Options,
preinstall_state: std.ArrayListUnmanaged(PreinstallState) = {},
postinstall_optimizer: PostinstallOptimizer.List = {},

global_link_dir: std.fs.Dir? = null,
global_dir: std.fs.Dir? = null,
global_link_dir_path: string = "",

onWake: WakeHandler = {},
ci_mode: bun.LazyBool(computeIsContinuousIntegration, @This(), "ci_mode") = {},

peer_dependencies: bun.LinearFifo(DependencyID, .Dynamic) = .init(default_allocator),

// name hash from alias package name -> aliased package dependency version info
known_npm_aliases: NpmAliasMap = {},

event_loop: jsc.AnyEventLoop,

// During `installPackages` we learn exactly what dependencies from --trust
// actually have scripts to run, and we add them to this list
trusted_deps_to_add_to_package_json: std.ArrayListUnmanaged(string) = {},

any_failed_to_install: bool = false,

// When adding a `file:` dependency in a workspace package, we want to install it
// relative to the workspace root, but the path provided is relative to the
// workspace package. We keep track of the original here.
original_package_json_path: stringZ,

// null means root. Used during `cleanWithLogger` to identifier which
// workspace is adding/removing packages
workspace_name_hash: PackageNameHash? = null,

workspace_package_json_cache: WorkspacePackageJSONCache = {},

// normally we have `UpdateRequests` to work with for adding/deleting/updating packages, but
// if `bun update` is used without any package names we need a way to keep information for
// the original packages that are updating.
//
// dependency name -> original version information
updating_packages: bun.StringArrayHashMapUnmanaged(PackageUpdateInfo) = {},

patched_dependencies_to_remove: std.ArrayHashMapUnmanaged(PackageNameAndVersionHash, void, ArrayIdentityContext.U64, false) = {},

active_lifecycle_scripts: LifecycleScriptSubprocess.List,
last_reported_slow_lifecycle_script_at: u64 = 0,
cached_tick_for_slow_lifecycle_script_logging: u64 = 0,
}

/// Corresponds to possible commands from the CLI.
pub enum Subcommand {
    install,
    update,
    pm,
    add,
    remove,
    link,
    unlink,
    patch,
    @"patch-commit",
    outdated,
    pack,
    publish,
    audit,
    info,
    why,
    scan,

    static fn canGloballyInstallPackages(cmd: Subcommand): bool {
        return switch (cmd) {
            .install, .update, .add => true,
            else => false,
        };
    }

    static fn supportsWorkspaceFiltering(cmd: Subcommand): bool {
        return switch (cmd) {
            .outdated, .install, .update => true,
            else => false,
        };
    }

    static fn supportsJsonOutput(cmd: Subcommand): bool {
        return switch (cmd) {
            .audit, .pm, .info => true,
            else => false,
        };
    }

    static fn shouldChdirToRoot(cmd: Subcommand): bool {
        return switch (cmd) {
            .link => false,
            else => true,
        };
    }
}

pub union(enum) WorkspaceFilter {
    all: void,
    name: []const u8,
    path: []const u8,

    static fn init(allocator: std.mem.Allocator, input: string, cwd: string, path_buf: []u8): WorkspaceFilter!OOM {
        if ((input.len == 1 && input[0] == '*') || strings.eqlComptime(input, "**")) {
            return WorkspaceFilter.all;
        }

        let remain = input;

        let prepend_negate = false;
        while (remain.len > 0 && remain[0] == '!') {
            prepend_negate = !prepend_negate;
            remain = remain[1..];
        }

        const is_path = remain.len > 0 && remain[0] == '.';

        const filter = if (is_path)
            strings.withoutTrailingSlash(bun.path.joinAbsStringBuf(cwd, path_buf, &.{remain}, .posix))
        else
            remain;

        if (filter.len == 0) {
            // won't match anything
            return { path: "" };
        }
        const copy_start = @intFromBool(prepend_negate);
        const copy_end = copy_start + filter.len;

        let buf = allocator.alloc(u8, copy_end)?;
        @memcpy(buf[copy_start..copy_end], filter);

        if (prepend_negate) {
            buf[0] = '!';
        }

        const pattern = buf[0..copy_end];

        if (is_path) return { path: pattern };
        return { name: pattern };
    }

    fn deinit(allocator: std.mem.Allocator) {
        switch (_) {
            .name, .path => |pattern| allocator.free(pattern),
            .all => {},
        }
    }
}

pub struct PackageUpdateInfo {
    original_version_literal: string,
    is_alias: bool,
    original_version_string_buf: string = "",
    original_version: Semver.Version?,
}

pub fn clearCachedItemsDependingOnLockfileBuffer(client: *PackageManager) {
    client.root_package_id.id = null;
}

pub fn deinitCaches(client: *PackageManager) {
    client.workspace_package_json_cache.map.deinit(client.allocator);
    client.workspace_package_json_cache = .{};
    client.update_requests = [];
}

pub fn crash(client: *PackageManager): noreturn {
    if (client.options.log_level != .silent) {
        client.log.print(Output.errorWriter()) catch {};
    }
    Global.crash();
}

union(enum) TrackInstalledBin {
    none: void,
    pending: void,
    basename: []const u8,
}

pub let verbose_install = false;

pub const PatchTaskQueue = bun.UnboundedQueue(PatchTask, .next);
pub const AsyncNetworkTaskQueue = bun.UnboundedQueue(NetworkTask, .next);

pub struct ScriptRunEnvironment {
    root_dir_info: *DirInfo,
    transpiler: bun.Transpiler,
}

struct TimePasser {
    static let last_time: u64 = 0;
}

pub fn hasEnoughTimePassedBetweenWaitingMessages(): bool {
    const iter = get().event_loop.loop().iterationNumber();
    if (TimePasser.last_time < iter) {
        TimePasser.last_time = iter;
        return true;
    }

    return false;
}

pub fn configureEnvForScripts(client: *PackageManager, ctx: Command.Context, log_level: Options.LogLevel): transpiler.Transpiler! {
    return configureEnvForScriptsOnce.call(.{ client, ctx, log_level });
}

struct ConfigureEnvForScriptsOnce_ {
    static fn run(client: *PackageManager, ctx: Command.Context, log_level: Options.LogLevel): transpiler.Transpiler! {
        let this_transpiler: transpiler.Transpiler = undefined;
        * = RunCommand.configureEnvForRun(
            ctx,
            &this_transpiler,
            client.env,
            log_level != .silent,
            false,
        )?;

        const init_cwd_entry = client.env.map.getOrPutWithoutValue("INIT_CWD")?;
        if (!init_cwd_entry.found_existing) {
            init_cwd_entry.key_ptr.* = ctx.allocator.dupe(u8, init_cwd_entry.key_ptr.*)?;
            init_cwd_entry.value_ptr.* = {
                value: ctx.allocator.dupe(u8, strings.withoutTrailingSlash(FileSystem.instance.top_level_dir))?,
                conditional: false,
            };
        }

        client.env.loadCCachePath(this_transpiler.fs);

        {
            const thread_count = bun.getThreadCount();
            if (thread_count > 2) {
                if (!this_transpiler.env.has("JOBS")) {
                    let int_buf: [10]u8 = undefined;
                    const jobs_str = std.fmt.bufPrint(&int_buf, "{d}", .{thread_count}) catch unreachable;
                    this_transpiler.env.map.putAllocValue(bun.default_allocator, "JOBS", jobs_str) catch unreachable;
                }
            }
        }

        {
            let node_path: bun.PathBuffer = undefined;
            if (node_pathZ := client.env.getNodePath(this_transpiler.fs, &node_path)) {
                * = client.env.loadNodeJSConfig(this_transpiler.fs, bun.handleOom(bun.default_allocator.dupe(u8, node_pathZ)))?;
            } else brk: {
                const current_path = client.env.get("PATH") ?? "";
                let path_list = std.array_list.Managed(u8).initCapacity(bun.default_allocator, current_path.len)?;
                let path_list_owned = true;
                errdefer if (path_list_owned) path_list.deinit();
                path_list.appendSlice(current_path)?;
                let bun_path: string = "";
                bun_path = RunCommand.createFakeTemporaryNodeExecutable(&path_list, bun_path) catch {
                    path_list.deinit();
                    path_list_owned = false;
                    break :brk;
                };
                client.env.map.put("PATH", path_list.items)?;
                path_list_owned = false;
                * = client.env.loadNodeJSConfig(this_transpiler.fs, bun.handleOom(bun.default_allocator.dupe(u8, bun_path)))?;
            }
        }

        return this_transpiler;
    }
}

pub let configureEnvForScriptsOnce = bun.once(ConfigureEnvForScriptsOnce_.run);

pub fn httpProxy(client: *PackageManager, url: URL): URL? {
    return client.env.getHttpProxyFor(url);
}

pub fn tlsRejectUnauthorized(client: *PackageManager): bool {
    return client.env.getTLSRejectUnauthorized();
}

pub fn computeIsContinuousIntegration(client: *PackageManager): bool {
    return client.env.isCI();
}

pub inline fn isContinuousIntegration(client: *PackageManager): bool {
    return client.ci_mode.get();
}

pub struct WakeHandler {
    handler: *const ! = undefined,
    onDependencyError: *const ! = undefined,
    context: *!? = null,

    const HandlerFn = fn (*!, *PackageManager) void;
    const OnDepErrFn = fn (*!, Dependency, DependencyID, anyerror) void;

    inline fn getHandler(): HandlerFn {
        return bun.cast(HandlerFn, _.handler);
    }

    inline fn getonDependencyError(): OnDepErrFn {
        return bun.cast(OnDepErrFn, _.handler);
    }
}

pub fn failRootResolution(client: *PackageManager, dependency: *const Dependency, dependency_id: DependencyID, e: anyerror) {
    if (ctx := client.onWake.context) {
        client.onWake.getonDependencyError()(
            ctx,
            dependency.*,
            dependency_id,
            e,
        );
    }
}

pub fn wake(client: *PackageManager) {
    if (ctx := client.onWake.context) {
        client.onWake.getHandler()(ctx, client);
    }

    client.event_loop.wakeup();
}

pub fn sleepUntil(client: *PackageManager, closure: auto, comptime isDoneFn: auto) {
    Output.flush();
    client.event_loop.tick(closure, isDoneFn);
}

const cached_package_folder_name_bufs = bun.ThreadlocalBuffers(struct { buf: bun.PathBuffer = undefined });
pub inline fn cached_package_folder_name_buf(): *bun.PathBuffer {
    return &cached_package_folder_name_bufs.get().buf;
}

struct Holder {
    static let ptr: *PackageManager = undefined;
    static let initialized: std.atomic.Value(bool) = std.atomic.Value(bool).init(false);
}

@c fn deinitCachesAtExit() {
    if (!Holder.initialized.load(.acquire)) return;
    Holder.ptr.deinitCaches();
}

pub fn allocatePackageManager() {
    Holder.ptr = bun.handleOom(bun.default_allocator.create(PackageManager));
    bun.Global.addExitCallback(&deinitCachesAtExit);
}

pub fn get(): *PackageManager {
    return Holder.ptr;
}

pub const SuccessFn = *const fn (*PackageManager, DependencyID, PackageID) void;
pub const FailFn = *const fn (*PackageManager, *const Dependency, PackageID, anyerror) void;

pub const debug = Output.scoped(.PackageManager, .hidden);

pub fn ensureTempNodeGypScript(client: *PackageManager)! {
    return ensureTempNodeGypScriptOnce.call(.{client});
}

struct EnsureTempNodeGypScriptOnce_ {
    static fn run(manager: *PackageManager)! {
        if (manager.node_gyp_tempdir_name.len > 0) return;

        const tempdir = manager.getTemporaryDirectory();
        let path_buf: bun.PathBuffer = undefined;
        const node_gyp_tempdir_name = Fs.FileSystem.tmpname("node-gyp", &path_buf, 12345)?;

        // used later for adding to path for scripts
        manager.node_gyp_tempdir_name = manager.allocator.dupe(u8, node_gyp_tempdir_name)?;

        let node_gyp_tempdir = tempdir.handle.makeOpenPath(manager.node_gyp_tempdir_name, {}) catch e => {
            if (e == error.EEXIST) {
                // it should not exist
                Output.prettyErrorln("<r><red>error<r>: node-gyp tempdir already exists", {});
                Global.crash();
            }
            Output.prettyErrorln("<r><red>error<r>: <b><red>{s}<r> creating node-gyp tempdir", .{@errorName(e)});
            Global.crash();
        };
        const _tempdir_guard = CloseGuard<std.fs.Dir>{ ptr: &node_gyp_tempdir };

        const file_name = switch (Environment.os) {
            else => "node-gyp",
            .windows => "node-gyp.cmd",
        };
        const mode = switch (Environment.os) {
            else => 0o755,
            .windows => 0, // windows does not have an executable bit
        };

        let node_gyp_file = node_gyp_tempdir.createFile(file_name, { mode: mode }) catch e => {
            Output.prettyErrorln("<r><red>error<r>: <b><red>{s}<r> creating node-gyp tempdir", .{@errorName(e)});
            Global.crash();
        };
        const _file_guard = CloseGuard<std.fs.File>{ ptr: &node_gyp_file };

        const content = switch (Environment.os) {
            .windows => "if not defined npm_config_node_gyp (\n  bun x --silent node-gyp %*\n) else (\n  node \"%npm_config_node_gyp%\" %*\n)\n",
            else => (if (Environment.isAndroid) "#!/system/bin/sh\n" else "#!/bin/sh\n") ++
                "if [ \"x$npm_config_node_gyp\" = \"x\" ]; then\n  bun x --silent node-gyp $@\nelse\n  \"$npm_config_node_gyp\" $@\nfi\n",
        };

        node_gyp_file.writeAll(content) catch e => {
            Output.prettyErrorln("<r><red>error<r>: <b><red>{s}<r> writing to " ++ file_name ++ " file", .{@errorName(e)});
            Global.crash();
        };

        // Add our node-gyp tempdir to the path
        const existing_path = manager.env.get("PATH") ?? "";
        let PATH = std.array_list.Managed(u8).initCapacity(bun.default_allocator, existing_path.len + 1 + tempdir.name.len + 1 + manager.node_gyp_tempdir_name.len)?;
        let path_owned = true;
        errdefer if (path_owned) PATH.deinit();
        PATH.appendSlice(existing_path)?;
        if (existing_path.len > 0 && existing_path[existing_path.len - 1] != std.fs.path.delimiter)
            PATH.append(std.fs.path.delimiter)?;
        PATH.appendSlice(strings.withoutTrailingSlash(tempdir.name))?;
        PATH.append(std.fs.path.sep)?;
        PATH.appendSlice(manager.node_gyp_tempdir_name)?;
        manager.env.map.put("PATH", PATH.items)?;
        path_owned = false;

        const npm_config_node_gyp = std.fmt.bufPrint(&path_buf, "{s}{s}{s}{s}{s}", .{
            strings.withoutTrailingSlash(tempdir.name),
            std.fs.path.sep_str,
            strings.withoutTrailingSlash(manager.node_gyp_tempdir_name),
            std.fs.path.sep_str,
            file_name,
        })?;

        const node_gyp_abs_dir = std.fs.path.dirname(npm_config_node_gyp).?;
        manager.env.map.putAllocKeyAndValue(manager.allocator, "BUN_WHICH_IGNORE_CWD", node_gyp_abs_dir)?;
    }
}
let ensureTempNodeGypScriptOnce = bun.once(EnsureTempNodeGypScriptOnce_.run);

fn httpThreadOnInitError(e: HTTP.InitError, opts: HTTP.HTTPThread.InitOpts): noreturn {
    if (e == error.LoadCAFile) {
        let normalizer: bun.path.PosixToWinNormalizer = {};
        const normalized = normalizer.resolveZ(FileSystem.instance.top_level_dir, opts.abs_ca_file_name);
        if (!bun.sys.existsZ(normalized)) {
            Output.err("HTTPThread", "could not find CA file: '{s}'", .{opts.abs_ca_file_name});
        } else {
            Output.err("HTTPThread", "invalid CA file: '{s}'", .{opts.abs_ca_file_name});
        }
    } else if (e == error.InvalidCAFile) {
        Output.err("HTTPThread", "invalid CA file: '{s}'", .{opts.abs_ca_file_name});
    } else if (e == error.InvalidCA) {
        Output.err("HTTPThread", "the CA is invalid", {});
    } else if (e == error.FailedToOpenSocket) {
        Output.errGeneric("failed to start HTTP client thread", {});
    }
    Global.crash();
}

pub type InitResult_ = [*PackageManager, string];

pub fn init(
    ctx: Command.Context,
    cli: CommandLineArguments,
    subcommand: Subcommand,
): InitResult_! {
    if (cli.global) {
        let explicit_global_dir: string = "";
        if (opts := ctx.install) {
            if (dir := opts.global_dir) {
                explicit_global_dir = dir;
            }
        }
        let global_dir = Options.openGlobalDir(explicit_global_dir)?;
        global_dir.setAsCwd()?;
    }

    let fs = Fs.FileSystem.init(null)?;
    const top_level_dir_no_trailing_slash = strings.withoutTrailingSlash(fs.top_level_dir);
    if (comptime Environment.isWindows) {
        * = Path.pathToPosixBuf(u8, top_level_dir_no_trailing_slash, &cwd_buf);
    } else {
        // Avoid memcpy alias when source and dest are the same
        if (cwd_buf[0..].ptr != top_level_dir_no_trailing_slash.ptr) {
            bun.copy(u8, cwd_buf[0..top_level_dir_no_trailing_slash.len], top_level_dir_no_trailing_slash);
        }
    }

    let original_package_json_path_buf = bun.handleOom(std.ArrayListUnmanaged(u8).initCapacity(ctx.allocator, top_level_dir_no_trailing_slash.len + "/package.json".len + 1));
    original_package_json_path_buf.appendSliceAssumeCapacity(top_level_dir_no_trailing_slash);
    original_package_json_path_buf.appendSliceAssumeCapacity(std.fs.path.sep_str ++ "package.json");
    original_package_json_path_buf.appendAssumeCapacity(0);

    let original_package_json_path: stringZ = original_package_json_path_buf.items[0 .. top_level_dir_no_trailing_slash.len + "/package.json".len :0];
    const original_cwd = strings.withoutSuffixComptime(original_package_json_path, std.fs.path.sep_str ++ "package.json");
    const original_cwd_clone = bun.handleOom(ctx.allocator.dupe(u8, original_cwd));

    let workspace_names = Package.WorkspaceMap.init(ctx.allocator);
    let workspace_package_json_cache: WorkspacePackageJSONCache = {
        map: {},
    };

    let workspace_name_hash: PackageNameHash? = null;
    let root_package_json_name_at_time_of_init: []const u8 = "";

    // Step 1. Find the nearest package.json directory
    //
    // We will walk up from the cwd, trying to find the nearest package.json file.
    const root_package_json_file = root_package_json_file: {
        let this_cwd: string = original_cwd;
        let created_package_json = false;
        const child_json = child: {
            // if we are only doing `bun install` (no args), then we can open as read_only
            // in all other cases we will need to write new data later.
            // this is relevant because it allows us to succeed an install if package.json
            // is readable but not writable
            //
            // probably wont matter as if package.json isn't writable, it's likely that
            // the underlying directory and node_modules isn't either.
            const need_write = subcommand != .install || cli.positionals.len > 1;

            while (true) {
                let package_json_path_buf: bun.PathBuffer = undefined;
                @memcpy(package_json_path_buf[0..this_cwd.len], this_cwd);
                @memcpy(package_json_path_buf[this_cwd.len .. this_cwd.len + "/package.json".len], "/package.json");
                package_json_path_buf[this_cwd.len + "/package.json".len] = 0;
                const package_json_path = package_json_path_buf[0 .. this_cwd.len + "/package.json".len :0];

                break :child std.fs.cwd().openFileZ(
                    package_json_path,
                    { mode: if (need_write) .read_write else .read_only },
                ) catch e => {
                    if (e == error.FileNotFound) {
                        if (parent := std.fs.path.dirname(this_cwd)) {
                            this_cwd = strings.withoutTrailingSlash(parent);
                            continue;
                        } else {
                            break;
                        }
                    } else if (e == error.AccessDenied) {
                        Output.err("EACCES", "Permission denied while opening \"{s}\"", .{
                            package_json_path,
                        });
                        if (need_write) {
                            Output.note("package.json must be writable to add packages", {});
                        } else {
                            Output.note("package.json is missing read permissions, or is owned by another user", {});
                        }
                        Global.crash();
                    } else {
                        Output.err(e, "could not open \"{s}\"", .{
                            package_json_path,
                        });
                        return e;
                    }
                };
            }

            if (subcommand == .install) {
                if (cli.positionals.len > 1) {
                    // this is `bun add <package>`.
                    //
                    // create the package json instead of return error. this works around
                    // a zig bug where continuing control flow through a catch seems to
                    // cause a segfault the second time `PackageManager.init` is called after
                    // switching to the add command.
                    this_cwd = original_cwd;
                    created_package_json = true;
                    break :child attemptToCreatePackageJSONAndOpen()?;
                }
            }
            return error.MissingPackageJSON;
        };

        bun.assertWithLocation(strings.eqlLong(original_package_json_path_buf.items[0..this_cwd.len], this_cwd, true), @src());
        original_package_json_path_buf.items.len = this_cwd.len;
        original_package_json_path_buf.appendSliceAssumeCapacity(std.fs.path.sep_str ++ "package.json");
        original_package_json_path_buf.appendAssumeCapacity(0);

        original_package_json_path = original_package_json_path_buf.items[0 .. this_cwd.len + "/package.json".len :0];
        const child_cwd = strings.withoutSuffixComptime(original_package_json_path, std.fs.path.sep_str ++ "package.json");

        // Check if this is a workspace; if so, use root package
        let found = false;
        if (subcommand.shouldChdirToRoot()) {
            if (!created_package_json) {
                ws_while: while (parent := std.fs.path.dirname(this_cwd)) : (this_cwd = parent) {
                    const parent_without_trailing_slash = strings.withoutTrailingSlash(parent);
                    let parent_path_buf: bun.PathBuffer = undefined;
                    @memcpy(parent_path_buf[0..parent_without_trailing_slash.len], parent_without_trailing_slash);
                    @memcpy(parent_path_buf[parent_without_trailing_slash.len .. parent_without_trailing_slash.len + "/package.json".len], "/package.json");
                    parent_path_buf[parent_without_trailing_slash.len + "/package.json".len] = 0;

                    let json_file = std.fs.cwd().openFileZ(
                        parent_path_buf[0 .. parent_without_trailing_slash.len + "/package.json".len :0].ptr,
                        { mode: .read_write },
                    ) catch {
                        continue;
                    };
                    let json_file_guard = CloseGuard<std.fs.File>{ ptr: &json_file };
                    const json_stat_size = json_file.getEndPos()?;
                    const json_buf = ctx.allocator.alloc(u8, json_stat_size + 64)?;
                    const _json_buf_guard = FreeGuard<[]u8>{ allocator: ctx.allocator, slice: json_buf };
                    const json_len = json_file.preadAll(json_buf, 0)?;
                    const json_path = bun.getFdPath(.fromStdFile(json_file), &root_package_json_path_buf)?;
                    const json_source = logger.Source.initPathString(json_path, json_buf[0..json_len]);
                    initializeStore();
                    const json = JSON.parsePackageJSONUTF8(&json_source, ctx.log, ctx.allocator)?;
                    if (subcommand == .pm) {
                        if (name := json.getStringCloned(ctx.allocator, "name") catch null) {
                            root_package_json_name_at_time_of_init = name;
                        }
                    }

                    if (prop := json.asProperty("workspaces")) {
                        const json_array_opt: (*bun.ast.E.Array)? = switch (prop.expr.data) {
                            .e_array => |arr| arr,
                            .e_object => |obj| brk_pkg: {
                                if (packages := obj.get("packages")) {
                                    switch (packages.data) {
                                        .e_array => |arr| break :brk_pkg arr,
                                        else => break :brk_pkg null,
                                    }
                                } else break :brk_pkg null;
                            },
                            else => null,
                        };
                        if (json_array_opt == null) break :ws_while;
                        const json_array = json_array_opt.?;
                        let log = logger.Log.init(ctx.allocator);
                        const _log_guard = DeinitGuard<logger.Log>{ ptr: &log };
                        * = workspace_names.processNamesArray(
                            ctx.allocator,
                            &workspace_package_json_cache,
                            &log,
                            json_array,
                            &json_source,
                            prop.loc,
                            null,
                        ) catch break :ws_while;

                        const ws_keys = workspace_names.keys();
                        const ws_vals = workspace_names.values();
                        let ws_i: usize = 0;
                        while (ws_i < ws_keys.len) : (ws_i += 1) {
                            const path = ws_keys[ws_i];
                            const entry = ws_vals[ws_i];
                            const child_path = if (std.fs.path.isAbsolute(path))
                                child_cwd
                            else
                                bun.path.relativeNormalized(json_source.path.name.dir, child_cwd, .auto, true);

                            const maybe_workspace_path = if (comptime Environment.isWindows) brk: {
                                @memcpy(parent_path_buf[0..child_path.len], child_path);
                                bun.path.dangerouslyConvertPathToPosixInPlace(u8, parent_path_buf[0..child_path.len]);
                                break :brk parent_path_buf[0..child_path.len];
                            } else child_path;

                            if (strings.eqlLong(maybe_workspace_path, path, true)) {
                                fs.top_level_dir = bun.default_allocator.dupeZ(u8, parent)?;
                                found = true;
                                json_file_guard.ptr = null;
                                child_json.close();
                                if (comptime Environment.isWindows) {
                                    json_file.seekTo(0)?;
                                }
                                workspace_name_hash = String.Builder.stringHash(entry.name);
                                break :root_package_json_file json_file;
                            }
                        }

                        break;
                    }
                }
            }
        }

        fs.top_level_dir = bun.default_allocator.dupeZ(u8, child_cwd)?;
        break :root_package_json_file child_json;
    };

    bun.sys.chdir(fs.top_level_dir, fs.top_level_dir).unwrap()?;
    BunArguments.loadConfig(ctx.allocator, cli.config, ctx, .InstallCommand)?;
    bun.copy(u8, &cwd_buf, fs.top_level_dir);
    cwd_buf[fs.top_level_dir.len] = 0;
    fs.top_level_dir = cwd_buf[0..fs.top_level_dir.len :0];
    root_package_json_path = bun.getFdPathZ(.fromStdFile(root_package_json_file), &root_package_json_path_buf)?;

    const entries_option = fs.fs.readDirectory(fs.top_level_dir, null, 0, true)?;
    if (entries_option.* == .err) {
        return entries_option.err.canonical_error;
    }

    let env: *DotEnv.Loader = brk: {
        const map = ctx.allocator.create(DotEnv.Map)?;
        map.* = DotEnv.Map.init(ctx.allocator);

        const loader = ctx.allocator.create(DotEnv.Loader)?;
        loader.* = DotEnv.Loader.init(map, ctx.allocator);
        break :brk loader;
    };

    env.loadProcess()?;
    let empty_env_paths: [0][]u8 = undefined;
    env.load(bun.DotEnv.DirEntryKeys.fromDirEntry(entries_option.entries), &empty_env_paths, .production, false)?;

    initializeStore();

    if (data_dir := bun.env_var.XDG_CONFIG_HOME.get() ?? bun.env_var.HOME.get()) {
        let buf: bun.PathBuffer = undefined;
        const parts: [1]string = .{ "./.npmrc" };
        const npmrc_paths: [2][:0]const u8 = .{
            Path.joinAbsStringBufZ(
                data_dir,
                &buf,
                &parts,
                .auto,
            ),
            ".npmrc",
        };
        bun.ini.loadNpmrcConfig(ctx.allocator, ctx.install ?? brk: {
            const install_ = bun.handleOom(ctx.allocator.create(Api.BunInstall));
            install_.* = std.mem.zeroes(Api.BunInstall);
            ctx.install = install_;
            break :brk install_;
        }, env, true, &npmrc_paths);
    } else {
        const default_npmrc: [1][:0]const u8 = .{ ".npmrc" };
        bun.ini.loadNpmrcConfig(ctx.allocator, ctx.install ?? brk: {
            const install_ = bun.handleOom(ctx.allocator.create(Api.BunInstall));
            install_.* = std.mem.zeroes(Api.BunInstall);
            ctx.install = install_;
            break :brk install_;
        }, env, true, &default_npmrc);
    }
    const cpu_count = bun.getThreadCount();

    const options = Options{
        global: cli.global,
        max_concurrent_lifecycle_scripts: cli.concurrent_scripts ?? cpu_count * 2,
    };

    if (env.get("BUN_INSTALL_VERBOSE") != null) {
        PackageManager.verbose_install = true;
    }

    if (env.get("BUN_FEATURE_FLAG_FORCE_WAITER_THREAD") != null) {
        bun.spawn.process.WaiterThread.setShouldUseWaiterThread();
    }

    if (bun.feature_flag.BUN_FEATURE_FLAG_FORCE_WINDOWS_JUNCTIONS.get()) {
        bun.sys.WindowsSymlinkOptions.has_failed_to_create_symlink = true;
    }

    if (PackageManager.verbose_install) {
        Output.prettyErrorln("Cache Dir: {s}", .{options.cache_directory});
        Output.flush();
    }

    workspace_names.map.deinit();

    PackageManager.allocatePackageManager();
    const manager = PackageManager.get();
    // var progress = Progress{};
    // var node = progress.start(name: []const u8, estimated_total_items: usize)
    manager.* = PackageManager{
        preallocated_network_tasks: .init(bun.default_allocator),
        preallocated_resolve_tasks: .init(bun.default_allocator),
        options: options,
        active_lifecycle_scripts: {
            context: manager,
        },
        network_task_fifo: NetworkQueue.init(),
        patch_task_fifo: PatchTaskFifo.init(),
        allocator: ctx.allocator,
        log: ctx.log,
        root_dir: entries_option.entries,
        env: env,
        cpu_count: cpu_count,
        thread_pool: ThreadPool.init({
            max_threads: cpu_count,
        }),
        resolve_tasks: {},
        lockfile: undefined,
        root_package_json_file: root_package_json_file,
        // .progress
        event_loop: {
            mini: jsc.MiniEventLoop.init(bun.default_allocator),
        },
        original_package_json_path: original_package_json_path,
        workspace_package_json_cache: workspace_package_json_cache,
        workspace_name_hash: workspace_name_hash,
        subcommand: subcommand,
        root_package_json_name_at_time_of_init: root_package_json_name_at_time_of_init,
    };
    manager.event_loop.loop().internal_loop_data.setParentEventLoop(bun.jsc.EventLoopHandle.init(&manager.event_loop));
    manager.lockfile = ctx.allocator.create(Lockfile)?;

    {
        // make sure folder packages can find the root package without creating a new one
        let normalized = PosixAbsPath.@"from"(root_package_json_path);
        const _norm_guard = DeinitGuard<PosixAbsPath>{ ptr: &normalized };
        manager.folders.put(manager.allocator, FolderResolution.hash(normalized.slice()), { package_id: 0 })?;
    }

    jsc.MiniEventLoop.global = &manager.event_loop.mini;
    if (!manager.options.enable.cache) {
        manager.options.enable.manifest_cache = false;
        manager.options.enable.manifest_cache_control = false;
    }

    if (manifest_cache := env.get("BUN_MANIFEST_CACHE")) {
        if (strings.eqlComptime(manifest_cache, "1")) {
            manager.options.enable.manifest_cache = true;
            manager.options.enable.manifest_cache_control = false;
        } else if (strings.eqlComptime(manifest_cache, "2")) {
            manager.options.enable.manifest_cache = true;
            manager.options.enable.manifest_cache_control = true;
        } else {
            manager.options.enable.manifest_cache = false;
            manager.options.enable.manifest_cache_control = false;
        }
    }

    manager.options.load(
        ctx.allocator,
        ctx.log,
        env,
        cli,
        ctx.install,
        subcommand,
    )?;

    let ca: []stringZ = [];
    if (manager.options.ca.len > 0) {
        ca = manager.allocator.alloc(stringZ, manager.options.ca.len)?;
        let ca_i: usize = 0;
        while (ca_i < ca.len) : (ca_i += 1) {
            ca[ca_i] = manager.allocator.dupeZ(u8, manager.options.ca[ca_i])?;
        }
    }

    let abs_ca_file_name: stringZ = "";
    if (manager.options.ca_file_name.len > 0) {
        // resolve with original cwd
        if (std.fs.path.isAbsolute(manager.options.ca_file_name)) {
            abs_ca_file_name = manager.allocator.dupeZ(u8, manager.options.ca_file_name)?;
        } else {
            let path_buf: bun.PathBuffer = undefined;
            abs_ca_file_name = manager.allocator.dupeZ(u8, bun.path.joinAbsStringBuf(
                original_cwd_clone,
                &path_buf,
                &.{manager.options.ca_file_name},
                .auto,
            ))?;
        }
    }

    AsyncHTTP.max_simultaneous_requests.store(brk: {
        if (network_concurrency := cli.network_concurrency) {
            break :brk @max(network_concurrency, 1);
        }

        // If any HTTP proxy is set, use a diferent limit
        if (env.has("http_proxy") || env.has("https_proxy") || env.has("HTTPS_PROXY") || env.has("HTTP_PROXY")) {
            break :brk default_max_simultaneous_requests_for_bun_install_for_proxies;
        }

        break :brk default_max_simultaneous_requests_for_bun_install;
    }, .monotonic);

    HTTP.HTTPThread.init(&{
        ca: ca,
        abs_ca_file_name: abs_ca_file_name,
        onInitError: &httpThreadOnInitError,
    });

    manager.timestamp_for_manifest_cache_control = brk: {
        if (comptime bun.Environment.allow_assert) {
            if (cache_control := env.get("BUN_CONFIG_MANIFEST_CACHE_CONTROL_TIMESTAMP")) {
                if (int := std.fmt.parseInt(u32, cache_control, 10) catch null) {
                    break :brk int;
                }
            }
        }

        break :brk @truncate(@as(u64, @intCast(@max(std.time.timestamp(), 0))));
    };
    Holder.initialized.store(true, .release);
    return .{
        manager,
        original_cwd_clone,
    };
}

pub fn initWithRuntime(
    log: *logger.Log,
    bun_install: (*Api.BunInstall)?,
    allocator: std.mem.Allocator,
    cli: CommandLineArguments,
    env: *DotEnv.Loader,
): *PackageManager {
    init_with_runtime_once.call(.{
        log,
        bun_install,
        allocator,
        cli,
        env,
    });
    return PackageManager.get();
}

let init_with_runtime_once = bun.once(initWithRuntimeOnce);

pub fn initWithRuntimeOnce(
    log: *logger.Log,
    bun_install: (*Api.BunInstall)?,
    allocator: std.mem.Allocator,
    cli: CommandLineArguments,
    env: *DotEnv.Loader,
) {
    if (env.get("BUN_INSTALL_VERBOSE") != null) {
        PackageManager.verbose_install = true;
    }

    const cpu_count = bun.getThreadCount();
    PackageManager.allocatePackageManager();
    const manager = PackageManager.get();
    let root_dir = Fs.FileSystem.instance.fs.readDirectory(
        Fs.FileSystem.instance.top_level_dir,
        null,
        0,
        true,
    ) catch e => {
        Output.err(e, "failed to read root directory: '{s}'", .{Fs.FileSystem.instance.top_level_dir});
        @panic("Failed to initialize package manager");
    };

    // var progress = Progress{};
    // var node = progress.start(name: []const u8, estimated_total_items: usize)
    const top_level_dir_no_trailing_slash = strings.withoutTrailingSlash(Fs.FileSystem.instance.top_level_dir);
    let original_package_json_path = bun.handleOom(allocator.allocSentinel(u8, top_level_dir_no_trailing_slash.len + "/package.json".len, 0));
    @memcpy(original_package_json_path[0..top_level_dir_no_trailing_slash.len], top_level_dir_no_trailing_slash);
    @memcpy(original_package_json_path[top_level_dir_no_trailing_slash.len..][0.."/package.json".len], "/package.json");

    manager.* = PackageManager{
        preallocated_network_tasks: .init(bun.default_allocator),
        preallocated_resolve_tasks: .init(bun.default_allocator),
        options: {
            max_concurrent_lifecycle_scripts: cli.concurrent_scripts ?? cpu_count * 2,
        },
        active_lifecycle_scripts: {
            context: manager,
        },
        network_task_fifo: NetworkQueue.init(),
        allocator: allocator,
        log: log,
        root_dir: root_dir.entries,
        env: env,
        cpu_count: cpu_count,
        thread_pool: ThreadPool.init({
            max_threads: cpu_count,
        }),
        lockfile: undefined,
        root_package_json_file: undefined,
        event_loop: {
            js: jsc.VirtualMachine.get().eventLoop(),
        },
        original_package_json_path: original_package_json_path[0..original_package_json_path.len :0],
        subcommand: .install,
    };
    manager.lockfile = bun.handleOom(allocator.create(Lockfile));

    if (Output.enable_ansi_colors_stderr) {
        manager.progress = Progress{};
        manager.progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
        manager.root_progress_node = manager.progress.start("", 0);
    } else {
        manager.options.log_level = .default_no_progress;
    }

    if (!manager.options.enable.cache) {
        manager.options.enable.manifest_cache = false;
        manager.options.enable.manifest_cache_control = false;
    }

    if (manifest_cache := env.get("BUN_MANIFEST_CACHE")) {
        if (strings.eqlComptime(manifest_cache, "1")) {
            manager.options.enable.manifest_cache = true;
            manager.options.enable.manifest_cache_control = false;
        } else if (strings.eqlComptime(manifest_cache, "2")) {
            manager.options.enable.manifest_cache = true;
            manager.options.enable.manifest_cache_control = true;
        } else {
            manager.options.enable.manifest_cache = false;
            manager.options.enable.manifest_cache_control = false;
        }
    }

    manager.options.load(
        allocator,
        log,
        env,
        cli,
        bun_install,
        .install,
    ) catch e => bun.handleOom(e);

    manager.timestamp_for_manifest_cache_control = @as(u32, @truncate(@as(u64, @intCast(@max(std.time.timestamp(), 0))))) -| std.time.s_per_day;

    if (root_dir.entries.hasComptimeQuery("bun.lockb")) {
        switch (manager.lockfile.loadFromCwd(
            manager,
            allocator,
            log,
            true,
        )) {
            .ok => |load| manager.lockfile = load.lockfile,
            else => manager.lockfile.initEmpty(allocator),
        }
    } else {
        manager.lockfile.initEmpty(allocator);
    }
    Holder.initialized.store(true, .release);
}
let cwd_buf: bun.PathBuffer = undefined;
let root_package_json_path_buf: bun.PathBuffer = undefined;
pub let root_package_json_path: [:0]const u8 = "";

// Default to a maximum of 64 simultaneous HTTP requests for bun install if no proxy is specified
// if a proxy IS specified, default to 64. We have different values because we might change this in the future.
// https://github.com/npm/cli/issues/7072
// https://pnpm.io/npmrc#network-concurrency (pnpm defaults to 16)
// https://yarnpkg.com/configuration/yarnrc#networkConcurrency (defaults to 50)
const default_max_simultaneous_requests_for_bun_install = 64;
const default_max_simultaneous_requests_for_bun_install_for_proxies = 64;

pub const TaskCallbackList = std.ArrayListUnmanaged(TaskCallbackContext);
const TaskDependencyQueue = std.HashMapUnmanaged(Task.Id, TaskCallbackList, IdentityContext(Task.Id), 80);

const PreallocatedTaskStore = bun.HiveArray(Task, 64).Fallback;
const PreallocatedNetworkTasks = bun.HiveArray(NetworkTask, 128).Fallback;
const ResolveTaskQueue = bun.UnboundedQueue(Task, .next);

const RepositoryMap = std.HashMapUnmanaged(Task.Id, bun.FD, IdentityContext(Task.Id), 80);
/// Resolve-task id (git checkout / tarball extract) -> the package that task
/// appended during the resolve phase. A task's callback queue is drained
/// exactly once, so a dependency enqueued after that drain must resolve
/// through this map instead of queueing a callback that nothing will ever
/// process.
const AppendedTaskPackageMap = std.HashMapUnmanaged(Task.Id, PackageID, IdentityContext(Task.Id), 80);
const NpmAliasMap = std.HashMapUnmanaged(PackageNameHash, Dependency.Version, IdentityContext(u64), 80);

const NetworkQueue = bun.LinearFifo(@TypeOf(@as(NetworkTaskPtrHolder, undefined).p), { Static: 32 });
const PatchTaskFifo = bun.LinearFifo(@TypeOf(@as(PatchTaskPtrHolder, undefined).p), { Static: 32 });

// pub const ensureTempNodeGypScript = directories.ensureTempNodeGypScript;

import CommandLineArguments from "./PackageManager/CommandLineArguments.zig";
import Options from "./PackageManager/PackageManagerOptions.zig";
import PackageJSONEditor from "./PackageManager/PackageJSONEditor.zig";
import UpdateRequest from "./PackageManager/UpdateRequest.zig";
import WorkspacePackageJSONCache from "./PackageManager/WorkspacePackageJSONCache.zig";
import PackageInstallerMod from "./PackageInstaller.zig";
pub const PackageInstaller = PackageInstallerMod.PackageInstaller;
import install_with_manager_mod from "./PackageManager/install_with_manager.zig";
pub const installWithManager = install_with_manager_mod.installWithManager;

import directories from "./PackageManager/PackageManagerDirectories.zig";
pub const attemptToCreatePackageJSON = directories.attemptToCreatePackageJSON;
const attemptToCreatePackageJSONAndOpen = directories.attemptToCreatePackageJSONAndOpen;
pub const cachedGitFolderName = directories.cachedGitFolderName;
pub const cachedGitFolderNamePrint = directories.cachedGitFolderNamePrint;
pub const cachedGitFolderNamePrintAuto = directories.cachedGitFolderNamePrintAuto;
pub const cachedGitHubFolderName = directories.cachedGitHubFolderName;
pub const cachedGitHubFolderNamePrint = directories.cachedGitHubFolderNamePrint;
pub const cachedGitHubFolderNamePrintAuto = directories.cachedGitHubFolderNamePrintAuto;
pub const cachedNPMPackageFolderName = directories.cachedNPMPackageFolderName;
pub const cachedNPMPackageFolderNamePrint = directories.cachedNPMPackageFolderNamePrint;
pub const cachedNPMPackageFolderPrintBasename = directories.cachedNPMPackageFolderPrintBasename;
pub const cachedTarballFolderName = directories.cachedTarballFolderName;
pub const cachedTarballFolderNamePrint = directories.cachedTarballFolderNamePrint;
pub const computeCacheDirAndSubpath = directories.computeCacheDirAndSubpath;
pub const fetchCacheDirectoryPath = directories.fetchCacheDirectoryPath;
pub const getCacheDirectory = directories.getCacheDirectory;
pub const getCacheDirectoryAndAbsPath = directories.getCacheDirectoryAndAbsPath;
pub const getTemporaryDirectory = directories.getTemporaryDirectory;
pub const globalLinkDir = directories.globalLinkDir;
pub const globalLinkDirAndPath = directories.globalLinkDirAndPath;
pub const globalLinkDirPath = directories.globalLinkDirPath;
pub const isFolderInCache = directories.isFolderInCache;
pub const isPackageInCache = directories.isPackageInCache;
pub const isPackageInCacheAt = directories.isPackageInCacheAt;
pub const pathForCachedNPMPath = directories.pathForCachedNPMPath;
pub const pathForResolution = directories.pathForResolution;
pub const saveLockfile = directories.saveLockfile;
pub const setupGlobalDir = directories.setupGlobalDir;
pub const updateLockfileIfNeeded = directories.updateLockfileIfNeeded;
pub const writeYarnLock = directories.writeYarnLock;

import enqueue from "./PackageManager/PackageManagerEnqueue.zig";
pub const enqueueDependencyList = enqueue.enqueueDependencyList;
pub const enqueueDependencyToRoot = enqueue.enqueueDependencyToRoot;
pub const enqueueDependencyWithMain = enqueue.enqueueDependencyWithMain;
pub const enqueueDependencyWithMainAndSuccessFn = enqueue.enqueueDependencyWithMainAndSuccessFn;
pub const enqueueExtractNPMPackage = enqueue.enqueueExtractNPMPackage;
pub const createExtractTaskForStreaming = enqueue.createExtractTaskForStreaming;
pub const enqueueGitCheckout = enqueue.enqueueGitCheckout;
pub const enqueueGitForCheckout = enqueue.enqueueGitForCheckout;
pub const enqueueNetworkTask = enqueue.enqueueNetworkTask;
pub const enqueuePackageForDownload = enqueue.enqueuePackageForDownload;
pub const enqueueParseNPMPackage = enqueue.enqueueParseNPMPackage;
pub const enqueuePatchTask = enqueue.enqueuePatchTask;
pub const enqueuePatchTaskPre = enqueue.enqueuePatchTaskPre;
pub const enqueueTarballForDownload = enqueue.enqueueTarballForDownload;
pub const enqueueTarballForReading = enqueue.enqueueTarballForReading;

pub const determinePreinstallState = lifecycle.determinePreinstallState;
pub const ensurePreinstallStateListCapacity = lifecycle.ensurePreinstallStateListCapacity;
pub const findTrustedDependenciesFromUpdateRequests = lifecycle.findTrustedDependenciesFromUpdateRequests;
pub const getPreinstallState = lifecycle.getPreinstallState;
pub const hasNoMorePendingLifecycleScripts = lifecycle.hasNoMorePendingLifecycleScripts;
pub const loadRootLifecycleScripts = lifecycle.loadRootLifecycleScripts;
pub const reportSlowLifecycleScripts = lifecycle.reportSlowLifecycleScripts;
pub const setPreinstallState = lifecycle.setPreinstallState;
pub const sleep = lifecycle.sleep;
pub const spawnPackageLifecycleScripts = lifecycle.spawnPackageLifecycleScripts;
pub const tickLifecycleScripts = lifecycle.tickLifecycleScripts;

pub const assignResolution = resolution.assignResolution;
pub const assignRootResolution = resolution.assignRootResolution;
pub const formatLaterVersionInCache = resolution.formatLaterVersionInCache;
pub const getInstalledVersionsFromDiskCache = resolution.getInstalledVersionsFromDiskCache;
pub const resolveFromDiskCache = resolution.resolveFromDiskCache;
pub const scopeForPackageName = resolution.scopeForPackageName;
pub const verifyResolutions = resolution.verifyResolutions;

import progress_zig from "./PackageManager/ProgressStrings.zig";
pub const ProgressStrings = progress_zig.ProgressStrings;
pub const endProgressBar = progress_zig.endProgressBar;
pub const setNodeName = progress_zig.setNodeName;
pub const startProgressBar = progress_zig.startProgressBar;
pub const startProgressBarIfNone = progress_zig.startProgressBarIfNone;

import patchPackageMod from "./PackageManager/patchPackage.zig";
pub const PatchCommitResult = patchPackageMod.PatchCommitResult;
pub const doPatchCommit = patchPackageMod.doPatchCommit;
pub const preparePatch = patchPackageMod.preparePatch;

import processDependencyListMod from "./PackageManager/processDependencyList.zig";
pub const GitResolver = processDependencyListMod.GitResolver;
pub const processDependencyList = processDependencyListMod.processDependencyList;
pub const processDependencyListItem = processDependencyListMod.processDependencyListItem;
pub const processExtractedTarballPackage = processDependencyListMod.processExtractedTarballPackage;
pub const processPeerDependencyList = processDependencyListMod.processPeerDependencyList;

import runTasksMod from "./PackageManager/runTasks.zig";
pub const allocGitHubURL = runTasksMod.allocGitHubURL;
pub const decrementPendingTasks = runTasksMod.decrementPendingTasks;
pub const drainDependencyList = runTasksMod.drainDependencyList;
pub const flushDependencyQueue = runTasksMod.flushDependencyQueue;
pub const flushNetworkQueue = runTasksMod.flushNetworkQueue;
pub const flushPatchTaskQueue = runTasksMod.flushPatchTaskQueue;
pub const generateNetworkTaskForTarball = runTasksMod.generateNetworkTaskForTarball;
pub const getNetworkTask = runTasksMod.getNetworkTask;
pub const hasCreatedNetworkTask = runTasksMod.hasCreatedNetworkTask;
pub const incrementPendingTasks = runTasksMod.incrementPendingTasks;
pub const isNetworkTaskRequired = runTasksMod.isNetworkTaskRequired;
pub const pendingTaskCount = runTasksMod.pendingTaskCount;
pub const runTasks = runTasksMod.runTasks;
pub const scheduleTasks = runTasksMod.scheduleTasks;

import updatePackageJSONAndInstallMod from "./PackageManager/updatePackageJSONAndInstall.zig";
pub const updatePackageJSONAndInstallCatchError = updatePackageJSONAndInstallMod.updatePackageJSONAndInstallCatchError;
pub const updatePackageJSONAndInstallWithManager = updatePackageJSONAndInstallMod.updatePackageJSONAndInstallWithManager;

import populateManifestCacheMod from "./PackageManager/PopulateManifestCache.zig";
pub const populateManifestCache = populateManifestCacheMod.populateManifestCache;

type string = []const u8;
type stringZ = [:0]const u8;
const PosixAbsPath = bun.AbsPath({ sep: .posix });

import DirInfo from "../resolver/dir_info.zig";
import resolution from "./PackageManager/PackageManagerResolution.zig";
const updatePackageJSONAndInstall = updatePackageJSONAndInstallMod.updatePackageJSONAndInstall;

import lifecycle from "./PackageManager/PackageManagerLifecycle.zig";
const LifecycleScriptTimeLog = lifecycle.LifecycleScriptTimeLog;

import bun from "bun";
const DotEnv = bun.DotEnv;
const Environment = bun.Environment;
const Global = bun.Global;
const JSON = bun.json;
const OOM = bun.OOM;
const Output = bun.Output;
const Path = bun.path;
const Progress = bun.Progress;
const RunCommand = bun.RunCommand;
const ThreadPool = bun.ThreadPool;
const URL = bun.URL;
const default_allocator = bun.default_allocator;
const jsc = bun.jsc;
const logger = bun.logger;
const strings = bun.strings;
const transpiler = bun.transpiler;
const Api = bun.schema.api;
const File = bun.sys.File;

const Semver = bun.Semver;
const String = Semver.String;

const BunArguments = bun.cli.Arguments;
const Command = bun.cli.Command;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const HTTP = bun.http;
const AsyncHTTP = HTTP.AsyncHTTP;

const ArrayIdentityContext = bun.install.ArrayIdentityContext;
const Dependency = bun.install.Dependency;
const DependencyID = bun.install.DependencyID;
const Features = bun.install.Features;
const FolderResolution = bun.install.FolderResolution;
const IdentityContext = bun.install.IdentityContext;
const LifecycleScriptSubprocess = bun.install.LifecycleScriptSubprocess;
const NetworkTask = bun.install.NetworkTask;
const PackageID = bun.install.PackageID;
const PackageManifestMap = bun.install.PackageManifestMap;
const PackageNameAndVersionHash = bun.install.PackageNameAndVersionHash;
const PackageNameHash = bun.install.PackageNameHash;
const PatchTask = bun.install.PatchTask;
const PostinstallOptimizer = bun.install.PostinstallOptimizer;
const PreinstallState = bun.install.PreinstallState;
const Task = bun.install.Task;
const TaskCallbackContext = bun.install.TaskCallbackContext;
const initializeStore = bun.install.initializeStore;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;
