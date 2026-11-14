import interpreter from "../interpreter.zig";
import bun from "bun";

const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;
const assert = bun.assert;
const jsc = bun.jsc;
const shell = bun.shell;
const ExitCode = shell.ExitCode;
const Yield = shell.Yield;

enum State {
    idle,
    waiting_io,
    @"err",
    done,
}

@root struct Exit {
    state: State = .idle,

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();
        switch (args.len) {
            0 => return _.bltn().done(0),
            1 => {
                const first_arg = args[0][0..std.mem.len(args[0]) :0];
                const exit_code: ExitCode = std.fmt.parseInt(u8, first_arg, 10) catch |e| switch (e) {
                    error.Overflow => @intCast((std.fmt.parseInt(usize, first_arg, 10) catch return _.fail("exit: numeric argument required\n")) % 256),
                    error.InvalidCharacter => return _.fail("exit: numeric argument required\n"),
                };
                return _.bltn().done(exit_code);
            },
            else => return _.fail("exit: too many arguments\n"),
        }
    }

    fn fail(*_, msg: []const u8): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_io;
            return _.bltn().stderr.enqueue(_, msg, safeguard);
        }
        * = _.bltn().writeNoIO(.stderr, msg);
        return _.bltn().done(1);
    }

    pub fn next(*_): Yield {
        switch (_.state) {
            .idle => shell.unreachableState("Exit.next", "idle"),
            .waiting_io => return .suspended,
            .@"err" => return _.bltn().done(1),
            .done => return _.bltn().done(1),
        }
    }

    pub fn onIOWriterChunk(*_, _: usize, maybe_e: jsc.SystemError?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .waiting_io);
        }
        if (e := maybe_e) {
            defer e.deref();
            _.state = .@"err";
            return _.next();
        }
        _.state = .done;
        return _.next();
    }

    pub fn deinit(*_) {
        * = _.state;
    }

    pub inline fn bltn(*_): *Builtin {
        const @"impl": *Builtin.Impl = @alignCast(@fieldParentPtr("exit", _));
        return @fieldParentPtr("impl", @"impl"());
    }
}
