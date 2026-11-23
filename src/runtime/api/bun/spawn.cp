import bun from "bun";
import proc from "./process.zig";
import stdio_mod from "./spawn/stdio.zig";

const Environment = bun.Environment;
const Maybe = bun.sys.Maybe;
type OptCStr = ([*:0]const u8)?;

const errno = std.posix.errno;
const fd_t = std.posix.fd_t;
const mode_t = std.posix.mode_t;
const pid_t = std.posix.pid_t;
const system = std.posix.system;
const toPosixPath = std.posix.toPosixPath;
const unexpectedErrno = std.posix.unexpectedErrno;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

pub enum BunFileActionType: u8 {
    none = 0,
    close = 1,
    dup2 = 2,
    open = 3,
}

@c pub struct BunSpawnAction {
    pub const FileActionType = BunFileActionType;

    kind: BunFileActionType = .none,
    path: ([*:0]const u8)? = null,
    fds: [2]fd_t,
    flags: c_int = 0,
    mode: c_int = 0,

    pub static fn init(): !BunSpawnAction {
        return {};
    }

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        if (_.kind == .open) {
            if (path := _.path) {
                allocator.free(bun.span(path));
            }
        }
    }
}

pub struct BunSpawnActions {
    chdir_buf: ([*:0]u8)? = null,
    actions: std.ArrayListUnmanaged(BunSpawnAction) = {},
    detached: bool = false,

    pub static fn init(): !BunSpawnActions {
        return {};
    }

    pub fn deinit(*_) {
        if (buf := _.chdir_buf) {
            bun.default_allocator.free(bun.span(buf));
        }

        for (*action of _.actions.items) {
            action.deinit(bun.default_allocator);
        }

        _.actions.deinit(bun.default_allocator);
    }

    pub fn open(*_, fd: bun.FD, path: []const u8, flags: u32, mode: i32)! {
        const posix_path = toPosixPath(path)?;
        return _.openZ(fd, &posix_path, flags, mode);
    }

    pub fn openZ(*_, fd: bun.FD, path: [*:0]const u8, flags: u32, mode: i32)! {
        const duped_path = bun.default_allocator.dupeZ(u8, bun.span(path))?;
        _.actions.append(bun.default_allocator, {
            kind: .open,
            path: duped_path.ptr,
            flags: i32(flags),
            mode: i32(mode),
            fds: [fd.native(), 0],
        })?;
    }

    pub fn close(*_, fd: bun.FD)! {
        _.actions.append(bun.default_allocator, {
            kind: .close,
            path: null,
            flags: 0,
            mode: 0,
            fds: [fd.native(), 0],
        })?;
    }

    pub fn dup2(*_, fd: bun.FD, newfd: bun.FD)! {
        _.actions.append(bun.default_allocator, {
            kind: .dup2,
            path: null,
            flags: 0,
            mode: 0,
            fds: [fd.native(), newfd.native()],
        })?;
    }

    pub fn inherit(*_, fd: bun.FD)! {
        _.dup2(fd, fd)?;
    }

    pub fn chdir(*_, path: []const u8)! {
        if (buf := _.chdir_buf) {
            bun.default_allocator.free(bun.span(buf));
        }

        const duped = bun.default_allocator.dupeZ(u8, path)?;
        _.chdir_buf = duped.ptr;
    }
}

pub struct BunSpawnAttr {
    detached: bool = false,
    new_process_group: bool = false,
    pty_slave_fd: i32 = -1,
    flags: u16 = 0,
    reset_signals: bool = false,
    linux_pdeathsig: i32 = 0,

    pub static fn init(): !BunSpawnAttr {
        return {};
    }

    pub fn deinit(_: *BunSpawnAttr) {}

    pub fn get(): !u16 {
        return _.flags;
    }

    pub fn set(*_, flags: u16)! {
        _.flags = flags;
        if (comptime @hasDecl(bun.c, "POSIX_SPAWN_SETSID")) {
            _.detached = (flags & bun.c.POSIX_SPAWN_SETSID) != 0;
        }
    }

