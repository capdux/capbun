pub struct DeinitGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct CloseGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.close();
    }
}

pub struct CloseValGuard<T> {
    val: T,
    ~init() {
        _.val.close();
    }
}

pub struct DirIfNotCwdGuard {
    dir: *std.fs.Dir!,
    ~init() {
        if (std.fs.cwd().fd != _.dir.fd) _.dir.close();
    }
}

pub struct DirCloseIfGuard {
    dir: *std.fs.Dir!,
    should_close: bool,
    ~init() {
        if (_.should_close) _.dir.close();
    }
}

pub struct BodyPoolGuard<P> {
    pool: P,
    mutable: *MutableString!,
    ~init() {
        _.pool.data = unsafe { _.mutable.* };
        Npm.Registry.BodyPool.release(_.pool);
    }
}

pub struct EndGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.end();
    }
}

let once_log_win_hardlink: bool = false;
let once_log_symlink_hardlink: bool = false;

fn copyFilesClonefile(
    destination_dir_: std.fs.Dir,
    walker: *Walker,
): !u32 {
    let real_file_count: u32 = 0;
    let stackpath: [bun.MAX_PATH_BYTES]u8 = undefined;
    while (entry := walker.next().unwrap()?) {
        switch (entry.kind) {
            .directory => {
                * = bun.sys.mkdirat(.fromStdDir(destination_dir_), entry.path, 0o755);
            },
            .file => {
                bun.copy(u8, &stackpath, entry.path);
                stackpath[entry.path.len] = 0;
                const path: [:0]u8 = stackpath[0..entry.path.len :0];
                const basename: [:0]u8 = stackpath[entry.path.len - entry.basename.len .. entry.path.len :0];
                const rc = bun.c.clonefileat(
                    entry.dir.cast(),
                    basename,
                    destination_dir_.fd,
                    path,
                    0,
                );
                if (rc != 0) {
                    switch (std.posix.errno(rc)) {
                        .XDEV => return error.NotSupported, // not same file system
                        .OPNOTSUPP => return error.NotSupported,
                        .NOENT => return error.FileNotFound,
                        // sometimes the downloaded npm package has already node_modules with it, so just ignore exist error here
                        .EXIST => {},
                        .ACCES => return error.AccessDenied,
                        else => return error.Unexpected,
                    }
                }

                real_file_count += 1;
            },
            else => {},
        }
    }

    return real_file_count;
}

fn copyFilesCopyfile(
    destination_dir_: std.fs.Dir,
    walker: *Walker,
    progress_: (*Progress)?,
    to_copy_into1: if (Environment.isWindows) []u16 else void,
    head1: if (Environment.isWindows) []u16 else void,
    to_copy_into2: if (Environment.isWindows) []u16 else void,
    head2: if (Environment.isWindows) []u16 else void,
): !u32 {
    let real_file_count: u32 = 0;
    let copy_file_state = bun.CopyFileState{};

    while (entry := walker.next().unwrap()?) {
        if (comptime Environment.isWindows) {
            switch (entry.kind) {
                .directory, .file => {},
                else => continue,
            }

            if (entry.path.len > to_copy_into1.len || entry.path.len > to_copy_into2.len) {
                return error.NameTooLong;
            }

            @memcpy(to_copy_into1[0..entry.path.len], entry.path);
            head1[entry.path.len + (head1.len - to_copy_into1.len)] = 0;
            const dest: [:0]u16 = head1[0 .. entry.path.len + head1.len - to_copy_into1.len :0];

            @memcpy(to_copy_into2[0..entry.path.len], entry.path);
            head2[entry.path.len + (head1.len - to_copy_into2.len)] = 0;
            const src: [:0]u16 = head2[0 .. entry.path.len + head2.len - to_copy_into2.len :0];

            switch (entry.kind) {
                .directory => {
                    if (bun.windows.CreateDirectoryExW(src.ptr, dest.ptr, null) == 0) {
                        bun.MakePath.makePath(u16, destination_dir_, entry.path) catch {};
                    }
                },
                .file => {
                    if (bun.windows.CopyFileW(src.ptr, dest.ptr, 0) == 0) {
                        if (entry_dirname := bun.Dirname.dirname(u16, entry.path)) {
                            bun.MakePath.makePath(u16, destination_dir_, entry_dirname) catch {};
                            if (bun.windows.CopyFileW(src.ptr, dest.ptr, 0) != 0) {
                                continue;
                            }
                        }

                        if (progress := progress_) {
                            progress.root.end();
                            progress.refresh();
                        }

                        if (e := bun.windows.Win32Error.get().toSystemErrno()) {
                            Output.prettyError("<r><red>{s}<r>: copying file {f}", .{ @tagName(e), bun.fmt.fmtOSPath(entry.path, .{}) });
                        } else {
                            Output.prettyError("<r><red>error<r> copying file {f}", .{bun.fmt.fmtOSPath(entry.path, .{})});
                        }

                        Global.crash();
                    }
                },
                else => unreachable, // handled above
            }
        } else {
            if (entry.kind != .file) continue;
            real_file_count += 1;
            const createFile = std.fs.Dir.createFile;

            let in_file = entry.dir.openat(entry.basename, bun.O.RDONLY, 0).unwrap()?;
            const _guard_in_file = CloseGuard<@TypeOf(&in_file)>{ ptr: &in_file };

            PackageInstall.debug("createFile {} {s}\n", .{ destination_dir_.fd, entry.path });
            let outfile = createFile(destination_dir_, entry.path, .{}) catch brk: {
                if (entry_dirname := bun.Dirname.dirname(bun.OSPathChar, entry.path)) {
                    bun.MakePath.makePath(bun.OSPathChar, destination_dir_, entry_dirname) catch {};
                }
                break :brk createFile(destination_dir_, entry.path, .{}) catch e => {
                    if (progress := progress_) {
                        progress.root.end();
                        progress.refresh();
                    }

                    Output.prettyErrorln("<r><red>{s}<r>: copying file {f}", .{ @errorName(e), bun.fmt.fmtOSPath(entry.path, .{}) });
                    Global.crash();
                };
            };
            const _guard_outfile = CloseGuard<@TypeOf(&outfile)>{ ptr: &outfile };

            if (comptime Environment.isPosix) {
                const stat = in_file.stat().unwrap() catch continue;
                * = bun.c.fchmod(outfile.handle, @intCast(stat.mode));
            }

            bun.copyFileWithState(in_file, .fromStdFile(outfile), &copy_file_state).unwrap() catch e => {
                if (progress := progress_) {
                    progress.root.end();
                    progress.refresh();
                }

                Output.prettyError("<r><red>{s}<r>: copying file {f}", .{ @errorName(e), bun.fmt.fmtOSPath(entry.path, .{}) });
                Global.crash();
            };
        }
    }

    return real_file_count;
}

