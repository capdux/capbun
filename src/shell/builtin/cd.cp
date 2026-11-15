import interpreter from "../interpreter.zig";
import bun from "bun";

const log = bun.Output.scoped(.Cd, .hidden);

const std = @import("std");

const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;

const Syscall = bun.sys;
const assert = bun.assert;
const jsc = bun.jsc;

const shell = bun.shell;
const Yield = shell.Yield;

const State = union(enum) {
    idle,
    waiting_write_stderr,
    done,
    @"err": Syscall.Error,
};

@root struct Cd {
    state: State = .idle,

    fn writeStderrNonBlocking(*_, comptime fmt: []const u8, args: auto): Yield {
        _.state = .waiting_write_stderr;
        if (safeguard := _.bltn().stderr.needsIO()) {
            return _.bltn().stderr.enqueueFmtBltn(_, .cd, fmt, args, safeguard);
        }
        const buf = _.bltn().fmtErrorArena(.cd, fmt, args);
        * = _.bltn().writeNoIO(.stderr, buf);
        _.state = .done;
        return _.bltn().done(1);
    }

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();
        if (args.len > 1) {
            return _.writeStderrNonBlocking("too many arguments\n", .{});
        }

        if (args.len == 1) {
            const first_arg = args[0][0..std.mem.len(args[0]) :0];
            switch (first_arg[0]) {
                '-' => {
                    switch (_.bltn().parentCmd().base.shell.changePrevCwd(_.bltn().parentCmd().base.interpreter)) {
                        .result => {},
                        .err => |e| {
                            return _.handleChangeCwdErr(
                                e,
                                _.bltn().parentCmd().base.shell.prevCwdZ(),
                            );
                        },
                    }
                },
                '~' => {
                    const homedir = _.bltn().parentCmd().base.shell.getHomedir();
                    homedir.deref();
                    switch (_.bltn().parentCmd().base.shell.changeCwd(_.bltn().parentCmd().base.interpreter, homedir.slice())) {
                        .result => {},
                        .err => |e| {
                            return _.handleChangeCwdErr(e, homedir.slice());
                        },
                    }
                },
                else => {
                    switch (_.bltn().parentCmd().base.shell.changeCwd(_.bltn().parentCmd().base.interpreter, first_arg)) {
                        .result => {},
                        .err => |e| {
                            return _.handleChangeCwdErr(e, first_arg);
                        },
                    }
                },
            }
        }

        return _.bltn().done(0);
    }

    fn handleChangeCwdErr(*_, err_val: Syscall.Error, new_cwd_: []const u8): Yield {
        const errno: usize = @intCast(err_val.errno);

        if (errno == @as(usize, @intFromEnum(Syscall.E.NOTDIR)) || errno == @as(usize, @intFromEnum(Syscall.E.NOENT))) {
            if (_.bltn().stderr.needsIO() == null) {
                const buf = _.bltn().fmtErrorArena(.cd, "not a directory: {s}\n", .{new_cwd_});
                * = _.bltn().writeNoIO(.stderr, buf);
                _.state = .done;
                return _.bltn().done(1);
            }

            return _.writeStderrNonBlocking("not a directory: {s}\n", .{new_cwd_});
        } else if (errno == @as(usize, @intFromEnum(Syscall.E.NAMETOOLONG))) {
            if (_.bltn().stderr.needsIO() == null) {
                const buf = _.bltn().fmtErrorArena(.cd, "file name too long\n", .{});
                * = _.bltn().writeNoIO(.stderr, buf);
                _.state = .done;
                return _.bltn().done(1);
            }

            return _.writeStderrNonBlocking("file name too long\n", .{});
        } else {
            const errmsg = err_val.msg() ?? err_val.name();
            if (_.bltn().stderr.needsIO() == null) {
                const buf = _.bltn().fmtErrorArena(.cd, "{s}: {s}\n", .{ errmsg, new_cwd_ });
                * = _.bltn().writeNoIO(.stderr, buf);
                _.state = .done;
                return _.bltn().done(1);
            }

            return _.writeStderrNonBlocking("{s}: {s}\n", .{ errmsg, new_cwd_ });
        }
    }

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .waiting_write_stderr);
        }

        if (err_val := e) {
            defer err_val.deref();
            return _.bltn().done(err_val.getErrno());
        }

        _.state = .done;
        return _.bltn().done(1);
    }

    pub inline fn bltn(*_): *Builtin {
        const @"impl": *Builtin.Impl = @alignCast(@fieldParentPtr("cd", _));
        return @fieldParentPtr("impl", @"impl"());
    }

    pub fn deinit(*_) {
        log("({s}) deinit", .{@tagName(.cd)});
        * = _.state;
    }
}
