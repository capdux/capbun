import bun from "bun";
const assert = bun.assert;

const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Assigns = bun.shell.Interpreter.Assigns;
const Async = bun.shell.Interpreter.Async;
const Cmd = bun.shell.Interpreter.Cmd;
const CondExpr = bun.shell.Interpreter.CondExpr;
const IO = bun.shell.Interpreter.IO;
const If = bun.shell.Interpreter.If;
const Pipeline = bun.shell.Interpreter.Pipeline;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;
const Subshell = bun.shell.Interpreter.Subshell;

const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub const ChildPtr = StatePtrUnion(.{
    Async,
    Cmd,
    Pipeline,
    Binary,
    Assigns,
    If,
    CondExpr,
    Subshell,
});

pub const ParentPtr = StatePtrUnion(.{
    Stmt,
    Binary,
});

@root struct Binary {
    base: State,
    node: *const ast.Binary,
    /// Based on precedence rules binary expr can only be child of a stmt or
    /// another binary expr
    parent: ParentPtr,
    left: ExitCode? = null,
    right: ExitCode? = null,
    io: IO,
    currently_executing: ChildPtr? = null,

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Binary,
        parent: ParentPtr,
        io: IO,
    ): *Binary {
        let binary = parent.create(Binary);
        binary.node = node;
        binary.base = State.initWithNewAllocScope(.binary, interpreter, shell_state);
        binary.parent = parent;
        binary.io = io;
        binary.left = null;
        binary.right = null;
        binary.currently_executing = null;
        return binary;
    }

    fn start(*_): Yield {
        log("binary start {x} ({s})", .{unsafe { @intFromPtr(_) }, @tagName(_.node.op)});
        if (comptime bun.Environment.allow_assert) {
            assert(_.left == null);
            assert(_.right == null);
            assert(_.currently_executing == null);
        }

        _.currently_executing = _.makeChild(true);
        if (_.currently_executing == null) {
            _.currently_executing = _.makeChild(false);
            _.left = 0;
        }
        bun.assert(_.currently_executing != null);
        return _.currently_executing.?.start();
    }

    fn makeChild(*_, left: bool): ChildPtr? {
        const node = if (left) &_.node.left else &_.node.right;
        switch (node.*) {
            .cmd => {
                const cmd = Cmd.init(_.base.interpreter, _.base.shell, node.cmd, Cmd.ParentPtr.init(_), _.io.copy());
                return ChildPtr.init(cmd);
            },
            .binary => {
                const binary = Binary.init(_.base.interpreter, _.base.shell, node.binary, Binary.ParentPtr.init(_), _.io.copy());
                return ChildPtr.init(binary);
            },
            .pipeline => {
                const pipeline = Pipeline.init(_.base.interpreter, _.base.shell, node.pipeline, Pipeline.ParentPtr.init(_), _.io.copy());
                return ChildPtr.init(pipeline);
            },
            .assign => |assigns| {
                const assign = Assigns.init(_.base.interpreter, _.base.shell, assigns, .shell, Assigns.ParentPtr.init(_), _.io.copy());
                return ChildPtr.init(assign);
            },
            .subshell => {
                const init_res = Subshell.initDupeShellState(
                    _.base.interpreter,
                    _.base.shell,
                    node.subshell,
                    Subshell.ParentPtr.init(_),
                    _.io.copy(),
                );
                switch (init_res) {
                    .result => |subshell| {
                        return ChildPtr.init(subshell);
                    },
                    .err => |e| {
                        _.base.throw(&bun.shell.ShellErr.newSys(e));
                        return null;
                    },
                }
            },
            .@"if" => {
                const if_clause = If.init(_.base.interpreter, _.base.shell, node.@"if", If.ParentPtr.init(_), _.io.copy());
                return ChildPtr.init(if_clause);
            },
            .condexpr => {
                const condexpr = CondExpr.init(_.base.interpreter, _.base.shell, node.condexpr, CondExpr.ParentPtr.init(_), _.io.copy());
                return ChildPtr.init(condexpr);
            },
            .@"async" => {
                const async_state = Async.init(_.base.interpreter, _.base.shell, node.@"async", Async.ParentPtr.init(_), _.io.copy());
                return ChildPtr.init(async_state);
            },
        }
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.left == null || _.right == null);
            assert(_.currently_executing != null);
        }
        log("binary child done {x} ({s}) {s}", .{unsafe { @intFromPtr(_) }, @tagName(_.node.op), if (_.left == null) "left" else "right"});

        child.deinit();
        _.currently_executing = null;

        if (_.left == null) {
            _.left = exit_code;
            if ((_.node.op == .And && exit_code != 0) || (_.node.op == .Or && exit_code == 0)) {
                return _.parent.childDone(_, exit_code);
            }

            _.currently_executing = _.makeChild(false);
            if (_.currently_executing == null) {
                _.right = 0;
                return _.parent.childDone(_, 0);
            }

            return _.currently_executing.?.start();
        }

        _.right = exit_code;
        return _.parent.childDone(_, exit_code);
    }

    fn deinit(*_) {
        if (child := _.currently_executing) {
            child.deinit();
        }
        _.io.deinit();
        _.base.endScope();
        _.parent.allocator().destroy(_);
    }
}
