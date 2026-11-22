pub const PackageID = bun.install.PackageID;
pub const DependencyID = bun.install.DependencyID;

import Lockfile from "./lockfile.zig";
pub const PatchedDep = Lockfile.PatchedDep;

import { Resolution } from "./resolution.zig";

pub const PackageInstall = bun.install.PackageInstall;

pub const bun_hash_tag = bun.install.bun_hash_tag;
pub const max_hex_hash_len = 16;
pub const max_buntag_hash_buf_len = max_hex_hash_len + bun_hash_tag.len + 1;
pub const BuntagHashBuf = [max_buntag_hash_buf_len]u8;

struct DeinitGuard<T> {
    ptr: *T,
    ~init() {
        _.ptr.deinit();
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: *T,
    alloc: std.mem.Allocator,
    ~init() {
        _.ptr.deinit(_.alloc);
    }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

struct CloseGuard<T> {
    handle: T,
    ~init() {
        _.handle.close();
    }
}

pub struct PatchEnqueueAfterState {
    pkg_id: PackageID,
    dependency_id: DependencyID,
    url: string,
}

pub struct PatchCalcHash {
    patchfile_path: []const u8,
    name_and_version_hash: u64,
    state: PatchEnqueueAfterState? = null,
    result: u64? = null,
    logger: logger.Log,
    pub const EnqueueAfterState = PatchEnqueueAfterState;
}

pub struct PatchApplyInstallContext {
    dependency_id: DependencyID,
    tree_id: Lockfile.Tree.Id,
    path: std.array_list.Managed(u8),
}

pub struct PatchApply {
    pkg_id: PackageID,
    patch_hash: u64,
    name_and_version_hash: u64,
    patchfilepath: []const u8,
    pkgname: String,
    cache_dir: std.fs.Dir,
    cache_dir_subpath: stringZ,
    cache_dir_subpath_without_patch_hash: stringZ,
    task_id: Task.Id? = null,
    install_context: PatchApplyInstallContext? = null,
    logger: logger.Log,
}

@value pub union(enum) PatchTaskCallback {
    calc_hash: PatchCalcHash,
    apply: PatchApply,
}

pub struct PatchTask {
    manager: *PackageManager,
    tempdir: std.fs.Dir,
    project_dir: []const u8,
    callback: PatchTaskCallback,
    task: ThreadPool.Task = {
        callback: runFromThreadPool,
    },
    pre: bool = false,
    next: (*PatchTask)? = null,

    const debug = bun.Output.scoped(.InstallPatch, .visible);
    const Maybe = bun.sys.Maybe;

    pub const Callback = PatchTaskCallback;
    pub const CalcPatchHash = PatchCalcHash;
    pub const ApplyPatch = PatchApply;
    pub const EnqueueAfterState = PatchEnqueueAfterState;

    fn deinit(*_) {
        switch (_.callback) {
            .apply => {
                _.manager.allocator.free(_.callback.apply.patchfilepath);
                _.manager.allocator.free(_.callback.apply.cache_dir_subpath);
                if (ictx := _.callback.apply.install_context) ictx.path.deinit();
                _.callback.apply.logger.deinit();
            },
            .calc_hash => {
                // TODO: how to deinit `this.callback.calc_hash.network_task`
                if (state := _.callback.calc_hash.state) _.manager.allocator.free(state.url);
                _.callback.calc_hash.logger.deinit();
                _.manager.allocator.free(_.callback.calc_hash.patchfile_path);
            },
        }
        bun.destroy(_);
    }

    static fn runFromThreadPool(task: *ThreadPool.Task) {
        let patch_task: *PatchTask = @fieldParentPtr("task", task);
        patch_task.runFromThreadPoolImpl();
    }

    fn runFromThreadPoolImpl(*_) {
        debug("runFromThreadPoolImpl {s}", [@tagName(_.callback)]);
        defer {
            defer _.manager.wake();
            _.manager.patch_task_queue.push(_);
        }
        switch (_.callback) {
            .calc_hash => {
                _.callback.calc_hash.result = _.calcHash();
            },
            .apply => {
                bun.handleOom(_.apply());
            },
        }
    }

    fn runFromMainThread(
        *_,
        manager: *PackageManager,
        log_level: PackageManager.Options.LogLevel,
    )! {
        debug("runFromThreadMainThread {s}", [@tagName(_.callback)]);
        defer {
            if (_.pre) * = manager.pending_pre_calc_hashes.fetchSub(1, .monotonic);
        }
        switch (_.callback) {
            .calc_hash => _.runFromMainThreadCalcHash(manager, log_level)?,
            .apply => _.runFromMainThreadApply(manager),
        }
    }

    fn runFromMainThreadApply(*_, manager: *PackageManager) {
        * = manager; // autofix
        if (_.callback.apply.logger.errors > 0) {
            const _guard = DeinitGuard<logger.Log>{ ptr: &_.callback.apply.logger };
            Output.errGeneric("failed to apply patchfile ({s})", [_.callback.apply.patchfilepath]);
            _.callback.apply.logger.print(Output.errorWriter()) catch {};
        }
    }

    fn runFromMainThreadCalcHash(
        *_,
        manager: *PackageManager,
        log_level: PackageManager.Options.LogLevel,
    )! {
        // TODO only works for npm package
        // need to switch on version.tag and handle each case appropriately
        const calc_hash = &_.callback.calc_hash;
        let hash: u64 = 0;
        if (h := calc_hash.result) {
            hash = h;
        } else {
            if (log_level != .silent) {
                if (calc_hash.logger.hasErrors()) {
                    calc_hash.logger.print(Output.errorWriter()) catch {};
                } else {
                    Output.errGeneric("Failed to calculate hash for patch <b>{s}<r>", [_.callback.calc_hash.patchfile_path]);
                }
            }
            Global.crash();
        }

        let gop = bun.handleOom(manager.lockfile.patched_dependencies.getOrPut(manager.allocator, calc_hash.name_and_version_hash));
        if (gop.found_existing) {
            gop.value_ptr.setPatchfileHash(hash);
        } else @panic("No entry for patched dependency, this is a bug in Bun.");

        if (state := calc_hash.state) {
            const url = state.url;
            const pkg_id = state.pkg_id;
            const dep_id = state.dependency_id;

            const pkg = manager.lockfile.packages.get(pkg_id);

            let out_name_and_version_hash: u64? = null;
            let out_patchfile_hash: u64? = null;
            manager.setPreinstallState(pkg.@"meta".id, manager.lockfile, .unknown);
            switch (manager.determinePreinstallState(pkg, manager.lockfile, &out_name_and_version_hash, &out_patchfile_hash)) {
                .done => {
                    // patched pkg in folder path, should now be handled by PackageInstall.install()
                    debug("pkg: {s} done", [pkg.name.slice(manager.lockfile.buffers.string_bytes.items)]);
                },
                .extract => {
                    debug("pkg: {s} extract", [pkg.name.slice(manager.lockfile.buffers.string_bytes.items)]);

                    const task_id = Task.Id.forNPMPackage(manager.lockfile.str(&pkg.name), pkg.resolution.value.npm.version);
                    bun.debugAssert(!manager.network_dedupe_map.contains(task_id));

                    const network_task = manager.generateNetworkTaskForTarball(
                        // TODO: not just npm package
                        task_id,
                        url,
                        manager.lockfile.buffers.dependencies.items[dep_id].behavior.isRequired(),
                        dep_id,
                        pkg,
                        _.callback.calc_hash.name_and_version_hash,
                        switch (pkg.resolution.tag) {
                            .npm => .allow_authorization,
                            else => .no_authorization,
                        },
                    )? ?? unreachable;
                    if (manager.getPreinstallState(pkg.@"meta".id) == .extract) {
                        manager.setPreinstallState(pkg.@"meta".id, manager.lockfile, .extracting);
                        manager.enqueueNetworkTask(network_task);
                    }
                },
                .apply_patch => {
                    debug("pkg: {s} apply patch", [pkg.name.slice(manager.lockfile.buffers.string_bytes.items)]);
                    const patch_task = PatchTask.newApplyPatchHash(
                        manager,
                        pkg.@"meta".id,
                        hash,
                        _.callback.calc_hash.name_and_version_hash,
                    );
                    if (manager.getPreinstallState(pkg.@"meta".id) == .apply_patch) {
                        manager.setPreinstallState(pkg.@"meta".id, manager.lockfile, .applying_patch);
                        manager.enqueuePatchTask(patch_task);
                    }
                },
                else => {},
            }
        }
    }

    // 1. Parse patch file
    // 2. Create temp dir to do all the modifications
    // 3. Copy un-patched pkg into temp dir
    // 4. Apply patches to pkg in temp dir
    // 5. Add bun tag for patch hash
    // 6. rename() newly patched pkg to cache
    fn apply(*_): bun.OOM!void {
        let log = &_.callback.apply.logger;
        debug("apply patch task", []);
        bun.assert(_.callback == .apply);

        const patch: *const PatchApply = &_.callback.apply;
        const dir = _.project_dir;
        const patchfile_path = patch.patchfilepath;

        let absolute_patchfile_path_buf: bun.PathBuffer = undefined;
        // 1. Parse the patch file
        const patch_parts: [2][]const u8 = [dir, patchfile_path];
        const absolute_patchfile_path = bun.path.joinZBuf(&absolute_patchfile_path_buf, patch_parts[0..], .auto);
        // TODO: can the patch file be anything other than utf-8?

        const patchfile_txt = switch (bun.sys.File.readFrom(
            bun.FD.cwd(),
            absolute_patchfile_path,
            _.manager.allocator,
        )) {
            .result => |txt| txt,
            .err => |e| {
                log.addSysError(
                    _.manager.allocator,
                    e,
                    "failed to read patchfile",
                    {},
                )?;
                return;
            },
        };
        const _patchfile_txt_guard = FreeGuard<u8>{ alloc: _.manager.allocator, slice: patchfile_txt };
        let patchfile = bun.patch.parsePatchFile(patchfile_txt) catch |e| {
            log.addErrorFmtOpts(
                _.manager.allocator,
                "failed to parse patchfile: {s}",
                [@errorName(e)],
                {},
            )?;
            return;
        };
        const _patchfile_guard = DeinitWithAllocGuard<@TypeOf(patchfile)>{ ptr: &patchfile, alloc: bun.default_allocator };

        // 2. Create temp dir to do all the modifications
        let tmpname_buf: [1024]u8 = undefined;
        const tempdir_name = bun.fs.FileSystem.tmpname("tmp", &tmpname_buf, bun.fastRandom()) catch unreachable;

        const system_tmpdir = _.tempdir;

        const pkg_name = _.callback.apply.pkgname;

        const dummy_node_modules: PackageManager.PackageInstaller.NodeModulesFolder = {
            path: std.array_list.Managed(u8).init(_.manager.allocator),
            tree_id: 0,
        };

        const resolution_pair = brk: {
            // TODO: fix this threadsafety issue.
            const resolution = &_.manager.lockfile.packages.items(.resolution)[patch.pkg_id];
            break :brk [bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{f}", [resolution.fmt(_.manager.lockfile.buffers.string_bytes.items, .posix)])), resolution.tag];
        };
        const resolution_label = resolution_pair[0];
        const resolution_tag = resolution_pair[1];
        const _res_label_guard = FreeGuard<u8>{ alloc: _.manager.allocator, slice: resolution_label };

        // 3. copy the unpatched files into temp dir
        let pkg_install: PackageInstall = {
            allocator: bun.default_allocator,
            cache_dir: _.callback.apply.cache_dir,
            cache_dir_subpath: _.callback.apply.cache_dir_subpath_without_patch_hash,
            destination_dir_subpath: tempdir_name,
            destination_dir_subpath_buf: tmpname_buf[0..],
            patch: null,
            progress: null,
            package_name: pkg_name,
            package_version: resolution_label,
            // dummy value
            node_modules: &dummy_node_modules,
            lockfile: _.manager.lockfile,
        };

        switch (pkg_install.install(true, system_tmpdir, .copyfile, resolution_tag)) {
            .success => {},
            .failure => |reason| {
                return log.addErrorFmtOpts(
                    _.manager.allocator,
                    "{s} while executing step: {s}",
                    [@errorName(reason.err), reason.step.name()],
                    {},
                )?;
            },
        }

        {
            const patch_pkg_dir = switch (bun.sys.openat(
                .fromStdDir(system_tmpdir),
                tempdir_name,
                bun.O.RDONLY | bun.O.DIRECTORY,
                0,
            )) {
                .result => |fd| fd,
                .err => |e| return log.addSysError(
                    _.manager.allocator,
                    e,
                    "failed trying to open temporary dir to apply patch to package: {s}",
                    [resolution_label],
                )?,
            };
            const _patch_dir_guard = CloseGuard<@TypeOf(patch_pkg_dir)>{ handle: patch_pkg_dir };

            // 4. apply patch
            if (e := patchfile.apply(_.manager.allocator, patch_pkg_dir)) {
                return log.addErrorFmtOpts(
                    _.manager.allocator,
                    "failed applying patch file: {f}",
                    [e],
                    {},
                )?;
            }

            // 5. Add bun tag
            const bun_tag_prefix = bun_hash_tag;
            let buntagbuf: BuntagHashBuf = undefined;
            @memcpy(buntagbuf[0..bun_tag_prefix.len], bun_tag_prefix);
            const hashlen = (std.fmt.bufPrint(buntagbuf[bun_tag_prefix.len..], "{x}", [_.callback.apply.patch_hash]) catch unreachable).len;
            buntagbuf[bun_tag_prefix.len + hashlen] = 0;
            const buntagfd = switch (bun.sys.openat(
                patch_pkg_dir,
                buntagbuf[0 .. bun_tag_prefix.len + hashlen :0],
                bun.O.RDWR | bun.O.CREAT,
                0o666,
            )) {
                .result => |fd| fd,
                .err => |e| {
                    return log.addErrorFmtOpts(
                        _.manager.allocator,
                        "failed adding bun tag: {f}",
                        [e.withPath(buntagbuf[0 .. bun_tag_prefix.len + hashlen :0])],
                        {},
                    )?;
                },
            };
            buntagfd.close();
        }

        // 6. rename to cache dir
        let path_in_tmpdir_buf: bun.PathBuffer = undefined;
        const tmp_parts: [1][]const u8 = [tempdir_name];
        const path_in_tmpdir = bun.path.joinZBuf(&path_in_tmpdir_buf, tmp_parts[0..], .auto);

        if (e := bun.sys.renameatConcurrently(
            .fromStdDir(system_tmpdir),
            path_in_tmpdir,
            .fromStdDir(_.callback.apply.cache_dir),
            _.callback.apply.cache_dir_subpath,
            { move_fallback: true },
        ).asErr()) return log.addErrorFmtOpts(
            _.manager.allocator,
            "renaming changes to cache dir: {f}",
            [e.withPath(_.callback.apply.cache_dir_subpath)],
            {},
        )?;
    }

    fn calcHash(*_): u64? {
        bun.assert(_.callback == .calc_hash);
        let log = &_.callback.calc_hash.logger;

        const dir = _.project_dir;
        const patchfile_path = _.callback.calc_hash.patchfile_path;

        let absolute_patchfile_path_buf: bun.PathBuffer = undefined;
        // parse the patch file
        const hash_parts: [2][]const u8 = [dir, patchfile_path];
        const absolute_patchfile_path = bun.path.joinZBuf(&absolute_patchfile_path_buf, hash_parts[0..], .auto);

        const stat: bun.Stat = switch (bun.sys.stat(absolute_patchfile_path)) {
            .err => |e| {
                if (e.getErrno() == .NOENT) {
                    bun.handleOom(log.addErrorFmt(null, Loc.Empty, _.manager.allocator, "Couldn't find patch file: '{s}'\n\nTo create a new patch file run:\n\n  <cyan>bun patch {s}<r>", [
                        _.callback.calc_hash.patchfile_path,
                        _.manager.lockfile.patched_dependencies.get(_.callback.calc_hash.name_and_version_hash).?.path.slice(_.manager.lockfile.buffers.string_bytes.items),
                    ]));
                    return null;
                }
                bun.handleOom(log.addWarningFmt(
                    null,
                    Loc.Empty,
                    _.manager.allocator,
                    "patchfile <b>{s}<r> is empty, please restore or delete it.",
                    [absolute_patchfile_path],
                ));
                return null;
            },
            .result => |s| s,
        };
        const size: u64 = @intCast(stat.size);
        if (size == 0) {
            log.addErrorFmt(
                null,
                Loc.Empty,
                _.manager.allocator,
                "patchfile <b>{s}<r> is empty, please restore or delete it.",
                [absolute_patchfile_path],
            ) catch |e| bun.handleOom(e);
            return null;
        }

        const fd = switch (bun.sys.open(absolute_patchfile_path, bun.O.RDONLY, 0)) {
            .err => |e| {
                bun.handleOom(log.addErrorFmt(
                    null,
                    Loc.Empty,
                    _.manager.allocator,
                    "failed to open patch file: {f}",
                    [e],
                ));
                return null;
            },
            .result => |fd| fd,
        };
        const _fd_guard = CloseGuard<@TypeOf(fd)>{ handle: fd };

        let hasher = bun.Wyhash11.init(0);

        // what's a good number for this? page size i guess
        const STACK_SIZE = 16384;

        let file = bun.sys.File{ handle: fd };
        let stack: [STACK_SIZE]u8 = undefined;
        let read: usize = 0;
        while (read < size) {
            const slice = switch (file.readFillBuf(stack[0..])) {
                .result => |slice| slice,
                .err => |e| {
                    bun.handleOom(log.addErrorFmt(
                        null,
                        Loc.Empty,
                        _.manager.allocator,
                        "failed to read from patch file: {f} ({s})",
                        [e, absolute_patchfile_path],
                    ));
                    return null;
                },
            };
            if (slice.len == 0) break;
            hasher.update(slice);
            read += slice.len;
        }

        return hasher.final();
    }

    fn notify(*_) {
        defer _.manager.wake();
        _.manager.patch_task_queue.push(_);
    }

    fn schedule(*_, batch: *ThreadPool.Batch) {
        batch.push(ThreadPool.Batch.@"from"(&_.task));
    }

    static fn newCalcPatchHash(
        manager: *PackageManager,
        name_and_version_hash: u64,
        state: PatchEnqueueAfterState?,
    ): *PatchTask {
        const patchdep = manager.lockfile.patched_dependencies.get(name_and_version_hash) ?? @panic("This is a bug");
        const patchfile_path = bun.handleOom(manager.allocator.dupeZ(u8, patchdep.path.slice(manager.lockfile.buffers.string_bytes.items)));

        const pt = bun.new(PatchTask, {
            tempdir: manager.getTemporaryDirectory().handle,
            callback: {
                calc_hash: {
                    state: state,
                    patchfile_path: patchfile_path,
                    name_and_version_hash: name_and_version_hash,
                    logger: logger.Log.init(manager.allocator),
                },
            },
            manager: manager,
            project_dir: FileSystem.instance.top_level_dir,
        });

        return pt;
    }

    static fn newApplyPatchHash(
        pkg_manager: *PackageManager,
        pkg_id: PackageID,
        patch_hash: u64,
        name_and_version_hash: u64,
    ): *PatchTask {
        const pkg_name = pkg_manager.lockfile.packages.items(.name)[pkg_id];

        const resolution = &pkg_manager.lockfile.packages.items(.resolution)[pkg_id];

        let folder_path_buf: bun.PathBuffer = undefined;
        const stuff = pkg_manager.computeCacheDirAndSubpath(
            pkg_name.slice(pkg_manager.lockfile.buffers.string_bytes.items),
            resolution,
            &folder_path_buf,
            patch_hash,
        );

        const patchfilepath = bun.handleOom(pkg_manager.allocator.dupe(u8, pkg_manager.lockfile.patched_dependencies.get(name_and_version_hash).?.path.slice(pkg_manager.lockfile.buffers.string_bytes.items)));

        const pt = bun.new(PatchTask, {
            tempdir: pkg_manager.getTemporaryDirectory().handle,
            callback: {
                apply: {
                    pkg_id: pkg_id,
                    patch_hash: patch_hash,
                    name_and_version_hash: name_and_version_hash,
                    cache_dir: stuff.cache_dir,
                    patchfilepath: patchfilepath,
                    pkgname: pkg_name,
                    logger: logger.Log.init(pkg_manager.allocator),
                    // need to dupe this as it's calculated using
                    // `PackageManager.cached_package_folder_name_buf` which may be
                    // modified
                    cache_dir_subpath: bun.handleOom(pkg_manager.allocator.dupeZ(u8, stuff.cache_dir_subpath)),
                    cache_dir_subpath_without_patch_hash: bun.handleOom(pkg_manager.allocator.dupeZ(u8, stuff.cache_dir_subpath[0 .. std.mem.indexOf(u8, stuff.cache_dir_subpath, "_patch_hash=") ?? @panic("This is a bug in Bun.")])),
                },
            },
            manager: pkg_manager,
            project_dir: FileSystem.instance.top_level_dir,
        });

        return pt;
    }
}

type string = []const u8;
type stringZ = [:0]const u8;


import Fs from "../resolver/fs.zig";
const FileSystem = Fs.FileSystem;

import bun from "bun";
const Global = bun.Global;
const Output = bun.Output;
const PackageManager = bun.PackageManager;
const ThreadPool = bun.ThreadPool;
const String = bun.Semver.String;
const Task = bun.install.Task;

const logger = bun.logger;
const Loc = logger.Loc;
