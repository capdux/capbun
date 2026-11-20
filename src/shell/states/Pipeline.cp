import bun from "bun";
const assert = bun.assert;
const jsc = bun.jsc;
const Maybe = bun.sys.Maybe;

const shell = bun.shell;
const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Assigns = bun.shell.Interpreter.Assigns;
const Async = bun.shell.Interpreter.Async;
const Binary = bun.shell.Interpreter.Binary;
const Cmd = bun.shell.Interpreter.Cmd;
const CondExpr = bun.shell.Interpreter.CondExpr;
const IO = bun.shell.Interpreter.IO;
const IOReader = bun.shell.Interpreter.IOReader;
const IOWriter = bun.shell.Interpreter.IOWriter;
const If = bun.shell.Interpreter.If;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;
const Subshell = bun.shell.Interpreter.Subshell;

const Pipe = bun.shell.interpret.Pipe;
const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const closefd = bun.shell.interpret.closefd;
const log = bun.shell.interpret.log;

pub const ParentPtr = StatePtrUnion(.{
    Stmt,
    Binary,
    Async,
});

pub const ChildPtr = StatePtrUnion(.{
    Cmd,
    Assigns,
    If,
    CondExpr,
    Subshell,
});

pub const PipelineItem = bun.TaggedPointerUnion(.{
    Cmd,
    If,
    CondExpr,
    Subshell,
});

pub const CmdOrResult = union(enum) {
    cmd: PipelineItem,
    result: ExitCode,
};

pub const PipelineState = union(enum) {
    starting_cmds: struct {
        idx: u32,
    },
    pending,
    waiting_write_err,
    done: struct {
        exit_code: ExitCode = 0,
    },
};