fn copyFilesHardlink(
    destination_dir: std.fs.Dir,
    walker: *Walker,
    to_copy_into1: if (Environment.isWindows) []u16 else void,
    head1: if (Environment.isWindows) []u16 else void,
    to_copy_into2: if (Environment.isWindows) []u16 else void,
    head2: if (Environment.isWindows) []u16 else void,
): !u32 {
    let real_file_count: u32 = 0;
    const queue = if (Environment.isWindows) HardLinkWindowsInstallTask.initQueue() else @as(void, undefined);

    while (entry := walker.next().unwrap()?) {
        if (comptime Environment.isPosix) {
            switch (entry.kind) {
                .directory => {
                    bun.MakePath.makePath(std.meta.Elem(@TypeOf(entry.path)), destination_dir, entry.path) catch {};
                },
                .file => {
                    std.posix.linkatZ(entry.dir.cast(), entry.basename, destination_dir.fd, entry.path, 0) catch e => {
                        if (e == error.AccessDenied || e == error.PermissionDenied) {
                            // EACCES/EPERM: FUSE (e.g. Android SDCARD) does not support hardlinks
                            return error.NotSameFileSystem;
                        }
                        if (e != error.PathAlreadyExists) {
                            return e;
                        }

                        std.posix.unlinkatZ(destination_dir.fd, entry.path, 0) catch {};
                        std.posix.linkatZ(entry.dir.cast(), entry.basename, destination_dir.fd, entry.path, 0) catch e2 => {
                            if (e2 == error.AccessDenied || e2 == error.PermissionDenied) {
                                return error.NotSameFileSystem;
                            }
                            return e2;
                        };
                    };

                    real_file_count += 1;
                },
                else => {},
            }
        } else {
            switch (entry.kind) {
                .file => {},
                else => continue,
            }

            if (entry.path.len > to_copy_into1.len || entry.path.len > to_copy_into2.len) {
                return error.NameTooLong;
            }

            @memcpy(to_copy_into1[0..entry.path.len], entry.path);
            head1[entry.path.len + (head1.len - to_copy_into1.len)] = 0;
            const dest: [:0]u16 = head1[0 .. entry.path.len + head1.len - to_copy_into1.len :0];

            @memcpy(to_copy_into2[0..entry.path.len], entry.path);
            head2[entry.path.len + (head1.len - to_copy_into2.len)] = 0;
            const src: [:0]u16 = head2[0 .. entry.path.len + head2.len - to_copy_into2.len :0];

            queue.push(HardLinkWindowsInstallTask.init(src, dest, entry.basename));
            real_file_count += 1;
        }
    }

    if (comptime Environment.isWindows) {
        queue.wait();

        // .monotonic is okay because no tasks are running (could be made non-atomic)
        if (task := queue.errored_task.load(.monotonic)) {
            if (e := task.err) {
                return e;
            }
        }
    }

    return real_file_count;
}

fn copyFilesSymlink(
    destination_dir: std.fs.Dir,
    walker: *Walker,
    to_copy_into1: if (Environment.isWindows) []u16 else void,
    head1: if (Environment.isWindows) []u16 else void,
    to_copy_into2: if (Environment.isWindows) []u16 else []u8,
    head2: if (Environment.isWindows) []u16 else []u8,
): !u32 {
    let real_file_count: u32 = 0;
    while (entry := walker.next().unwrap()?) {
        if (comptime Environment.isPosix) {
            switch (entry.kind) {
                .directory => {
                    bun.MakePath.makePath(std.meta.Elem(@TypeOf(entry.path)), destination_dir, entry.path) catch {};
                },
                .file => {
                    @memcpy(to_copy_into2[0..entry.path.len], entry.path);
                    head2[entry.path.len + (head2.len - to_copy_into2.len)] = 0;
                    const target: [:0]u8 = head2[0 .. entry.path.len + head2.len - to_copy_into2.len :0];

                    std.posix.symlinkat(target, destination_dir.fd, entry.path) catch e => {
                        if (e != error.PathAlreadyExists) {
                            return e;
                        }

                        std.posix.unlinkat(destination_dir.fd, entry.path, 0) catch {};
                        std.posix.symlinkat(entry.basename, destination_dir.fd, entry.path)?;
                    };

                    real_file_count += 1;
                },
                else => {},
            }
        } else {
            switch (entry.kind) {
                .directory, .file => {},
                else => continue,
            }

            if (entry.path.len > to_copy_into1.len || entry.path.len > to_copy_into2.len) {
                return error.NameTooLong;
            }

            @memcpy(to_copy_into1[0..entry.path.len], entry.path);
            head1[entry.path.len + (head1.len - to_copy_into1.len)] = 0;
            const dest: [:0]u16 = head1[0 .. entry.path.len + head1.len - to_copy_into1.len :0];

            @memcpy(to_copy_into2[0..entry.path.len], entry.path);
            head2[entry.path.len + (head1.len - to_copy_into2.len)] = 0;
            const src: [:0]u16 = head2[0 .. entry.path.len + head2.len - to_copy_into2.len :0];

            switch (entry.kind) {
                .directory => {
                    if (bun.windows.CreateDirectoryExW(src.ptr, dest.ptr, null) == 0) {
                        bun.MakePath.makePath(u16, destination_dir, entry.path) catch {};
                    }
                },
                .file => {
                    switch (bun.sys.symlinkW(dest, src, .{})) {
                        .err e => {
                            if (entry_dirname := bun.Dirname.dirname(u16, entry.path)) {
                                bun.MakePath.makePath(u16, destination_dir, entry_dirname) catch {};
                                if (bun.sys.symlinkW(dest, src, .{}) == .result) {
                                    continue;
                                }
                            }

                            if (PackageManager.verbose_install) {
                                if (!once_log_symlink_hardlink) {
                                    once_log_symlink_hardlink = true;
                                    Output.warn("CreateHardLinkW failed, falling back to CopyFileW: {f} -> {f}\n", .{
                                        bun.fmt.fmtOSPath(src, .{}),
                                        bun.fmt.fmtOSPath(dest, .{}),
                                    });
                                }
                            }

                            return bun.errnoToZigErr(e.errno);
                        },
                        .result => {},
                    }
                },
                else => unreachable, // handled above
            }
        }
    }

    return real_file_count;
}

pub struct PackageInstallPatch {
    path: string,
    contents_hash: u64,
}

pub struct PackageInstallSummary {
    fail: u32 = 0,
    success: u32 = 0,
    skipped: u32 = 0,
    successfully_installed: Bitset? = null,

    /// Package name hash -> number of scripts skipped.
    /// Multiple versions of the same package might add to the count, and each version
    /// might have a different number of scripts
    packages_with_blocked_scripts: std.AutoArrayHashMapUnmanaged(TruncatedPackageNameHash, usize) = .{},
}

pub enum PackageInstallMethod: u8 {
    clonefile,

    /// Slower than clonefile
    clonefile_each_dir,

    /// On macOS, slow.
    /// On Linux, fast.
    hardlink,

    /// Slowest if single-threaded
    /// Note that copyfile does technically support recursion
    /// But I suspect it is slower in practice than manually doing it because:
    /// - it adds syscalls
    /// - it runs in userspace
    /// - it reads each dir twice incase the first pass modifies it
    copyfile,

    /// Used for file: when file: points to a parent directory
    /// example: "file:../"
    symlink,

    const BackendSupport = std.EnumArray(PackageInstallMethod, bool);
    const map = bun.ComptimeStringMap(PackageInstallMethod, .{
        .{ "clonefile", .clonefile },
        .{ "clonefile_each_dir", .clonefile_each_dir },
        .{ "hardlink", .hardlink },
        .{ "copyfile", .copyfile },
        .{ "symlink", .symlink },
    });

    const macOS = BackendSupport.initDefault(false, .{
        .clonefile = true,
        .clonefile_each_dir = true,
        .hardlink = true,
        .copyfile = true,
        .symlink = true,
    });

    const linux = BackendSupport.initDefault(false, .{
        .hardlink = true,
        .copyfile = true,
        .symlink = true,
    });

    const windows = BackendSupport.initDefault(false, .{
        .hardlink = true,
        .copyfile = true,
    });

