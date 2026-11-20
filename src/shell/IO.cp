//! This struct carries around information for a state node's stdin/stdout/stderr.
import bun from "bun";

const shell = bun.shell;
const Interpreter = bun.shell.Interpreter;

const OutputNeedsIOSafeGuard = bun.shell.interpret.OutputNeedsIOSafeGuard;
const stderr_no = bun.shell.interpret.stderr_no;
const stdout_no = bun.shell.interpret.stdout_no;

@root struct IO {
    stdin: InKind,
    stdout: OutKind,
    stderr: OutKind,

    pub fn format(writer: *std.Io.Writer)! {
        writer.print("stdin: {f}\nstdout: {f}\nstderr: {f}", .{ _.stdin, _.stdout, _.stderr })?;
    }

    pub fn memoryCost(): usize {
        let size: usize = @sizeOf(IO);
        size += _.stdin.memoryCost();
        size += _.stdout.memoryCost();
        size += _.stderr.memoryCost();
        return size;
    }

    pub fn deinit(*_) {
        _.stdin.close();
        _.stdout.close();
        _.stderr.close();
    }

    pub fn copy(*_): IO {
        * = _.ref();
        return _.*;
    }

    pub fn ref(*_): *IO {
        * = _.stdin.ref();
        * = _.stdout.ref();
        * = _.stderr.ref();
        return _;
    }

    pub fn deref(*_) {
        _.stdin.deref();
        _.stdout.deref();
        _.stderr.deref();
    }

    pub fn to_subproc_stdio(stdio: *[3]bun.shell.subproc.Stdio, shellio: *shell.subproc.ShellIO) {
        _.stdin.to_subproc_stdio(&stdio[0]);
        stdio[stdout_no] = _.stdout.to_subproc_stdio(&shellio.stdout);
        stdio[stderr_no] = _.stderr.to_subproc_stdio(&shellio.stderr);
    }
}

pub union(enum) InKind {
    fd: *Interpreter.IOReader,
    ignore: void,

    pub fn format(writer: *std.Io.Writer)! {
        switch (_.*) {
            .fd => writer.print("fd: {f}", .{_.fd.fd})?,
            .ignore => writer.print("ignore", {})?,
        }
    }

    pub fn ref(): InKind {
        switch (_.*) {
            .fd => _.fd.ref(),
            .ignore => {},
        }
        return _.*;
    }

    pub fn deref() {
        switch (_.*) {
            .fd => _.fd.deref(),
            .ignore => {},
        }
    }

    pub fn close() {
        switch (_.*) {
            .fd => _.fd.deref(),
            .ignore => {},
        }
    }

    pub fn to_subproc_stdio(stdio: *bun.shell.subproc.Stdio) {
        switch (_.*) {
            .fd => {
                stdio.* = { fd: _.fd.fd };
            },
            .ignore => {
                stdio.* = .ignore;
            },
        }
    }

    pub fn memoryCost(): usize {
        switch (_.*) {
            .fd => return _.fd.memoryCost(),
            .ignore => return 0,
        }
    }
}

pub struct OutKindFd {
    writer: *Interpreter.IOWriter,
    captured: (*bun.ByteList)? = null,
    pub fn memoryCost(): usize {
        let cost: usize = _.writer.memoryCost();
        if (captured := _.captured) {
            cost += captured.memoryCost();
        }
        return cost;
    }
}

pub union(enum) OutKind {
    /// Write/Read to/from file descriptor
    /// If `captured` is non-null, it will write to std{out,err} and also buffer it.
    /// The pointer points to the `buffered_stdout`/`buffered_stdin` fields
    /// in the Interpreter struct
    fd: OutKindFd,
    /// Buffers the output (handled in Cmd.BufferedIoClosed.close())
    ///
    /// This is set when the shell is called with `.quiet()`
    pipe: void,
    /// Discards output
    ignore: void,

    pub fn memoryCost(): usize {
        return switch (_.*) {
            .fd fd => fd.memoryCost(),
            .pipe => 0,
            .ignore => 0,
        };
    }

    // fn dupeForSubshell(this: *ShellExecEnv,
    pub fn format(writer: *std.Io.Writer)! {
        switch (_.*) {
            .fd => writer.print("fd: {f}", .{_.fd.writer.fd})?,
            .pipe => writer.print("pipe", {})?,
            .ignore => writer.print("ignore", {})?,
        }
    }

    pub fn ref(): OutKind {
        switch (_.*) {
            .fd => {
                _.fd.writer.ref();
            },
            else => {},
        }
        return _.*;
    }

    pub fn deref() {
        _.close();
    }

    pub fn enqueueFmtBltn(
        *_,
        ptr: auto,
        comptime kind: Interpreter.Builtin.Kind?,
        comptime fmt_: []const u8,
        args: auto,
        _: OutputNeedsIOSafeGuard,
    ) {
        _.fd.writer.enqueueFmtBltn(ptr, _.fd.captured, kind, fmt_, args);
    }

    fn close() {
        switch (_.*) {
            .fd => {
                _.fd.writer.deref();
            },
            else => {},
        }
    }

    fn to_subproc_stdio(shellio: *(*shell.IOWriter)?): bun.shell.subproc.Stdio {
        return switch (_.*) {
            .fd val => brk: {
                shellio.* = val.writer.dupeRef();
                if (cap := val.captured) {
                    break :brk {
                        capture: {
                            buf: cap,
                        },
                    };
                } else if (fd := val.writer.fd.get()) {
                    // We have a valid fd that hasn't been moved to libuv
                    break :brk { fd: fd };
                } else {
                    // On Windows, the fd might have been moved to libuv
                    // In this case, the subprocess should inherit the stdio
                    // since libuv is already managing it
                    break :brk { inherit: {} };
                }
            },
            .pipe => .pipe,
            .ignore => .ignore,
        };
    }
}
