import std from "std";
import bun from "bun";
import FileCloner from "./FileCloner.zig";
import Hardlinker from "./Hardlinker.zig";
import SymlinkerModule from "./Symlinker.zig";
const Symlinker = SymlinkerModule.Symlinker;

const Environment = bun.Environment;
const FD = bun.FD;
const Global = bun.Global;
const OOM = bun.OOM;
const Output = bun.Output;
const Progress = bun.Progress;
const ThreadPool = bun.ThreadPool;
const strings = bun.strings;
const sys = bun.sys;
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const Command = bun.cli.Command;
const Mutex = bun.threading.Mutex;
const String = bun.Semver.String;

const install = bun.install;
const Bin = install.Bin;
const DependencyID = install.DependencyID;
const FileCopier = bun.install.FileCopier;
const PackageID = install.PackageID;
const PackageInstall = install.PackageInstall;
const PackageManager = install.PackageManager;
const PackageNameHash = install.PackageNameHash;
const PostinstallOptimizer = install.PostinstallOptimizer;
const Resolution = install.Resolution;
const Store = install.Store;
const TruncatedPackageNameHash = install.TruncatedPackageNameHash;
const invalid_dependency_id = install.invalid_dependency_id;

const Lockfile = install.Lockfile;
const Package = Lockfile.Package;

const debug = Output.scoped(.IsolatedInstaller, .hidden);

const AbsPathAuto = bun.AbsPath(.{ .sep = .auto });
const RelPathAuto = bun.RelPath(.{ .sep = .auto });
const PathAuto = bun.Path(.{ .sep = .auto });
const AbsPathOsAuto = bun.AbsPath(.{ .unit = .os, .sep = .auto });
const PathOsAuto = bun.Path(.{ .unit = .os, .sep = .auto });
const AbsPathEmpty = bun.AbsPath(.{});
const EntryDedupeMap = std.AutoArrayHashMap(Store.Entry.Id, void);
const SeenMap = bun.StringHashMap(void);

fn doSymlinkGlobalStore(d: [:0]const u8, t: [:0]const u8): sys.Maybe(void) {
    if (comptime Environment.isWindows) {
        return sys.symlinkOrJunction(d, t, t);
    }
    return sys.symlink(t, d);
}

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct DeinitWithAllocGuard<T> {
    val: T,
    alloc: std.mem.Allocator,
    ~init() {
        _.val.deinit(_.alloc);
    }
}

pub struct OptDeinitGuard<T> {
    val: T?,
    ~init() {
        if (*v := _.val) {
            v.deinit();
        }
    }
}

pub struct CloseGuard {
    fd: FD,
    ~init() {
        _.fd.close();
    }
}

pub struct OptCloseGuard {
    fd: FD?,
    ~init() {
        if (d := _.fd) {
            d.close();
        }
    }
}

pub struct RestoreGuard<T> {
    val: T,
    ~init() {
        _.val.restore();
    }
}

pub struct MutexUnlockGuard {
    mutex: *Mutex,
    ~init() {
        _.mutex.unlock();
    }
}

pub struct PathBufPoolGuard {
    buf: *bun.PathBuffer,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

pub enum OnTaskCompleteState {
    success,
    skipped,
    fail,
}

pub enum TopWhich {
    /// The published location (`<cache>/links/<entry>`). Use for symlink
    /// *targets* that point at other entries, && for the warm-hit check.
    final,
    /// The per-process temp sibling (`<entry>.tmp-<suffix>`) the build
    /// steps write into. Use for *destinations* of clonefile/hardlink/
    /// dep-symlink/bin-link when building this entry.
    staging,
}

pub struct PatchInfoRemove {
    name_and_version_hash: u64,
}

pub struct PatchInfoPatch {
    name_and_version_hash: u64,
    patch_path: string,
    contents_hash: u64,
}

pub union(enum) TopPatchInfo {
    none: void,
    remove: PatchInfoRemove,
    patch: PatchInfoPatch,

    const Remove = PatchInfoRemove;
    const Patch = PatchInfoPatch;

    fn contentsHash(): u64? {
        return switch (_) {
            .none, .remove => null,
            .patch patch => patch.contents_hash,
        };
    }

    fn nameAndVersionHash(): u64? {
        return switch (_) {
            .none, .remove => null,
            .patch patch => patch.name_and_version_hash,
        };
    }
}


pub struct TaskDownloadError {
    download_err: anyerror,
    url: []const u8,
}

pub union(enum) TaskError {
    link_package: sys.Error,
    symlink_dependencies: sys.Error,
    run_scripts: anyerror,
    binaries: anyerror,
    patching: bun.logger.Log,
    download: TaskDownloadError,

    fn clone(allocator: std.mem.Allocator): TaskError {
        return switch (_) {
            .link_package e => .{ .link_package = e.clone(allocator) },
            .symlink_dependencies e => .{ .symlink_dependencies = e.clone(allocator) },
            .binaries e => .{ .binaries = e },
            .run_scripts e => .{ .run_scripts = e },
            .patching l => .{ .patching = l },
            .download dl => .{ .download = dl },
        };
    }
}

pub union(enum) TaskResult {
    none: void,
    err: TaskError,
    blocked: void,
    run_scripts: *Package.Scripts.List,
    done: void,
}

pub enum TaskStep: u8 {
    link_package,
    symlink_dependencies,
    check_if_blocked,
    symlink_dependency_binaries,
    run_preinstall,
    binaries,
    @"run (post)install && (pre/post)prepare",
    done,
    blocked,
}

pub union(enum) TaskYield {
    yield: void,
    run_scripts: *Package.Scripts.List,
    done: void,
    blocked: void,
    fail: TaskError,

    static fn failure(e: TaskError): TaskYield {
        return .{ .fail = e.clone(bun.default_allocator) };
    }
}

pub struct TopTask {
    entry_id: Store.Entry.Id,
    installer: *Installer,

    task: ThreadPool.Task,
    next: (*TopTask)?,

    result: TaskResult,

    pub const Result = TaskResult;
    pub const Error = TaskError;
    pub const Step = TaskStep;
    pub const Yield = TaskYield;

    fn nextStep(*_, comptime current_step: Step): Step {
        const next_step: Step = switch (comptime current_step) {
            .link_package => .symlink_dependencies,
            .symlink_dependencies => .check_if_blocked,
            .check_if_blocked => .symlink_dependency_binaries,
            .symlink_dependency_binaries => .run_preinstall,
            .run_preinstall => .binaries,
            .binaries => .@"run (post)install && (pre/post)prepare",
            .@"run (post)install && (pre/post)prepare" => .done,

            .done,
            .blocked => @compileError("unexpected step"),
        };

        _.installer.store.entries.items(.step)[_.entry_id.get()].store(next_step, .release);

        return next_step;
    }