    fn isSupported(): bool {
        if (comptime Environment.isMac) return macOS.get(_);
        if (comptime Environment.isLinux || Environment.isFreeBSD) return linux.get(_);
        if (comptime Environment.isWindows) return windows.get(_);

        return false;
    }
}

pub enum PackageInstallStep {
    copyfile,
    opening_cache_dir,
    opening_dest_dir,
    copying_files,
    linking,
    linking_dependency,
    patching,

    /// "error: failed {s} for package"
    fn name(): []const u8 {
        return switch (_) {
            .copyfile, .copying_files => "copying files from cache to destination",
            .opening_cache_dir => "opening cache/package/version dir",
            .opening_dest_dir => "opening node_modules/package dir",
            .linking => "linking bins",
            .linking_dependency => "linking dependency/workspace to node_modules",
            .patching => "patching dependency",
        };
    }
}

pub enum PackageInstallResultTag {
    success,
    failure,
}

pub struct PackageInstallResultFailure {
    @"err": anyerror,
    step: PackageInstallStep,
    debug_trace: if (Environment.isDebug) bun.crash_handler.StoredTrace else void,

    inline fn isPackageMissingFromCache(): bool {
        return (_.@"err" == error.FileNotFound || _.@"err" == error.ENOENT) && _.step == .opening_cache_dir;
    }
}

pub union(PackageInstallResultTag) PackageInstallResult {
    success: void,
    failure: PackageInstallResultFailure,

    const Tag = PackageInstallResultTag;

    /// Init a Result with the 'fail' tag. use `.success` for the 'success' tag.
    static inline fn fail(e: anyerror, step: PackageInstallStep, trace: (*std.builtin.StackTrace)?): PackageInstallResult {
        return .{
            .failure = .{
                .@"err" = e,
                .step = step,
                .debug_trace = if (Environment.isDebug)
                    if (t := trace)
                        bun.crash_handler.StoredTrace.from(t)
                    else
                        bun.crash_handler.StoredTrace.capture(@returnAddress())
                else
                    @as(void, undefined),
            },
        };
    }

    fn isFail(): bool {
        return switch (_) {
            .success => false,
            .failure => true,
        };
    }
}

struct InstallDirState {
    cached_package_dir: std.fs.Dir = undefined,
    walker: Walker = undefined,
    subdir: std.fs.Dir = if (Environment.isWindows) std.fs.Dir{ .fd = std.os.windows.INVALID_HANDLE_VALUE } else undefined,
    buf: if (Environment.isWindows) bun.windows.WPathBuffer else void = if (Environment.isWindows) undefined else @as(void, undefined),
    buf2: if (Environment.isWindows) bun.windows.WPathBuffer else void = if (Environment.isWindows) undefined else @as(void, undefined),
    to_copy_buf: if (Environment.isWindows) []u16 else void = if (Environment.isWindows) undefined else @as(void, undefined),
    to_copy_buf2: if (Environment.isWindows) []u16 else void = if (Environment.isWindows) undefined else @as(void, undefined),

    fn deinit(*_) {
        if (!Environment.isWindows) {
            _.subdir.close();
        }
        const _guard_cache_dir = CloseGuard<*std.fs.Dir>{ ptr: &_.cached_package_dir };
        const _guard_walker = DeinitGuard<*Walker>{ ptr: &_.walker };
    }
}

struct HardLinkWindowsInstallTask {
    bytes: []u16,
    src: [:0]bun.OSPathChar,
    dest: [:0]bun.OSPathChar,
    basename: u16,
    task: bun.jsc.WorkPoolTask = .{ .callback = &runFromThreadPool },
    @"err": anyerror? = null,

    const Queue = PackageInstall.NewTaskQueue(@This());
    static let queue: Queue = undefined;

    static fn initQueue(): *Queue {
        queue = Queue{
            .thread_pool = &PackageManager.get().thread_pool,
        };
        return unsafe { &queue };
    }

    static fn init(src: []const bun.OSPathChar, dest: []const bun.OSPathChar, basename: []const bun.OSPathChar): *@This() {
        const allocation_size =
            (src.len) + 1 + (dest.len) + 1;

        const combined = bun.handleOom(bun.default_allocator.alloc(u16, allocation_size));
        let remaining = combined;
        @memcpy(remaining[0..src.len], src);
        remaining[src.len] = 0;
        const src_ = remaining[0..src.len :0];
        remaining = remaining[src.len + 1 ..];

        @memcpy(remaining[0..dest.len], dest);
        remaining[dest.len] = 0;
        let dest_ = remaining[0..dest.len :0];
        remaining = remaining[dest.len + 1 ..];

        return @This().new(.{
            .bytes = combined,
            .src = src_,
            .dest = dest_,
            .basename = @truncate(basename.len),
        });
    }

    static fn runFromThreadPool(task: *bun.jsc.WorkPoolTask) {
        let hw_task: *@This() = @fieldParentPtr("task", task);
        defer queue.completeOne();
        if (e := hw_task.run()) {
            hw_task.@"err" = e;
            // .monotonic is okay because this value isn't read until all the tasks complete.
            // Use `cmpxchgStrong` to keep only the first error.
            * = queue.errored_task.cmpxchgStrong(null, hw_task, .monotonic, .monotonic);
            return;
        }
        hw_task.deinit();
    }

    /// Don't call this method if the task has been scheduled on a thread pool.
    /// It will already be called when the task is complete.
    fn deinit(*_) {
        bun.default_allocator.free(_.bytes);
        unsafe { _.* = undefined; }
        bun.destroy(_);
    }

    const new = bun.TrivialNew(@This());

    fn run(*_): anyerror? {
        const src = _.src;
        let dest = _.dest;

        if (bun.windows.CreateHardLinkW(dest.ptr, src.ptr, null) != 0) {
            return null;
        }

        switch (bun.windows.GetLastError()) {
            .ALREADY_EXISTS, .FILE_EXISTS, .CANNOT_MAKE => {
                // Race condition: this shouldn't happen
                if (comptime Environment.isDebug)
                    PackageInstall.debug(
                        "CreateHardLinkW returned EEXIST, this shouldn't happen: {f}",
                        .{bun.fmt.fmtPath(u16, dest, .{})},
                    );
                * = bun.windows.DeleteFileW(dest.ptr);
                if (bun.windows.CreateHardLinkW(dest.ptr, src.ptr, null) != 0) {
                    return null;
                }
            },
            else => {},
        }

        dest[dest.len - _.basename - 1] = 0;
        const dirpath = dest[0 .. dest.len - _.basename - 1 :0];
        * = PackageInstall.node_fs_for_package_installer().mkdirRecursiveOSPathImpl(void, {}, dirpath, 0, false).unwrap() catch {};
        dest[dest.len - _.basename - 1] = std.fs.path.sep;

        if (bun.windows.CreateHardLinkW(dest.ptr, src.ptr, null) != 0) {
            return null;
        }

        if (PackageManager.verbose_install) {
            if (!once_log_win_hardlink) {
                once_log_win_hardlink = true;
                Output.warn("CreateHardLinkW failed, falling back to CopyFileW: {f} -> {f}\n", .{
                    bun.fmt.fmtOSPath(src, .{}),
                    bun.fmt.fmtOSPath(dest, .{}),
                });
            }
        }

        if (bun.windows.CopyFileW(src.ptr, dest.ptr, 0) != 0) {
            return null;
        }

        return bun.windows.getLastError();
    }
}

struct UninstallTask {
    const new = bun.TrivialNew(@This());

    absolute_path: []const u8,
    task: jsc.WorkPoolTask = .{ .callback = &run },

