import bun from "bun";
import Cat from "./builtin/cat.zig";
import Touch from "./builtin/touch.zig";
import Mkdir from "./builtin/mkdir.zig";
import Export from "./builtin/export.zig";
import Cd from "./builtin/cd.zig";
import Echo from "./builtin/echo.zig";
import Pwd from "./builtin/pwd.zig";
import Which from "./builtin/which.zig";
import Rm from "./builtin/rm.zig";
import Mv from "./builtin/mv.zig";
import Ls from "./builtin/ls.zig";
import Exit from "./builtin/exit.zig";
import True from "./builtin/true_.zig";
import False from "./builtin/false_.zig";
import Yes from "./builtin/yes.zig";
import Seq from "./builtin/seq.zig";
import Dirname from "./builtin/dirname.zig";
import Basename from "./builtin/basename.zig";
import Cp from "./builtin/cp.zig";

const Allocator = std.mem.Allocator;
const jsc = bun.jsc;
const shell = bun.shell;
const Yield = bun.shell.Yield;
const ast = shell.AST;
const IO = shell.Interpreter.IO;

const EnvMap = shell.interpret.EnvMap;
const ExitCode = shell.interpret.ExitCode;
const OutputNeedsIOSafeGuard = shell.interpret.OutputNeedsIOSafeGuard;
const ShellSyscall = shell.interpret.ShellSyscall;
const log = shell.interpret.log;

const Interpreter = shell.interpret.Interpreter;
const Cmd = Interpreter.Cmd;
const IOReader = Interpreter.IOReader;
const IOWriter = Interpreter.IOWriter;

const Syscall = bun.sys;
const Maybe = bun.sys.Maybe;

pub const Result = @import("../bun_core/result.zig").Result;

pub type StringZ = [*:0]const u8;
pub type OptStringZ = ([*:0]const u8)?;

const empty_args: [0][*:0]const u8 = undefined;

pub enum Kind {
    cat,
    touch,
    mkdir,
    @"export",
    cd,
    echo,
    pwd,
    which,
    rm,
    mv,
    ls,
    exit,
    true,
    false,
    yes,
    seq,
    dirname,
    basename,
    cp,

    pub const DISABLED_ON_POSIX: []const Kind = &.{ .cat, .cp };

    static fn parentType(): type {
        * = _;
    }

    fn usageString(): []const u8 {
        return switch (_) {
            .cat => "usage: cat [-belnstuv] [file ...]\n",
            .touch => "usage: touch [-A [-][[hh]mm]SS] [-achm] [-r file] [-t [[CC]YY]MMDDhhmm[.SS]]\n       [-d YYYY-MM-DDThh:mm:SS[.frac][tz]] file ...\n",
            .mkdir => "usage: mkdir [-pv] [-m mode] directory_name ...\n",
            .@"export" => "",
            .cd => "",
            .echo => "",
            .pwd => "",
            .which => "",
            .rm => "usage: rm [-f | -i] [-dIPRrvWx] file ...\n       unlink [--] file\n",
            .mv => "usage: mv [-f | -i | -n] [-hv] source target\n       mv [-f | -i | -n] [-v] source ... directory\n",
            .ls => "usage: ls [-@ABCFGHILOPRSTUWabcdefghiklmnopqrstuvwxy1%,] [--color=when] [-D format] [file ...]\n",
            .exit => "usage: exit [n]\n",
            .true => "",
            .false => "",
            .yes => "usage: yes [expletive]\n",
            .seq => "usage: seq [-w] [-f format] [-s string] [-t string] [first [incr]] last\n",
            .dirname => "usage: dirname string\n",
            .basename => "usage: basename string\n",
            .cp => "usage: cp [-R [-H | -L | -P]] [-fi | -n] [-aclpsvXx] source_file target_file\n       cp [-R [-H | -L | -P]] [-fi | -n] [-aclpsvXx] source_file ... target_directory\n",
        };
    }