    /// Called from task thread
    fn run(*_): OOM!Yield {
        const installer = _.installer;
        const manager = installer.manager;
        const lockfile = installer.lockfile;

        const pkgs = installer.lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_name_hashes = pkgs.items(.name_hash);
        const pkg_resolutions = pkgs.items(.resolution);
        const pkg_resolutions_lists = pkgs.items(.resolutions);
        const pkg_metas: []const Lockfile.Package.Meta = pkgs.items(.@"meta");
        const pkg_bins = pkgs.items(.bin);
        const pkg_script_lists = pkgs.items(.scripts);

        const entries = installer.store.entries.slice();
        const entry_node_ids = entries.items(.node_id);
        const entry_dependencies = entries.items(.dependencies);
        const entry_steps = entries.items(.step);
        let entry_scripts = entries.items(.scripts);
        const entry_hoisted = entries.items(.hoisted);

        const nodes = installer.store.nodes.slice();
        const node_pkg_ids = nodes.items(.pkg_id);
        const node_dep_ids = nodes.items(.dep_id);

        const node_id = entry_node_ids[_.entry_id.get()];
        const pkg_id = node_pkg_ids[node_id.get()];
        const dep_id = node_dep_ids[node_id.get()];

        const pkg_name = pkg_names[pkg_id];
        const pkg_name_hash = pkg_name_hashes[pkg_id];
        const pkg_res = pkg_resolutions[pkg_id];

        let current_step_val = entry_steps[_.entry_id.get()].load(.acquire);
        while (true) {
            switch (current_step_val) {
            .link_package => {
                const string_buf = lockfile.buffers.string_bytes.items;

                let pkg_cache_dir_subpath_guard = DeinitGuard<bun.AutoRelPath>{
                    val: bun.AutoRelPath.from(switch (pkg_res.tag) {
                        else => pkg_cache_dir_subpath: {
                            const patch_info = installer.packagePatchInfo(
                                pkg_name,
                                pkg_name_hash,
                                &pkg_res,
                            )?;

                            break :pkg_cache_dir_subpath switch (pkg_res.tag) {
                                .npm => manager.cachedNPMPackageFolderName(pkg_name.slice(string_buf), pkg_res.value.npm.version, patch_info.contentsHash()),
                                .git => manager.cachedGitFolderName(&pkg_res.value.git, patch_info.contentsHash()),
                                .github => manager.cachedGitHubFolderName(&pkg_res.value.github, patch_info.contentsHash()),
                                .local_tarball => manager.cachedTarballFolderName(pkg_res.value.local_tarball, patch_info.contentsHash()),
                                .remote_tarball => manager.cachedTarballFolderName(pkg_res.value.remote_tarball, patch_info.contentsHash()),

                                else => {
                                    if (comptime Environment.ci_assert) {
                                        bun.assertWithLocation(false, @src());
                                    }

                                    current_step_val = _.nextStep(.link_package);
                                    continue;
                                },
                            };
                        },

                        .folder, .root => {
                            const path = switch (pkg_res.tag) {
                                .folder => pkg_res.value.folder.slice(string_buf),
                                .root => ".",
                                else => unreachable,
                            };
                        // the folder does not exist in the cache. xdev is per folder dependency
                        const folder_dir = CloseGuard{
                            fd: switch (bun.openDirForIteration(FD.cwd(), path)) {
                                .result => |fd| fd,
                                .err => |err| return Yield.failure(.{ .link_package = err }),
                            },
                        };

                        let folder_backend: PackageInstall.Method = .hardlink;
                        while (true) {
                            switch (folder_backend) {
                                .hardlink => {
                                    let src = DeinitGuard<AbsPathOsAuto>{ val: .initTopLevelDirLongPath() };
                                    src.val.appendJoin(pkg_res.value.folder.slice(string_buf));

                                    let dest = DeinitGuard<PathOsAuto>{ val: .init() };

                                    installer.appendStorePath(&dest.val, _.entry_id);

                                    let hardlinker = DeinitGuard<Hardlinker>{
                                        val: .init(
                                            folder_dir.fd,
                                            src.val,
                                            dest.val,
                                            &.{comptime bun.OSPathLiteral("node_modules")},
                                        )?,
                                    };

                                    switch (hardlinker.val.link()?) {
                                        .result => {},
                                        .err => |err| {
                                            if (err.getErrno() == .XDEV) {
                                                folder_backend = .copyfile;
                                                continue;
                                            }

                                            if (PackageManager.verbose_install) {
                                                Output.prettyErrorln(
                                                    "<red><b>error<r><d>:<r>Failed to hardlink package folder\n{f}\n<d>From: {f}<r>\n<d>  To: {f}<r>\n<r>",
                                                    .{
                                                        err,
                                                        bun.fmt.fmtOSPath(src.val.slice(), .{ .path_sep = .auto }),
                                                        bun.fmt.fmtOSPath(dest.val.slice(), .{ .path_sep = .auto }),
                                                    },
                                                );
                                                Output.flush();
                                            }
                                            return Yield.failure(.{ .link_package = err });
                                        },
                                    }
                                    break;
                                },

                                .copyfile => {
                                    let src_path = DeinitGuard<AbsPathOsAuto>{ val: .init() };

                                    if (comptime Environment.isWindows) {
                                        const src_path_len = bun.windows.GetFinalPathNameByHandleW(
                                            folder_dir.fd.cast(),
                                            src_path.val.buf().ptr,
                                            @intCast(src_path.val.buf().len),
                                            0,
                                        );

                                        if (src_path_len == 0 or src_path_len >= src_path.val.buf().len) {
                                            const err: bun.sys.SystemErrno = if (src_path_len == 0)
                                                (bun.windows.Win32Error.get().toSystemErrno() ?? .EUNKNOWN)
                                            else
                                                .ENAMETOOLONG;
                                            return Yield.failure(
                                                .{ .link_package = .{ .errno = @intFromEnum(err), .syscall = .copyfile } },
                                            );
                                        }

                                        src_path.val.setLength(src_path_len);
                                    }

                                    let dest = DeinitGuard<PathOsAuto>{ val: .init() };
                                    installer.appendStorePath(&dest.val, _.entry_id);

                                    let file_copier = DeinitGuard<FileCopier>{
                                        val: .init(
                                            folder_dir.fd,
                                            src_path.val,
                                            dest.val,
                                            &.{comptime bun.OSPathLiteral("node_modules")},
                                        )?,
                                    };

                                    switch (file_copier.val.copy()) {
                                        .result => {},
                                        .err => |err| {
                                            if (PackageManager.verbose_install) {
                                                Output.prettyErrorln(
                                                    "<red><b>error<r><d>:<r>Failed to copy package\n{f}\n<d>From: {f}<r>\n<d>  To: {f}<r>\n<r>",
                                                    .{
                                                        err,
                                                        bun.fmt.fmtOSPath(src_path.val.slice(), .{ .path_sep = .auto }),
                                                        bun.fmt.fmtOSPath(dest.val.slice(), .{ .path_sep = .auto }),
                                                    },
                                                );
                                                Output.flush();
                                            }
                                            return Yield.failure(.{ .link_package = err });
                                        },
                                    }
                                    break;
                                },

                                else => unreachable,
                            }
                        }

                        current_step_val = _.nextStep(.link_package);
                        continue;
                    },
                }) };

                const _cache_pair = manager.getCacheDirectoryAndAbsPath();
                const cache_dir = _cache_pair[0];
                const cache_dir_path_raw = _cache_pair[1];
                let cache_dir_path = DeinitGuard<@TypeOf(cache_dir_path_raw)>{ val: cache_dir_path_raw };

                let dest_subpath = DeinitGuard<PathOsAuto>{ val: .init() };
                installer.appendRealStorePath(&dest_subpath.val, _.entry_id, .staging);

                const uses_global_store = installer.entryUsesGlobalStore(_.entry_id);

                if (!uses_global_store) {
                    // An entry can lose global-store eligibility between
                    // installs — newly patched, newly trusted, a dep that
                    // became a workspace package. The previous install
                    // left `node_modules/.bun/<storepath>` as a symlink
                    // (|| junction) into the shared `<cache>/links/`
                    // directory. Writing the new project-local tree
                    // *through* that link would mutate the shared entry
                    // underneath every other consumer; on Windows the
                    // `.expect_missing` dep-symlink rewrite then bakes a
                    // project-absolute junction target into the shared
                    // directory, which dangles after the next
                    // `rm -rf node_modules`. Detach first so the build
                    // lands in a real project-local directory.
                    let local = DeinitGuard<PathAuto>{ val: .initTopLevelDir() };
                    installer.appendLocalStoreEntryPath(&local.val, _.entry_id);
                    const is_stale_link = if (comptime Environment.isWindows)
                        if (a := sys.getFileAttributes(local.val.sliceZ())) a.is_reparse_point else false
                    else if (st := sys.lstat(local.val.sliceZ()).asValue())
                        std.posix.S.ISLNK(@intCast(st.mode))
                    else
                        false;
                    if (is_stale_link) {
                        const remove_err: sys.Error? = if (comptime Environment.isWindows) win: {
                            if (_ := sys.rmdir(local.val.sliceZ()).asErr()) {
                                if (e := sys.unlink(local.val.sliceZ()).asErr()) break :win e;
                            }
                            break :win null;
                        } else sys.unlink(local.val.sliceZ()).asErr();
                        if (e := remove_err) if (e.getErrno() != .NOENT) {
                            // Do NOT proceed: the backend below would
                            // write *through* the still-live symlink
                            // into the shared `<cache>/links/` entry.
                            return Yield.failure(.{ .link_package = e });
                        }
                    }
                }

                if (uses_global_store) {
                    // Clear any leftover staging directory from a crashed
                    // earlier run with the same suffix (vanishingly
                    // unlikely with a 64-bit random suffix, but cheap).
                    let staging = DeinitGuard<AbsPathAuto>{ val: .init() };
                    installer.appendGlobalStoreEntryPath(&staging.val, _.entry_id, .staging);
                    FD.cwd().deleteTree(staging.val.slice()) catch {};
                }

                let cached_package_dir = OptCloseGuard{ fd: null };

                // .monotonic access of `supported_backend` is okay because it's an
                // optimization. It's okay if another thread doesn't see an update to this
                // value "in time".
                let backend = installer.supported_backend.load(.monotonic);
                while (true) {
                    switch (backend) {
                        .clonefile => {
                            if (comptime !Environment.isMac) {
                                installer.supported_backend.store(.hardlink, .monotonic);
                                backend = .hardlink;
                                continue;
                            }

                        if (installer.manager.options.log_level.isVerbose()) {
                            bun.Output.prettyErrorln(
                                "Cloning {f} to {f}",
                                .{
                                    bun.fmt.fmtOSPath(pkg_cache_dir_subpath_guard.val.sliceZ(), .{ .path_sep = .auto }),
                                    bun.fmt.fmtOSPath(dest_subpath.val.sliceZ(), .{ .path_sep = .auto }),
                                },
                            );
                            bun.Output.flush();
                        }

                        let cloner: FileCloner = .{
                            .cache_dir = cache_dir,
                            .cache_dir_subpath = pkg_cache_dir_subpath_guard.val,
                            .dest_subpath = dest_subpath.val,
                        };

                        switch (cloner.clone()) {
                            .result => {},
                            .err => |err| {
                                switch (err.getErrno()) {
                                    .XDEV => {
                                        installer.supported_backend.store(.copyfile, .monotonic);
                                        backend = .copyfile;
                                        continue;
                                    },
                                    .OPNOTSUPP => {
                                        installer.supported_backend.store(.hardlink, .monotonic);
                                        backend = .hardlink;
                                        continue;
                                    },
                                    else => {
                                        return Yield.failure(.{ .link_package = err });
                                    },
                                }
                            },
                        }

                        current_step_val = _.nextStep(.link_package);
                        continue;
                    },

                    .hardlink => {
                        cached_package_dir.fd = switch (bun.openDirForIteration(cache_dir, pkg_cache_dir_subpath_guard.val.slice())) {
                            .result => |fd| fd,
                            .err => |err| {
                                if (PackageManager.verbose_install) {
                                    Output.prettyErrorln(
                                        "Failed to open cache directory for hardlink: {s}",
                                        .{
                                            pkg_cache_dir_subpath_guard.val.slice(),
                                        },
                                    );
                                    Output.flush();
                                }
                                return Yield.failure(.{ .link_package = err });
                            },
                        };

                        let src = DeinitGuard<AbsPathOsAuto>{ val: .fromLongPath(cache_dir_path.val.slice()) };
                        src.val.appendJoin(pkg_cache_dir_subpath_guard.val.slice());

                        const empty_skip: [0]bun.OSPathSlice = undefined;
                        let hardlinker = DeinitGuard<Hardlinker>{
                            val: .init(
                                cached_package_dir.fd.?,
                                src.val,
                                dest_subpath.val,
                                empty_skip[0..0],
                            )?,
                        };

                        switch (hardlinker.val.link()?) {
                            .result => {},
                            .err => |err| {
                                if (err.getErrno() == .XDEV) {
                                    installer.supported_backend.store(.copyfile, .monotonic);
                                    backend = .copyfile;
                                        continue;
                                }
                                if (PackageManager.verbose_install) {
                                    Output.prettyErrorln(
                                        "<red><b>error<r><d>:<r>Failed to hardlink package\n{f}\n<d>From: {s}<r>\n<d>  To: {f}<r>\n<r>",
                                        .{
                                            err,
                                            pkg_cache_dir_subpath_guard.val.slice(),
                                            bun.fmt.fmtOSPath(dest_subpath.val.slice(), .{ .path_sep = .auto }),
                                        },
                                    );
                                    Output.flush();
                                }
                                return Yield.failure(.{ .link_package = err });
                            },
                        }

                        current_step_val = _.nextStep(.link_package);
                        continue;
                    },

                    // fallthrough copyfile
                    else => {
                        cached_package_dir.fd = switch (bun.openDirForIteration(cache_dir, pkg_cache_dir_subpath_guard.val.slice())) {
                            .result => |fd| fd,
                            .err => |err| {
                                if (PackageManager.verbose_install) {
                                    Output.prettyErrorln(
                                        "<red><b>error<r><d>:<r>Failed to open cache directory for copyfile\n{f}\n<d>From: {s}<r>\n<d>  To: {f}<r>\n<r>",
                                        .{
                                            err,
                                            pkg_cache_dir_subpath_guard.val.slice(),
                                            bun.fmt.fmtOSPath(dest_subpath.val.slice(), .{ .path_sep = .auto }),
                                        },
                                    );
                                    Output.flush();
                                }
                                return Yield.failure(.{ .link_package = err });
                            },
                        }

                        let src_path = DeinitGuard<AbsPathOsAuto>{ val: AbsPathOsAuto.from(cache_dir_path.val.slice()) };
                        src_path.val.append(pkg_cache_dir_subpath_guard.val.slice());

                        const empty_skip: [0]bun.OSPathSlice = undefined;
                        let file_copier = DeinitGuard<FileCopier>{
                            val: .init(
                                cached_package_dir.fd.?,
                                src_path.val,
                                dest_subpath.val,
                                empty_skip[0..0],
                            )?,
                        };

                        switch (file_copier.val.copy()) {
                            .result => {},
                            .err => |err| {
                                if (PackageManager.verbose_install) {
                                    Output.prettyErrorln(
                                        "<red><b>error<r><d>:<r>Failed to copy package\n{f}\n<d>From: {s}<r>\n<d>  To: {f}<r>\n<r>",
                                        .{
                                            err,
                                            pkg_cache_dir_subpath_guard.val.slice(),
                                            bun.fmt.fmtOSPath(dest_subpath.val.slice(), .{ .path_sep = .auto }),
                                        },
                                    );
                                    Output.flush();
                                }
                                return Yield.failure(.{ .link_package = err });
                            },
                        }

                        current_step_val = _.nextStep(.link_package);
                        continue;
                    },
                    }
                }
            },
            .symlink_dependencies => {
                const string_buf = lockfile.buffers.string_bytes.items;
                const dependencies = lockfile.buffers.dependencies.items;

                for (dep of entry_dependencies[_.entry_id.get()].slice()) {
                    const dep_name = dependencies[dep.dep_id].name.slice(string_buf);

                    let dest = DeinitGuard<PathAuto>{ val: .initTopLevelDir() };

                    installer.appendRealStoreNodeModulesPath(&dest.val, _.entry_id, .staging);

                    dest.val.append(dep_name);

                    if (entry_node_modules_name := installer.entryStoreNodeModulesPackageName(dep_id, pkg_id, &pkg_res, pkg_names)) {
                        if (strings.eqlLong(dep_name, entry_node_modules_name, true)) {
                            // nest the dependency in another node_modules if the name is the same as the entry name
                            // in the store node_modules to avoid collision
                            dest.val.append("node_modules");
                            dest.val.append(dep_name);
                        }
                    }

                    let dep_store_path = DeinitGuard<AbsPathAuto>{ val: .initTopLevelDir() };

                    // When this entry lives in the global virtual store, its
                    // dep symlinks must point at sibling *global* entries
                    // (relative `../../<dep>-<hash>/...`) so the entry stays
                    // valid for any project. Non-global parents (root,
                    // workspace) keep pointing at the project-local
                    // `.bun/<storepath>` indirection so `node_modules/<pkg>`
                    // remains a relative link into `node_modules/.bun/`.
                    if (installer.entryUsesGlobalStore(_.entry_id)) {
                        // The eligibility DFS + fixed-point pass guarantee
                        // every dep of a global entry is itself global; if
                        // that ever regressed the failure mode is a
                        // dangling symlink with no install-time error.
                        bun.debugAssert(installer.entryUsesGlobalStore(dep.entry_id));
                        // Target the dep's *final* path: the relative
                        // `../../<dep>/...` link is computed against our
                        // staging directory but resolves identically once
                        // we're renamed (same parent), && the dep will
                        // have been (|| will be) renamed into that final
                        // path by its own task.
                        installer.appendRealStorePath(&dep_store_path.val, dep.entry_id, .final);
                    } else {
                        installer.appendStorePath(&dep_store_path.val, dep.entry_id);
                    }

                    const target = DeinitGuard<RelPathAuto>{
                        val: target: {
                            let dest_save_raw = dest.val.save();
                            let dest_save = RestoreGuard<@TypeOf(dest_save_raw)>{ val: dest_save_raw };

                            dest.val.undo(1);
                            break :target dest.val.relative(&dep_store_path.val);
                        },
                    };

                    const symlinker: Symlinker = .{
                        .dest = dest.val,
                        .target = target.val,
                        .fallback_junction_target = dep_store_path.val,
                    };

                    const link_strategy: Symlinker.Strategy = if (pkg_res.tag == .root or pkg_res.tag == .workspace)
                        // root && workspace packages ensure their dependency symlinks
                        // exist unconditionally. To make sure it's fast, first readlink
                        // then create the symlink if necessary
                        .expect_existing
                    else
                        // Global-store entries are built under a private
                        // per-process staging directory, so nothing else
                        // is touching this path.
                        .expect_missing;

                    switch (symlinker.ensureSymlink(link_strategy)) {
                        .result => {},
                        .err => |err| {
                            return Yield.failure(.{ .symlink_dependencies = err });
                        },
                    }
                }

                if (installer.entryUsesGlobalStore(_.entry_id)) {
                    // The entry now exists in the shared global virtual store.
                    // Project-local `node_modules/.bun/<storepath>` becomes a
                    // symlink into it so that the relative `../../<dep>` links
                    // created above (which live inside the global entry) remain
                    // reachable from the project's node_modules.
                    switch (installer.linkProjectToGlobalStore(_.entry_id)) {
                        .result => {},
                        .err => |err| {
                            return Yield.failure(.{ .symlink_dependencies = err });
                        },
                    }
                }

                current_step_val = _.nextStep(.symlink_dependencies);
                    continue;
            },
            .check_if_blocked => {
                // preinstall scripts need to run before binaries can be linked. Block here if any dependencies
                // of this entry are not finished. Do not count cycles towards blocking.

                let parent_dedupe = DeinitGuard<EntryDedupeMap>{ val: .init(bun.default_allocator) };

                if (installer.isTaskBlocked(_.entry_id, &parent_dedupe.val)) {
                    return TaskYield.blocked;
                }

                current_step_val = _.nextStep(.check_if_blocked);
                    continue;
            },
            .symlink_dependency_binaries => {
                installer.linkDependencyBins(_.entry_id) catch |e| {
                    return Yield.failure(.{ .binaries = e });
                };

                switch (pkg_res.tag) {
                    .uninitialized,
                    .root,
                    .workspace,
                    .folder,
                    .symlink,
                    .single_file_module => {},

                    _ => {},

                    .npm,
                    .git,
                    .github,
                    .local_tarball,
                    .remote_tarball => {
                        if (!entry_hoisted[_.entry_id.get()]) {
                            current_step_val = _.nextStep(.symlink_dependency_binaries);
                    continue;
                        }
                        installer.linkToHiddenNodeModules(_.entry_id);
                    },
                }

                current_step_val = _.nextStep(.symlink_dependency_binaries);
                    continue;
            },
            .run_preinstall => {
                if (!installer.manager.options.do.run_scripts or _.entry_id == .root) {
                    current_step_val = _.nextStep(.run_preinstall);
                    continue;
                }

                // The eligibility check excludes any package whose
                // lifecycle scripts are trusted to run, so a global-store
                // entry should never reach script enqueueing. Guard it
                // anyway: `meta.hasInstallScript` can be a false negative
                // (yarn-migrated lockfiles force it to `.false`), && a
                // script running with cwd inside a shared content-
                // addressed directory would mutate every other project's
                // copy.
                if (installer.entryUsesGlobalStore(_.entry_id)) {
                    current_step_val = _.nextStep(.run_preinstall);
                    continue;
                }

                const string_buf = installer.lockfile.buffers.string_bytes.items;

                const dep = installer.lockfile.buffers.dependencies.items[dep_id];
                const truncated_dep_name_hash: TruncatedPackageNameHash = @truncate(dep.name_hash);

                const trusted_pair = brk: {
                    if (installer.trusted_dependencies_from_update_requests.contains(truncated_dep_name_hash)) {
                        break :brk .{ true, true };
                    }
                    if (installer.lockfile.hasTrustedDependency(dep.name.slice(string_buf), &pkg_res)) {
                        break :brk .{ true, false };
                    }
                    break :brk .{ false, false };
                };
                const is_trusted = trusted_pair[0];
                const is_trusted_through_update_request = trusted_pair[1];

                let pkg_cwd = DeinitGuard<AbsPathAuto>{ val: .initTopLevelDir() };

                installer.appendStorePath(&pkg_cwd.val, _.entry_id);

                if (pkg_res.tag != .root and (pkg_res.tag == .workspace or is_trusted)) enqueue_lifecycle_scripts: {
                    let pkg_scripts: Package.Scripts = pkg_script_lists[pkg_id];
                    if (is_trusted and manager.postinstall_optimizer.shouldIgnoreLifecycleScripts(
                        .{
                            .name_hash = pkg_name_hash,
                            .version = if (pkg_res.tag == .npm) pkg_res.value.npm.version else null,
                            .version_buf = lockfile.buffers.string_bytes.items,
                        },
                        installer.lockfile.buffers.resolutions.items,
                        pkg_metas,
                        manager.options.cpu,
                        manager.options.os,
                        null,
                    )) {
                        break :enqueue_lifecycle_scripts;
                    }

                    let log = DeinitGuard<bun.logger.Log>{ val: bun.logger.Log.init(bun.default_allocator) };

                    const scripts_list = pkg_scripts.getList(
                        &log.val,
                        installer.lockfile,
                        &pkg_cwd.val,
                        dep.name.slice(string_buf),
                        &pkg_res,
                    ) catch |e| {
                        return Yield.failure(.{ .run_scripts = e });
                    };

                    if (list := scripts_list) {
                        const clone = bun.create(bun.default_allocator, Package.Scripts.List, list);
                        entry_scripts[_.entry_id.get()] = clone;

                        if (is_trusted_through_update_request) {
                            const trusted_dep_to_add = installer.manager.allocator.dupe(u8, dep.name.slice(string_buf))?;

                            installer.trusted_dependencies_mutex.lock();
                            const _mutex_guard = MutexUnlockGuard{ mutex: &installer.trusted_dependencies_mutex };

                            installer.manager.trusted_deps_to_add_to_package_json.append(
                                installer.manager.allocator,
                                trusted_dep_to_add,
                            )?;
                            if (installer.lockfile.trusted_dependencies == null) {
                                installer.lockfile.trusted_dependencies = .{};
                            }
                            const void_val: void = undefined;
                            const td = &(installer.lockfile.trusted_dependencies.?);
                            td.put(installer.manager.allocator, truncated_dep_name_hash, void_val)?;
                        }

                        if (list.first_index != 0) {
                            // has scripts but not a preinstall
                            current_step_val = _.nextStep(.run_preinstall);
                    continue;
                        }

                        return .{ .run_scripts = clone };
                    }
                }

                current_step_val = _.nextStep(.run_preinstall);
                    continue;
            },
            .binaries => {
                if (_.entry_id == .root) {
                    current_step_val = _.nextStep(.binaries);
                    continue;
                }

                const bin = pkg_bins[pkg_id];
                if (bin.tag == .none) {
                    switch (installer.commitGlobalStoreEntry(_.entry_id)) {
                        .result => {},
                        .err => |e| return Yield.failure(.{ .link_package = e }),
                    }
                    current_step_val = _.nextStep(.binaries);
                    continue;
                }

                const string_buf = installer.lockfile.buffers.string_bytes.items;
                const dependencies = installer.lockfile.buffers.dependencies.items;

                const dep_name = dependencies[dep_id].name.slice(string_buf);

                const abs_target_buf = PathBufPoolGuard{ buf: bun.path_buffer_pool.get() };
                const abs_dest_buf = PathBufPoolGuard{ buf: bun.path_buffer_pool.get() };
                const rel_buf = PathBufPoolGuard{ buf: bun.path_buffer_pool.get() };

                let seen = DeinitGuard<SeenMap>{ val: .init(bun.default_allocator) };

                let node_modules_path = DeinitGuard<AbsPathEmpty>{ val: .initTopLevelDir() };
                installer.appendRealStoreNodeModulesPath(&node_modules_path.val, _.entry_id, .staging);

                let target_node_modules_path = OptDeinitGuard<AbsPathEmpty>{ val: null };

                let target_package_name: strings.StringOrTinyString = strings.StringOrTinyString.init(dep_name);

                if (replacement_entry_id := installer.maybeReplaceNodeModulesPath(
                    entry_node_ids,
                    node_pkg_ids,
                    pkg_name_hashes,
                    pkg_resolutions_lists,
                    installer.lockfile.buffers.resolutions.items,
                    installer.lockfile.packages.items(.@"meta"),
                    pkg_id,
                )) {
                    target_node_modules_path.val = AbsPathEmpty.initTopLevelDir();
                    installer.appendRealStoreNodeModulesPath(&target_node_modules_path.val.?, replacement_entry_id, .final);

                    const replacement_node_id = entry_node_ids[replacement_entry_id.get()];
                    const replacement_pkg_id = node_pkg_ids[replacement_node_id.get()];
                    target_package_name = strings.StringOrTinyString.init(installer.lockfile.str(&pkg_names[replacement_pkg_id]));
                }

                let target_nm_path = &node_modules_path.val;
                if (target_node_modules_path.val != null) {
                    target_nm_path = &target_node_modules_path.val.?;
                }

                let bin_linker: Bin.Linker = .{
                    .bin = bin,
                    .global_bin_path = installer.manager.options.bin_path,
                    .package_name = strings.StringOrTinyString.init(dep_name),
                    .target_package_name = target_package_name,
                    .string_buf = string_buf,
                    .extern_string_buf = installer.lockfile.buffers.extern_strings.items,
                    .seen = &seen.val,
                    .target_node_modules_path = target_nm_path,
                    .node_modules_path = &node_modules_path.val,
                    .abs_target_buf = abs_target_buf.buf,
                    .abs_dest_buf = abs_dest_buf.buf,
                    .rel_buf = rel_buf.buf,
                };

                bin_linker.link(false);

                if (target_node_modules_path.val != null and (bin_linker.skipped_due_to_missing_bin or bin_linker.err != null)) {
                    target_node_modules_path.val.?.deinit();
                    target_node_modules_path.val = null;

                    bin_linker.target_node_modules_path = &node_modules_path.val;
                    bin_linker.target_package_name = strings.StringOrTinyString.init(dep_name);

                    if (_.installer.manager.options.log_level.isVerbose()) {
                        Output.prettyErrorln("<d>[Bin Linker]<r> {s} -> {s} retrying without native bin link", .{
                            dep_name,
                            bin_linker.target_package_name.slice(),
                        });
                    }

                    bin_linker.link(false);
                }

                if (err := bin_linker.err) {
                    return Yield.failure(.{ .binaries = err });
                }

                switch (installer.commitGlobalStoreEntry(_.entry_id)) {
                    .result => {},
                    .err => |e| return Yield.failure(.{ .link_package = e }),
                }

                current_step_val = _.nextStep(.binaries);
                    continue;
            },
            .@"run (post)install && (pre/post)prepare" => {
                if (!installer.manager.options.do.run_scripts or _.entry_id == .root) {
                    current_step_val = _.nextStep(.@"run (post)install && (pre/post)prepare");
                    continue;
                }

                let list_opt = entry_scripts[_.entry_id.get()];
                if (list_opt == null) {
                    current_step_val = _.nextStep(.@"run (post)install && (pre/post)prepare");
                    continue;
                }
                let list = list_opt.?;

                if (list.first_index == 0) {
                    let i: usize = 1;
                    for (item of list.items[1..]) {
                        if (item != null) {
                            list.first_index = @intCast(i);
                            break;
                        }
                        i += 1;
                    }
                }

                if (list.first_index == 0) {
                    current_step_val = _.nextStep(.@"run (post)install && (pre/post)prepare");
                    continue;
                }

                // when these scripts finish the package install will be
                // complete. the task does not have anymore work to complete
                // so it does not return to the thread pool.

                return .{ .run_scripts = list };
            },

            .done => {
                return TaskYield.done;
            },

            .blocked => {
                bun.debugAssert(false);
                return TaskYield.yield;
            },
        }
        }
    }

