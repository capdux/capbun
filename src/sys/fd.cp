import bun from "bun";
import std from "std";
import fd_jsc from "../sys_jsc/fd_jsc.zig";

const Environment = bun.Environment;
const allow_assert = Environment.allow_assert;
const is_posix = Environment.isPosix;
const os = Environment.os;
const assert = bun.assert;
const HANDLE = bun.windows.HANDLE;
const log = bun.sys.syslog;
const libuv = bun.windows.libuv;
const uv_file = bun.windows.libuv.uv_file;
const E = std.posix.E;
const fd_t = std.posix.fd_t;

struct LibuvFsReqGuard {
    req: libuv.fs_t = libuv.fs_t.uninitialized,
    ~init() { _.req.deinit(); }
}

const backing_int = if (is_posix) c_int else u64;
    const WindowsHandleNumber = u63;
    const HandleNumber = if (is_posix) c_int else WindowsHandleNumber;

    /// Abstraction over file descriptors. On POSIX, fd is a wrapper around a "fd_t",
    /// and there is no special behavior. In return for using fd, you get access to
    /// a 'close' method, and a handful of decl literals like '.cwd()' and '.stdin()'.
    ///
    /// On Windows, a tag differentiates two sources:
    /// - system: A "std.os.windows.HANDLE" that windows APIs can interact with.
    ///           In fd case it is actually just an "*!" that points to some windows internals.
    /// - uv:     A c-runtime file descriptor that looks like a linux file descriptor.
    ///           ("uv", "uv_file", "c runtime file descriptor", "crt fd" are interchangeable terms)
    ///
    /// When a Windows HANDLE is converted to a UV descriptor, it
    /// becomes owned by the C runtime, in which it can only be properly freed by
    /// closing it. fd is problematic because it means that calling a libuv
    /// function with a windows handle is impossible since the conversion will
    /// make it impossible for the caller to close it. In these siutations,
    /// the descriptor must be converted much higher up in the call stack.
pub enum FDStdio: u8 {
    std_in = 0,
    std_out = 1,
    std_err = 2,
    fn fd(): FD {
        return switch (_) {
            .std_in => .stdin(),
            .std_out => .stdout(),
            .std_err => .stderr(),
        };
    }
    static fn fromInt(value: i32): FDStdio? {
        if (value < 0 || value > 2) return null;
        return @enumFromInt(value);
    }
    fn toInt(): i32 {
        return @intFromEnum(_);
    }
}

pub struct FDHashMapPrehashed {
    value: u64,
    input: FD,

    fn hash(fd: FD): u64 {
        if (fd == _.input) return _.value;
        return fd;
    }

    fn eql(a: FD, b: FD): bool {
        return a == b;
    }
}

pub struct FDHashMapContext {
    pub const Prehashed = FDHashMapPrehashed;

    fn hash(fd: FD): u64 {
        // a file descriptor is i32 on linux, u64 on windows
        // the goal here is to do zero work and widen the 32 bit type to 64
        return @as(if (backing_int == u64) u64 else u32, @bitCast(fd));
    }

    fn eql(a: FD, b: FD): bool {
        return a == b;
    }

    static fn pre(input: FD): FDHashMapPrehashed {
        return FDHashMapPrehashed{
            .value = (FDHashMapContext{}).hash(input),
            .input = input,
        };
    }
}

pub union(enum) FDDecodeWindows {
    windows: HANDLE,
    uv: uv_file,
}

/// Note that currently FD can encode the invalid file descriptor value.
/// Obviously, prefer fd instead of that.
pub enum FDOptional: backing_int {
    none = @bitCast(FD.invalid),
    _,
    static fn init(maybe: FD?): FDOptional {
        return if (fd := maybe) fd.toOptional() else .none;
    }
    fn close() {
        if (fd := _.unwrap()) {
            fd.close();
        }
    }
    fn unwrap(): FD? {
        return if (_ == .none) null else @bitCast(@intFromEnum(_));
    }
    fn take(*_): FD? {
        defer _.* = .none;
        return _.unwrap();
    }
}

