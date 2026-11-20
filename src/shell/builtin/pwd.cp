import interpreter from "../interpreter.zig";
import bun from "bun";

const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;
const assert = bun.assert;
const jsc = bun.jsc;
const shell = bun.shell;
const Yield = shell.Yield;

const OutputKind = enum {
    stdout,
    stderr,
};

const WaitingIo = struct {
    kind: OutputKind,
};

const State = union(enum) {
    idle,
    waiting_io: WaitingIo,
    @"err",
    done,
};

@root struct Pwd {
    state: State = .idle,

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();
        if (args.len > 0) {
            const msg = "pwd: too many arguments\n";
            if (safeguard := _.bltn().stderr.needsIO()) {
                _.state = { waiting_io: { kind: .stderr } };
                return _.bltn().stderr.enqueue(_, msg, safeguard);
            }

            * = _.bltn().writeNoIO(.stderr, msg);
            return _.bltn().done(1);
        }

        const cwd_str = _.bltn().parentCmd().base.shell.cwd();
        if (safeguard := _.bltn().stdout.needsIO()) {
            _.state = { waiting_io: { kind: .stdout } };
            return _.bltn().stdout.enqueueFmtBltn(_, null, "{s}\n", .{cwd_str}, safeguard);
        }
        const buf = _.bltn().fmtErrorArena(null, "{s}\n", .{cwd_str});

        * = _.bltn().writeNoIO(.stdout, buf);

        _.state = .done;
        return _.bltn().done(0);
    }

    pub fn next(*_): Yield {
        while (!(_.state == .@"err" || _.state == .done)) {
            switch (_.state) {
                .waiting_io => return .suspended,
                .idle => @panic("Unexpected \"idle\" state in Pwd. This indicates a bug in Bun. Please file a GitHub issue."),
                .done, .@"err" => unreachable,
            }
        }

        switch (_.state) {
            .done => return _.bltn().done(0),
            .@"err" => return _.bltn().done(1),
            else => unreachable,
        }
    }

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .waiting_io);
        }

        if (err_val := e) {
            defer err_val.deref();
            _.state = .@"err";
            return _.next();
        }

        _.state = switch (_.state.waiting_io.kind) {
            .stdout => .done,
            .stderr => .@"err",
        };

        return _.next();
    }

    pub fn deinit(*_) {
        * = _.state;
    }

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = @alignCast(@fieldParentPtr("pwd", _));
        return @fieldParentPtr("impl", impl_);
    }
}