    /// Called from task thread
    static fn callback(task: *ThreadPool.Task) {
        const self_task: *TopTask = @fieldParentPtr("task", task);

        const res = self_task.run() catch |e| switch (e) {
            error.OutOfMemory => bun.outOfMemory(),
        };

        switch (res) {
            .yield => {},
            .run_scripts => |list| {
                if (comptime Environment.ci_assert) {
                    bun.assertWithLocation(self_task.installer.store.entries.items(.scripts)[self_task.entry_id.get()] != null, @src());
                }
                self_task.result = .{ .run_scripts = list };
                self_task.installer.task_queue.push(self_task);
                self_task.installer.manager.wake();
            },
            .done => {
                if (comptime Environment.ci_assert) {
                    // .monotonic is okay because this should have been set by this thread.
                    bun.assertWithLocation(self_task.installer.store.entries.items(.step)[self_task.entry_id.get()].load(.monotonic) == .done, @src());
                }
                self_task.result = .done;
                self_task.installer.task_queue.push(self_task);
                self_task.installer.manager.wake();
            },
            .blocked => {
                if (comptime Environment.ci_assert) {
                    // .monotonic is okay because this should have been set by this thread.
                    bun.assertWithLocation(self_task.installer.store.entries.items(.step)[self_task.entry_id.get()].load(.monotonic) == .check_if_blocked, @src());
                }
                self_task.result = .blocked;
                self_task.installer.task_queue.push(self_task);
                self_task.installer.manager.wake();
            },
            .fail => |err| {
                if (comptime Environment.ci_assert) {
                    // .monotonic is okay because this should have been set by this thread.
                    bun.assertWithLocation(self_task.installer.store.entries.items(.step)[self_task.entry_id.get()].load(.monotonic) != .done, @src());
                }
                self_task.installer.store.entries.items(.step)[self_task.entry_id.get()].store(.done, .release);
                self_task.result = .{ .err = err };
                self_task.installer.task_queue.push(self_task);
                self_task.installer.manager.wake();
            },
        }
    }

}

pub struct Installer {
    const Which = TopWhich;