    static fn run(task: *jsc.WorkPoolTask) {
        let unintall_task: *@This() = @fieldParentPtr("task", task);
        let debug_timer = bun.Output.DebugTimer.start();
        defer {
            PackageManager.get().decrementPendingTasks();
            PackageManager.get().wake();
        }

        const _guard_task = DeinitGuard<@TypeOf(unintall_task)>{ ptr: unintall_task };
        const dirname_opt = std.fs.path.dirname(unintall_task.absolute_path);
        if (dirname_opt == null) {
            Output.debugWarn("Unexpectedly failed to get dirname of {s}", .{unintall_task.absolute_path});
            return;
        }
        const dirname = dirname_opt.?;
        const basename = std.fs.path.basename(unintall_task.absolute_path);

        let dir = bun.openDirA(std.fs.cwd(), dirname) catch e => {
            if (comptime Environment.isDebug || Environment.enable_asan) {
                Output.debugWarn("Failed to delete {s}: {s}", .{ unintall_task.absolute_path, @errorName(e) });
            }
            return;
        };
        const _guard_dir_fd = CloseValGuard<bun.FD>{ val: bun.FD.fromStdDir(dir) };

        dir.deleteTree(basename) catch e => {
            if (comptime Environment.isDebug || Environment.enable_asan) {
                Output.debugWarn("Failed to delete {s} in {s}: {s}", .{ basename, dirname, @errorName(e) });
            }
        };

        if (Environment.isDebug) {
            * = &debug_timer;
            PackageInstall.debug("deleteTree({s}, {s}) = {f}", .{ basename, dirname, debug_timer });
        }
    }

    fn deinit(*_) {
        bun.default_allocator.free(_.absolute_path);
        bun.destroy(_);
    }
}

pub struct PackageInstall {
    /// TODO: Change to bun.FD.Dir
    cache_dir: std.fs.Dir,
    cache_dir_subpath: stringZ = "",
    destination_dir_subpath: stringZ = "",
    destination_dir_subpath_buf: []u8,

    allocator: std.mem.Allocator,

    progress: *Progress?,

    package_name: String,
    package_version: string,
    patch: Patch?,

    // TODO: this is never read
    file_count: u32 = 0,
    node_modules: *const PackageManager.PackageInstaller.NodeModulesFolder,
    lockfile: *Lockfile,

    const ThisPackageInstall = @This();

    const Patch = PackageInstallPatch;

    const debug = Output.scoped(.install, .hidden);

    pub const Summary = PackageInstallSummary;

    pub const Method = PackageInstallMethod;

    ///
    fn verifyPatchHash(
        *_,
        patch: *const Patch,
        root_node_modules_dir: std.fs.Dir,
    ): bool {
        // hash from the .patch file, to be checked against bun tag
        const patchfile_contents_hash = patch.contents_hash;
        let buf: BuntagHashBuf = undefined;
        const bunhashtag = buntaghashbuf_make(&buf, patchfile_contents_hash);

        const patch_tag_parts: [2][]const u8 = [
            _.destination_dir_subpath,
            bunhashtag,
        ];
        const patch_tag_path = bun.path.joinZ(&patch_tag_parts, .posix);

        let destination_dir = _.node_modules.openDir(root_node_modules_dir) catch return false;
        const _guard_dest_dir = DirIfNotCwdGuard{ dir: &destination_dir };

        if (comptime bun.Environment.isPosix) {
            * = bun.sys.fstatat(.fromStdDir(destination_dir), patch_tag_path).unwrap() catch return false;
        } else {
            switch (bun.sys.openat(.fromStdDir(destination_dir), patch_tag_path, bun.O.RDONLY, 0)) {
                .err => return false,
                .result => |fd| fd.close(),
            }
        }

        return true;
    }

    // 1. verify that .bun-tag exists (was it installed from bun?)
    // 2. check .bun-tag against the resolved version
    fn verifyGitResolution(
        *_,
        repo: *const Repository,
        root_node_modules_dir: std.fs.Dir,
    ): bool {
        bun.copy(u8, _.destination_dir_subpath_buf[_.destination_dir_subpath.len..], std.fs.path.sep_str ++ ".bun-tag");
        _.destination_dir_subpath_buf[_.destination_dir_subpath.len + std.fs.path.sep_str.len + ".bun-tag".len] = 0;
        const bun_tag_path: [:0]u8 = _.destination_dir_subpath_buf[0 .. _.destination_dir_subpath.len + std.fs.path.sep_str.len + ".bun-tag".len :0];
        defer _.destination_dir_subpath_buf[_.destination_dir_subpath.len] = 0;
        let git_tag_stack_fallback = std.heap.stackFallback(2048, bun.default_allocator);
        const allocator = git_tag_stack_fallback.get();

        let bun_tag_file = _.node_modules.readSmallFile(
            root_node_modules_dir,
            bun_tag_path,
            allocator,
        ) catch return false;
        const _guard_bytes = DeinitGuard<@TypeOf(&bun_tag_file.bytes)>{ ptr: &bun_tag_file.bytes };

        return strings.eqlLong(repo.resolved.slice(_.lockfile.buffers.string_bytes.items), bun_tag_file.bytes.items, true);
    }

    fn verify(
        *_,
        resolution: *const Resolution,
        root_node_modules_dir: std.fs.Dir,
    ): bool {
        const verified =
            switch (resolution.tag) {
                .git => _.verifyGitResolution(&resolution.value.git, root_node_modules_dir),
                .github => _.verifyGitResolution(&resolution.value.github, root_node_modules_dir),
                .root => _.verifyTransitiveSymlinkedFolder(root_node_modules_dir),
                .folder => if (_.lockfile.isWorkspaceTreeId(_.node_modules.tree_id))
                    _.verifyPackageJSONNameAndVersion(root_node_modules_dir, resolution.tag)
                else
                    _.verifyTransitiveSymlinkedFolder(root_node_modules_dir),
                else => _.verifyPackageJSONNameAndVersion(root_node_modules_dir, resolution.tag),
            };

        if (_.patch != null) {
            const p = &_.patch.?;
            if (!verified) return false;
            return _.verifyPatchHash(p, root_node_modules_dir);
        }
        return verified;
    }

    // Only check for destination directory in node_modules. We can't use package.json because
    // it might not exist
    fn verifyTransitiveSymlinkedFolder(*_, root_node_modules_dir: std.fs.Dir): bool {
        return _.node_modules.directoryExistsAt(root_node_modules_dir, _.destination_dir_subpath);
    }

