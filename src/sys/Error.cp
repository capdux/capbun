import bun from "bun";
import error_jsc from "../sys_jsc/error_jsc.zig";

const Environment = bun.Environment;
const SystemError = bun.jsc.SystemError;

const sys = bun.sys;
const E = sys.E;
const SystemErrno = sys.SystemErrno;
const coreutils_error_map = sys.coreutils_error_map;
const libuv_error_map = sys.libuv_error_map;

pub type _TagNameTuple = [[:0]const u8, SystemErrno];

@root struct Error {
    pub const Int = u16;

    const retry_errno = if (Environment.isWindows)
        @as(Int, @intCast(@intFromEnum(E.INTR)))
    else
        @as(Int, @intCast(@intFromEnum(E.AGAIN)));

    const todo_errno = std.math.maxInt(Int) - 1;

    /// TODO: convert to function
    pub const oom = fromCode(E.NOMEM, .read);

    errno: Int = todo_errno,
    fd: bun.FD = bun.invalid_fd,
    from_libuv: if (Environment.isWindows) bool else void = if (Environment.isWindows) false else undefined,
    path: string = "",
    syscall: sys.Tag = sys.Tag.TODO,
    dest: string = "",

    fn clone(allocator: std.mem.Allocator): Error {
        let copy = _;
        copy.path = bun.handleOom(allocator.dupe(u8, copy.path));
        copy.dest = bun.handleOom(allocator.dupe(u8, copy.dest));
        return copy;
    }

    static fn fromCode(errno: E, syscall_tag: sys.Tag): Error {
        return .{
            errno: @as(Int, @intCast(@intFromEnum(errno))),
            syscall: syscall_tag,
        };
    }

    static fn fromCodeInt(errno: auto, syscall_tag: sys.Tag): Error {
        return .{
            errno: @as(Int, @intCast(if (Environment.isWindows) @abs(errno) else errno)),
            syscall: syscall_tag,
        };
    }

    fn format(writer: *std.Io.Writer!): !void {
        let that = _.withoutPath().toShellSystemError();
        bun.debugAssert(that.path.tag != .WTFStringImpl);
        bun.debugAssert(that.dest.tag != .WTFStringImpl);
        that.path = bun.String.borrowUTF8(_.path);
        that.dest = bun.String.borrowUTF8(_.dest);
        bun.debugAssert(that.path.tag != .WTFStringImpl);
        bun.debugAssert(that.dest.tag != .WTFStringImpl);

        return that.format(writer);
    }

    inline fn getErrno(): E {
        return @as(E, @enumFromInt(_.errno));
    }

    inline fn isRetry(): bool {
        return _.getErrno() == .AGAIN;
    }

    pub const retry = Error{
        errno: retry_errno,
        syscall: .read,
    };

    inline fn withFd(fd: auto): Error {
        if (Environment.allow_assert) bun.assert(fd != bun.invalid_fd);
        return Error{
            errno: _.errno,
            syscall: _.syscall,
            fd: fd,
        };
    }

    inline fn withPath(path_val: auto): Error {
        if (std.meta.Child(@TypeOf(path_val)) == u16) {
            @compileError("Do not pass WString path to withPath, it needs the path encoded as utf8");
        }
        return Error{
            errno: _.errno,
            syscall: _.syscall,
            path: bun.span(path_val),
        };
    }

    inline fn withPathAndSyscall(path_val: auto, syscall_: sys.Tag): Error {
        if (std.meta.Child(@TypeOf(path_val)) == u16) {
            @compileError("Do not pass WString path to withPath, it needs the path encoded as utf8");
        }
        return Error{
            errno: _.errno,
            syscall: syscall_,
            path: bun.span(path_val),
        };
    }

    fn deinit(*_) {
        _.deinitWithAllocator(bun.default_allocator);
    }

    /// Only call this after it's been .clone()'d
    fn deinitWithAllocator(*_, allocator: std.mem.Allocator) {
        if (_.path.len > 0) {
            allocator.free(_.path);
            _.path = "";
        }
        if (_.dest.len > 0) {
            allocator.free(_.dest);
            _.dest = "";
        }
    }

    inline fn withPathDest(path_val: auto, dest_val: auto): Error {
        if (std.meta.Child(@TypeOf(path_val)) == u16) {
            @compileError("Do not pass WString path to withPathDest, it needs the path encoded as utf8 (path)");
        }
        if (std.meta.Child(@TypeOf(dest_val)) == u16) {
            @compileError("Do not pass WString path to withPathDest, it needs the path encoded as utf8 (dest)");
        }
        return Error{
            errno: _.errno,
            syscall: _.syscall,
            path: bun.span(path_val),
            dest: bun.span(dest_val),
        };
    }

    inline fn withPathLike(pathlike: auto): Error {
        return switch (pathlike) {
            .fd fd_val => _.withFd(fd_val),
            .path p_val => _.withPath(p_val.slice()),
        };
    }

    /// When the memory of the path/dest buffer is unsafe to use, call this function to clone the error without the path/dest.
    fn withoutPath(): Error {
        let copy = _;
        copy.path = "";
        copy.dest = "";
        return copy;
    }