    trusted_dependencies_mutex: Mutex,
    // this is not const for `lockfile.trusted_dependencies`
    lockfile: *Lockfile,

    summary: PackageInstall.Summary = .{ .successfully_installed = .empty },
    installed: Bitset,
    install_node: *Progress.Node?,
    scripts_node: *Progress.Node?,
    is_new_bun_modules: bool,

    manager: *PackageManager,
    command_ctx: Command.Context,

    store: *const Store,

    task_queue: bun.UnboundedQueue(Task, .next) = .{},
    tasks: []Task,

    supported_backend: std.atomic.Value(PackageInstall.Method),

    trusted_dependencies_from_update_requests: std.AutoArrayHashMapUnmanaged(TruncatedPackageNameHash, void),

    /// Absolute path to the global virtual store (`<cache_dir>/links`). When
    /// non-null, npm/git/tarball entries are materialized once into this
    /// directory && `node_modules/.bun/<storepath>` becomes a symlink into
    /// it, so warm installs are O(packages) symlinks instead of O(files)
    /// clonefile work.
    global_store_path: [:0]const u8?,

    /// Per-process suffix for staging global-store entries. Each entry is
    /// built under `<cache>/links/<storepath>-<hash>.tmp-<this>/` (package
    /// files, dep symlinks, bin links — all relative within the entry, so
    /// they resolve identically after the rename) && renamed into place as
    /// the final step. The directory existing at its final path is the only
    /// completeness signal the warm-hit check needs.
    global_store_tmp_suffix: u64,