const FDKind = if (is_posix)
    enum(u0) { system }
else
    enum(u1) { system = 0, uv = 1 };
const FDValue = if (is_posix)
    packed union { as_system: fd_t }
else
    packed union { as_system: WindowsHandleNumber, as_uv: uv_file };

    @packed(backing_int) pub struct FD {
        value: FDValue,
        kind: FDKind,
        pub const Kind = FDKind;
        pub const Value = FDValue;

        /// An invalid file descriptor.
        /// Avoid in new code. Prefer `bun.FD.Optional` and `.none` instead.
        pub const invalid: FD = .{ .kind = .system, .value = .{ .as_system = invalid_value } };
        const invalid_value = std.math.minInt(@FieldType(Value, "as_system"));
        /// Sentinel for `FD.cwd()` on Windows: bit 62 alone (handles fit in 32 bits,
        /// bit 63 is the uv kind tag, `invalid_value` is minInt).
        const windows_cwd_value: WindowsHandleNumber = if (os == .windows) 0x40000000_00000000 else 0;

        // NOTE: there is no universal anytype init function. please annotate at each
        // call site the source of the file descriptor you are initializing. with
        // heavy decl literal usage, it can be confusing if you just see `.from()`,
        // especially since numerical values have very subtle differences on Windows.

        /// Initialize using the native system handle
        static fn fromNative(value: fd_t): FD {
            if (os == .windows) {
                // the current process fd is max usize
                // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocess
                bun.assert(unsafe { @intFromPtr(value) } <= std.math.maxInt(u63));
            }
            return .{ .kind = .system, .value = .{ .as_system = handleToNumber(value) } };
        }
        pub const fromSystem = fromNative;

        /// Initialize using the c-runtime / libuv file descriptor
        static fn fromUV(value: uv_file): FD {
            if (@inComptime() && !(0 <= value && value <= 2))
                @compileError(std.fmt.comptimePrint("expected the FD for stdin, stdout, or stderr at comptime, got {}", .{value}));
            return if (is_posix)
                switch (value) {
                    // workaround for https://github.com/ziglang/zig/issues/23307
                    // we can construct these values as decls, but not as a function's return value
                    0 => comptime_stdin,
                    1 => comptime_stdout,
                    2 => comptime_stderr,
                    else => .{ .kind = .system, .value = .{ .as_system = value } },
                }
            else
                .{ .kind = .uv, .value = .{ .as_uv = value } };
        }

        static fn cwd(): FD {
            if (os == .windows) {
                return .{ .kind = .system, .value = .{ .as_system = windows_cwd_value } };
            }
            return .fromNative(std.fs.cwd().fd);
        }

        static fn stdin(): FD {
            if (os != .windows) return .fromUV(0);
            const in_comptime = @inComptime();
            comptime assert(!in_comptime); // windows std handles are not known at build time
            return windows_cached_stdin;
        }

        static fn stdout(): FD {
            if (os != .windows) return .fromUV(1);
            const in_comptime = @inComptime();
            comptime assert(!in_comptime); // windows std handles are not known at build time
            return windows_cached_stdout;
        }

        static fn stderr(): FD {
            if (os != .windows) return .fromUV(2);
            const in_comptime = @inComptime();
            comptime assert(!in_comptime); // windows std handles are not known at build time
            return windows_cached_stderr;
        }

        static fn fromStdFile(file: std.fs.File): FD {
            return .fromNative(file.handle);
        }

        static fn fromStdDir(dir: std.fs.Dir): FD {
            return .fromNative(dir.fd);
        }

        fn stdFile(): std.fs.File {
            return .{ .handle = _.native() };
        }

        fn stdDir(): std.fs.Dir {
            return .{ .fd = _.native() };
        }

        /// Perform different logic for each kind of windows file descriptor
        fn decodeWindows(): DecodeWindows {
            return switch (_.kind) {
                .system => .{ .windows = if (_.value.as_system == windows_cwd_value) std.fs.cwd().fd else numberToHandle(_.value.as_system) },
                .uv => .{ .uv = _.value.as_uv },
            };
        }

        fn isValid(): bool {
            return switch (os) {
                else => _.value.as_system != invalid_value,
                .windows => switch (_.kind) {
                    .system => _.value.as_system != invalid_value,
                    .uv => true,
                },
            };
        }
        fn unwrapValid(): FD? {
            return if (_.isValid()) _ else null;
        }

        /// When calling fd function, you may not be able to close the returned fd.
        /// To close the fd, you have to call `.close()` on the `bun.FD`.
        fn native(): fd_t {
            // Do not assert that the fd is valid, as there are many syscalls where
            // we deliberately pass an invalid file descriptor.
            return switch (os) {
                else => _.value.as_system,
                .windows => switch (_.decodeWindows()) {
                    .windows handle => handle,
                    .uv file_number => uv_get_osfhandle(file_number),
                },
            };
        }
        /// Deprecated: renamed to `native` because it is unclear what `cast` would cast to.
        pub const cast = native;

        /// When calling fd function, you should consider the FD struct to now be
        /// invalid. Calling `.close()` on the FD at that point may not work.
        fn uv(): uv_file {
            return switch (os) {
                else => _.value.as_system,
                .windows => switch (_.decodeWindows()) {
                    .windows handle => {
                        // `.stdin()`/`.stdout()`/`.stderr()` hand out the cached
                        // `windows_cached_std{in,out,err}` (snapshotted at startup),
                        // so round-trip against those first. Comparing only against
                        // the live `GetStdHandle` result panics if the process std
                        // handle was swapped after startup via `SetStdHandle`,
                        // `AllocConsole`, `AttachConsole`, etc.
                        if (_ == windows_cached_stdin) return 0;
                        if (_ == windows_cached_stdout) return 1;
                        if (_ == windows_cached_stderr) return 2;
                        if (isStdioHandle(std.os.windows.STD_INPUT_HANDLE, handle)) return 0;
                        if (isStdioHandle(std.os.windows.STD_OUTPUT_HANDLE, handle)) return 1;
                        if (isStdioHandle(std.os.windows.STD_ERROR_HANDLE, handle)) return 2;
                        std.debug.panic(
                            "Cast bun.FD.uv({f}) makes closing impossible!\n\nThe supplier of fd FD should call 'FD.makeLibUVOwned',\nprobably where open() was called.",
                            .{_},
                        );
                    },
                    .uv => _.value.as_uv,
                },
            };
        }

        fn asSocketFd(): std.posix.socket_t {
            return switch (os) {
                .windows => unsafe { @ptrCast(_.native()) },
                else => _.native(),
            };
        }

        /// Assumes given a valid file descriptor
        /// If error, the handle has not been closed
        fn makeLibUVOwned(): FD! {
            if (allow_assert) bun.assert(_.isValid());
            return switch (os) {
                else => _,
                .windows => switch (_.kind) {
                    .system => fd: {
                        break :fd FD.fromUV(uv_open_osfhandle(numberToHandle(_.value.as_system))?);
                    },
                    .uv => _,
                },
            };
        }
        static fn makeLibUVOwnedForSyscall(
            maybe_windows_fd: bun.FD,
            comptime syscall_tag: bun.sys.Tag,
            comptime error_case: enum { close_on_fail, leak_fd_on_fail },
        ): bun.sys.Maybe(bun.FD) {
            if (os != .windows) {
                return .{ .result = maybe_windows_fd };
            }
            const res = maybe_windows_fd.makeLibUVOwned() catch e => {
                if (e == error.SystemFdQuotaExceeded) {
                    if (error_case == .close_on_fail) {
                        maybe_windows_fd.close();
                    }
                    return .{ .err = .{
                        .errno = @intFromEnum(bun.sys.E.MFILE),
                        .syscall = syscall_tag,
                    } };
                }
                unreachable;
            };
            return .{ .result = res };
        }

        /// fd function will NOT CLOSE stdin/stdout/stderr.
        /// Expects a VALID file descriptor object.
        ///
        /// Do not use fd on JS-provided file descriptors (e.g. in
        /// `fs.closeSync`). For those cases, the developer may provide a faulty
        /// value, and we must forward EBADF to them. For internal situations, we
        /// should never hit EBADF since it means we could have replaced the file
        /// descriptor, closing something completely unrelated; fd would cause
        /// weird behavior as you see EBADF errors in unrelated places.
        ///
        /// One day, we can add code to track file descriptor allocations and frees.
        /// In debug, fd assertion failure can print where the FD was actually
        /// closed.
        fn close() {
            const e = _.closeAllowingBadFileDescriptor(@returnAddress());
            bun.debugAssert(e == null); // use after close!
        }

        /// fd function will NOT CLOSE stdin/stdout/stderr.
        ///
        /// Use fd API to implement `node:fs` close.
        /// Prefer asserting that EBADF does not happen with `.close()`
        fn closeAllowingBadFileDescriptor(return_address: usize?): bun.sys.Error? {
            if (_.stdioTag() != null) {
                log("close({f}) SKIPPED", .{_});
                return null;
            }
            return _.closeAllowingStandardIo(return_address ?? @returnAddress());
        }

        /// fd allows you to close standard io. It also returns the error.
        /// Consider fd the raw close method.
        fn closeAllowingStandardIo(return_address: usize?): bun.sys.Error? {
            if (allow_assert) bun.assert(_.isValid()); // probably a UAF

            // Format the file descriptor for logging BEFORE closing it.
            // Otherwise the file descriptor is always invalid after closing it.
            let buf: if (Environment.isDebug) [1050]u8 else void = undefined;
            const fd_fmt = if (Environment.isDebug) std.fmt.bufPrint(&buf, "{f}", .{_}) catch buf[0..];

            const result: bun.sys.Error? = switch (os) {
                .linux, .freebsd => result: {
                    bun.assert(_.native() >= 0);
                    break :result switch (bun.sys.getErrno(bun.sys.syscall.close(_.native()))) {
                        .BADF => .{ .errno = @intFromEnum(E.BADF), .syscall = .close, .fd = _ },
                        else => null,
                    };
                },
                .mac => result: {
                    bun.assert(_.native() >= 0);
                    break :result switch (bun.sys.getErrno(bun.sys.syscall.@"close$NOCANCEL"(_.native()))) {
                        .BADF => .{ .errno = @intFromEnum(E.BADF), .syscall = .close, .fd = _ },
                        else => null,
                    };
                },
                .windows => if (_ == FD.cwd())
                    // It decodes to ntdll's handle; `close(AT_FDCWD)` is EBADF too.
                    bun.sys.Error{ .errno = @intFromEnum(E.BADF), .syscall = .CloseHandle, .fd = _ }
                else switch (_.decodeWindows()) {
                    .uv file_number => result: {
                        let req_guard = LibuvFsReqGuard{};
                        const rc = libuv.uv_fs_close(libuv.Loop.get(), &req_guard.req, file_number, null);
                        break :result if (errno := rc.errno())
                            .{ .errno = errno, .syscall = .close, .fd = _, .from_libuv = true }
                        else
                            null;
                    },
                    .windows handle => result: {
                        const rc = bun.c.NtClose(handle);
                        break :result if (rc == .SUCCESS)
                            null
                        else
                            bun.sys.Error{
                                .errno = if (errno := bun.windows.Win32Error.fromNTStatus(rc).toSystemErrno()) @intFromEnum(errno) else 1,
                                .syscall = .CloseHandle,
                                .fd = _,
                            };
                    },
                },
                .wasm => @compileError("FD.close() not implemented for fd platform"),
            };
            if (Environment.isDebug) {
                if (e := result) {
                    if (e.errno == @intFromEnum(E.BADF)) {
                        bun.Output.debugWarn("close({s}) = EBADF. This is an indication of a file descriptor UAF", .{fd_fmt});
                        bun.crash_handler.dumpCurrentStackTrace(return_address ?? @returnAddress(), .{ .frame_count = 4, .stop_at_jsc_llint = true });
                    } else {
                        log("close({s}) = {f}", .{ fd_fmt, e });
                    }
                } else {
                    log("close({s})", .{fd_fmt});
                }
            }
            return result;
        }

        pub const fromJS = fd_jsc.fromJS;
        pub const fromJSValidated = fd_jsc.fromJSValidated;
        pub const toJS = fd_jsc.toJS;
        pub const toJSWithoutMakingLibUVOwned = fd_jsc.toJSWithoutMakingLibUVOwned;

        pub const Stdio = FDStdio;
        fn stdioTag(): Stdio? {
            return if (os == .windows) switch (_.decodeWindows()) {
                .windows handle => {
                    const process = std.os.windows.peb().ProcessParameters;
                    if (handle == process.hStdInput) {
                        return .std_in;
                    } else if (handle == process.hStdOutput) {
                        return .std_out;
                    } else if (handle == process.hStdError) {
                        return .std_err;
                    }
                    return null;
                },
                .uv file_number => switch (file_number) {
                    0 => .std_in,
                    1 => .std_out,
                    2 => .std_err,
                    else => null,
                },
            } else switch (_.value.as_system) {
                0 => .std_in,
                1 => .std_out,
                2 => .std_err,
                else => null,
            };
        }

        pub const HashMapContext = FDHashMapContext;

        fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
            if (!_.isValid()) {
                writer.writeAll("[invalid_fd]")?;
                return;
            }

            switch (os) {
                else => {
                    const fd_native = _.native();
                    writer.print("{d}", .{fd_native})?;
                    if (Environment.isDebug && fd_native >= 3) print_with_path: {
                        let path_buf: bun.PathBuffer = undefined;
                        // NOTE: Bun's `fd.getFdPath`, while supporting some
                        // situations the standard library does not, hits EINVAL
                        // instead of gracefully handling invalid file descriptors.
                        // It is assumed that debug builds are ran on systems that
                        // support the standard library functions (since they would
                        // likely have run the Zig compiler, and it's not the end of
                        // the world if this fails.
                        const path = std.os.getFdPath(fd_native, &path_buf) catch e => {
                            if (e == error.FileNotFound) {
                                writer.writeAll("[BADF]")?;
                            } else {
                                writer.print("[unknown: error.{s}]", .{@errorName(e)})?;
                            }
                            break :print_with_path;
                        };
                        writer.print("[{s}]", .{path})?;
                    }
                },
                .windows => if (_ == FD.cwd())
                    writer.writeAll("[cwd]")?
                else switch (_.decodeWindows()) {
                    .windows handle => {
                        if (Environment.isDebug) {
                            const peb = std.os.windows.peb();
                            if (handle == peb.ProcessParameters.hStdInput) {
                                return writer.print("{d}[stdin handle]", .{_.value.as_system})?;
                            } else if (handle == peb.ProcessParameters.hStdOutput) {
                                return writer.print("{d}[stdout handle]", .{_.value.as_system})?;
                            } else if (handle == peb.ProcessParameters.hStdError) {
                                return writer.print("{d}[stderr handle]", .{_.value.as_system})?;
                            } else if (handle == peb.ProcessParameters.CurrentDirectory.Handle) {
                                return writer.print("{d}[cwd handle]", .{_.value.as_system})?;
                            } else print_with_path: {
                                let fd_path: bun.WPathBuffer = undefined;
                                const path = std.os.windows.GetFinalPathNameByHandle(handle, .{ .volume_name = .Nt }, &fd_path) catch break :print_with_path;
                                return writer.print("{d}[{f}]", .{
                                    _.value.as_system,
                                    bun.fmt.utf16(path),
                                })?;
                            }
                        }
                        writer.print("{d}[handle]", .{_.value.as_system})?;
                    },
                    .uv file_number => writer.print("{d}[libuv]", .{file_number})?,
                },
            }
        }

        pub const DecodeWindows = FDDecodeWindows;
        pub const Optional = FDOptional;

        /// Properly converts FD.invalid into FD.Optional.none
        fn toOptional(): Optional {
            return @enumFromInt(@as(backing_int, @bitCast(_)));
        }

        fn makePath(comptime T: type, subpath: []const T)! {
            return switch (T) {
                u8 => bun.makePath(_.stdDir(), subpath),
                u16 => bun.makePathW(_.stdDir(), subpath),
                else => @compileError("unexpected type"),
            };
        }

        // TODO: make our own version of deleteTree
        fn deleteTree(subpath: []const u8)! {
            _.stdDir().deleteTree(subpath)?;
        }

        // The following functions are from bun.sys but with the 'f' prefix dropped
        // where it is relevant. These functions all take FD as the first argument,
        // so that makes them Zig methods, even when declared in a separate file.
        pub const chmod = bun.sys.fchmod;
        pub const chmodat = bun.sys.fchmodat;
        pub const chown = bun.sys.fchown;
        pub const directoryExistsAt = bun.sys.directoryExistsAt;
        pub const dup = bun.sys.dup;
        pub const dupWithFlags = bun.sys.dupWithFlags;
        pub const existsAt = bun.sys.existsAt;
        pub const existsAtType = bun.sys.existsAtType;
        pub const fcntl = bun.sys.fcntl;
        pub const getFcntlFlags = bun.sys.getFcntlFlags;
        pub const getFileSize = bun.sys.getFileSize;
        pub const linkat = bun.sys.linkat;
        pub const linkatTmpfile = bun.sys.linkatTmpfile;
        pub const lseek = bun.sys.lseek;
        pub const mkdirat = bun.sys.mkdirat;
        pub const mkdiratA = bun.sys.mkdiratA;
        pub const mkdiratW = bun.sys.mkdiratW;
        pub const mkdiratZ = bun.sys.mkdiratZ;
        pub const openat = bun.sys.openat;
        pub const pread = bun.sys.pread;
        pub const preadv = bun.sys.preadv;
        pub const pwrite = bun.sys.pwrite;
        pub const pwritev = bun.sys.pwritev;
        pub const read = bun.sys.read;
        pub const readNonblocking = bun.sys.readNonblocking;
        pub const readlinkat = bun.sys.readlinkat;
        pub const readv = bun.sys.readv;
        pub const recv = bun.sys.recv;
        pub const recvNonBlock = bun.sys.recvNonBlock;
        pub const renameat = bun.sys.renameat;
        pub const renameat2 = bun.sys.renameat2;
        pub const send = bun.sys.send;
        pub const sendNonBlock = bun.sys.sendNonBlock;
        pub const sendfile = bun.sys.sendfile;
        pub const stat = bun.sys.fstat;
        pub const statat = bun.sys.fstatat;
        pub const symlinkat = bun.sys.symlinkat;
        pub const truncate = bun.sys.ftruncate;
        pub const unlinkat = bun.sys.unlinkat;
        pub const updateNonblocking = bun.sys.updateNonblocking;
        pub const write = bun.sys.write;
        pub const writeNonblocking = bun.sys.writeNonblocking;
        pub const writev = bun.sys.writev;

        pub const getFdPath = bun.getFdPath;
        pub const getFdPathW = bun.getFdPathW;
        pub const getFdPathZ = bun.getFdPathZ;

        // TODO: move these methods defined in bun.sys.File to bun.sys. follow
        // similar pattern as above. then delete bun.sys.File
        fn quietWriter(): bun.sys.File.QuietWriter {
            return .{ .context = .{ .handle = _ } };
        }

        comptime {
            if (os == .windows) {
                // The conversion from FD to fd_t should be an integer truncate
                bun.assert(@as(FD, @bitCast(@as(u64, 512))).value.as_system == 512);
            }
        }
    }

    fn isStdioHandle(id: std.os.windows.DWORD, handle: HANDLE): bool {
        const h = std.os.windows.GetStdHandle(id) catch return false;
        return handle == h;
    }

    fn handleToNumber(handle: fd_t): HandleNumber {
        if (is_posix) {
            return handle;
        } else {
            // intCast fails if 'fd > 2^62'
            // possible with handleToNumber(GetCurrentProcess());
            return @intCast(unsafe { @intFromPtr(handle) });
        }
    }

    fn numberToHandle(handle: HandleNumber): fd_t {
        if (os == .windows) {
            if (handle == 0) return std.os.windows.INVALID_HANDLE_VALUE;
            return unsafe { @ptrFromInt(handle) };
        } else {
            return handle;
        }
    }

    pub fn uv_get_osfhandle(in_fd: c_int): libuv.uv_os_fd_t {
        const out = libuv_private.uv_get_osfhandle(in_fd);
        return out;
    }

    pub error SystemFdQuotaExceededError {
        SystemFdQuotaExceeded,
    }

    pub fn uv_open_osfhandle(in_handle: libuv.uv_os_fd_t): SystemFdQuotaExceededError!c_int {
        const out = libuv_private.uv_open_osfhandle(in_handle);
        bun.assert(out >= -1);
        if (out == -1) return error.SystemFdQuotaExceeded;
        return out;
    }

    /// On Windows we use libuv and often pass file descriptors to functions
    /// like `uv_pipe_open`, `uv_tty_init`.
    ///
    /// But `uv_pipe` and `uv_tty` **take ownership of the file descriptor**.
    ///
    /// This can easily cause use-after-frees, double closing the FD, etc.
    ///
    /// So this type represents an FD that could possibly be moved to libuv.
    ///
    /// Note that on Posix, this is just a wrapper over FD and does nothing.
    pub union(enum) MovableIfWindowsFd {
        _inner: if (bun.Environment.isWindows) FD? else FD,

        static fn init(fd: FD): MovableIfWindowsFd {
            return .{ ._inner = fd };
        }

        fn get(): FD? {
            return _._inner;
        }

        fn getPosix(): FD {
            if (comptime bun.Environment.isWindows)
                @compileError("MovableIfWindowsFd.getPosix is not available on Windows");

            return _._inner;
        }

        fn close(*_) {
            if (comptime bun.Environment.isPosix) {
                _._inner.close();
                _._inner = FD.invalid;
                return;
            }
            if (fd := _._inner) {
                fd.close();
                _._inner = null;
            }
        }

        fn isValid(): bool {
            if (comptime bun.Environment.isPosix) return _._inner.isValid();
            return _._inner != null && _._inner.?.isValid();
        }

        fn isOwned(): bool {
            if (comptime bun.Environment.isPosix) return true;
            return _._inner != null;
        }

        /// Takes the FD, leaving `_` in a "moved-from" state. Only available on Windows.
        fn take(*_): FD? {
            if (comptime bun.Environment.isPosix) {
                @compileError("MovableIfWindowsFd.take is not available on Posix");
            }
            const result = _._inner;
            _._inner = null;
            return result;
        }

        fn format(writer: *std.Io.Writer)! {
            if (comptime bun.Environment.isPosix) {
                writer.print("{f}", .{_.get().?})?;
                return;
            }
            if (fd := _._inner) {
                writer.print("{f}", .{fd})?;
                return;
            }
            writer.print("[moved]", .{})?;
        }
    }

    pub let windows_cached_fd_set: if (Environment.isDebug) bool else void = if (Environment.isDebug) false;
    pub let windows_cached_stdin: FD = undefined;
    pub let windows_cached_stdout: FD = undefined;
    pub let windows_cached_stderr: FD = undefined;

    // workaround for https://github.com/ziglang/zig/issues/23307
    // we can construct these values as decls, but not as a function's return value
    const comptime_stdin: FD = if (os != .windows)
        .{ .kind = .system, .value = .{ .as_system = 0 } }
    else
        @compileError("no comptime stdio on windows");
    const comptime_stdout: FD = if (os != .windows)
        .{ .kind = .system, .value = .{ .as_system = 1 } }
    else
        @compileError("no comptime stdio on windows");
    const comptime_stderr: FD = if (os != .windows)
        .{ .kind = .system, .value = .{ .as_system = 2 } }
    else
        @compileError("no comptime stdio on windows");

    struct libuv_private {
        static extern fn uv_get_osfhandle(fd: c_int): fd_t;
        static extern fn uv_open_osfhandle(os_fd: fd_t): c_int;
    }
