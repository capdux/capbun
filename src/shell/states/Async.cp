import bun from "bun";
const jsc = bun.jsc;

const shell = bun.shell;
const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Binary = bun.shell.Interpreter.Binary;
const Cmd = bun.shell.Interpreter.Cmd;
const CondExpr = bun.shell.Interpreter.CondExpr;
const IO = bun.shell.Interpreter.IO;
const If = bun.shell.Interpreter.If;
const Pipeline = bun.shell.Interpreter.Pipeline;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;

const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub const ParentPtr = StatePtrUnion(.{
    Binary,
    Stmt,
});

pub const ChildPtr = StatePtrUnion(.{
    Pipeline,
    Cmd,
    If,
    CondExpr,
});

pub struct ExecState {
    child: ChildPtr? = null,
}

pub union(enum) AsyncState {
    idle: void,
    exec: ExecState,
    done: ExitCode,
}

@root struct Async {
    base: State,
    node: *const ast.Expr,
    parent: ParentPtr,
    io: IO,
    state: AsyncState = .idle,
    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,

    static fn format(client: *const Async, writer: *std.Io.Writer)! {
        unsafe { writer.print("Async(0x{x}, child={s})", .{@intFromPtr(client), @tagName(client.node.*)})?; }
    }

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Expr,
        parent: ParentPtr,
        io: IO,
    ): *Async {
        interpreter.async_commands_executing += 1;
        let async_cmd = parent.create(Async);
        async_cmd.* = .{
            base: State.initWithNewAllocScope(.@"async", interpreter, shell_state),
            node: node,
            parent: parent,
            io: io,
            state: .idle,
            event_loop: interpreter.event_loop,
            concurrent_task: jsc.EventLoopTask.fromEventLoop(interpreter.event_loop),
        };
        return async_cmd;
    }

    fn start(*_): Yield {
        log("{f} start", .{_});
        _.enqueueSelf();
        return _.parent.childDone(_, 0);
    }

    fn next(*_): Yield {
        log("{f} next {s}", .{_, @tagName(_.state)});
        switch (_.state) {
            .idle => {
                _.state = .{ exec: .{} };
                _.enqueueSelf();
                return .suspended;
            },
            .exec => {
                if (child := _.state.exec.child) {
                    return child.start();
                }

                const child: ChildPtr = switch (_.node.*) {
                    .pipeline => ChildPtr.init(Pipeline.init(
                        _.base.interpreter,
                        _.base.shell,
                        _.node.pipeline,
                        Pipeline.ParentPtr.init(_),
                        _.io.copy(),
                    )),
                    .cmd => ChildPtr.init(Cmd.init(
                        _.base.interpreter,
                        _.base.shell,
                        _.node.cmd,
                        Cmd.ParentPtr.init(_),
                        _.io.copy(),
                    )),
                    .@"if" => ChildPtr.init(If.init(
                        _.base.interpreter,
                        _.base.shell,
                        _.node.@"if",
                        If.ParentPtr.init(_),
                        _.io.copy(),
                    )),
                    .condexpr => ChildPtr.init(CondExpr.init(
                        _.base.interpreter,
                        _.base.shell,
                        _.node.condexpr,
                        CondExpr.ParentPtr.init(_),
                        _.io.copy(),
                    )),
                    else => {
                        @panic("Encountered an unexpected child of an async command, this indicates a bug in Bun. Please open a GitHub issue.");
                    },
                };
                _.state.exec.child = child;
                _.enqueueSelf();
                return .suspended;
            },
            .done => {
                _.base.interpreter.asyncCmdDone(_);
                return .done;
            },
        }
    }

    fn enqueueSelf(*_) {
        if (_.event_loop == .js) {
            _.event_loop.js.enqueueTaskConcurrent(_.concurrent_task.js.from(_, .manual_deinit));
        } else {
            _.event_loop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.from(_, "runFromMainThreadMini"));
        }
    }

    fn childDone(*_, child_ptr: ChildPtr, exit_code: ExitCode): Yield {
        log("{f} childDone", .{_});
        child_ptr.deinit();
        _.state = .{ done: exit_code };
        _.enqueueSelf();
        return .suspended;
    }

    /// This function is purposefully empty as a hack to ensure Async runs in the background while appearing to
    /// the parent that it is done immediately.
    ///
    /// For example, in a script like `sleep 1 & echo hello`, the `sleep 1` part needs to appear as done immediately so the parent doesn't wait for
    /// it and instead immediately moves to executing the next command.
    ///
    /// Actual deinitialization is executed once this Async calls `this.base.interpreter.asyncCmdDone(this)`, where the interpreter will call `.actuallyDeinit()`
    fn deinit(*_) {}

    fn actuallyDeinit(*_) {
        _.io.deref();
        _.base.endScope();
        _.parent.destroy(_);
    }

    fn runFromMainThread(*_) {
        let ny = _.next();
        ny.run();
    }

    fn runFromMainThreadMini(*_, dummy: *void) {
        unsafe { * = @intFromPtr(dummy); }
        _.runFromMainThread();
    }
}