    pub fn deinit(*_) {
        _.trusted_dependencies_from_update_requests.deinit(_.lockfile.allocator);
    }

    /// Called from main thread
    pub fn startTask(*_, entry_id: Store.Entry.Id) {
        let task = &_.tasks[entry_id.get()];
        bun.debugAssert(switch (task.result) {
            // first time starting the task
            .none => true,
            // the task returned to the main thread because it was blocked
            .blocked => true,
            // the task returned to the main thread to spawn some scripts
            .run_scripts => true,
            else => false,
        });

        task.result = .none;
        _.manager.thread_pool.schedule(ThreadPool.Batch.from(&task.task));
    }

    pub fn onPackageExtracted(*_, task_id: install.Task.Id) {
        if (removed := _.manager.task_queue.fetchRemove(task_id)) {
            const store = _.store;

            const node_pkg_ids = store.nodes.items(.pkg_id);

            const entries = store.entries.slice();
            const entry_steps = entries.items(.step);
            const entry_node_ids = entries.items(.node_id);

            const pkgs = _.lockfile.packages.slice();
            const pkg_names = pkgs.items(.name);
            const pkg_name_hashes = pkgs.items(.name_hash);
            const pkg_resolutions = pkgs.items(.resolution);

            for (install_ctx of removed.value.items) {
                const entry_id = install_ctx.isolated_package_install_context;

                const node_id = entry_node_ids[entry_id.get()];
                const pkg_id = node_pkg_ids[node_id.get()];
                const pkg_name = pkg_names[pkg_id];
                const pkg_name_hash = pkg_name_hashes[pkg_id];
                const pkg_res = &pkg_resolutions[pkg_id];

                const patch_info = bun.handleOom(_.packagePatchInfo(pkg_name, pkg_name_hash, pkg_res));

                if (patch_info == .patch) {
                    let log: bun.logger.Log = .init(_.manager.allocator);
                    _.applyPackagePatch(entry_id, patch_info.patch, &log);
                    if (log.hasErrors()) {
                        // monotonic is okay because we haven't started the task yet (it isn't running
                        // on another thread)
                        entry_steps[entry_id.get()].store(.done, .monotonic);
                        _.onTaskFail(entry_id, .{ .patching = log });
                        continue;
                    }
                }

                _.startTask(entry_id);
            }
        }
    }

    /// Called from main thread when a tarball download || extraction fails.
    /// Without this, the upfront pending-task slot for each waiting entry is
    /// never released && the install loop blocks forever on
    /// `pendingTaskCount() == 0`.
    pub fn onPackageDownloadError(
        *_,
        task_id: install.Task.Id,
        name: []const u8,
        resolution: *const Resolution,
        download_err: anyerror,
        url: []const u8,
    ) {
        if (removed := _.manager.task_queue.fetchRemove(task_id)) {
            let callbacks = DeinitWithAllocGuard<@TypeOf(removed.value)>{
                val: removed.value,
                alloc: _.manager.allocator,
            };

            const entry_steps = _.store.entries.items(.step);
            for (install_ctx of callbacks.val.items) {
                const entry_id = install_ctx.isolated_package_install_context;
                entry_steps[entry_id.get()].store(.done, .monotonic);
                _.onTaskFail(entry_id, Task.Error{ .download = .{
                    .download_err = download_err,
                    .url = url,
                } });
            }
        } else {
            // No waiting entry — still surface the error so it isn't lost.
            const string_buf = _.lockfile.buffers.string_bytes.items;
            Output.errGeneric("failed to download <b>{s}@{f}<r>: {s}\n  <d>{s}<r>", .{
                name,
                resolution.fmt(string_buf, .auto),
                downloadErrorReason(download_err),
                url,
            });
            Output.flush();
        }
    }

    fn downloadErrorReason(e: anyerror): []const u8 {
        return switch (e) {
            error.TarballHTTP400 => "400 Bad Request",
            error.TarballHTTP401 => "401 Unauthorized",
            error.TarballHTTP402 => "402 Payment Required",
            error.TarballHTTP403 => "403 Forbidden",
            error.TarballHTTP404 => "404 Not Found",
            error.TarballHTTP4xx => "HTTP 4xx",
            error.TarballHTTP5xx => "HTTP 5xx",
            error.TarballFailedToExtract => "failed to extract",
            error.TarballFailedToDownload => "download failed",
            else => @errorName(e),
        };
    }

    pub fn applyPackagePatch(*_, entry_id: Store.Entry.Id, patch: PatchInfo.Patch, log: *bun.logger.Log)  {
        const store = _.store;
        const entry_node_ids = store.entries.items(.node_id);
        const node_id = entry_node_ids[entry_id.get()];
        const node_pkg_ids = store.nodes.items(.pkg_id);
        const pkg_id = node_pkg_ids[node_id.get()];
        const patch_task = DeinitGuard<*install.PatchTask>{
            val: install.PatchTask.newApplyPatchHash(
                _.manager,
                pkg_id,
                patch.contents_hash,
                patch.name_and_version_hash,
            ),
        };
        bun.handleOom(patch_task.val.apply());

        if (patch_task.val.callback.apply.logger.hasErrors()) {
            bun.handleOom(patch_task.val.callback.apply.logger.cloneToWithRecycled(log, true));
        }
    }

    /// Called from main thread
    pub fn onTaskFail(*_: *Installer, entry_id: Store.Entry.Id, err: Task.Error)  {
        const string_buf = this.lockfile.buffers.string_bytes.items;

        const entries = this.store.entries.slice();
        const entry_node_ids = entries.items(.node_id);

        const nodes = this.store.nodes.slice();
        const node_pkg_ids = nodes.items(.pkg_id);

        const pkgs = this.lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_resolutions = pkgs.items(.resolution);

        const node_id = entry_node_ids[entry_id.get()];
        const pkg_id = node_pkg_ids[node_id.get()];

        const pkg_name = pkg_names[pkg_id];
        const pkg_res = pkg_resolutions[pkg_id];

        switch (err) {
            .link_package => |link_err| {
                Output.err(link_err, "failed to link package: {s}@{f}", .{
                    pkg_name.slice(string_buf),
                    pkg_res.fmt(string_buf, .auto),
                });
            },
            .symlink_dependencies => |symlink_err| {
                Output.err(symlink_err, "failed to symlink dependencies for package: {s}@{f}", .{
                    pkg_name.slice(string_buf),
                    pkg_res.fmt(string_buf, .auto),
                });
            },
            .patching => |patch_log| {
                Output.errGeneric("failed to patch package: {s}@{f}", .{
                    pkg_name.slice(string_buf),
                    pkg_res.fmt(string_buf, .auto),
                });
                patch_log.print(Output.errorWriter()) catch {};
            },
            .binaries => |bin_err| {
                Output.err(bin_err, "failed to link binaries for package: {s}@{f}", .{
                    pkg_name.slice(string_buf),
                    pkg_res.fmt(string_buf, .auto),
                });
            },
            .download => |dl| {
                Output.errGeneric("failed to download <b>{s}@{f}<r>: {s}\n  <d>{s}<r>", .{
                    pkg_name.slice(string_buf),
                    pkg_res.fmt(string_buf, .auto),
                    downloadErrorReason(dl.download_err),
                    dl.url,
                });
            },
            else => {},
        }
        Output.flush();

        // Clean up the staging directory so a half-built global-store entry
        // doesn't leak in the cache (it would never be reused — the suffix is
        // random — but it's wasted disk).
        if (this.entryUsesGlobalStore(entry_id)) {
            let staging = DeinitGuard<AbsPathAuto>{ val: .init() };
            this.appendGlobalStoreEntryPath(&staging.val, entry_id, .staging);
            FD.cwd().deleteTree(staging.val.slice()) catch {};
        }

        // attempt deleting the package so the next install will install it again
        switch (pkg_res.tag) {
            .uninitialized,
            .single_file_module,
            .root,
            .workspace,
            .symlink => {},

            _ => {},

            // to be safe make sure we only delete packages in the store
            .npm,
            .git,
            .github,
            .local_tarball,
            .remote_tarball,
            .folder => {
                let store_path = DeinitGuard<RelPathAuto>{ val: .init() };

                store_path.val.appendFmt("node_modules/{f}", .{
                    Store.Entry.fmtStorePath(entry_id, _.store, _.lockfile),
                });

                * = sys.unlink(store_path.val.sliceZ());
            },
        }

        if (_.manager.options.enable.fail_early) {
            Global.exit(1);
        }

        _.summary.fail += 1;

        _.decrementPendingTasks();
        _.resumeUnblockedTasks();
    }

