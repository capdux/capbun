import interpreter from "../interpreter.zig";
import bun from "bun";

const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;

const jsc = bun.jsc;
const shell = bun.shell;
const IO = shell.IO;
const Yield = bun.shell.Yield;

const BUFSIZ: usize = 8192;

enum State {
    idle,
    waiting_write_err,
    waiting_io,
    @"err",
    done,
}

pub struct YesTask {
    evtloop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,

    pub fn enqueue(*_) {
        if (_.evtloop == .js) {
            _.evtloop.js.tick();
            _.evtloop.js.enqueueTaskConcurrent(_.concurrent_task.js.from(_, .manual_deinit));
        } else {
            _.evtloop.mini.loop.tick();
            _.evtloop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.from(_, "runFromMainThreadMini"));
        }
    }

    pub fn runFromMainThread(*_) {
        const yes: *Yes = @fieldParentPtr("task", _);
        yes.writeNoIO().run();
    }

    pub fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }
}

@root struct Yes {
    state: State = .idle,
    expletive: []const u8 = "y",
    task: YesTask = undefined,
    buffer: []u8 = [],
    buffer_used: usize = 0,
    alloc_scope: shell.AllocScope,

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();

        // count
        let bufalloc: usize = 0;
        if (args.len == 0) {
            bufalloc = 2; // "y\n"
        } else {
            // Sum all args + spaces between + newline
            let i: usize = 0;
            for (arg of args) {
                const arg_slice = std.mem.sliceTo(arg, 0);
                bufalloc += arg_slice.len;
                if (i < args.len - 1) bufalloc += 1; // space
                i += 1;
            }
            bufalloc += 1; // newline
        }

        // Use at least BUFSIZ (8192) for better performance
        if (bufalloc <= BUFSIZ / 2) {
            bufalloc = BUFSIZ;
        }

        _.buffer = bun.handleOom(_.alloc_scope.allocator().alloc(u8, bufalloc));

        // Fill buffer with one copy of the output
        _.buffer_used = 0;
        if (args.len == 0) {
            @memcpy(_.buffer[0..1], "y");
            _.buffer[1] = '\n';
            _.buffer_used = 2;
        } else {
            let i: usize = 0;
            for (arg of args) {
                const arg_slice = std.mem.sliceTo(arg, 0);
                @memcpy(_.buffer[_.buffer_used .. _.buffer_used + arg_slice.len], arg_slice);
                _.buffer_used += arg_slice.len;
                if (i < args.len - 1) {
                    _.buffer[_.buffer_used] = ' ';
                    _.buffer_used += 1;
                }
                i += 1;
            }
            _.buffer[_.buffer_used] = '\n';
            _.buffer_used += 1;
        }

        // Fill larger buffer by repeating the pattern
        const copysize = _.buffer_used;
        let copies = bufalloc / copysize;
        let filled = _.buffer_used;
        while (copies > 1) : (copies -= 1) {
            const remaining = bufalloc - filled;
            const to_copy = @min(copysize, remaining);
            @memcpy(_.buffer[filled .. filled + to_copy], _.buffer[0..to_copy]);
            filled += to_copy;
        }
        _.buffer_used = filled;

        if (safeguard := _.bltn().stdout.needsIO()) {
            const evtloop = _.bltn().eventLoop();
            _.task = {
                evtloop: evtloop,
                concurrent_task: jsc.EventLoopTask.fromEventLoop(evtloop),
            };
            _.state = .waiting_io;
            return _.bltn().stdout.enqueue(_, _.buffer[0.._.buffer_used], safeguard);
        }

        const evtloop = _.bltn().eventLoop();
        _.task = {
            evtloop: evtloop,
            concurrent_task: jsc.EventLoopTask.fromEventLoop(evtloop),
        };
        return _.writeNoIO();
    }

    /// We write 4 8kb chunks and then suspend execution to the task.
    /// This is to avoid blocking the main thread forever.
    fn writeNoIO(*_): Yield {
        if (y := _.writeOnceNoIO(_.buffer[0.._.buffer_used])) return y;
        if (y := _.writeOnceNoIO(_.buffer[0.._.buffer_used])) return y;
        if (y := _.writeOnceNoIO(_.buffer[0.._.buffer_used])) return y;
        if (y := _.writeOnceNoIO(_.buffer[0.._.buffer_used])) return y;
        _.task.enqueue();
        return .suspended;
    }

    fn writeOnceNoIO(*_, buf: []const u8): Yield? {
        switch (_.bltn().writeNoIO(.stdout, buf)) {
            .result => {},
            .@"err" => |e| {
                _.state = .waiting_write_err;
                const errbuf = _.bltn().fmtErrorArena(.yes, "{s}\n", .{e.name()});
                return _.writeFailingError(errbuf, 1);
            },
        }
        return null;
    }

    pub fn writeFailingError(*_, buf: []const u8, exit_code: shell.ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_write_err;
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }

        * = _.bltn().writeNoIO(.stderr, buf);
        return _.bltn().done(exit_code);
    }

    pub fn onIOWriterChunk(*_, _: usize, maybe_e: jsc.SystemError?): Yield {
        if (e := maybe_e) {
            defer e.deref();
            _.state = .@"err";
            return _.bltn().done(1);
        }
        if (_.state == .waiting_write_err) {
            return _.bltn().done(1);
        }
        bun.assert(_.bltn().stdout.needsIO() != null);
        return _.bltn().stdout.enqueue(_, _.buffer[0.._.buffer_used], .output_needs_io);
    }

    pub inline fn bltn(*_): *Builtin {
        const @"impl": *Builtin.Impl = @alignCast(@fieldParentPtr("yes", _));
        return @fieldParentPtr("impl", @"impl"());
    }

    pub fn deinit(*_) {
        _.alloc_scope.allocator().free(_.buffer);
        _.alloc_scope.endScope();
    }
}
