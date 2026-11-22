//! https://www.gnu.org/software/bash/manual/bash.html#Bash-Conditional-Expressions
//!
import bun from "bun";
const assert = bun.assert;
const jsc = bun.jsc;
const Maybe = bun.sys.Maybe;

const shell = bun.shell;
const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Async = bun.shell.Interpreter.Async;
const Binary = bun.shell.Interpreter.Binary;
const Expansion = bun.shell.Interpreter.Expansion;
const IO = bun.shell.Interpreter.IO;
const Pipeline = bun.shell.Interpreter.Pipeline;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;

const ShellSyscall = bun.shell.interpret.ShellSyscall;
const ShellTask = bun.shell.interpret.ShellTask;
const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub type StringZ = [:0]const u8;

pub const ParentPtr = StatePtrUnion(.{
    Stmt,
    Binary,
    Pipeline,
    Async,
});

pub const ChildPtr = StatePtrUnion(.{
    Expansion,
});

pub const CondExprState = union(enum) {
    idle,
    expanding_args: struct {
        idx: u32 = 0,
        expansion: Expansion,
        last_exit_code: ExitCode = 0,
    },
    waiting_stat,
    stat_complete: struct {
        stat: Maybe(bun.Stat),
    },
    waiting_write_err,
    done,
};

pub struct DeinitGuard<T> {
    ptr: *T!,