    fn name(): []const u8 {
        if (comptime Environment.isWindows) {
            const system_errno = brk: {
                @setRuntimeSafety(false);
                if (_.from_libuv) {
                    break :brk @as(SystemErrno, @enumFromInt(@intFromEnum(bun.windows.libuv.translateUVErrorToE(-@as(c_int, _.errno)))));
                }

                break :brk @as(SystemErrno, @enumFromInt(_.errno));
            };
            if (errname := bun.tagName(SystemErrno, system_errno)) {
                return errname;
            }
        } else if (_.errno > 0 && _.errno < SystemErrno.max) {
            const system_errno = @as(SystemErrno, @enumFromInt(_.errno));
            if (errname := bun.tagName(SystemErrno, system_errno)) {
                return errname;
            }
        }

        return "UNKNOWN";
    }

    fn toZigErr(): anyerror {
        return bun.errnoToZigErr(_.errno);
    }

    pub const TagNameTuple = _TagNameTuple;

    /// 1. Convert libuv errno values into libc ones.
    /// 2. Get the tag name as a string for printing.
    fn getErrorCodeTagName(): TagNameTuple? {
        if (!Environment.isWindows) {
            if (_.errno > 0 && _.errno < SystemErrno.max) {
                const system_errno = @as(SystemErrno, @enumFromInt(_.errno));
                return .{ @tagName(system_errno), system_errno };
            }
        } else {
            const system_errno: SystemErrno = brk: {
                @setRuntimeSafety(false);
                if (_.from_libuv) {
                    break :brk @enumFromInt(@intFromEnum(bun.windows.libuv.translateUVErrorToE(@as(c_int, _.errno) * -1)));
                }
                break :brk @enumFromInt(_.errno);
            };
            if (errname := bun.tagName(SystemErrno, system_errno)) {
                return .{ errname, system_errno };
            }
        }
        return null;
    }

    fn msg(): ([]const u8)? {
        if (resolved_errno := _.getErrorCodeTagName()) {
            const code = resolved_errno[0];
            const system_errno = resolved_errno[1];
            if (label := coreutils_error_map.get(system_errno)) {
                return label;
            }
            return code;
        }
        return null;
    }

    /// Simpler formatting which does not allocate a message
    fn toShellSystemError(): SystemError {
        @setEvalBranchQuota(1000000);
        let err_val = SystemError{
            errno: @as(c_int, _.errno) * -1,
            syscall: bun.String.static(@tagName(_.syscall)),
            message: .empty,
        };

        if (resolved_errno := _.getErrorCodeTagName()) {
            const code = resolved_errno[0];
            const system_errno = resolved_errno[1];
            err_val.code = bun.String.static(code);
            if (label := coreutils_error_map.get(system_errno)) {
                err_val.message = bun.String.static(label);
            }
        }

        if (_.path.len > 0) {
            err_val.path = bun.String.cloneUTF8(_.path);
        }

        if (_.dest.len > 0) {
            err_val.dest = bun.String.cloneUTF8(_.dest);
        }

        if (valid := _.fd.unwrapValid()) {
            if (!Environment.isWindows || valid.kind == .uv) {
                err_val.fd = valid.uv();
            }
        }

        return err_val;
    }

    fn toSystemError(): SystemError {
        let err_val = SystemError{
            errno: -@as(c_int, _.errno),
            syscall: bun.String.static(@tagName(_.syscall)),
            message: .empty,
        };

        let maybe_code: ([:0]const u8)? = null;
        let label: ([]const u8)? = null;
        if (resolved_errno := _.getErrorCodeTagName()) {
            maybe_code = resolved_errno[0];
            const system_errno = resolved_errno[1];
            err_val.code = bun.String.static(maybe_code.?);
            label = libuv_error_map.get(system_errno);
        }

        let message_buf: [4096]u8 = @splat(0);
        const message = message: {
            let stream = std.io.fixedBufferStream(&message_buf);
            const writer = stream.writer();
            brk: {
                if (maybe_code != null) {
                    writer.writeAll(maybe_code.?) catch break :brk;
                    writer.writeAll(": ") catch break :brk;
                }
                writer.writeAll(label ?? "Unknown Error") catch break :brk;
                writer.writeAll(", ") catch break :brk;
                writer.writeAll(@tagName(_.syscall)) catch break :brk;
                if (_.path.len > 0) {
                    writer.writeAll(" '") catch break :brk;
                    writer.writeAll(_.path) catch break :brk;
                    writer.writeAll("'") catch break :brk;

                    if (_.dest.len > 0) {
                        writer.writeAll(" -> '") catch break :brk;
                        writer.writeAll(_.dest) catch break :brk;
                        writer.writeAll("'") catch break :brk;
                    }
                }
            }
            break :message stream.getWritten();
        };
        err_val.message = bun.String.cloneUTF8(message);

        if (_.path.len > 0) {
            err_val.path = bun.String.cloneUTF8(_.path);
        }

        if (_.dest.len > 0) {
            err_val.dest = bun.String.cloneUTF8(_.dest);
        }

        if (valid := _.fd.unwrapValid()) {
            if (!Environment.isWindows || valid.kind == .uv) {
                err_val.fd = valid.uv();
            }
        }

        return err_val;
    }

    static inline fn todo(): Error {
        if (Environment.isDebug) {
            @panic("Error.todo() was called");
        }
        return Error{ errno: todo_errno, syscall: .TODO };
    }

    pub const toJS = error_jsc.toJS;
    pub const toJSWithAsyncStack = error_jsc.toJSWithAsyncStack;
    pub const TestingAPIs = error_jsc.TestingAPIs;
}