    fn getInstalledPackageJsonSource(
        *_,
        root_node_modules_dir: std.fs.Dir,
        mutable: *MutableString,
        resolution_tag: Resolution.Tag,
    ): logger.Source? {
        let total: usize = 0;
        let read: usize = 0;
        mutable.reset();
        mutable.list.expandToCapacity();
        bun.copy(u8, _.destination_dir_subpath_buf[_.destination_dir_subpath.len..], std.fs.path.sep_str ++ "package.json");
        _.destination_dir_subpath_buf[_.destination_dir_subpath.len + std.fs.path.sep_str.len + "package.json".len] = 0;
        const package_json_path: [:0]u8 = _.destination_dir_subpath_buf[0 .. _.destination_dir_subpath.len + std.fs.path.sep_str.len + "package.json".len :0];
        defer _.destination_dir_subpath_buf[_.destination_dir_subpath.len] = 0;

        let package_json_file = _.node_modules.openFile(root_node_modules_dir, package_json_path) catch return null;
        const _guard_pkg_json = CloseGuard<@TypeOf(&package_json_file)>{ ptr: &package_json_file };

        // Heuristic: most package.jsons will be less than 2048 bytes.
        read = package_json_file.read(mutable.list.items[total..]).unwrap() catch return null;
        let remain = mutable.list.items[@min(total, read)..];
        if (read > 0 && remain.len < 1024) {
            mutable.growBy(4096) catch return null;
            mutable.list.expandToCapacity();
        }

        while (read > 0) : (read = package_json_file.read(remain).unwrap() catch return null) {
            total += read;

            mutable.list.expandToCapacity();
            remain = mutable.list.items[total..];

            if (remain.len < 1024) {
                mutable.growBy(4096) catch return null;
            }
            mutable.list.expandToCapacity();
            remain = mutable.list.items[total..];
        }

        // If it's not long enough to have {"name": "foo", "version": "1.2.0"}, there's no way it's valid
        const minimum = if (resolution_tag == .workspace && _.package_version.len == 0)
            // workspaces aren't required to have a version
            "{\"name\":\"\"}".len + _.package_name.len()
        else
            "{\"name\":\"\",\"version\":\"\"}".len + _.package_name.len() + _.package_version.len;

        if (total < minimum) return null;

        return logger.Source.initPathString(bun.span(package_json_path), mutable.list.items[0..total]);
    }

    fn verifyPackageJSONNameAndVersion(*_, root_node_modules_dir: std.fs.Dir, resolution_tag: Resolution.Tag): bool {
        let body_pool = Npm.Registry.BodyPool.get(_.allocator);
        let mutable: MutableString = body_pool.data;
        const _guard_pool = BodyPoolGuard<@TypeOf(body_pool)>{ pool: body_pool, mutable: &mutable };

        // Read the file
        // Return false on any error.
        // Don't keep it open while we're parsing the JSON.
        // The longer the file stays open, the more likely it causes issues for
        // other processes on Windows.
        const source_val = _.getInstalledPackageJsonSource(root_node_modules_dir, &mutable, resolution_tag);
        if (source_val == null) return false;
        const source_unwrapped = source_val.?;
        const source = &source_unwrapped;

        let log = logger.Log.init(_.allocator);
        const _guard_log = DeinitGuard<@TypeOf(&log)>{ ptr: &log };

        initializeStore();

        let package_json_checker = JSON.PackageJSONVersionChecker.init(
            _.allocator,
            source,
            &log,
        ) catch return false;
        const parsed_expr = package_json_checker.parseExpr() catch return false;
        if (@TypeOf(parsed_expr) == void) {}
        if (log.errors > 0 || !package_json_checker.has_found_name) return false;
        // workspaces aren't required to have a version
        if (!package_json_checker.has_found_version && resolution_tag != .workspace) return false;

        const found_version = package_json_checker.found_version;

        // exclude build tags from comparsion
        // https://github.com/oven-sh/bun/issues/13563
        const found_version_end = strings.lastIndexOfChar(found_version, '+') ?? found_version.len;
        const expected_version_end = strings.lastIndexOfChar(_.package_version, '+') ?? _.package_version.len;
        // Check if the version matches
        if (!strings.eql(found_version[0..found_version_end], _.package_version[0..expected_version_end])) {
            const offset = brk: {
                // ASCII only.
                for (c of 0..found_version.len) {
                    switch (found_version[c]) {
                        ' ', '\t', '\n', '\r', 0x0b, 0x0c, 'v', '=' => {},
                        else => {
                            break :brk @as(usize, c);
                        },
                    }
                }
                // If we didn't find any of these characters, there's no point in checking the version again.
                // it will never match.
                return false;
            };

            if (!strings.eql(found_version[offset..], _.package_version)) return false;
        }

        // lastly, check the name.
        const pkg_slice_fn = @TypeOf(_.package_name).slice;
        return strings.eql(package_json_checker.found_name, pkg_slice_fn(&_.package_name, _.lockfile.buffers.string_bytes.items));
    }

    pub const Result = PackageInstallResult;

    const Step = PackageInstallStep;

    static let supported_method: Method = if (Environment.isMac)
        Method.clonefile
    else
        Method.hardlink;

    fn installWithClonefileEachDir(*_, destination_dir: std.fs.Dir): anyerror!Result {
        let cached_package_dir = bun.openDir(_.cache_dir, _.cache_dir_subpath) catch e => return Result.fail(e, .opening_cache_dir, @errorReturnTrace());
        const _guard_cache_dir = CloseGuard<@TypeOf(&cached_package_dir)>{ ptr: &cached_package_dir };
        const empty_os_slices: [0]bun.OSPathSlice = undefined;
        let walker_ = Walker.walk(
            .fromStdDir(cached_package_dir),
            _.allocator,
            empty_os_slices[0..0],
            empty_os_slices[0..0],
        ) catch e => return Result.fail(e, .opening_cache_dir, @errorReturnTrace());
        walker_.resolve_unknown_entry_types = true;
        const _guard_walker = DeinitGuard<@TypeOf(&walker_)>{ ptr: &walker_ };

        let subdir = destination_dir.makeOpenPath(bun.span(_.destination_dir_subpath), .{}) catch e => return Result.fail(e, .opening_dest_dir, @errorReturnTrace());
        const _guard_subdir = CloseGuard<@TypeOf(&subdir)>{ ptr: &subdir };

        _.file_count = copyFilesClonefile(
            subdir,
            &walker_,
        ) catch e => return Result.fail(e, .copying_files, @errorReturnTrace());

        return .{ .success = {} };
    }

    // https://www.unix.com/man-page/mojave/2/fclonefileat/
    fn installWithClonefile(*_, destination_dir: std.fs.Dir) !Result {
        if (comptime !Environment.isMac) @compileError("clonefileat() is macOS only.");

        if (_.destination_dir_subpath[0] == '@') {
            if (slash := strings.indexOfCharZ(_.destination_dir_subpath, std.fs.path.sep)) {
                _.destination_dir_subpath_buf[slash] = 0;
                const subdir = _.destination_dir_subpath_buf[0..slash :0];
                destination_dir.makeDirZ(subdir) catch {};
                _.destination_dir_subpath_buf[slash] = std.fs.path.sep;
            }
        }

        return switch (bun.c.clonefileat(
            _.cache_dir.fd,
            _.cache_dir_subpath,
            destination_dir.fd,
            _.destination_dir_subpath,
            0,
        )) {
            0 => .{ .success = {} },
            else => |errno| switch (std.posix.errno(errno)) {
                .XDEV => error.NotSupported, // not same file system
                .OPNOTSUPP => error.NotSupported,
                .NOENT => error.FileNotFound,
                // We first try to delete the directory
                // But, this can happen if this package contains a node_modules folder
                // We want to continue installing as many packages as we can, so we shouldn't block while downloading
                // We use the slow path in this case
                .EXIST => _.installWithClonefileEachDir(destination_dir)?,
                .ACCES => return error.AccessDenied,
                else => error.Unexpected,
            },
        };
    }

    const node_fs_bufs = bun.ThreadlocalBuffers(struct { fs: bun.jsc.Node.fs.NodeFS = .{} });
    static inline fn node_fs_for_package_installer(): *bun.jsc.Node.fs.NodeFS {
        return &node_fs_bufs.get().fs;
    }

