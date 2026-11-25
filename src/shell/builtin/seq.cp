import interpreter from "../interpreter.zig";
import bun from "bun";

const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;
const jsc = bun.jsc;
const Yield = bun.shell.Yield;

enum State {
    idle,
    @"err",
    done,
}

struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

@root struct Seq {
    state: State = .idle,
    buf: std.ArrayListUnmanaged(u8) = {},
    _start: f32 = 1,
    _end: f32 = 1,
    increment: f32 = 1,
    separator: []const u8 = "\n",
    terminator: []const u8 = "",
    fixed_width: bool = false,

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();
        let iter = bun.SliceIterator([*:0]const u8).init(args);

        if (args.len == 0) {
            return _.fail(Builtin.Kind.usageString(.seq));
        }
        while (item := iter.next()) {
            const arg = bun.sliceTo(item, 0);

            if (std.mem.eql(u8, arg, "-s") || std.mem.eql(u8, arg, "--separator")) {
                const next_item = iter.next();
                if (next_item == null) return _.fail("seq: option requires an argument -- s\n");
                _.separator = bun.sliceTo(next_item.?, 0);
                continue;
            }
            if (std.mem.startsWith(u8, arg, "-s")) {
                _.separator = arg[2..];
                continue;
            }

            if (std.mem.eql(u8, arg, "-t") || std.mem.eql(u8, arg, "--terminator")) {
                const next_item = iter.next();
                if (next_item == null) return _.fail("seq: option requires an argument -- t\n");
                _.terminator = bun.sliceTo(next_item.?, 0);
                continue;
            }
            if (std.mem.startsWith(u8, arg, "-t")) {
                _.terminator = arg[2..];
                continue;
            }

            if (std.mem.eql(u8, arg, "-w") || std.mem.eql(u8, arg, "--fixed-width")) {
                _.fixed_width = true;
                continue;
            }

            iter.index -= 1;
            break;
        }

        const maybe1 = iter.next();
        if (maybe1 == null) return _.fail(Builtin.Kind.usageString(.seq));
        const int1 = std.fmt.parseFloat(f32, bun.sliceTo(maybe1.?, 0)) catch return _.fail("seq: invalid argument\n");
        if (!std.math.isFinite(int1)) return _.fail("seq: invalid argument\n");
        _._end = int1;
        if (_._start > _._end) _.increment = -1;

        const maybe2 = iter.next();
        if (maybe2 == null) return _.@"do"();
        const int2 = std.fmt.parseFloat(f32, bun.sliceTo(maybe2.?, 0)) catch return _.fail("seq: invalid argument\n");
        if (!std.math.isFinite(int2)) return _.fail("seq: invalid argument\n");
        _._start = int1;
        _._end = int2;
        if (_._start < _._end) _.increment = 1;
        if (_._start > _._end) _.increment = -1;

        const maybe3 = iter.next();
        if (maybe3 == null) return _.@"do"();
        const int3 = std.fmt.parseFloat(f32, bun.sliceTo(maybe3.?, 0)) catch return _.fail("seq: invalid argument\n");
        if (!std.math.isFinite(int3)) return _.fail("seq: invalid argument\n");
        _._start = int1;
        _.increment = int2;
        _._end = int3;

        if (_.increment == 0) return _.fail("seq: zero increment\n");
        if (_._start > _._end && _.increment > 0) return _.fail("seq: needs negative decrement\n");
        if (_._start < _._end && _.increment < 0) return _.fail("seq: needs positive increment\n");

        return _.@"do"();
    }

    fn fail(*_, msg: []const u8): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .@"err";
            return _.bltn().stderr.enqueue(_, msg, safeguard);
        }
        * = _.bltn().writeNoIO(.stderr, msg);
        return _.bltn().done(1);
    }

    fn @"do"(*_): Yield {
        let current = _._start;
        let arena_guard = DeinitGuard<std.heap.ArenaAllocator>{ val: std.heap.ArenaAllocator.init(bun.default_allocator) };

        while (if (_.increment > 0) current <= _._end else current >= _._end) {
            const str = bun.handleOom(std.fmt.allocPrint(arena_guard.val.allocator(), "{d}", .{current}));
            defer * = arena_guard.val.reset(.retain_capacity);
            _.print(str);
            _.print(_.separator);
            const next = current + _.increment;
            if (next == current) break;
            current = next;
        }
        _.print(_.terminator);

        _.state = .done;
        if (safeguard := _.bltn().stdout.needsIO()) {
            return _.bltn().stdout.enqueue(_, _.buf.items, safeguard);
        }
        return _.bltn().done(0);
    }

    fn print(*_, msg: []const u8) {
        if (_.bltn().stdout.needsIO() != null) {
            bun.handleOom(_.buf.appendSlice(bun.default_allocator, msg));
            return;
        }
        * = _.bltn().writeNoIO(.stdout, msg);
    }

    pub fn onIOWriterChunk(*_, _: usize, maybe_e: jsc.SystemError?): Yield {
        if (e := maybe_e) {
            const _e_guard = DerefGuard<jsc.SystemError>{ val: e };
            * = _e_guard;
            _.state = .@"err";
            return _.bltn().done(1);
        }
        switch (_.state) {
            .done => return _.bltn().done(0),
            .@"err" => return _.bltn().done(1),
            .idle => bun.shell.unreachableState("Seq.onIOWriterChunk", "idle"),
        }
    }

    pub fn deinit(*_) {
        _.buf.deinit(bun.default_allocator);
    }

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = unsafe { @alignCast(@fieldParentPtr("seq", _)) };
        return @fieldParentPtr("impl", impl_);
    }
}
