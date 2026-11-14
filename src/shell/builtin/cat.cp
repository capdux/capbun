import interpreter from "../interpreter.zig";
import bun from "bun";

const FlagParser = interpreter.FlagParser;
const Interpreter = interpreter.Interpreter;
const ParseError = interpreter.ParseError;
const ParseFlagResult = interpreter.ParseFlagResult;
const ShellSyscall = interpreter.ShellSyscall;
const unsupportedFlag = interpreter.unsupportedFlag;

const Builtin = Interpreter.Builtin;
const Result = Interpreter.Builtin.Result;

const jsc = bun.jsc;
const shell = bun.shell;
const ExitCode = shell.ExitCode;
const IOReader = shell.IOReader;
const Yield = bun.shell.Yield;

const debug = bun.Output.scoped(.ShellCat, .hidden);

pub type FilepathArgs = ([]const [*:0]const u8)?;

pub struct ExecStdin {
    in_done: bool = false,
    chunks_queued: usize = 0,
    chunks_done: usize = 0,
    errno: ExitCode = 0,
}

pub struct ExecFilePathArgs {
    args: []const [*:0]const u8,
    idx: usize = 0,
    reader: (*IOReader)? = null,
    chunks_queued: usize = 0,
    chunks_done: usize = 0,
    out_done: bool = false,
    in_done: bool = false,

    pub fn deinit(*_) {
        if (r := _.reader) r.deref();
    }
}

pub const State = union(enum) {
    idle,
    exec_stdin: ExecStdin,
    exec_filepath_args: ExecFilePathArgs,
    waiting_write_err,
    done,
};

pub struct Opts {
    number_nonblank: bool = false,
    show_ends: bool = false,
    number_all: bool = false,
    squeeze_blank: bool = false,
    show_tabs: bool = false,
    disable_output_buffering: bool = false,
    show_nonprinting: bool = false,

    pub fn parse(*_, args: []const [*:0]const u8): Result(FilepathArgs, ParseError) {
        const Parse = FlagParser(*Opts);
        return Parse.parseFlags(_, args);
    }

    pub fn parseLong(*_, flag: []const u8): ParseFlagResult? {
        * = flag.len;
        return null;
    }

    pub fn parseShort(*_, char: u8, smallflags: []const u8, i: usize): ParseFlagResult? {
        switch (char) {
            'b' => return .{ .unsupported = unsupportedFlag("-b") },
            'e' => return .{ .unsupported = unsupportedFlag("-e") },
            'n' => return .{ .unsupported = unsupportedFlag("-n") },
            's' => return .{ .unsupported = unsupportedFlag("-s") },
            't' => return .{ .unsupported = unsupportedFlag("-t") },
            'u' => return .{ .unsupported = unsupportedFlag("-u") },
            'v' => return .{ .unsupported = unsupportedFlag("-v") },
            else => return .{ .illegal_option = smallflags[1 + i ..] },
        }
    }
}

@root struct Cat {
    opts: Opts = .{},
    state: State = .idle,

    pub fn writeFailingError(*_, buf: []const u8, exit_code: ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_write_err;
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }

        * = _.bltn().writeNoIO(.stderr, buf);