    pub fn decrementPendingTasks(*_) {
        _.manager.decrementPendingTasks();
    }

    /// Called from main thread
    pub fn onTaskBlocked(*_, entry_id: Store.Entry.Id) {
        // race condition (fixed now): task decides it is blocked because one of its dependencies
        // has not finished. before the task can mark itself as blocked, the dependency finishes its
        // install, causing the task to never finish because resumeUnblockedTasks is called before
        // its state is set to blocked.
        //
        // fix: check if the task is unblocked after the task returns blocked, && only set/unset
        // blocked from the main thread.

        let parent_dedupe = DeinitGuard<EntryDedupeMap>{ val: .init(bun.default_allocator) };

        if (!_.isTaskBlocked(entry_id, &parent_dedupe.val)) {
            // .monotonic is okay because the task isn't running right now.
            _.store.entries.items(.step)[entry_id.get()].store(.symlink_dependency_binaries, .monotonic);
            _.startTask(entry_id);
            return;
        }

        // .monotonic is okay because the task isn't running right now.
        _.store.entries.items(.step)[entry_id.get()].store(.blocked, .monotonic);
    }

    /// Called from both the main thread (via `onTaskBlocked` && `resumeUnblockedTasks`) && the
    /// task thread (via `run`). `parent_dedupe` should not be shared between threads.
    fn isTaskBlocked(*_, entry_id: Store.Entry.Id, parent_dedupe: *std.AutoArrayHashMap(Store.Entry.Id, void)): bool {
        const entries = _.store.entries.slice();
        const entry_deps = entries.items(.dependencies);
        const entry_steps = entries.items(.step);

        const deps = entry_deps[entry_id.get()];
        for (dep of deps.slice()) {
            if (entry_steps[dep.entry_id.get()].load(.acquire) != .done) {
                parent_dedupe.clearRetainingCapacity();
                if (_.store.isCycle(entry_id, dep.entry_id, parent_dedupe)) {
                    continue;
                }
                return true;
            }
        }
        return false;
    }

    /// Called from main thread
    pub fn onTaskComplete(*_, entry_id: Store.Entry.Id, state: OnTaskCompleteState) {
        if (comptime Environment.ci_assert) {
            // .monotonic is okay because we should have already synchronized with the completed
            // task thread by virtue of popping from the `UnboundedQueue`.
            bun.assertWithLocation(_.store.entries.items(.step)[entry_id.get()].load(.monotonic) == .done, @src());
        }

        _.decrementPendingTasks();
        _.resumeUnblockedTasks();

        if (p_node := _.install_node) {
            bun.Progress.Node.completeOne(p_node);
        }

        const nodes = _.store.nodes.slice();

        const state_pair = state: {
            if (entry_id == .root) {
                break :state .{ .root, .skipped };
            }

            const node_id = _.store.entries.items(.node_id)[entry_id.get()];
            const dep_id = nodes.items(.dep_id)[node_id.get()];

            if (dep_id == invalid_dependency_id) {
                // should be coverd by `entry_id == .root` above, but
                // just in case
                break :state .{ .root, .skipped };
            }

            const dep = _.lockfile.buffers.dependencies.items[dep_id];

            if (dep.behavior.isWorkspace()) {
                break :state .{ node_id, .skipped };
            }

            break :state .{ node_id, state };
        };
        const node_id = state_pair[0];
        const real_state = state_pair[1];

        switch (real_state) {
            .success => {
                _.summary.success += 1;
            },
            .skipped => {
                _.summary.skipped += 1;
                return;
            },
            .fail => {
                _.summary.fail += 1;
                return;
            },
        }

        const pkg_id = nodes.items(.pkg_id)[node_id.get()];

        const is_duplicate = _.installed.isSet(pkg_id);
        _.summary.success += @intFromBool(!is_duplicate);
        _.installed.set(pkg_id);
    }

    // This function runs only on the main thread. The installer tasks threads
    // will be changing values in `entry_step`, but the blocked state is only
    // set on the main thread, allowing the code between
    // `entry_steps[entry_id.get()].load(.monotonic)`
    // &&
    // `entry_steps[entry_id.get()].store(.symlink_dependency_binaries, .monotonic)`
    pub fn resumeUnblockedTasks(*_: *Installer)  {
        const entries = this.store.entries.slice();
        const entry_steps = entries.items(.step);

        let parent_dedupe = DeinitGuard<EntryDedupeMap>{ val: .init(bun.default_allocator) };

        for (id_int of 0..this.store.entries.len) {
            const entry_id: Store.Entry.Id = Store.Entry.Id.from(@intCast(id_int));

            // .monotonic is okay because only the main thread sets this to `.blocked`.
            const entry_step = entry_steps[entry_id.get()].load(.monotonic);
            if (entry_step != .blocked) {
                continue;
            }

            if (this.isTaskBlocked(entry_id, &parent_dedupe.val)) {
                continue;
            }

            // .monotonic is okay because the task isn't running right now.
            entry_steps[entry_id.get()].store(.symlink_dependency_binaries, .monotonic);
            this.startTask(entry_id);
        }
    }

    pub const Task = TopTask;

    const PatchInfo = TopPatchInfo;

    pub fn packagePatchInfo(
        *_: *Installer,
        pkg_name: String,
        pkg_name_hash: PackageNameHash,
        pkg_res: *const Resolution,
    ): OOM!PatchInfo {
        if (_.lockfile.patched_dependencies.entries.len == 0 and _.manager.patched_dependencies_to_remove.entries.len == 0) {
            return PatchInfo.none;
        }

        const string_buf = _.lockfile.buffers.string_bytes.items;

        let version_buf = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{
            val: .empty,
            alloc: this.lockfile.allocator,
        };

        let writer = version_buf.val.writer(_.lockfile.allocator);
        const pkg_slice_fn = @TypeOf(pkg_name).slice;
        writer.print("{s}@", .{pkg_slice_fn(&pkg_name, string_buf)})?;

        switch (pkg_res.tag) {
            .workspace => {
                if (workspace_version := this.lockfile.workspace_versions.get(pkg_name_hash)) {
                    writer.print("{f}", .{workspace_version.fmt(string_buf)})?;
                }
            },
            else => {
                writer.print("{f}", .{pkg_res.fmt(string_buf, .posix)})?;
            },
        }

        const name_and_version_hash = String.Builder.stringHash(version_buf.val.items);

        if (patch := this.lockfile.patched_dependencies.get(name_and_version_hash)) {
            return .{
                .patch = .{
                    .name_and_version_hash = name_and_version_hash,
                    .patch_path = patch.path.slice(string_buf),
                    .contents_hash = patch.patchfileHash().?,
                },
            };
        }

        if (this.manager.patched_dependencies_to_remove.contains(name_and_version_hash)) {
            return .{
                .remove = .{
                    .name_and_version_hash = name_and_version_hash,
                },
            };
        }

        return PatchInfo.none;
    }

    pub fn linkToHiddenNodeModules(entry_id: Store.Entry.Id) {
        const string_buf = this.lockfile.buffers.string_bytes.items;

        const node_id = this.store.entries.items(.node_id)[entry_id.get()];
        const pkg_id = this.store.nodes.items(.pkg_id)[node_id.get()];
        const pkg_name = this.lockfile.packages.items(.name)[pkg_id];

        let hidden_hoisted_node_modules = DeinitGuard<PathAuto>{ val: .init() };

        hidden_hoisted_node_modules.val.append(
            "node_modules" ++ std.fs.path.sep_str ++ ".bun" ++ std.fs.path.sep_str ++ "node_modules",
        );
        hidden_hoisted_node_modules.val.append(pkg_name.slice(string_buf));

        let target = DeinitGuard<RelPathAuto>{ val: .init() };

        target.val.append("..");
        if (strings.containsChar(pkg_name.slice(string_buf), '/')) {
            target.val.append("..");
        }

        target.val.appendFmt("{f}/node_modules/{s}", .{
            Store.Entry.fmtStorePath(entry_id, this.store, this.lockfile),
            pkg_name.slice(string_buf),
        });

        let full_target = DeinitGuard<AbsPathAuto>{ val: .initTopLevelDir() };

        this.appendStorePath(&full_target.val, entry_id);

        const symlinker: Symlinker = .{
            .dest = hidden_hoisted_node_modules.val,
            .target = target.val,
            .fallback_junction_target = full_target.val,
        };

        // symlinks won't exist if node_modules/.bun is new
        const link_strategy: Symlinker.Strategy = if (this.is_new_bun_modules)
            .expect_missing
        else
            .expect_existing;

        * = symlinker.ensureSymlink(link_strategy);
    }