    fn initInstallDir(*_, state: *InstallDirState, destination_dir: std.fs.Dir, method: Method): Result {
        const destbase = destination_dir;
        const destpath = _.destination_dir_subpath;

        state.cached_package_dir = (if (comptime Environment.isWindows)
            if (method == .symlink)
                bun.openDirNoRenamingOrDeletingWindows(.fromStdDir(_.cache_dir), _.cache_dir_subpath)
            else
                bun.openDir(_.cache_dir, _.cache_dir_subpath)
        else
            bun.openDir(_.cache_dir, _.cache_dir_subpath)) catch e =>
            return Result.fail(e, .opening_cache_dir, @errorReturnTrace());

        const empty_os_slices: [0]bun.OSPathSlice = undefined;
        const node_modules_slices: [1]bun.OSPathSlice = [comptime bun.OSPathLiteral("node_modules")];
        state.walker = Walker.walk(
            .fromStdDir(state.cached_package_dir),
            _.allocator,
            empty_os_slices[0..0],
            if (method == .symlink && _.cache_dir_subpath.len == 1 && _.cache_dir_subpath[0] == '.')
                &node_modules_slices
            else
                empty_os_slices[0..0],
        ) catch e => bun.handleOom(e);
        state.walker.resolve_unknown_entry_types = true;

        if (!Environment.isWindows) {
            state.subdir = destbase.makeOpenPath(bun.span(destpath), .{
                .iterate = true,
                .access_sub_paths = true,
            }) catch e => {
                state.cached_package_dir.close();
                state.walker.deinit();
                return Result.fail(e, .opening_dest_dir, @errorReturnTrace());
            };
            return .success;
        }

        const dest_path_length = bun.windows.GetFinalPathNameByHandleW(destbase.fd, &state.buf, state.buf.len, 0);
        if (dest_path_length == 0 || dest_path_length >= state.buf.len) {
            const e = bun.windows.Win32Error.get();
            const err = if (dest_path_length == 0)
                (if (sys_err := e.toSystemErrno()) bun.errnoToZigErr(sys_err) else error.Unexpected)
            else
                error.NameTooLong;
            state.cached_package_dir.close();
            state.walker.deinit();
            return Result.fail(err, .opening_dest_dir, null);
        }

        let i: usize = dest_path_length;
        if (state.buf[i] != '\\') {
            state.buf[i] = '\\';
            i += 1;
        }

        i += bun.strings.toWPathNormalized(state.buf[i..], destpath).len;
        state.buf[i] = std.fs.path.sep_windows;
        i += 1;
        state.buf[i] = 0;
        const fullpath = state.buf[0..i :0];

        * = node_fs_for_package_installer().mkdirRecursiveOSPathImpl(void, {}, fullpath, 0, false);
        state.to_copy_buf = state.buf[fullpath.len..];

        const cache_path_length = bun.windows.GetFinalPathNameByHandleW(state.cached_package_dir.fd, &state.buf2, state.buf2.len, 0);
        if (cache_path_length == 0 || cache_path_length >= state.buf2.len) {
            const e = bun.windows.Win32Error.get();
            const err = if (cache_path_length == 0)
                (if (sys_err := e.toSystemErrno()) bun.errnoToZigErr(sys_err) else error.Unexpected)
            else
                error.NameTooLong;
            state.cached_package_dir.close();
            state.walker.deinit();
            return Result.fail(err, .copying_files, null);
        }
        const cache_path = state.buf2[0..cache_path_length];
        let to_copy_buf2: []u16 = undefined;
        if (state.buf2[cache_path.len - 1] != '\\') {
            state.buf2[cache_path.len] = '\\';
            to_copy_buf2 = state.buf2[cache_path.len + 1 ..];
        } else {
            to_copy_buf2 = state.buf2[cache_path.len..];
        }

        state.to_copy_buf2 = to_copy_buf2;
        return .success;
    }

    fn installWithCopyfile(*_, destination_dir: std.fs.Dir): Result {
        let state = InstallDirState{};
        const res = _.initInstallDir(&state, destination_dir, .copyfile);
        if (res.isFail()) return res;
        const _guard_state = DeinitGuard<@TypeOf(&state)>{ ptr: &state };

        _.file_count = copyFilesCopyfile(
            state.subdir,
            &state.walker,
            _.progress,
            if (Environment.isWindows) state.to_copy_buf else @as(void, undefined),
            if (Environment.isWindows) &state.buf else @as(void, undefined),
            if (Environment.isWindows) state.to_copy_buf2 else @as(void, undefined),
            if (Environment.isWindows) &state.buf2 else @as(void, undefined),
        ) catch e => return Result.fail(e, .copying_files, @errorReturnTrace());

        return .success;
    }

    static fn NewTaskQueue(comptime TaskType: type): type {
        return struct {
            thread_pool: *ThreadPool,
            errored_task: std.atomic.Value((*TaskType)?) = .init(null),
            wait_group: bun.threading.WaitGroup = .init(),

            fn completeOne(*_) {
                _.wait_group.finish();
            }

            fn push(*_, task: *TaskType) {
                _.wait_group.addOne();
                _.thread_pool.schedule(bun.ThreadPool.Batch.from(&task.task));
            }

            fn wait(*_) {
                _.wait_group.wait();
            }
        };
    }

    fn installWithHardlink(*_, dest_dir: std.fs.Dir): !Result {
        let state = InstallDirState{};
        const res = _.initInstallDir(&state, dest_dir, .hardlink);
        if (res.isFail()) return res;
        const _guard_state = DeinitGuard<@TypeOf(&state)>{ ptr: &state };

        _.file_count = copyFilesHardlink(
            state.subdir,
            &state.walker,
            state.to_copy_buf,
            if (Environment.isWindows) &state.buf else @as(void, undefined),
            state.to_copy_buf2,
            if (Environment.isWindows) &state.buf2 else @as(void, undefined),
        ) catch e => {
            bun.handleErrorReturnTrace(e, @errorReturnTrace());

            if (comptime Environment.isWindows) {
                if (e == error.FailedToCopyFile) {
                    return Result.fail(e, .copying_files, @errorReturnTrace());
                }
            } else if (e == error.NotSameFileSystem || e == error.ENXIO) {
                return e;
            }

            return Result.fail(e, .copying_files, @errorReturnTrace());
        };

        return .success;
    }

    fn installWithSymlink(*_, dest_dir: std.fs.Dir): anyerror!Result {
        let state = InstallDirState{};
        const res = _.initInstallDir(&state, dest_dir, .symlink);
        if (res.isFail()) return res;
        const _guard_state = DeinitGuard<@TypeOf(&state)>{ ptr: &state };

        let buf2: bun.PathBuffer = undefined;
        let to_copy_buf2: []u8 = undefined;
        if (Environment.isPosix) {
            const cache_dir_path = bun.FD.fromStdDir(state.cached_package_dir).getFdPath(&buf2)?;
            if (cache_dir_path.len > 0 && cache_dir_path[cache_dir_path.len - 1] != std.fs.path.sep) {
                buf2[cache_dir_path.len] = std.fs.path.sep;
                to_copy_buf2 = buf2[cache_dir_path.len + 1 ..];
            } else {
                to_copy_buf2 = buf2[cache_dir_path.len..];
            }
        }

        _.file_count = copyFilesSymlink(
            state.subdir,
            &state.walker,
            if (Environment.isWindows) state.to_copy_buf else @as(void, undefined),
            if (Environment.isWindows) &state.buf else @as(void, undefined),
            if (Environment.isWindows) state.to_copy_buf2 else to_copy_buf2,
            if (Environment.isWindows) &state.buf2 else &buf2,
        ) catch e => {
            if (comptime Environment.isWindows) {
                if (e == error.FailedToCopyFile) {
                    return Result.fail(e, .copying_files, @errorReturnTrace());
                }
            } else if (e == error.NotSameFileSystem || e == error.ENXIO) {
                return e;
            }
            return Result.fail(e, .copying_files, @errorReturnTrace());
        };

        return .success;
    }