    ~init() {
        unsafe {
            _.ptr.deinit();
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    val: T,
    alloc: std.mem.Allocator,

    ~init() {
        _.val.deinit(_.alloc);
    }
}

pub struct DerefGuard<T> {
    val: T,

    ~init() {
        _.val.deref();
    }
}

pub struct ShellCondExprStatTask {
    task: ShellTask(ShellCondExprStatTask, runFromThreadPool, runFromMainThread, log),
    condexpr: *CondExpr,
    result: (Maybe(bun.Stat))? = null,
    path: [:0]const u8,
    cwdfd: bun.FD,

    fn runFromThreadPool(*_) {
        _.result = ShellSyscall.statat(_.cwdfd, _.path);
    }

    fn runFromMainThread(*_) {
        const _guard = DeinitGuard<ShellCondExprStatTask>{ ptr: unsafe { _ } };
        const ret = _.result.?;
        _.result = null;
        _.condexpr.onStatTaskComplete(ret);
    }

    fn runFromMainThreadMini(*_, _unused: *void) {
        _.runFromMainThread();
    }

    fn deinit(*_) {
        bun.destroy(_);
    }
}

@root struct CondExpr {
    base: State,
    node: *const ast.CondExpr,
    parent: ParentPtr,
    io: IO,
    state: CondExprState = .idle,
    args: std.array_list.Managed(StringZ),

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.CondExpr,
        parent: ParentPtr,
        io: IO,
    ): *CondExpr {
        let condexpr = parent.create(CondExpr);
        condexpr.* = {
            base: State.initWithNewAllocScope(.condexpr, interpreter, shell_state),
            node: node,
            parent: parent,
            io: io,
            args: undefined,
        };
        condexpr.args = std.array_list.Managed([:0]const u8).init(condexpr.base.allocator());
        return condexpr;
    }

    static fn format(condexpr: *const CondExpr, writer: *std.Io.Writer)! {
        unsafe {
            try writer.print("CondExpr(0x{x}, op={s})", .{ @intFromPtr(condexpr), @tagName(condexpr.node.op) });
        }
    }

    fn start(*_): Yield {
        log("{f} start", .{_});
        return { cond_expr: _ };
    }

    fn next(*_): Yield {
        while (_.state != .done) {
            switch (_.state) {
                .idle => {
                    _.state = { expanding_args: { idx: 0, expansion: undefined, last_exit_code: 0 } };
                    continue;
                },
                .expanding_args => {
                    if (_.state.expanding_args.idx >= _.node.args.len()) {
                        return _.commandImplStart();
                    }

                    bun.handleOom(_.args.ensureUnusedCapacity(1));
                    Expansion.init(
                        _.base.interpreter,
                        _.base.shell,
                        &_.state.expanding_args.expansion,
                        _.node.args.getConst(_.state.expanding_args.idx),
                        Expansion.ParentPtr.init(_),
                        {
                            array_of_slice: &_.args,
                        },
                        _.io.copy(),
                    );
                    _.state.expanding_args.idx += 1;
                    return _.state.expanding_args.expansion.start();
                },
                .waiting_stat => {
                    return .suspended;
                },
                .stat_complete => {
                    switch (_.state.stat_complete.stat) {
                        .result => |st| {
                            switch (_.node.op) {
                                .@"-f" => {
                                    return _.parent.childDone(_, if (bun.S.ISREG(@intCast(st.mode))) 0 else 1);
                                },
                                .@"-d" => {
                                    return _.parent.childDone(_, if (bun.S.ISDIR(@intCast(st.mode))) 0 else 1);
                                },
                                .@"-c" => {
                                    return _.parent.childDone(_, if (bun.S.ISCHR(@intCast(st.mode))) 0 else 1);
                                },
                                .@"-z", .@"-n", .@"==", .@"!=" => @panic("This conditional expression op does not need `stat()`. This indicates a bug in Bun. Please file a GitHub issue."),
                                else => {
                                    if (bun.Environment.allow_assert) {
                                        inline for (supported of ast.CondExpr.Op.SUPPORTED) {
                                            if (supported == _.node.op) {
                                                @panic("DEV: You did not support the \"" ++ @tagName(supported) ++ "\" conditional expression operation here.");
                                            }
                                        }
                                    }
                                    @panic("Invalid conditional expression op, this indicates a bug in Bun. Please file a GithHub issue.");
                                },
                            }
                        },
                        .err => {
                            return _.parent.childDone(_, 1);
                        },
                    }
                },
                .waiting_write_err => {
                    return .suspended;
                },
                .done => assert(false),
            }
        }

        return _.parent.childDone(_, 0);
    }

    fn commandImplStart(*_): Yield {
        switch (_.node.op) {
            .@"-c",
            .@"-d",
            .@"-f" => {
                // Empty string expansion produces no args, or the path is an empty string;
                // the path doesn't exist. On Windows, stat("") can succeed and return the
                // cwd's stat, so we must check for empty paths explicitly.
                if (_.args.items.len == 0 || _.args.items[0].len == 0) return _.parent.childDone(_, 1);
                _.state = .waiting_stat;
                return _.doStat();
            },
            .@"-z" => return _.parent.childDone(_, if (_.args.items.len == 0 || _.args.items[0].len == 0) 0 else 1),
            .@"-n" => return _.parent.childDone(_, if (_.args.items.len > 0 && _.args.items[0].len != 0) 0 else 1),
            .@"==" => {
                const is_eq = _.args.items.len == 0 || (_.args.items.len >= 2 && bun.strings.eql(_.args.items[0], _.args.items[1]));
                return _.parent.childDone(_, if (is_eq) 0 else 1);
            },
            .@"!=" => {
                const is_neq = _.args.items.len >= 2 && !bun.strings.eql(_.args.items[0], _.args.items[1]);
                return _.parent.childDone(_, if (is_neq) 0 else 1);
            },
            else => {
                if (bun.Environment.allow_assert) {
                    inline for (supported of ast.CondExpr.Op.SUPPORTED) {
                        if (supported == _.node.op) {
                            @panic("DEV: You did not support the \"" ++ @tagName(supported) ++ "\" conditional expression operation here.");
                        }
                    }
                }

                @panic("Invalid cond expression op, this indicates a bug in Bun. Please file a GithHub issue.");
            },
        }
    }

    fn doStat(*_): Yield {
        const stat_task = bun.new(ShellCondExprStatTask, {
            task: {
                event_loop: _.base.eventLoop(),
                concurrent_task: jsc.EventLoopTask.fromEventLoop(_.base.eventLoop()),
            },
            condexpr: _,
            path: _.args.items[0],
            cwdfd: _.base.shell.cwd_fd,
        });
        stat_task.task.schedule();
        return .suspended;
    }

    fn deinit(*_) {
        _.io.deinit();
        for (item of _.args.items) {
            _.base.allocator().free(item);
        }
        _.args.deinit();
        _.base.endScope();
        _.parent.destroy(_);
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        if (child.ptr.is(Expansion)) {
            if (exit_code != 0) {
                const e = _.state.expanding_args.expansion.state.err;
                const _guard = DeinitWithAllocGuard<@TypeOf(e)>{ val: e, alloc: bun.default_allocator };
                _.state.expanding_args.expansion.deinit();
                return _.writeFailingError("{f}\n", .{e});
            }
            child.deinit();
            return _.next();
        }

        @panic("Invalid child to cond expression, this indicates a bug in Bun. Please file a report on Github.");
    }

    fn onStatTaskComplete(*_, result: Maybe(bun.Stat)) {
        if (bun.Environment.allow_assert) assert(_.state == .waiting_stat);

        _.state = {
            stat_complete: { stat: result },
        };
        let y = _.next();
        y.run();
    }

    static fn enqueueCb(ctx: *CondExpr) {
        ctx.state = .waiting_write_err;
    }

    fn writeFailingError(*_, comptime fmt: []const u8, args: auto): Yield {
        return _.base.shell.writeFailingErrorFmt(_, CondExpr.enqueueCb, fmt, args);
    }

    fn onIOWriterChunk(*_, _len: usize, err_opt: (jsc.SystemError)?): Yield {
        if (e := err_opt) {
            const _guard = DerefGuard<jsc.SystemError>{ val: e };
            const exit_code: ExitCode = @intFromEnum(e.getErrno());
            return _.parent.childDone(_, exit_code);
        }

        if (_.state == .waiting_write_err) {
            return _.parent.childDone(_, 1);
        }

        bun.shell.unreachableState("CondExpr.onIOWriterChunk", @tagName(_.state));
    }
}