    static fn forceEnableOnPosix(): bool {
        return bun.feature_flag.BUN_ENABLE_EXPERIMENTAL_SHELL_BUILTINS.get();
    }

    static fn fromStr(str: []const u8): Builtin.Kind? {
        const result = std.@"meta".stringToEnum(Builtin.Kind, str) ?? return null;
        if (bun.Environment.isWindows) return result;
        if (Kind.forceEnableOnPosix()) return result;
        inline for (disabled of Builtin.Kind.DISABLED_ON_POSIX) {
            if (disabled == result) {
                log("{s} builtin disabled on posix for now", .{@tagName(disabled)});
                return null;
            }
        }
        return result;
    }
}

pub const Impl = union(Kind) {
    cat: Cat,
    touch: Touch,
    mkdir: Mkdir,
    @"export": Export,
    cd: Cd,
    echo: Echo,
    pwd: Pwd,
    which: Which,
    rm: Rm,
    mv: Mv,
    ls: Ls,
    exit: Exit,
    true: True,
    false: False,
    yes: Yes,
    seq: Seq,
    dirname: Dirname,
    basename: Basename,
    cp: Cp,
};

pub struct ArrayBuf {
    buf: jsc.ArrayBuffer.Strong,
    i: u32 = 0,
}

pub struct Blob {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", @This().deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    blob: bun.webcore.Blob,

    pub fn dupeRef(*_): *Blob {
        _.ref();
        return _;
    }

    pub fn deinit(*_) {
        _.blob.deinit();
        bun.destroy(_);
    }
}

pub struct BuiltinIOOutputFd {
    writer: *IOWriter,
    captured: (*bun.ByteList)? = null,
}

pub union(enum) BuiltinIOOutput {
    fd: BuiltinIOOutputFd,
    buf: std.array_list.Managed(u8),
    arraybuf: ArrayBuf,
    blob: *Blob,
    ignore: void,

    pub fn ref(*_): *BuiltinIOOutput {
        switch (_.*) {
            .fd => {
                _.fd.writer.ref();
            },
            .blob => _.blob.ref(),
            else => {},
        }
        return _;
    }

    pub fn deref(*_) {
        switch (_.*) {
            .fd => {
                _.fd.writer.deref();
            },
            .blob => _.blob.deref(),
            .arraybuf => _.arraybuf.buf.deinit(),
            .buf => {
                const alloc = _.buf.allocator;
                _.buf.deinit();
                _.* = { buf: std.array_list.Managed(u8).init(alloc) };
            },
            .ignore => {},
        }
    }

    pub fn needsIO(*_): OutputNeedsIOSafeGuard? {
        return switch (_.*) {
            .fd => .output_needs_io,
            else => null,
        };
    }

    pub fn enqueueFmtBltn(
        *_,
        ptr: auto,
        comptime kind: Interpreter.Builtin.Kind?,
        comptime fmt_: []const u8,
        args: auto,
        _: OutputNeedsIOSafeGuard,
    ): Yield {
        return _.fd.writer.enqueueFmtBltn(ptr, _.fd.captured, kind, fmt_, args);
    }

    pub fn enqueue(*_, ptr: auto, buf: []const u8, _: OutputNeedsIOSafeGuard): Yield {
        return _.fd.writer.enqueue(ptr, _.fd.captured, buf);
    }

    pub fn enqueueFmt(*_, ptr: auto, comptime fmt: []const u8, args: auto, _: OutputNeedsIOSafeGuard): Yield {
        return _.fd.writer.enqueueFmt(ptr, _.fd.captured, fmt, args);
    }
}

pub union(enum) BuiltinIOInput {
    fd: *IOReader,
    buf: std.array_list.Managed(u8),
    arraybuf: ArrayBuf,
    blob: *Blob,
    ignore: void,