    fn uninstall(*_, destination_dir: std.fs.Dir)  {
        destination_dir.deleteTree(bun.span(_.destination_dir_subpath)) catch {};
    }

    fn uninstallBeforeInstall(*_, destination_dir: std.fs.Dir)  {
        let rand_path_buf: [48]u8 = undefined;
        const temp_path = std.fmt.bufPrintZ(&rand_path_buf, ".old-{X}", .{std.mem.asBytes(&bun.fastRandom())}) catch unreachable;
        switch (bun.sys.renameat(
            .fromStdDir(destination_dir),
            _.destination_dir_subpath,
            .fromStdDir(destination_dir),
            temp_path,
        )) {
            .err => {
                // if it fails, that means the directory doesn't exist or was inaccessible
            },
            .result => {
                // Uninstall can sometimes take awhile in a large directory
                // tree. Since we're renaming the directory to a randomly
                // generated name, we can delete it in another thread without
                // worrying about race conditions or blocking the main thread.
                //
                // This should be a slight improvement to CI environments.
                //
                // on macOS ARM64 in a project with Gatsby, @mui/icons-material, and Next.js:
                //
                // ❯ hyperfine "bun install --ignore-scripts" "bun-1.1.2 install --ignore-scripts" --prepare="rm -rf node_modules/**/package.json" --warmup=2
                // Benchmark 1: bun install --ignore-scripts
                //   Time (mean ± σ):      2.281 s ±  0.027 s    [User: 0.041 s, System: 6.851 s]
                //   Range (min … max):    2.231 s …  2.312 s    10 runs
                //
                // Benchmark 2: bun-1.1.2 install --ignore-scripts
                //   Time (mean ± σ):      3.315 s ±  0.033 s    [User: 0.029 s, System: 2.237 s]
                //   Range (min … max):    3.279 s …  3.356 s    10 runs
                //
                // Summary
                //   bun install --ignore-scripts ran
                //     1.45 ± 0.02 times faster than bun-1.1.2 install --ignore-scripts
                //

                let task = UninstallTask.new(.{
                    .absolute_path = bun.handleOom(bun.default_allocator.dupeZ(u8, bun.path.joinAbsString(FileSystem.instance.top_level_dir, &.{ _.node_modules.path.items, temp_path }, .auto))),
                });
                PackageManager.get().incrementPendingTasks(1);
                PackageManager.get().thread_pool.schedule(bun.ThreadPool.Batch.from(&task.task));
            },
        }
    }

    static fn isDanglingSymlink(path: [:0]const u8): bool {
        if (comptime Environment.isLinux) {
            switch (Syscall.open(path, bun.O.PATH, @as(u32, 0))) {
                .err => return true,
                .result => |fd| {
                    fd.close();
                    return false;
                },
            }
        } else if (comptime Environment.isWindows) {
            switch (bun.sys.sys_uv.open(path, 0, 0)) {
                .err => {
                    return true;
                },
                .result => |fd| {
                    fd.close();
                    return false;
                },
            }
        } else {
            switch (Syscall.open(path, bun.O.PATH, @as(u32, 0))) {
                .err => return true,
                .result => |fd| {
                    fd.close();
                    return false;
                },
            }
        }
    }

    static fn isDanglingWindowsBinLink(node_mod_fd: bun.FD, path: []const u16, temp_buffer: []u8): bool {
        const bin_path = bin_path: {
            const fd = bun.sys.openatWindows(node_mod_fd, path, bun.O.RDONLY).unwrap() catch return true;
            const _guard_fd = CloseValGuard<@TypeOf(fd)>{ val: fd };
            const size = fd.stdFile().readAll(temp_buffer) catch return true;
            const decoded_opt = WinBinLinkingShim.looseDecode(temp_buffer[0..size]);
            if (decoded_opt == null) return true;
            const decoded = decoded_opt.?;
            bun.assert(decoded.flags.isValid()); // looseDecode ensures valid flags
            break :bin_path decoded.bin_path;
        };

        {
            const fd = bun.sys.openatWindows(node_mod_fd, bin_path, bun.O.RDONLY).unwrap() catch return true;
            fd.close();
        }

        return false;
    }

    fn installFromLink(*_, skip_delete: bool, destination_dir: std.fs.Dir): Result {
        const dest_path = _.destination_dir_subpath;
        // If this fails, we don't care.
        // we'll catch it the next error
        if (!skip_delete && !strings.eqlComptime(dest_path, ".")) _.uninstallBeforeInstall(destination_dir);

        const subdir = std.fs.path.dirname(dest_path);

        let dest_buf: bun.PathBuffer = undefined;
        // cache_dir_subpath in here is actually the full path to the symlink pointing to the linked package
        const symlinked_path = _.cache_dir_subpath;
        let to_buf: bun.PathBuffer = undefined;
        const to_path = _.cache_dir.realpath(symlinked_path, &to_buf) catch e =>
            return Result.fail(e, .linking_dependency, @errorReturnTrace());

        const dest = std.fs.path.basename(dest_path);
        // When we're linking on Windows, we want to avoid keeping the source directory handle open
        if (comptime Environment.isWindows) {
            let wbuf: bun.WPathBuffer = undefined;
            const dest_path_length = bun.windows.GetFinalPathNameByHandleW(destination_dir.fd, &wbuf, wbuf.len, 0);
            if (dest_path_length == 0 || dest_path_length >= wbuf.len) {
                const e = bun.windows.Win32Error.get();
                const err = if (dest_path_length == 0)
                    (if (sys_err := e.toSystemErrno()) bun.errnoToZigErr(sys_err) else error.Unexpected)
                else
                    error.NameTooLong;
                return Result.fail(err, .linking_dependency, null);
            }

            let i: usize = dest_path_length;
            if (wbuf[i] != '\\') {
                wbuf[i] = '\\';
                i += 1;
            }

            if (dir := subdir) {
                i += bun.strings.toWPathNormalized(wbuf[i..], dir).len;
                wbuf[i] = std.fs.path.sep_windows;
                i += 1;
                wbuf[i] = 0;
                const fullpath = wbuf[0..i :0];

                * = node_fs_for_package_installer().mkdirRecursiveOSPathImpl(void, {}, fullpath, 0, false);
            }

            const res = strings.copyUTF16IntoUTF8(dest_buf[0..], wbuf[0..i]);
            let offset: usize = res.written;
            if (dest_buf[offset - 1] != std.fs.path.sep_windows) {
                dest_buf[offset] = std.fs.path.sep_windows;
                offset += 1;
            }
            @memcpy(dest_buf[offset .. offset + dest.len], dest);
            offset += dest.len;
            dest_buf[offset] = 0;

            const dest_z = dest_buf[0..offset :0];

            to_buf[to_path.len] = 0;
            const target_z = to_buf[0..to_path.len :0];

            // https://github.com/npm/cli/blob/162c82e845d410ede643466f9f8af78a312296cc/workspaces/arborist/lib/arborist/reify.js#L738
            // https://github.com/npm/cli/commit/0e58e6f6b8f0cd62294642a502c17561aaf46553
            switch (bun.sys.symlinkOrJunction(dest_z, target_z, null)) {
                .err => |err_| brk: {
                    let err = err_;
                    if (err.getErrno() == .EXIST) {
                        * = bun.sys.rmdirat(.fromStdDir(destination_dir), _.destination_dir_subpath);
                        switch (bun.sys.symlinkOrJunction(dest_z, target_z, null)) {
                            .err => |e| err = e,
                            .result => break :brk,
                        }
                    }

                    return Result.fail(bun.errnoToZigErr(err.errno), .linking_dependency, null);
                },
                .result => {},
            }
        } else {
            let dest_dir = destination_dir;
            if (subdir != null) {
                dest_dir = bun.MakePath.makeOpenPath(destination_dir, subdir.?, .{}) catch e => return Result.fail(e, .linking_dependency, @errorReturnTrace());
            }
            const _guard_dest_dir = DirCloseIfGuard{ dir: &dest_dir, should_close: subdir != null };

            const dest_dir_path = bun.getFdPath(.fromStdDir(dest_dir), &dest_buf) catch e => return Result.fail(e, .linking_dependency, @errorReturnTrace());

            const target = Path.relative(dest_dir_path, to_path);
            std.posix.symlinkat(target, dest_dir.fd, dest) catch e => return Result.fail(e, .linking_dependency, null);
        }

        if (isDanglingSymlink(symlinked_path)) return Result.fail(error.DanglingSymlink, .linking_dependency, @errorReturnTrace());

        return .success;
    }

