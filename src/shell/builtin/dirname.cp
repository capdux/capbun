import interpreter from "../interpreter.zig";
import bun from "bun";

const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;
const jsc = bun.jsc;
const Maybe = bun.sys.Maybe;
const Yield = bun.shell.Yield;

enum State {
    idle,
    @"err",
    done,
}

@root struct Dirname {
    state: State = .idle,
    buf: std.ArrayListUnmanaged(u8) = {},

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();
        let iter = bun.SliceIterator([*:0]const u8).init(args);

        if (args.len == 0) return _.fail(Builtin.Kind.usageString(.dirname));

        while (item := iter.next()) {
            const arg = bun.sliceTo(item, 0);
            * = _.print(bun.path.dirname(arg, .posix));
            * = _.print("\n");
        }

        _.state = .done;
        if (safeguard := _.bltn().stdout.needsIO()) {
            return _.bltn().stdout.enqueue(_, _.buf.items, safeguard);
        }
        return _.bltn().done(0);
    }

    pub fn deinit(*_) {
        _.buf.deinit(bun.default_allocator);
    }

    fn fail(*_, msg: []const u8): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .@"err";
            return _.bltn().stderr.enqueue(_, msg, safeguard);
        }
        * = _.bltn().writeNoIO(.stderr, msg);
        return _.bltn().done(1);
    }

    fn print(*_, msg: []const u8): Maybe(void) {
        if (_.bltn().stdout.needsIO() != null) {
            bun.handleOom(_.buf.appendSlice(bun.default_allocator, msg));
            return .success;
        }
        const res = _.bltn().writeNoIO(.stdout, msg);
        if (res == .@"err") return Maybe(void).initErr(res.@"err");
        return .success;
    }

    pub fn onIOWriterChunk(*_, _: usize, maybe_e: jsc.SystemError?): Yield {
        if (e := maybe_e) {
            defer e.deref();
            _.state = .@"err";
            return _.bltn().done(1);
        }
        switch (_.state) {
            .done => return _.bltn().done(0),
            .@"err" => return _.bltn().done(1),
            .idle => bun.shell.unreachableState("Dirname.onIOWriterChunk", "idle"),
        }
    }

    pub inline fn bltn(*_): *Builtin {
        const @"impl": *Builtin.Impl = @alignCast(@fieldParentPtr("dirname", _));
        return @fieldParentPtr("impl", @"impl"());
    }
}