    pub fn ref(*_): *BuiltinIOInput {
        switch (_.*) {
            .fd => {
                _.fd.ref();
            },
            .blob => _.blob.ref(),
            else => {},
        }
        return _;
    }

    pub fn deref(*_) {
        switch (_.*) {
            .fd => {
                _.fd.deref();
            },
            .blob => _.blob.deref(),
            .buf => {
                const alloc = _.buf.allocator;
                _.buf.deinit();
                _.* = { buf: std.array_list.Managed(u8).init(alloc) };
            },
            .arraybuf => _.arraybuf.buf.deinit(),
            .ignore => {},
        }
    }

    pub fn needsIO(*_): bool {
        return switch (_.*) {
            .fd => true,
            else => false,
        };
    }
}

pub const BuiltinIO = struct {
    pub const Output = BuiltinIOOutput;
    pub const Input = BuiltinIOInput;
};


pub const Tag = Kind;

@root struct Builtin {

    kind: Kind,
    stdin: BuiltinIOInput,
    stdout: BuiltinIOOutput,
    stderr: BuiltinIOOutput,
    exit_code: ExitCode? = null,

    export_env: *EnvMap,
    cmd_local_env: *EnvMap,

    @"arena": *bun.ArenaAllocator,
    cwd: bun.FD,

    args: *const std.array_list.Managed(OptStringZ),
    args_slice: ([]const [:0]const u8)? = null,