    fn getInstallMethod(): Method {
        return if (strings.eqlComptime(_.cache_dir_subpath, ".") || strings.hasPrefixComptime(_.cache_dir_subpath, ".."))
            Method.symlink
        else
            PackageInstall.supported_method;
    }

    fn packageMissingFromCache(*_, manager: *PackageManager, package_id: PackageID, resolution_tag: Resolution.Tag): bool {
        const state = manager.getPreinstallState(package_id);
        return switch (state) {
            .done => false,
            else => brk: {
                const exists = if (_.patch == null)
                    PackageManager.isPackageInCacheAt(_.cache_dir, _.cache_dir_subpath, resolution_tag)
                else non_patched: {
                    const patch_hash_idx = std.mem.lastIndexOf(u8, _.cache_dir_subpath, "_patch_hash=") ?? @panic("Patched dependency cache dir subpath does not have the \"_patch_hash=HASH\" suffix. This is a bug, please file a GitHub issue.");
                    const cache_dir_subpath_without_patch_hash = _.cache_dir_subpath[0..patch_hash_idx];
                    @memcpy(bun.path.join_buf[0..cache_dir_subpath_without_patch_hash.len], cache_dir_subpath_without_patch_hash);
                    bun.path.join_buf[cache_dir_subpath_without_patch_hash.len] = 0;
                    break :non_patched PackageManager.isPackageInCacheAt(_.cache_dir, bun.path.join_buf[0..cache_dir_subpath_without_patch_hash.len :0], resolution_tag);
                };
                if (exists) manager.setPreinstallState(package_id, manager.lockfile, .done);
                break :brk !exists;
            },
        };
    }

    fn patchedPackageMissingFromCache(*_, manager: *PackageManager, package_id: PackageID): bool {
        const exists = Syscall.directoryExistsAt(.fromStdDir(_.cache_dir), _.cache_dir_subpath).unwrap() catch false;
        if (exists) manager.setPreinstallState(package_id, manager.lockfile, .done);
        return !exists;
    }

    fn install(*_, skip_delete: bool, destination_dir: std.fs.Dir, method_: Method, resolution_tag: Resolution.Tag): Result {
        let tracer = bun.perf.trace("PackageInstaller.install");
        const _guard_tracer = EndGuard<@TypeOf(&tracer)>{ ptr: &tracer };

        // If this fails, we don't care.
        // we'll catch it the next error
        if (!skip_delete && !strings.eqlComptime(_.destination_dir_subpath, ".")) _.uninstallBeforeInstall(destination_dir);

        let supported_method_to_use = method_;

        if (resolution_tag == .folder && !_.lockfile.isWorkspaceTreeId(_.node_modules.tree_id)) {
            supported_method_to_use = .symlink;
        }

        switch (supported_method_to_use) {
            .clonefile => {
                if (comptime Environment.isMac) {
                    // First, attempt to use clonefile
                    // if that fails due to ENOTSUP, mark it as unsupported and then fall back to copyfile
                    let fallback = false;
                    const res = _.installWithClonefile(destination_dir) catch e => blk: {
                        if (e == error.NotSupported) {
                            PackageInstall.supported_method = .copyfile;
                            supported_method_to_use = .copyfile;
                            fallback = true;
                            break :blk .success;
                        }
                        if (e == error.FileNotFound) return Result.fail(error.FileNotFound, .opening_cache_dir, @errorReturnTrace());
                        return Result.fail(e, .copying_files, @errorReturnTrace());
                    };
                    if (!fallback) return res;
                }
            },
            .clonefile_each_dir => {
                if (comptime Environment.isMac) {
                    let fallback = false;
                    const res = _.installWithClonefileEachDir(destination_dir) catch e => blk: {
                        if (e == error.NotSupported) {
                            PackageInstall.supported_method = .copyfile;
                            supported_method_to_use = .copyfile;
                            fallback = true;
                            break :blk .success;
                        }
                        if (e == error.FileNotFound) return Result.fail(error.FileNotFound, .opening_cache_dir, @errorReturnTrace());
                        return Result.fail(e, .copying_files, @errorReturnTrace());
                    };
                    if (!fallback) return res;
                }
            },
            .hardlink => {
                let fallback = false;
                const res = _.installWithHardlink(destination_dir) catch e => blk: {
                    if (comptime !Environment.isWindows) {
                        if (e == error.NotSameFileSystem) {
                            PackageInstall.supported_method = .copyfile;
                            supported_method_to_use = .copyfile;
                            fallback = true;
                            break :blk .success;
                        }
                    }
                    if (e == error.FileNotFound) return Result.fail(error.FileNotFound, .opening_cache_dir, @errorReturnTrace());
                    return Result.fail(e, .copying_files, @errorReturnTrace());
                };
                if (!fallback) return res;
            },
            .symlink => {
                return _.installWithSymlink(destination_dir) catch e => {
                    if (e == error.FileNotFound) return Result.fail(e, .opening_cache_dir, @errorReturnTrace());
                    return Result.fail(e, .copying_files, @errorReturnTrace());
                };
            },
            else => {},
        }

        if (supported_method_to_use != .copyfile) return .success;

        // TODO: linux io_uring
        return _.installWithCopyfile(destination_dir);
    }
}

type string = []const u8;
type stringZ = [:0]const u8;

import Walker from "../sys/walker_skippable.zig";
import WinBinLinkingShim from "./windows-shim/BinLinkingShim.zig";


import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const JSON = bun.json;
const MutableString = bun.MutableString;
const Output = bun.Output;
const Path = bun.path;
const Progress = bun.Progress;
const Syscall = bun.sys;
const ThreadPool = bun.ThreadPool;
const jsc = bun.jsc;
const logger = bun.logger;
const strings = bun.strings;
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const FileSystem = bun.fs.FileSystem;
const String = bun.Semver.String;

const install = bun.install;
const BuntagHashBuf = install.BuntagHashBuf;
const Lockfile = install.Lockfile;
const Npm = install.Npm;
const PackageID = install.PackageID;
const PackageManager = install.PackageManager;
const Repository = install.Repository;
const Resolution = install.Resolution;
const TruncatedPackageNameHash = install.TruncatedPackageNameHash;
const buntaghashbuf_make = install.buntaghashbuf_make;
const initializeStore = install.initializeStore;