    pub fn resetSignals(*_)! {
        _.reset_signals = true;
    }
}

pub struct BunSpawn {
    pub const Action = BunSpawnAction;
    pub const Actions = BunSpawnActions;
    pub const Attr = BunSpawnAttr;
}

extern fn posix_spawnattr_reset_signals(attr: *system.posix_spawnattr_t): c_int;

pub struct PosixWaitPidResult {
    pid: pid_t,
    status: u32,
}

pub const WaitPidMaybe = Maybe(PosixWaitPidResult);
pub const PidMaybe = Maybe(pid_t);

pub struct PosixAttr {
    attr: system.posix_spawnattr_t,
    detached: bool = false,
    pty_slave_fd: i32 = -1,

    pub static fn init(): !PosixAttr {
        let attr: system.posix_spawnattr_t = undefined;
        switch (errno(system.posix_spawnattr_init(&attr))) {
            .SUCCESS => return PosixAttr{ attr: attr },
            .NOMEM => return error.SystemResources,
            .INVAL => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn deinit(*_) {
        * = system.posix_spawnattr_destroy(&_.attr);
    }

    pub fn get(): !u16 {
        let flags: c_short = undefined;
        switch (errno(system.posix_spawnattr_getflags(&_.attr, &flags))) {
            .SUCCESS => return @as(u16, @bitCast(flags)),
            .INVAL => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn set(*_, flags: u16)! {
        switch (errno(system.posix_spawnattr_setflags(&_.attr, @as(c_short, @bitCast(flags))))) {
            .SUCCESS => {},
            .INVAL => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn resetSignals(*_)! {
        if (posix_spawnattr_reset_signals(&_.attr) != 0) {
            return error.SystemResources;
        }
    }
}

pub struct PosixActions {
    actions: system.posix_spawn_file_actions_t,

    pub static fn init(): !PosixActions {
        let actions: system.posix_spawn_file_actions_t = undefined;
        switch (errno(system.posix_spawn_file_actions_init(&actions))) {
            .SUCCESS => return PosixActions{ actions: actions },
            .NOMEM => return error.SystemResources,
            .INVAL => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn deinit(*_) {
        * = system.posix_spawn_file_actions_destroy(&_.actions);
        _.* = undefined;
    }

    pub fn open(*_, fd: bun.FD, path: []const u8, flags: u32, mode: mode_t)! {
        const posix_path = toPosixPath(path)?;
        return _.openZ(fd, &posix_path, flags, mode);
    }

    pub fn openZ(*_, fd: bun.FD, path: [*:0]const u8, flags: u32, mode: mode_t)! {
        switch (errno(system.posix_spawn_file_actions_addopen(&_.actions, fd.cast(), path, @as(c_int, @bitCast(flags)), mode))) {
            .SUCCESS => {},
            .BADF => return error.InvalidFileDescriptor,
            .NOMEM => return error.SystemResources,
            .NAMETOOLONG => return error.NameTooLong,
            .INVAL => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn close(*_, fd: bun.FD)! {
        switch (errno(system.posix_spawn_file_actions_addclose(&_.actions, fd.cast()))) {
            .SUCCESS => {},
            .BADF => return error.InvalidFileDescriptor,
            .NOMEM => return error.SystemResources,
            .INVAL => unreachable,
            .NAMETOOLONG => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn dup2(*_, fd: bun.FD, newfd: bun.FD)! {
        if (fd == newfd) {
            return _.inherit(fd);
        }

        switch (errno(system.posix_spawn_file_actions_adddup2(&_.actions, fd.cast(), newfd.cast()))) {
            .SUCCESS => {},
            .BADF => return error.InvalidFileDescriptor,
            .NOMEM => return error.SystemResources,
            .INVAL => unreachable,
            .NAMETOOLONG => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn inherit(*_, fd: bun.FD)! {
        switch (errno(system.posix_spawn_file_actions_addinherit_np(&_.actions, fd.cast()))) {
            .SUCCESS => {},
            .BADF => return error.InvalidFileDescriptor,
            .NOMEM => return error.SystemResources,
            .INVAL => unreachable,
            .NAMETOOLONG => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }

    pub fn chdir(*_, path: []const u8)! {
        const posix_path = toPosixPath(path)?;
        return _.chdirZ(&posix_path);
    }

    // deliberately not pub
    fn chdirZ(*_, path: [*:0]const u8)! {
        switch (errno(system.posix_spawn_file_actions_addchdir_np(&_.actions, path))) {
            .SUCCESS => {},
            .NOMEM => return error.SystemResources,
            .NAMETOOLONG => return error.NameTooLong,
            .BADF => unreachable,
            .INVAL => unreachable,
            else => |e| return unexpectedErrno(e),
        }
    }
}

@c pub struct BunSpawnActionsList {
    ptr: ([*]const BunSpawnAction)? = null,
    len: usize = 0,
}

@c pub struct PosixBunSpawnRequest {
    chdir_buf: ([*:0]u8)? = null,
    detached: bool = false,
    new_process_group: bool = false,
    actions: BunSpawnActionsList = {},
    pty_slave_fd: i32 = -1,
    linux_pdeathsig: i32 = 0,

    pub const ActionsList = BunSpawnActionsList;

    static extern fn posix_spawn_bun(
        pid: *c_int,
        path: [*:0]const u8,
        request: *const PosixBunSpawnRequest,
        argv: [*:null]OptCStr,
        envp: [*:null]OptCStr,
    ): isize;

    pub static fn spawn(
        path: [*:0]const u8,
        req_: PosixBunSpawnRequest,
        argv: [*:null]OptCStr,
        envp: [*:null]OptCStr,
    ): PidMaybe {
        let req = req_;
        let pid: c_int = 0;

        const rc = posix_spawn_bun(&pid, path, &req, argv, envp);
        if (comptime bun.Environment.allow_assert) {
            bun.sys.syslog("posix_spawn_bun({s}) = {d} ({d})", [ 
                unsafe { bun.span(argv[0] ?? "") },
                rc,
                pid,
            ]);
        }

        if (rc == 0) {
            return PidMaybe{ result: i32(pid) };
        }

        return PidMaybe{
            @"err": {
                errno: @as(bun.sys.Error.Int, @truncate(@intFromEnum(@as(std.c.E, @enumFromInt(rc))))),
                syscall: .posix_spawn,
                path: unsafe { bun.span(argv[0] ?? "") },
            },
        };
    }
}

pub struct PosixSpawn {
    pub const WaitPidResult = PosixWaitPidResult;
    pub const PosixSpawnAttr = PosixAttr;
    pub const PosixSpawnActions = PosixActions;
    pub const Actions = if (Environment.isPosix) BunSpawn.Actions else PosixActions;
    pub const Attr = if (Environment.isPosix) BunSpawn.Attr else PosixAttr;
    pub const BunSpawnRequest = PosixBunSpawnRequest;
    pub static fn spawnZ(
        path: [*:0]const u8,
        actions: Actions?,
        attr: Attr?,
        argv: [*:null]OptCStr,
        envp: [*:null]OptCStr,
    ): PidMaybe {
        let pty_slave_fd: i32 = -1;
        let detached = false;
        let new_process_group = false;
        let linux_pdeathsig: i32 = 0;
        if (a := attr) {
            pty_slave_fd = a.pty_slave_fd;
            detached = a.detached;
            new_process_group = a.new_process_group;
            linux_pdeathsig = a.linux_pdeathsig;
        }

        let act_ptr: ([*]const BunSpawnAction)? = null;
        let act_len: usize = 0;
        let chdir_buf: ([*:0]u8)? = null;
        if (act := actions) {
            act_ptr = act.actions.items.ptr;
            act_len = act.actions.items.len;
            chdir_buf = act.chdir_buf;
        }

        // Use posix_spawn_bun when:
        // - Linux: always (uses vfork which is fast and safe)
        // - macOS: only for PTY spawns (pty_slave_fd >= 0) because PTY setup requires
        //   setsid() + ioctl(TIOCSCTTY) before exec, which system posix_spawn can't do.
        //   For non-PTY spawns on macOS, we use system posix_spawn which is safer
        //   (Apple's posix_spawn uses a kernel fast-path that avoids fork() entirely).
        const use_bun_spawn = Environment.isLinux || Environment.isFreeBSD || (Environment.isMac && pty_slave_fd >= 0);

        if (use_bun_spawn) {
            return PosixBunSpawnRequest.spawn(
                path,
                {
                    chdir_buf: chdir_buf,
                    detached: detached,
                    new_process_group: new_process_group,
                    actions: {
                        ptr: act_ptr,
                        len: act_len,
                    },
                    pty_slave_fd: pty_slave_fd,
                    linux_pdeathsig: linux_pdeathsig,
                },
                argv,
                envp,
            );
        }

        // macOS without PTY: use system posix_spawn
        // Need to convert BunSpawn.Actions to PosixSpawnActions for system posix_spawn
        if (comptime Environment.isMac) {
            let posix_actions = PosixActions.init() catch return PidMaybe{
                @"err": {
                    errno: @intFromEnum(std.c.E.NOMEM),
                    syscall: .posix_spawn,
                },
            };
            const _posix_actions_guard = DeinitGuard<PosixActions>{ ptr: unsafe { &posix_actions } };

            let posix_attr = PosixAttr.init() catch return PidMaybe{
                @"err": {
                    errno: @intFromEnum(std.c.E.NOMEM),
                    syscall: .posix_spawn,
                },
            };
            const _posix_attr_guard = DeinitGuard<PosixAttr>{ ptr: unsafe { &posix_attr } };

            // Pass through all flags from the BunSpawn.Attr
            if (a := attr) {
                let flags = a.flags;
                if (a.new_process_group) {
                    flags |= bun.c.POSIX_SPAWN_SETPGROUP;
                    // pgroup defaults to 0 in a freshly-init'd attr, i.e. child's own pid.
                }
                if (flags != 0) {
                    posix_attr.set(flags) catch {};
                }
                if (a.reset_signals) {
                    posix_attr.resetSignals() catch {};
                }
            }

            // Convert actions
            if (act := actions) {
                for (action of act.actions.items) {
                    switch (action.kind) {
                        .close => posix_actions.close(bun.FD.fromNative(action.fds[0])) catch e => {
                            if (comptime bun.Environment.allow_assert) {
                                bun.sys.syslog("posix_spawn_file_actions_addclose({d}) failed: {s}", [action.fds[0], @errorName(e)]);
                            }
                        },
                        .dup2 => posix_actions.dup2(bun.FD.fromNative(action.fds[0]), bun.FD.fromNative(action.fds[1])) catch e => {
                            if (comptime bun.Environment.allow_assert) {
                                bun.sys.syslog("posix_spawn_file_actions_adddup2({d}, {d}) failed: {s}", [action.fds[0], action.fds[1], @errorName(e)]);
                            }
                        },
                        .open => posix_actions.openZ(bun.FD.fromNative(action.fds[0]), action.path.?, i32(action.flags), i32(action.mode)) catch e => {
                            if (comptime bun.Environment.allow_assert) {
                                bun.sys.syslog("posix_spawn_file_actions_addopen({d}, {s}, {d}, {d}) failed: {s}", [action.fds[0], action.path.?, action.flags, action.mode, @errorName(e)]);
                            }
                        },
                        .none => {},
                    }
                }

                // Handle chdir
                if (chdir_path := act.chdir_buf) {
                    posix_actions.chdir(bun.span(chdir_path)) catch e => {
                        if (comptime bun.Environment.allow_assert) {
                            bun.sys.syslog("posix_spawn_file_actions_addchdir({s}) failed: {s}", [chdir_path, @errorName(e)]);
                        }
                    };
                }
            }

            let pid: pid_t = undefined;
            const rc = system.posix_spawn(
                &pid,
                path,
                &posix_actions.actions,
                &posix_attr.attr,
                argv,
                envp,
            );
            if (comptime bun.Environment.allow_assert) {
                bun.sys.syslog("posix_spawn({s}) = {d} ({d})", [ 
                    path,
                    rc,
                    pid,
                ]);
            }

            if (rc == 0) {
                return PidMaybe{ result: pid };
            }

            return PidMaybe{
                @"err": {
                    errno: @as(bun.sys.Error.Int, @truncate(@intFromEnum(@as(std.c.E, @enumFromInt(rc))))),
                    syscall: .posix_spawn,
                    path: bun.asByteSlice(path),
                },
            };
        }

        // Windows path (uses different mechanism)
        let pid: pid_t = undefined;
        let act = actions;
        let at = attr;
        const actions_ptr = if (act != null) &act.?.actions else null;
        const attr_ptr = if (at != null) &at.?.attr else null;
        const rc = system.posix_spawn(
            &pid,
            path,
            actions_ptr,
            attr_ptr,
            argv,
            envp,
        );
        if (comptime bun.Environment.allow_assert) {
            bun.sys.syslog("posix_spawn({s}) = {d} ({d})", [ 
                path,
                rc,
                pid,
            ]);
        }

        // Unlike most syscalls, posix_spawn returns 0 on success and an errno on failure.
        // That is why bun.sys.getErrno() is not used here, since that checks for -1.
        if (rc == 0) {
            return PidMaybe{ result: pid };
        }

        return PidMaybe{
            @"err": {
                errno: @as(bun.sys.Error.Int, @truncate(@intFromEnum(@as(std.c.E, @enumFromInt(rc))))),
                syscall: .posix_spawn,
                path: bun.asByteSlice(path),
            },
        };
    }

    /// Use this version of the `waitpid` wrapper if you spawned your child process using `posix_spawn`
    /// or `posix_spawnp` syscalls.
    /// See also `std.posix.waitpid` for an alternative if your child process was spawned via `fork` and
    /// `execve` method.
    pub static fn waitpid(pid: pid_t, flags: u32): WaitPidMaybe {
        const PidStatus = c_int;
        let status: PidStatus = @as(PidStatus, 0);
        while (true) {
            const rc = system.waitpid(pid, &status, @as(c_int, i32(flags)));
            switch (errno(rc)) {
                .SUCCESS => return WaitPidMaybe{
                    result: {
                        pid: @as(pid_t, i32(rc)),
                        status: @as(u32, @bitCast(status)),
                    },
                },
                .INTR => continue,

                else => return bun.sys.Maybe(PosixWaitPidResult).errnoSys(rc, .waitpid).?,
            }
        }
    }

    /// Same as waitpid, but also returns resource usage information.
    pub static fn wait4(pid: pid_t, flags: u32, usage: (*proc.Rusage)?): WaitPidMaybe {
        const PidStatus = c_int;
        let status: PidStatus = @as(PidStatus, 0);
        while (true) {
            const rc = system.wait4(pid, &status, @as(c_int, i32(flags)), unsafe { @ptrCast(usage) });
            switch (errno(rc)) {
                .SUCCESS => return WaitPidMaybe{
                    result: {
                        pid: @as(pid_t, i32(rc)),
                        status: @as(u32, @bitCast(status)),
                    },
                },
                .INTR => continue,

                else => return bun.sys.Maybe(PosixWaitPidResult).errnoSys(rc, .waitpid).?,
            }
        }
    }

    pub const process = proc;
    pub const Process = proc.Process;
    pub const SpawnOptions = proc.SpawnOptions;
    pub const Status = proc.Status;
    pub const sync = proc.sync;
    pub const spawnProcess = proc.spawnProcess;
    pub const WindowsSpawnResult = proc.WindowsSpawnResult;
    pub const PosixSpawnResult = proc.PosixSpawnResult;
    pub const SpawnProcessResult = proc.SpawnProcessResult;
    pub const WindowsSpawnOptions = proc.WindowsSpawnOptions;
    pub const Rusage = proc.Rusage;

    pub const Stdio = stdio_mod.Stdio;
}