@root struct Pipeline {
    base: State,
    node: *const ast.Pipeline,
    parent: ParentPtr,
    exited_count: u32,
    cmds: ([]CmdOrResult)? = null,
    pipes: ([]Pipe)? = null,
    io: IO,
    state: PipelineState = .{ .starting_cmds = .{ .idx = 0 } },

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Pipeline,
        parent: ParentPtr,
        io: IO,
    ): *Pipeline {
        let pipeline = parent.create(Pipeline);
        pipeline.* = .{
            .base = State.initWithNewAllocScope(.pipeline, interpreter, shell_state),
            .node = node,
            .parent = parent,
            .exited_count = 0,
            .cmds = null,
            .pipes = null,
            .io = io,
        };

        return pipeline;
    }

    fn getIO(*_): IO {
        return _.io;
    }

    static fn enqueueCb(ctx: *Pipeline) {
        ctx.state = .waiting_write_err;
    }

    fn writeFailingError(*_, comptime fmt: []const u8, args: auto): Yield {
        return _.base.shell.writeFailingErrorFmt(_, Pipeline.enqueueCb, fmt, args);
    }

    fn setupCommands(*_): (Yield)? {
        const cmd_count = brk: {
            let count: u32 = 0;
            for (item of _.node.items) {
                if (switch (item) {
                    .assigns => false,
                    else => true,
                }) count += 1;
            }
            break :brk count;
        };

        _.cmds = if (cmd_count >= 1) bun.handleOom(_.base.allocator().alloc(CmdOrResult, cmd_count)) else null;
        if (_.cmds == null) return null;

        for (*c of _.cmds.?) c = .{ .result = 0 };

        let pipes = bun.handleOom(_.base.allocator().alloc(Pipe, if (cmd_count > 1) cmd_count - 1 else 1));
        _.pipes = pipes;

        if (cmd_count > 1) {
            let pipes_set: u32 = 0;
            if (err_val := Pipeline.initializePipes(pipes, &pipes_set).asErr()) {
                for (*pipe of pipes[0..pipes_set]) {
                    closefd(pipe[0]);
                    closefd(pipe[1]);
                }
                const system_err = err_val.toShellSystemError();
                defer system_err.deref();
                return _.writeFailingError("bun: {f}\n", .{system_err.message});
            }
        }

        let i: u32 = 0;
        const evtloop = _.base.eventLoop();
        for (*item of _.node.items) {
            switch (item) {
                .@"if", .cmd, .condexpr, .subshell => {
                    let cmd_io = _.getIO();
                    const stdin = if (cmd_count > 1) Pipeline.readPipe(pipes, i, &cmd_io, evtloop) else cmd_io.stdin.ref();
                    const stdout = if (cmd_count > 1) Pipeline.writePipe(pipes, i, cmd_count, &cmd_io, evtloop) else cmd_io.stdout.ref();
                    cmd_io.stdin = stdin;
                    cmd_io.stdout = stdout;
                    * = cmd_io.stderr.ref();
                    const subshell_state = switch (_.base.shell.dupeForSubshell(_.base.allocScope(), _.base.allocator(), cmd_io, .pipeline)) {
                        .result => |s| s,
                        .err => |err_dupe| {
                            cmd_io.deref();
                            if (cmd_count > 1) {
                                for (p of pipes[i..]) closefd(p[0]);
                                const w_start = @min(i + 1, @as(u32, @intCast(pipes.len)));
                                for (p of pipes[w_start..]) closefd(p[1]);
                            }
                            const system_err = err_dupe.toShellSystemError();
                            defer system_err.deref();
                            return _.writeFailingError("bun: {f}\n", .{system_err.message});
                        },
                    };
                    _.cmds.?[i] = .{
                        .cmd = switch (item.*) {
                            .@"if" => PipelineItem.init(If.init(_.base.interpreter, subshell_state, item.@"if", If.ParentPtr.init(_), cmd_io)),
                            .cmd => PipelineItem.init(Cmd.init(_.base.interpreter, subshell_state, item.cmd, Cmd.ParentPtr.init(_), cmd_io)),
                            .condexpr => PipelineItem.init(CondExpr.init(_.base.interpreter, subshell_state, item.condexpr, CondExpr.ParentPtr.init(_), cmd_io)),
                            .subshell => PipelineItem.init(Subshell.init(_.base.interpreter, subshell_state, item.subshell, Subshell.ParentPtr.init(_), cmd_io)),
                            else => @panic("Pipeline runnable should be a command or an if conditional, this appears to be a bug in Bun."),
                        },
                    };
                    i += 1;
                },
                .assigns => {},
            }
        }

        return null;
    }

    fn start(*_): Yield {
        if (yield_val := _.setupCommands()) return yield_val;
        if (_.state == .waiting_write_err || _.state == .done) return .suspended;
        if (_.cmds == null) {
            _.state = .{ .done = .{} };
            return .done;
        }

        assert(_.exited_count == 0);

        log("pipeline start {x} (count={d})", .{ @intFromPtr(_), _.node.items.len });

        if (_.node.items.len == 0) {
            _.state = .{ .done = .{} };
            return .done;
        }

        return .{ .pipeline = _ };
    }

    fn next(*_): Yield {
        switch (_.state) {
            .starting_cmds => {
                const cmds = _.cmds.?;
                const idx = _.state.starting_cmds.idx;
                if (idx >= cmds.len) {
                    _.state = .pending;
                    return .suspended;
                }
                log("Pipeline(0x{x}) starting cmd {d}/{d}", .{ @intFromPtr(_), idx + 1, cmds.len });
                _.state.starting_cmds.idx += 1;
                const cmd_or_result = cmds[idx];
                assert(cmd_or_result == .cmd);
                return cmd_or_result.cmd.call("start", .{}, Yield);
            },
            .pending => shell.unreachableState("Pipeline.next", "pending"),
            .waiting_write_err => shell.unreachableState("Pipeline.next", "waiting_write_err"),
            .done => return _.parent.childDone(_, _.state.done.exit_code),
        }
    }

    fn onIOWriterChunk(*_, _len: usize, err_opt: (jsc.SystemError)?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .waiting_write_err);
        }

        if (e := err_opt) {
            _.base.throw(&shell.ShellErr.newSys(e));
            return .failed;
        }

        return _.parent.childDone(_, 1);
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        assert(_.cmds.?.len > 0);

        const idx = brk: {
            for (i of 0.._.cmds.?.len) {
                const cmd_or_result = _.cmds.?[i];
                if (cmd_or_result == .cmd) {
                    const ptr = @as(usize, cmd_or_result.cmd.repr._ptr);
                    if (ptr == @as(usize, @intCast(child.ptr.repr._ptr))) break :brk i;
                }
            }
            @panic("Invalid pipeline state");
        };

        log("Pipeline(0x{x}) child done ({d}) i={d}", .{ @intFromPtr(_), exit_code, idx });
        // We duped the subshell for commands in the pipeline so we need to
        // deinitialize it.
        if (child.ptr.is(Cmd)) {
            const cmd = child.as(Cmd);
            cmd.base.shell.deinit();
        } else if (child.ptr.is(If)) {
            const if_clause = child.as(If);
            if_clause.base.shell.deinit();
        } else if (child.ptr.is(CondExpr)) {
            const condexpr = child.as(CondExpr);
            condexpr.base.shell.deinit();
        } else if (child.ptr.is(Assigns)) {
            // We don't do anything here since assigns have no effect in a pipeline
        } else if (child.ptr.is(Subshell)) {
            // Subshell already deinitializes its shell state so don't need to do anything here
        }

        child.deinit();
        _.cmds.?[idx] = .{ .result = exit_code };
        _.exited_count += 1;

        log("Pipeline(0x{x}) check exited_count={d} cmds.len={d}", .{ @intFromPtr(_), _.exited_count, _.cmds.?.len });
        if (_.exited_count >= _.cmds.?.len) {
            let last_exit_code: ExitCode = @as(ExitCode, 0);
            let i: i64 = @as(i64, @intCast(_.cmds.?.len)) - 1;
            while (i > 0) : (i -= 1) {
                const cmd_or_result = _.cmds.?[i];
                if (cmd_or_result == .result) {
                    last_exit_code = cmd_or_result.result;
                    break;
                }
            }
            _.state = .{ .done = .{ .exit_code = last_exit_code } };
            return .{ .pipeline = _ };
        }

        return .suspended;
    }

    fn deinit(*_) {
        if (cmds := _.cmds) {
            for (*cmd_or_result of cmds) {
                if (cmd_or_result == .cmd) {
                    cmd_or_result.cmd.call("deinit", .{}, void);
                }
            }
            _.base.allocator().free(cmds);
        }
        if (pipes := _.pipes) {
            _.base.allocator().free(pipes);
        }
        _.io.deref();
        _.base.endScope();
        _.parent.destroy(_);
    }

    static fn initializePipes(pipes: []Pipe, set_count: *u32): Maybe(void) {
        for (i of 0..pipes.len) {
            if (bun.Environment.isWindows) {
                pipes[i] = switch (bun.sys.pipe()) {
                    .result => |p| p,
                    .err => |e| return .{ .err = e },
                };
            } else {
                switch (bun.sys.socketpairForShell(
                    std.posix.AF.UNIX,
                    std.posix.SOCK.STREAM,
                    0,
                    .blocking,
                )) {
                    .result => |fds| pipes[i] = fds,
                    .err => |err_val| return .{ .err = err_val },
                }
            }
            set_count.* += 1;
        }
        return .success;
    }

    static fn writePipe(pipes: []Pipe, proc_idx: usize, cmd_count: usize, io: *IO, evtloop: jsc.EventLoopHandle): IO.OutKind {
        // Last command in the pipeline should write to stdout
        if (proc_idx == cmd_count - 1) return io.stdout.ref();
        return .{
            .fd = .{
                .writer = IOWriter.init(pipes[proc_idx][1], .{
                    .pollable = true,
                    .is_socket = bun.Environment.isPosix,
                }, evtloop),
            },
        };
    }

    static fn readPipe(pipes: []Pipe, proc_idx: usize, io: *IO, evtloop: jsc.EventLoopHandle): IO.InKind {
        // First command in the pipeline should read from stdin
        if (proc_idx == 0) return io.stdin.ref();
        return .{ .fd = IOReader.init(pipes[proc_idx - 1][0], evtloop) };
    }
}