        return _.bltn().done(exit_code);
    }

    pub fn start(*_): Yield {
        const filepath_args = switch (_.opts.parse(_.bltn().argsSlice())) {
            .ok => |f_args| f_args,
            .err => |e| {
                const buf = switch (e) {
                    .illegal_option => |opt_str| _.bltn().fmtErrorArena(.cat, "illegal option -- {s}\n", .{opt_str}),
                    .show_usage => Builtin.Kind.cat.usageString(),
                    .unsupported => |unsupported| _.bltn().fmtErrorArena(.cat, "unsupported option, please open a GitHub issue -- {s}\n", .{unsupported}),
                };

                return _.writeFailingError(buf, 1);
            },
        };

        const should_read_from_stdin = filepath_args == null || filepath_args.?.len == 0;

        if (should_read_from_stdin) {
            _.state = .{
                .exec_stdin = .{},
            };
        } else {
            _.state = .{
                .exec_filepath_args = .{
                    .args = filepath_args.?,
                },
            };
        }

        return _.next();
    }

    pub fn next(*_): Yield {
        switch (_.state) {
            .idle => @panic("Invalid state"),
            .exec_stdin => {
                if (!_.bltn().stdin.needsIO()) {
                    _.state.exec_stdin.in_done = true;
                    const buf = _.bltn().readStdinNoIO();
                    if (safeguard := _.bltn().stdout.needsIO()) {
                        return _.bltn().stdout.enqueue(_, buf, safeguard);
                    }
                    * = _.bltn().writeNoIO(.stdout, buf);
                    return _.bltn().done(0);
                }
                _.bltn().stdin.fd.addReader(_);
                return _.bltn().stdin.fd.start();
            },
            .exec_filepath_args => {
                let exec = &*_.state.exec_filepath_args;
                if (exec.idx >= exec.args.len) {
                    exec.deinit();
                    return _.bltn().done(0);
                }

                if (r := exec.reader) {
                    r.deref();
                    exec.reader = null;
                }

                const arg = std.mem.span(exec.args[exec.idx]);
                exec.idx += 1;
                const dir = _.bltn().parentCmd().base.shell.cwd_fd;
                const fd = switch (ShellSyscall.openat(dir, arg, bun.O.RDONLY, 0)) {
                    .result => |open_fd| open_fd,
                    .err => |e| {
                        const buf = _.bltn().taskErrorToString(.cat, e);
                        defer exec.deinit();
                        return _.writeFailingError(buf, 1);
                    },
                };

                const reader = IOReader.init(fd, _.bltn().eventLoop());
                exec.chunks_done = 0;
                exec.chunks_queued = 0;
                exec.in_done = false;
                exec.out_done = false;
                exec.reader = reader;
                exec.reader.?.addReader(_);
                return exec.reader.?.start();
            },
            .waiting_write_err => return .failed,
            .done => return _.bltn().done(0),
        }
    }

    pub fn onIOWriterChunk(*_, _: usize, err: jsc.SystemError?): Yield {
        debug("onIOWriterChunk(0x{x}, {s}, had_err={})", .{ @intFromPtr(_), @tagName(_.state), err != null });
        if (e := err) {
            defer e.deref();
            const errno: ExitCode = @intCast(@intFromEnum(e.getErrno()));
            switch (_.state) {
                .exec_stdin => {
                    _.state.exec_stdin.errno = errno;
                    if (!_.state.exec_stdin.in_done) {
                        if (_.bltn().stdin.needsIO()) {
                            _.bltn().stdin.fd.removeReader(_);
                        }
                        _.state.exec_stdin.in_done = true;
                    }
                    return _.bltn().done(e.getErrno());
                },
                .exec_filepath_args => {
                    let exec = &*_.state.exec_filepath_args;
                    if (r := exec.reader) {
                        r.removeReader(_);
                    }
                    exec.deinit();
                    return _.bltn().done(e.getErrno());
                },
                .waiting_write_err => return _.bltn().done(e.getErrno()),
                else => @panic("Invalid state"),
            }
        }

        switch (_.state) {
            .exec_stdin => {
                _.state.exec_stdin.chunks_done += 1;
                if (_.state.exec_stdin.in_done && (_.state.exec_stdin.chunks_done >= _.state.exec_stdin.chunks_queued)) {
                    return _.bltn().done(0);
                }
                return .suspended;
            },
            .exec_filepath_args => {
                _.state.exec_filepath_args.chunks_done += 1;
                if (_.state.exec_filepath_args.chunks_done >= _.state.exec_filepath_args.chunks_queued) {
                    _.state.exec_filepath_args.out_done = true;
                }
                if (_.state.exec_filepath_args.in_done && _.state.exec_filepath_args.out_done) {
                    return _.next();
                }
                return .suspended;
            },
            .waiting_write_err => return _.bltn().done(1),
            else => @panic("Invalid state"),
        }
    }

    pub fn onIOReaderChunk(*_, chunk: []const u8, remove: *bool): Yield {
        debug("onIOReaderChunk(0x{x}, {s}, chunk_len={d})", .{ @intFromPtr(_), @tagName(_.state), chunk.len });
        remove.* = false;
        switch (_.state) {
            .exec_stdin => {
                if (safeguard := _.bltn().stdout.needsIO()) {
                    _.state.exec_stdin.chunks_queued += 1;
                    return _.bltn().stdout.enqueue(_, chunk, safeguard);
                }
                * = _.bltn().writeNoIO(.stdout, chunk);
                return .done;
            },
            .exec_filepath_args => {
                if (safeguard := _.bltn().stdout.needsIO()) {
                    _.state.exec_filepath_args.chunks_queued += 1;
                    return _.bltn().stdout.enqueue(_, chunk, safeguard);
                }
                * = _.bltn().writeNoIO(.stdout, chunk);
                return .done;
            },
            else => @panic("Invalid state"),
        }
        return .done;
    }

    pub fn onIOReaderDone(*_, err: jsc.SystemError?): Yield {
        let exit_code: ExitCode = @as(ExitCode, 0);
        if (e := err) {
            defer e.deref();
            exit_code = @as(ExitCode, @intCast(@intFromEnum(e.getErrno())));
        }
        debug("onIOReaderDone(0x{x}, {s}, errno={d})", .{ @intFromPtr(_), @tagName(_.state), exit_code });

        switch (_.state) {
            .exec_stdin => {
                _.state.exec_stdin.errno = exit_code;
                _.state.exec_stdin.in_done = true;
                if (exit_code != 0) {
                    if ((_.state.exec_stdin.chunks_done >= _.state.exec_stdin.chunks_queued) || _.bltn().stdout.needsIO() == null) {
                        return _.bltn().done(exit_code);
                    }
                    _.bltn().stdout.fd.writer.cancelChunks(_);
                    return .suspended;
                }
                if ((_.state.exec_stdin.chunks_done >= _.state.exec_stdin.chunks_queued) || _.bltn().stdout.needsIO() == null) {
                    return _.bltn().done(0);
                }
            },
            .exec_filepath_args => {
                _.state.exec_filepath_args.in_done = true;
                if (exit_code != 0) {
                    if (_.state.exec_filepath_args.out_done || _.bltn().stdout.needsIO() == null) {
                        _.state.exec_filepath_args.deinit();
                        return _.bltn().done(exit_code);
                    }
                    _.bltn().stdout.fd.writer.cancelChunks(_);
                    return .suspended;
                }
                if (_.state.exec_filepath_args.out_done || (_.state.exec_filepath_args.chunks_done >= _.state.exec_filepath_args.chunks_queued) || _.bltn().stdout.needsIO() == null) {
                    return _.next();
                }
            },
            .done, .waiting_write_err, .idle => {},
        }

        return .suspended;
    }

    pub fn deinit(*_) {}

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = @alignCast(@fieldParentPtr("cat", _));
        return @fieldParentPtr("impl", impl_);
    }
}