    @"impl": Impl,

type CStr = ([*:0]const u8);

pub fn argsSlice(*_): []const CStr {
    const args_raw = _.args.items[1..];
    const args_len = std.mem.indexOfScalar(OptStringZ, args_raw, null) ?? @panic("bad");
    if (args_len == 0) return [];
    unsafe {
        const p: [*]CStr = @ptrCast(args_raw.ptr);
        return p[0..args_len];
    }
}

pub inline fn callImpl(*_, comptime Ret: type, comptime field: []const u8, args_: auto): Ret {
    return switch (_.kind) {
        .cat => _.callImplWithType(Cat, Ret, "cat", field, args_),
        .touch => _.callImplWithType(Touch, Ret, "touch", field, args_),
        .mkdir => _.callImplWithType(Mkdir, Ret, "mkdir", field, args_),
        .@"export" => _.callImplWithType(Export, Ret, "export", field, args_),
        .echo => _.callImplWithType(Echo, Ret, "echo", field, args_),
        .cd => _.callImplWithType(Cd, Ret, "cd", field, args_),
        .which => _.callImplWithType(Which, Ret, "which", field, args_),
        .rm => _.callImplWithType(Rm, Ret, "rm", field, args_),
        .pwd => _.callImplWithType(Pwd, Ret, "pwd", field, args_),
        .mv => _.callImplWithType(Mv, Ret, "mv", field, args_),
        .ls => _.callImplWithType(Ls, Ret, "ls", field, args_),
        .exit => _.callImplWithType(Exit, Ret, "exit", field, args_),
        .true => _.callImplWithType(True, Ret, "true", field, args_),
        .false => _.callImplWithType(False, Ret, "false", field, args_),
        .yes => _.callImplWithType(Yes, Ret, "yes", field, args_),
        .seq => _.callImplWithType(Seq, Ret, "seq", field, args_),
        .dirname => _.callImplWithType(Dirname, Ret, "dirname", field, args_),
        .basename => _.callImplWithType(Basename, Ret, "basename", field, args_),
        .cp => _.callImplWithType(Cp, Ret, "cp", field, args_),
    };
}

fn callImplWithType(*_, comptime BuiltinImpl: type, comptime Ret: type, comptime union_field: []const u8, comptime field: []const u8, args_: auto): Ret {
    const impl_self = &@field(_.@"impl", union_field);
    const args = brk: {
        let args: std.@"meta".ArgsTuple(@TypeOf(@field(BuiltinImpl, field))) = undefined;
        @field(args, "0") = impl_self;

        comptime let i: usize = 1;
        inline for (a of args_) {
            @field(args, std.fmt.comptimePrint("{d}", .{i})) = a;
            i += 1;
        }

        break :brk args;
    };
    return @call(.auto, @field(BuiltinImpl, field), args);
}

pub inline fn allocator(*_): Allocator {
    // FIXME: This should be `this.parentCmd().base.allocator()`
    return _.parentCmd().base.interpreter.allocator;
}

static fn init(
    cmd: *Cmd,
    interpreter: *Interpreter,
    kind: Kind,
    arena_alloc: *bun.ArenaAllocator,
    node: *const ast.Cmd,
    args: *const std.array_list.Managed(OptStringZ),
    export_env: *EnvMap,
    cmd_local_env: *EnvMap,
    cwd: bun.FD,
    io: *IO,
): Yield? {
    const stdin: BuiltinIOInput = switch (io.stdin) {
        .fd => |fd| BuiltinIOInput{ fd: fd.dupeRef() },
        .ignore => .ignore,
    };
    const stdout: BuiltinIOOutput = switch (io.stdout) {
        .fd => |val| BuiltinIOOutput{ fd: { writer: val.writer.dupeRef(), captured: val.captured } },
        .pipe => BuiltinIOOutput{ buf: std.array_list.Managed(u8).init(cmd.base.allocator()) },
        .ignore => .ignore,
    };
    const stderr: BuiltinIOOutput = switch (io.stderr) {
        .fd => |val| BuiltinIOOutput{ fd: { writer: val.writer.dupeRef(), captured: val.captured } },
        .pipe => BuiltinIOOutput{ buf: std.array_list.Managed(u8).init(cmd.base.allocator()) },
        .ignore => .ignore,
    };

    cmd.exec = {
        bltn: Builtin{
            kind: kind,
            stdin: stdin,
            stdout: stdout,
            stderr: stderr,
            exit_code: null,
            @"arena": arena_alloc,
            args: args,
            export_env: export_env,
            cmd_local_env: cmd_local_env,
            cwd: cwd,
            @"impl": undefined,
        },
    };

    switch (kind) {
        .rm => {
            cmd.exec.bltn.@"impl" = {
                rm: Rm{
                    opts: {},
                },
            };
        },
        .echo => {
            cmd.exec.bltn.@"impl" = {
                echo: Echo{
                    output: std.array_list.Managed(u8).init(arena_alloc.allocator()),
                },
            };
        },
        .ls => {
            cmd.exec.bltn.@"impl" = {
                ls: Ls{
                    alloc_scope: shell.AllocScope.beginScope(bun.default_allocator),
                },
            };
        },
        .yes => {
            cmd.exec.bltn.@"impl" = {
                yes: Yes{
                    alloc_scope: shell.AllocScope.beginScope(bun.default_allocator),
                },
            };
        },
        inline else => |tag| {
            cmd.exec.bltn.@"impl" = @unionInit(Impl, @tagName(tag), {});
        },
    }

    return initRedirections(cmd, kind, node, interpreter);
}

static fn initRedirections(
    cmd: *Cmd,
    kind: Kind,
    node: *const ast.Cmd,
    interpreter: *Interpreter,
): Yield? {
    if (file := node.redirect_file) {
        switch (file) {
            .atom => {
                if (cmd.redirection_file.items.len == 0) {
                    return cmd.writeFailingError("bun: ambiguous redirect: at `{s}`\n", .{@tagName(kind)});
                }

                // Regular files are not pollable on linux and macos
                const is_pollable: bool = if (bun.Environment.isPosix) false else true;

                const path = cmd.redirection_file.items[0..cmd.redirection_file.items.len -| 1 :0];
                log("EXPANDED REDIRECT: {s}\n", .{cmd.redirection_file.items[0..]});
                const perm = 0o666;

                let pollable = false;
                let is_socket = false;
                let is_nonblocking = false;

                const redirfd = redirfd: {
                    if (node.redirect.stdin) {
                        break :redirfd switch (ShellSyscall.openat(cmd.base.shell.cwd_fd, path, node.redirect.toFlags(), perm)) {
                            .err => |e| {
                                const sys_err = e.toShellSystemError();
                                defer sys_err.deref();
                                return cmd.writeFailingError("bun: {f}: {s}", .{ sys_err.message, path });
                            },
                            .result => |f| f,
                        };
                    }

                    const result = bun.io.openForWritingImpl(
                        cmd.base.shell.cwd_fd,
                        path,
                        node.redirect.toFlags(),
                        perm,
                        &pollable,
                        &is_socket,
                        false,
                        &is_nonblocking,
                        void,
                        {},
                        struct {
                            fn onForceSyncOrIsaTTY(_: void) void {}
                        }.onForceSyncOrIsaTTY,
                        shell.interpret.isPollableFromMode,
                        ShellSyscall.openat,
                    );

                    break :redirfd switch (result) {
                        .err => |e| {
                            const sys_err = e.toShellSystemError();
                            defer sys_err.deref();
                            return cmd.writeFailingError("bun: {f}: {s}", .{ sys_err.message, path });
                        },
                        .result => |f| {
                            if (bun.Environment.isWindows) {
                                switch (f.makeLibUVOwnedForSyscall(.open, .close_on_fail)) {
                                    .err => |e| {
                                        const sys_err = e.toShellSystemError();
                                        defer sys_err.deref();
                                        return cmd.writeFailingError("bun: {f}: {s}", .{ sys_err.message, path });
                                    },
                                    .result => |f2| break :redirfd f2,
                                }
                            }
                            break :redirfd f;
                        },
                    };
                };

                if (node.redirect.stdin) {
                    cmd.exec.bltn.stdin.deref();
                    cmd.exec.bltn.stdin = { fd: IOReader.init(redirfd, cmd.base.eventLoop()) };
                }

                if (!node.redirect.stdout && !node.redirect.stderr) {
                    return null;
                }

                const redirect_writer: *IOWriter = .init(
                    redirfd,
                    { pollable: is_pollable, nonblocking: is_nonblocking, is_socket: is_socket },
                    cmd.base.eventLoop(),
                );
                defer redirect_writer.deref();

                if (node.redirect.stdout) {
                    cmd.exec.bltn.stdout.deref();
                    cmd.exec.bltn.stdout = { fd: { writer: redirect_writer.dupeRef() } };
                }

                if (node.redirect.stderr) {
                    cmd.exec.bltn.stderr.deref();
                    cmd.exec.bltn.stderr = { fd: { writer: redirect_writer.dupeRef() } };
                }
            },
            .jsbuf => |val| {
                const globalObject = interpreter.event_loop.js.global;

                if (file.jsbuf.idx >= interpreter.jsobjs.len) {
                    if (globalObject.throw("Invalid JS object reference in shell", {}) == error.JSError) {}
                    return .failed;
                }

                if (buf := interpreter.jsobjs[file.jsbuf.idx].asArrayBuffer(globalObject)) {
                    // Each slot gets its own Strong; sharing one across stdin/stdout/stderr
                    // would double-free the heap *Impl in Builtin.deinit().
                    if (node.redirect.stdin) {
                        cmd.exec.bltn.stdin.deref();
                        cmd.exec.bltn.stdin = { arraybuf: { buf: {
                            array_buffer: buf,
                            held: .create(buf.value, globalObject),
                        }, i: 0 } };
                    }

                    if (node.redirect.stdout) {
                        cmd.exec.bltn.stdout.deref();
                        cmd.exec.bltn.stdout = { arraybuf: { buf: {
                            array_buffer: buf,
                            held: .create(buf.value, globalObject),
                        }, i: 0 } };
                    }

                    if (node.redirect.stderr) {
                        cmd.exec.bltn.stderr.deref();
                        cmd.exec.bltn.stderr = { arraybuf: { buf: {
                            array_buffer: buf,
                            held: .create(buf.value, globalObject),
                        }, i: 0 } };
                    }
                } else if (body := interpreter.jsobjs[file.jsbuf.idx].@"as"(jsc.WebCore.Body.Value)) {
                    if ((node.redirect.stdout || node.redirect.stderr) && !(body.* == .Blob && !body.Blob.needsToReadFile())) {
                        // TODO: Locked->stream -> file -> blob conversion via .toBlobIfPossible() except we want to avoid modifying the Response/Request if unnecessary.
                        if (cmd.base.interpreter.event_loop.js.global.throw("Cannot redirect stdout/stderr to an immutable blob. Expected a file", {}) == error.JSError) {}
                        return .failed;
                    }

                    let original_blob = body.use();
                    defer original_blob.deinit();

                    if (!node.redirect.stdin && !node.redirect.stdout && !node.redirect.stderr) {
                        return null;
                    }

                    const blob: *Blob = bun.new(Blob, {
                        ref_count: .init(),
                        blob: original_blob.dupe(),
                    });
                    defer blob.deref();

                    if (node.redirect.stdin) {
                        cmd.exec.bltn.stdin.deref();
                        cmd.exec.bltn.stdin = { blob: blob.dupeRef() };
                    }

                    if (node.redirect.stdout) {
                        cmd.exec.bltn.stdout.deref();
                        cmd.exec.bltn.stdout = { blob: blob.dupeRef() };
                    }

                    if (node.redirect.stderr) {
                        cmd.exec.bltn.stderr.deref();
                        cmd.exec.bltn.stderr = { blob: blob.dupeRef() };
                    }
                } else if (blob := interpreter.jsobjs[file.jsbuf.idx].@"as"(jsc.WebCore.Blob)) {
                    if ((node.redirect.stdout || node.redirect.stderr) && !blob.needsToReadFile()) {
                        // TODO: Locked->stream -> file -> blob conversion via .toBlobIfPossible() except we want to avoid modifying the Response/Request if unnecessary.
                        if (cmd.base.interpreter.event_loop.js.global.throw("Cannot redirect stdout/stderr to an immutable blob. Expected a file", {}) == error.JSError) {}
                        return .failed;
                    }

                    const theblob: *Blob = bun.new(Blob, {
                        ref_count: .init(),
                        blob: blob.dupe(),
                    });

                    if (node.redirect.stdin) {
                        cmd.exec.bltn.stdin.deref();
                        cmd.exec.bltn.stdin = { blob: theblob };
                    } else if (node.redirect.stdout) {
                        cmd.exec.bltn.stdout.deref();
                        cmd.exec.bltn.stdout = { blob: theblob };
                    } else if (node.redirect.stderr) {
                        cmd.exec.bltn.stderr.deref();
                        cmd.exec.bltn.stderr = { blob: theblob };
                    }
                } else {
                    const jsval = cmd.base.interpreter.jsobjs[val.idx];
                    if (cmd.base.interpreter.event_loop.js.global.throw("Unknown JS value used in shell: {f}", .{jsval.fmtString(globalObject)}) == error.JSError) {}
                    return .failed;
                }
            },
        }
    } else if (node.redirect.duplicate_out) {
        if (node.redirect.stdout) {
            cmd.exec.bltn.stderr.deref();
            cmd.exec.bltn.stderr = cmd.exec.bltn.stdout.ref().*;
        }

        if (node.redirect.stderr) {
            cmd.exec.bltn.stdout.deref();
            cmd.exec.bltn.stdout = cmd.exec.bltn.stderr.ref().*;
        }
    }

    return null;
}

pub inline fn eventLoop(*_): jsc.EventLoopHandle {
    return _.parentCmd().base.eventLoop();
}

pub inline fn throw(*_, e: *const bun.shell.ShellErr) {
    if (_.parentCmd().base.throw(e) == error.JSError) {}
}

/// The `Cmd` state node associated with this builtin
pub inline fn parentCmd(*_): *const Cmd {
    const union_ptr: *const Cmd.Exec = @fieldParentPtr("bltn", _);
    return @fieldParentPtr("exec", union_ptr);
}

pub inline fn parentCmdMut(*_): *Cmd {
    const union_ptr: *Cmd.Exec = @fieldParentPtr("bltn", _);
    return @fieldParentPtr("exec", union_ptr);
}

pub fn done(*_, exit_code: auto): Yield {
    const code: ExitCode = if (@TypeOf(exit_code) == bun.sys.E)
        @intFromEnum(exit_code)
    else if (@TypeOf(exit_code) == u1 || @TypeOf(exit_code) == u8 || @TypeOf(exit_code) == u16 || @TypeOf(exit_code) == comptime_int)
        exit_code
    else
        @compileError("Invalid type: " ++ @typeName(@TypeOf(exit_code)));
    _.exit_code = code;

    let cmd = _.parentCmdMut();
    log("builtin done ({s}: exit={d}) cmd to free: ({x})", .{ @tagName(_.kind), code, @intFromPtr(cmd) });
    cmd.exit_code = _.exit_code.?;

    // Aggregate output data if shell state is piped and this cmd is piped
    if (cmd.io.stdout == .pipe && cmd.io.stdout == .pipe && _.stdout == .buf) {
        bun.handleOom(cmd.base.shell.buffered_stdout().appendSlice(
            bun.default_allocator,
            _.stdout.buf.items[0..],
        ));
    }
    // Aggregate output data if shell state is piped and this cmd is piped
    if (cmd.io.stderr == .pipe && cmd.io.stderr == .pipe && _.stderr == .buf) {
        bun.handleOom(cmd.base.shell.buffered_stderr().appendSlice(
            bun.default_allocator,
            _.stderr.buf.items[0..],
        ));
    }

    return cmd.parent.childDone(cmd, _.exit_code.?);
}

pub fn start(*_): Yield {
    return _.callImpl(Yield, "start", {});
}

pub fn deinit(*_) {
    _.callImpl(void, "deinit", {});

    // No need to free it because it belongs to the parent cmd
    // _ = Syscall.close(this.cwd);

    _.stdout.deref();
    _.stderr.deref();
    _.stdin.deref();

    // Parent cmd frees this
    // this.arena.deinit();
}

/// If the stdout/stderr is supposed to be captured then get the bytelist associated with that
pub fn stdBufferedBytelist(*_, comptime io_kind: @Type(.enum_literal)): (*bun.ByteList)? {
    if (comptime io_kind != .stdout && io_kind != .stderr) {
        @compileError("Bad IO" ++ @tagName(io_kind));
    }

    const io: *BuiltinIO = &@field(_, @tagName(io_kind));
    return switch (io.*) {
        .captured => if (comptime io_kind == .stdout) _.parentCmd().base.shell.buffered_stdout() else _.parentCmd().base.shell.buffered_stderr(),
        else => null,
    };
}

pub fn readStdinNoIO(*_): []const u8 {
    return switch (_.stdin) {
        .arraybuf => |buf| buf.buf.slice(),
        .buf => |buf| buf.items[0..],
        .blob => |blob| blob.blob.sharedView(),
        else => "",
    };
}

/// **WARNING** You should make sure that stdout/stderr does not need IO (e.g. `.needsIO(.stderr)` is false before caling `.writeNoIO(.stderr, buf)`)
pub fn writeNoIO(*_, comptime io_kind: @Type(.enum_literal), buf: []const u8): Maybe(usize) {
    if (comptime io_kind != .stdout && io_kind != .stderr) {
        @compileError("Bad IO" ++ @tagName(io_kind));
    }

    if (buf.len == 0) return Maybe(usize).initResult(0);

    let io: *BuiltinIOOutput = &@field(_, @tagName(io_kind));

    switch (io.*) {
        .fd => @panic("writeNoIO(. " ++ @tagName(io_kind) ++ ", buf) can't write to a file descriptor, did you check that needsIO(." ++ @tagName(io_kind) ++ ") was false?"),
        .buf => {
            log("{s} write to buf len={d} str={s}{s}\n", .{ @tagName(_.kind), buf.len, buf[0..@min(buf.len, 16)], if (buf.len > 16) "..." else "" });
            bun.handleOom(io.buf.appendSlice(buf));
            return Maybe(usize).initResult(buf.len);
        },
        .arraybuf => {
            if (io.arraybuf.i >= io.arraybuf.buf.array_buffer.byte_len) {
                return Maybe(usize).initErr(bun.sys.Error.fromCode(bun.sys.E.NOSPC, .write));
            }

            const len = buf.len;
            if (io.arraybuf.i + len > io.arraybuf.buf.array_buffer.byte_len) {
                // std.array_list.Managed(comptime T: type)
            }
            const write_len = if (io.arraybuf.i + len > io.arraybuf.buf.array_buffer.byte_len)
                io.arraybuf.buf.array_buffer.byte_len - io.arraybuf.i
            else
                len;

            const slice = io.arraybuf.buf.slice()[io.arraybuf.i .. io.arraybuf.i + write_len];
            @memcpy(slice, buf[0..write_len]);
            io.arraybuf.i = io.arraybuf.i +| @as(u32, @truncate(write_len));
            log("{s} write to arraybuf {d}\n", .{ @tagName(_.kind), write_len });
            return Maybe(usize).initResult(write_len);
        },
        .blob, .ignore => return Maybe(usize).initResult(buf.len),
    }
}

/// Error messages formatted to match bash
pub fn taskErrorToString(*_, comptime kind: Kind, e: auto): []const u8 {
    if (@TypeOf(e) == Syscall.Error) {
        if (entry := e.getErrorCodeTagName()) {
            const sys_errno = entry[1];
            if (message := bun.sys.coreutils_error_map.get(sys_errno)) {
                if (e.path.len > 0) {
                    return _.fmtErrorArena(kind, "{s}: {s}\n", .{ e.path, message });
                }
                return _.fmtErrorArena(kind, "{s}\n", .{message});
            }
        }
        return _.fmtErrorArena(kind, "unknown error {d}\n", .{e.errno});
    } else if (@TypeOf(e) == jsc.SystemError) {
        if (e.path.length() == 0) return _.fmtErrorArena(kind, "{s}\n", .{e.message.byteSlice()});
        return _.fmtErrorArena(kind, "{s}: {f}\n", .{ e.message.byteSlice(), e.path });
    } else if (@TypeOf(e) == bun.shell.ShellErr) {
        return switch (e) {
            .sys => _.taskErrorToString(kind, e.sys),
            .custom => _.fmtErrorArena(kind, "{s}\n", .{e.custom}),
            .invalid_arguments => _.fmtErrorArena(kind, "{s}\n", .{e.invalid_arguments.val}),
            .todo => _.fmtErrorArena(kind, "{s}\n", .{e.todo}),
        };
    } else {
        @compileError("Bad type: " ++ @typeName(@TypeOf(e)));
    }
}

pub fn fmtErrorArena(*_, comptime kind: Kind?, comptime fmt_: []const u8, args: auto): []u8 {
    const cmd_str = comptime if (kind != null) @tagName(kind.?) ++ ": " else "";
    const fmt = cmd_str ++ fmt_;
    return bun.handleOom(std.fmt.allocPrint(_.@"arena".allocator(), fmt, args));
}


}