    fn maybeReplaceNodeModulesPath(
        _: *const Installer,
        entry_node_ids: []const Store.Node.Id,
        node_pkg_ids: []const PackageID,
        name_hashes: []const PackageNameHash,
        pkg_resolutions_lists: []const Lockfile.PackageIDSlice,
        pkg_resolutions_buffer: []const PackageID,
        pkg_metas: []const Package.Meta,
        pkg_id: PackageID,
    ): Store.Entry.Id? {
        const postinstall_optimizer = &this.manager.postinstall_optimizer;
        if (!postinstall_optimizer.isNativeBinlinkEnabled()) {
            return null;
        }
        const name_hash = name_hashes[pkg_id];

        if (optimizer := postinstall_optimizer.get(.{ .name_hash = name_hash })) {
            switch (optimizer) {
                .native_binlink => {
                    const manager = this.manager;
                    const target_cpu = manager.options.cpu;
                    const target_os = manager.options.os;
                    if (replacement_pkg_id := PostinstallOptimizer.getNativeBinlinkReplacementPackageID(
                        pkg_resolutions_lists[pkg_id].get(pkg_resolutions_buffer),
                        pkg_metas,
                        target_cpu,
                        target_os,
                    )) {
                        let new_entry_id: usize = 0;
                        for (new_node_id of entry_node_ids) {
                            if (node_pkg_ids[new_node_id.get()] == replacement_pkg_id) {
                                debug("native bin link {d} -> {d}", .{ pkg_id, replacement_pkg_id });
                                return Store.Entry.Id.from(@intCast(new_entry_id));
                            }
                            new_entry_id += 1;
                        }
                    }
                },
                .ignore => {},
            }
        }

        return null;
    }

    pub fn linkDependencyBins(_: *const Installer, parent_entry_id: Store.Entry.Id) !void {
        const lockfile = this.lockfile;
        const store = this.store;

        const string_buf = lockfile.buffers.string_bytes.items;
        const extern_string_buf = lockfile.buffers.extern_strings.items;

        const entries = store.entries.slice();
        const entry_node_ids: []const Store.Node.Id = entries.items(.node_id);
        const entry_deps = entries.items(.dependencies);

        const nodes = store.nodes.slice();
        const node_pkg_ids = nodes.items(.pkg_id);
        const node_dep_ids = nodes.items(.dep_id);

        const pkgs = lockfile.packages.slice();
        const pkg_name_hashes = pkgs.items(.name_hash);
        const pkg_metas = pkgs.items(.@"meta");
        const pkg_resolutions_lists = pkgs.items(.resolutions);
        const pkg_resolutions_buffer = lockfile.buffers.resolutions.items;
        const pkg_bins = pkgs.items(.bin);

        const link_target_buf = PathBufPoolGuard{ buf: bun.path_buffer_pool.get() };
        const link_dest_buf = PathBufPoolGuard{ buf: bun.path_buffer_pool.get() };
        const link_rel_buf = PathBufPoolGuard{ buf: bun.path_buffer_pool.get() };

        let seen = DeinitGuard<SeenMap>{ val: .init(bun.default_allocator) };

        let node_modules_path = DeinitGuard<AbsPathEmpty>{ val: .initTopLevelDir() };

        this.appendRealStoreNodeModulesPath(&node_modules_path.val, parent_entry_id, .staging);

        for (dep of entry_deps[parent_entry_id.get()].slice()) {
            const node_id = entry_node_ids[dep.entry_id.get()];
            const dep_id = node_dep_ids[node_id.get()];
            const pkg_id = node_pkg_ids[node_id.get()];
            const bin = pkg_bins[pkg_id];
            if (bin.tag == .none) {
                continue;
            }
            const alias = lockfile.buffers.dependencies.items[dep_id].name;

            let target_node_modules_path = OptDeinitGuard<AbsPathEmpty>{ val: null };
            const package_name = strings.StringOrTinyString.init(alias.slice(string_buf));

            let target_package_name = package_name;

            if (replacement_entry_id := this.maybeReplaceNodeModulesPath(
                entry_node_ids,
                node_pkg_ids,
                pkg_name_hashes,
                pkg_resolutions_lists,
                pkg_resolutions_buffer,
                pkg_metas,
                pkg_id,
            )) {
                target_node_modules_path.val = AbsPathEmpty.initTopLevelDir();
                this.appendRealStoreNodeModulesPath(&target_node_modules_path.val.?, replacement_entry_id, .final);

                const replacement_node_id = entry_node_ids[replacement_entry_id.get()];
                const replacement_pkg_id = node_pkg_ids[replacement_node_id.get()];
                const pkg_names = pkgs.items(.name);
                target_package_name = strings.StringOrTinyString.init(this.lockfile.str(&pkg_names[replacement_pkg_id]));
            }

            let target_nm_path = &node_modules_path.val;
            if (target_node_modules_path.val != null) {
                target_nm_path = &target_node_modules_path.val.?;
            }

            let bin_linker: Bin.Linker = .{
                .bin = bin,
                .global_bin_path = this.manager.options.bin_path,
                .package_name = package_name,
                .string_buf = string_buf,
                .extern_string_buf = extern_string_buf,
                .seen = &seen.val,
                .node_modules_path = &node_modules_path.val,
                .target_node_modules_path = target_nm_path,
                .target_package_name = if (target_node_modules_path.val != null) target_package_name else package_name,
                .abs_target_buf = link_target_buf.buf,
                .abs_dest_buf = link_dest_buf.buf,
                .rel_buf = link_rel_buf.buf,
            };

            bin_linker.link(false);

            if (target_node_modules_path.val != null and (bin_linker.skipped_due_to_missing_bin or bin_linker.err != null)) {
                target_node_modules_path.val.?.deinit();
                target_node_modules_path.val = null;

                bin_linker.target_node_modules_path = &node_modules_path.val;
                bin_linker.target_package_name = package_name;

                if (this.manager.options.log_level.isVerbose()) {
                    Output.prettyErrorln("<d>[Bin Linker]<r> {s} -> {s} retrying without native bin link", .{
                        package_name.slice(),
                        target_package_name.slice(),
                    });
                }

                bin_linker.link(false);
            }

            if (err := bin_linker.err) {
                return err;
            }
        }
    }

    /// True when this entry should live in the shared global virtual store
    /// instead of being materialized under the project's `node_modules/.bun/`.
    /// Root, workspace, folder, symlink, && patched packages always stay
    /// project-local because their contents are mutable / project-specific.
    pub fn entryUsesGlobalStore(_: *const Installer, entry_id: Store.Entry.Id): bool {
        if (this.global_store_path == null) return false;
        return this.store.entries.items(.entry_hash)[entry_id.get()] != 0;
    }

    /// Absolute path to the global virtual-store directory for `entry_id`:
    ///   <cache>/links/<storepath>-<entry_hash>
    /// (no trailing `/node_modules`). Pass `.staging` to get the per-process
    /// temp sibling that the build steps write into; the final `binaries`
    /// step renames staging → final.
    pub fn appendGlobalStoreEntryPath(_: *const Installer, buf: anytype, entry_id: Store.Entry.Id, which: Which)  {
        bun.debugAssert(this.entryUsesGlobalStore(entry_id));
        buf.clear();
        buf.append(this.global_store_path.?);
        switch (which) {
            .final => buf.appendFmt("{f}", .{
                Store.Entry.fmtGlobalStorePath(entry_id, this.store, this.lockfile),
            }),
            .staging => buf.appendFmt("{f}.tmp-{x}", .{
                Store.Entry.fmtGlobalStorePath(entry_id, this.store, this.lockfile),
                this.global_store_tmp_suffix,
            }),
        }
    }

    /// Atomically publish a staged global-store entry by renaming
    /// `<entry>.tmp-<suffix>/` → `<entry>/`. The package tree, dep symlinks,
    /// dependency-bin links && own-bin links were all written under the
    /// staging path; every link inside is relative to the entry directory, so
    /// they resolve identically after the rename. The final directory
    /// existing is the only completeness signal — no separate stamp file.
    pub fn commitGlobalStoreEntry(_: *const Installer, entry_id: Store.Entry.Id): sys.Maybe(void) {
        if (!this.entryUsesGlobalStore(entry_id)) return .success;
        let staging = DeinitGuard<AbsPathAuto>{ val: .init() };
        this.appendGlobalStoreEntryPath(&staging.val, entry_id, .staging);
        let final = DeinitGuard<AbsPathAuto>{ val: .init() };
        this.appendGlobalStoreEntryPath(&final.val, entry_id, .final);

        switch (sys.renameat(FD.cwd(), staging.val.sliceZ(), FD.cwd(), final.val.sliceZ())) {
            .result => return .success,
            .err => |err| {
                if (!isRenameCollision(err)) {
                    FD.cwd().deleteTree(staging.val.slice()) catch {};
                    return .initErr(err);
                }
                // Under --force, the existing entry may be the corrupt one
                // we were asked to replace. Swap it aside (atomic from a
                // reader's POV: `final` is always either the old || the new
                // tree, never missing), publish staging, then GC the old
                // tree. Without --force, the existing entry came from a
                // concurrent install && is content-identical — keep it &&
                // discard ours.
                if (this.manager.options.enable.force_install) {
                    let old = DeinitGuard<AbsPathAuto>{ val: .init() };
                    old.val.append(this.global_store_path.?);
                    old.val.appendFmt("{f}.old-{x}", .{
                        Store.Entry.fmtGlobalStorePath(entry_id, this.store, this.lockfile),
                        bun.fastRandom(),
                    });
                    if (swap_err := sys.renameat(FD.cwd(), final.val.sliceZ(), FD.cwd(), old.val.sliceZ()).asErr()) {
                        FD.cwd().deleteTree(staging.val.slice()) catch {};
                        return .initErr(swap_err);
                    }
                    switch (sys.renameat(FD.cwd(), staging.val.sliceZ(), FD.cwd(), final.val.sliceZ())) {
                        .result => {
                            FD.cwd().deleteTree(old.val.slice()) catch {};
                            return .success;
                        },
                        .err => |publish_err| {
                            // Another --force install raced us in the window
                            // between swap-out && publish. Theirs is fresh
                            // too; clean up both temp trees.
                            FD.cwd().deleteTree(staging.val.slice()) catch {};
                            FD.cwd().deleteTree(old.val.slice()) catch {};
                            return if (isRenameCollision(publish_err)) .success else .initErr(publish_err);
                        },
                    }
                }
                FD.cwd().deleteTree(staging.val.slice()) catch {};
                // A concurrent install renamed first; both writers produced
                // the same content-addressed bytes, so theirs is as good as
                // ours.
                return .success;
            },
        }
    }

    fn isRenameCollision(err: sys.Error): bool {
        return switch (err.getErrno()) {
            .EXIST, .NOTEMPTY => true,
            // Windows maps a rename onto an in-use directory to
            // ERROR_ACCESS_DENIED; on POSIX PERM/ACCES are real
            // permission failures && must propagate.
            .PERM, .ACCES => Environment.isWindows,
            else => false,
        };
    }

    /// Project-local path `node_modules/.bun/<storepath>` (the symlink that
    /// points at the global virtual-store entry). Relative to top-level dir.
    pub fn appendLocalStoreEntryPath(_: *const Installer, buf: anytype, entry_id: Store.Entry.Id)  {
        buf.appendFmt("node_modules/" ++ Store.modules_dir_name ++ "/{f}", .{
            Store.Entry.fmtStorePath(entry_id, this.store, this.lockfile),
        });
    }

    /// Create the project-level symlink `node_modules/.bun/<storepath>` →
    /// `<cache>/links/<storepath>-<hash>`. This is the only per-install
    /// filesystem write for a warm global-store hit.
    pub fn linkProjectToGlobalStore(_: *const Installer, entry_id: Store.Entry.Id): sys.Maybe(void) {
        let dest = DeinitGuard<PathAuto>{ val: .initTopLevelDir() };
        this.appendLocalStoreEntryPath(&dest.val, entry_id);

        let target_abs = DeinitGuard<AbsPathAuto>{ val: .init() };
        this.appendGlobalStoreEntryPath(&target_abs.val, entry_id, .final);

        switch (doSymlinkGlobalStore(dest.val.sliceZ(), target_abs.val.sliceZ())) {
            .result => return .success,
            .err => |err| switch (err.getErrno()) {
                .NOENT => {
                    if (parent := dest.val.dirname()) {
                        FD.cwd().makePath(u8, parent) catch {};
                    }
                },
                .EXIST => {
                    // Existing entry from a previous install. If it's a
                    // symlink, replace it (stale link from a different
                    // hash). If it's a real directory, that's the
                    // pre-global-store layout (`bun patch` detaches
                    // `node_modules/<pkg>`, not this path).
                    const is_symlink = if (comptime Environment.isWindows)
                        if (a := sys.getFileAttributes(dest.val.sliceZ())) a.is_reparse_point else true
                    else if (st := sys.lstat(dest.val.sliceZ()).asValue())
                        std.posix.S.ISLNK(@intCast(st.mode))
                    else
                        true;

                    if (is_symlink) {
                        if (comptime Environment.isWindows) {
                            if (_ := sys.rmdir(dest.val.sliceZ()).asErr()) {
                                * = sys.unlink(dest.val.sliceZ());
                            }
                        } else {
                            * = sys.unlink(dest.val.sliceZ());
                        }
                    } else {
                        FD.cwd().deleteTree(dest.val.slice()) catch {};
                    }
                },
                else => return .initErr(err),
            },
        }
        return doSymlinkGlobalStore(dest.val.sliceZ(), target_abs.val.sliceZ());
    }

    pub fn appendStoreNodeModulesPath(_: *const Installer, buf: anytype, entry_id: Store.Entry.Id)  {
        const string_buf = this.lockfile.buffers.string_bytes.items;

        const entries = this.store.entries.slice();
        const entry_node_ids = entries.items(.node_id);

        const nodes = this.store.nodes.slice();
        const node_pkg_ids = nodes.items(.pkg_id);

        const pkgs = this.lockfile.packages.slice();
        const pkg_resolutions = pkgs.items(.resolution);

        const node_id = entry_node_ids[entry_id.get()];
        const pkg_id = node_pkg_ids[node_id.get()];
        const pkg_res = pkg_resolutions[pkg_id];

        switch (pkg_res.tag) {
            .root => {
                buf.append("node_modules");
            },
            .workspace => {
                buf.append(pkg_res.value.workspace.slice(string_buf));
                buf.append("node_modules");
            },
            else => {
                buf.appendFmt("node_modules/" ++ Store.modules_dir_name ++ "/{f}/node_modules", .{
                    Store.Entry.fmtStorePath(entry_id, this.store, this.lockfile),
                });
            },
        }
    }

    /// Like `appendStoreNodeModulesPath`, but resolves to the *physical*
    /// location of the entry's `node_modules` directory: the global virtual
    /// store for global-eligible entries, || the project-local `.bun/` path
    /// otherwise. See `Which` for when to pass `.staging` vs `.final`.
    pub fn appendRealStoreNodeModulesPath(_: *const Installer, buf: anytype, entry_id: Store.Entry.Id, which: Which)  {
        if (this.entryUsesGlobalStore(entry_id)) {
            this.appendGlobalStoreEntryPath(buf, entry_id, which);
            buf.append("node_modules");
            return;
        }
        this.appendStoreNodeModulesPath(buf, entry_id);
    }

    /// `appendStorePath` resolved to the entry's *physical* location. See
    /// `Which` for when to pass `.staging` vs `.final`.
    pub fn appendRealStorePath(_: *const Installer, buf: anytype, entry_id: Store.Entry.Id, which: Which)  {
        if (this.entryUsesGlobalStore(entry_id)) {
            const string_buf = this.lockfile.buffers.string_bytes.items;
            const node_id = this.store.entries.items(.node_id)[entry_id.get()];
            const pkg_id = this.store.nodes.items(.pkg_id)[node_id.get()];
            const pkg_name = this.lockfile.packages.items(.name)[pkg_id];
            this.appendGlobalStoreEntryPath(buf, entry_id, which);
            buf.append("node_modules");
            buf.append(pkg_name.slice(string_buf));
            return;
        }
        this.appendStorePath(buf, entry_id);
    }

    pub fn appendStorePath(_: *const Installer, buf: anytype, entry_id: Store.Entry.Id)  {
        const string_buf = this.lockfile.buffers.string_bytes.items;

        const entries = this.store.entries.slice();
        const entry_node_ids = entries.items(.node_id);

        const nodes = this.store.nodes.slice();
        const node_pkg_ids = nodes.items(.pkg_id);
        const node_dep_ids = nodes.items(.dep_id);
        // const node_peers = nodes.items(.peers);

        const pkgs = this.lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_resolutions = pkgs.items(.resolution);

        const node_id = entry_node_ids[entry_id.get()];
        // const peers = node_peers[node_id.get()];
        const pkg_id = node_pkg_ids[node_id.get()];
        const dep_id = node_dep_ids[node_id.get()];
        const pkg_res = pkg_resolutions[pkg_id];

        switch (pkg_res.tag) {
            .root => {
                if (dep_id != invalid_dependency_id) {
                    const pkg_name = pkg_names[pkg_id];
                    buf.append("node_modules/" ++ Store.modules_dir_name);
                    buf.appendFmt("{f}", .{
                        Store.Entry.fmtStorePath(entry_id, this.store, this.lockfile),
                    });
                    buf.append("node_modules");
                    if (pkg_name.isEmpty()) {
                        buf.append(std.fs.path.basename(bun.fs.FileSystem.instance.top_level_dir));
                    } else {
                        buf.append(pkg_name.slice(string_buf));
                    }
                } else {
                    // append nothing. buf is already top_level_dir
                }
            },
            .workspace => {
                buf.append(pkg_res.value.workspace.slice(string_buf));
            },
            .symlink => {
                const symlink_dir_path = this.manager.globalLinkDirPath();

                buf.clear();
                buf.append(symlink_dir_path);
                buf.append(pkg_res.value.symlink.slice(string_buf));
            },
            else => {
                const pkg_name = pkg_names[pkg_id];
                buf.append("node_modules/" ++ Store.modules_dir_name);
                buf.appendFmt("{f}", .{
                    Store.Entry.fmtStorePath(entry_id, this.store, this.lockfile),
                });
                buf.append("node_modules");
                buf.append(pkg_name.slice(string_buf));
            },
        }
    }

    /// The directory name for the entry store node_modules install
    /// folder.
    /// ./node_modules/.bun/jquery@3.7.1/node_modules/jquery
    ///                                               ^ this one
    /// Need to know this to avoid collisions with dependencies
    /// with the same name as the package.
    pub fn entryStoreNodeModulesPackageName(
        _: *const Installer,
        dep_id: DependencyID,
        pkg_id: PackageID,
        pkg_res: *const Resolution,
        pkg_names: []const String,
    ): ([]const u8)? {
        const string_buf = _.lockfile.buffers.string_bytes.items;

        return switch (pkg_res.tag) {
            .root => {
                if (dep_id != invalid_dependency_id) {
                    const pkg_name = pkg_names[pkg_id];
                    if (pkg_name.isEmpty()) {
                        return std.fs.path.basename(bun.fs.FileSystem.instance.top_level_dir);
                    }
                    const slice_fn = @TypeOf(pkg_name).slice;
                    return slice_fn(&pkg_name, string_buf);
                }
                return null;
            },
            .workspace => null,
            .symlink => null,
            else => brk: {
                const p_name = &pkg_names[pkg_id];
                const slice_fn = @TypeOf(p_name.*).slice;
                break :brk slice_fn(p_name, string_buf);
            },
        };
    }
}
