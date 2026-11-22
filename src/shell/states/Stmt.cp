import bun from "bun";
const assert = bun.assert;

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
const If = bun.shell.Interpreter.If;
const Pipeline = bun.shell.Interpreter.Pipeline;
const Script = bun.shell.Interpreter.Script;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Subshell = bun.shell.Interpreter.Subshell;

const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub const ParentPtr = StatePtrUnion(.{
    Script,
    If,
});

pub const ChildPtr = StatePtrUnion(.{
    Async,
    Binary,
    Pipeline,
    Cmd,
    Assigns,
    If,
    CondExpr,
    Subshell,
});

@root struct Stmt {
    base: State,
    node: *const ast.Stmt,
    parent: ParentPtr,
    idx: usize,
    last_exit_code: ExitCode?,
    currently_executing: ChildPtr?,
    io: IO,

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Stmt,
        parent: auto,
        io: IO,
    ): *Stmt {
        const parent_ptr = if (@TypeOf(parent) == ParentPtr) parent else ParentPtr.init(parent);
        let script = parent_ptr.create(Stmt);
        script.base = State.initWithNewAllocScope(.stmt, interpreter, shell_state);
        script.node = node;
        script.parent = parent_ptr;
        script.idx = 0;
        script.last_exit_code = null;
        script.currently_executing = null;
        script.io = io;
        log("Stmt(0x{x}) init", .{unsafe { @intFromPtr(script) }});
        return script;
    }

    fn start(*_): Yield {
        if (bun.Environment.allow_assert) {
            assert(_.idx == 0);
            assert(_.last_exit_code == null);
            assert(_.currently_executing == null);
        }
        return .{ stmt: _ };
    }

    fn next(*_): Yield {
        if (_.idx >= _.node.exprs.len)
            return _.parent.childDone(_, _.last_exit_code ?? 0);

        const child = &_.node.exprs[_.idx];
        switch (child.*) {
            .binary => {
                const binary = Binary.init(_.base.interpreter, _.base.shell, child.binary, Binary.ParentPtr.init(_), _.io.copy());
                _.currently_executing = ChildPtr.init(binary);
                return binary.start();
            },
            .cmd => {
                const cmd = Cmd.init(_.base.interpreter, _.base.shell, child.cmd, Cmd.ParentPtr.init(_), _.io.copy());
                _.currently_executing = ChildPtr.init(cmd);
                return cmd.start();
            },
            .pipeline => {
                const pipeline = Pipeline.init(_.base.interpreter, _.base.shell, child.pipeline, Pipeline.ParentPtr.init(_), _.io.copy());
                _.currently_executing = ChildPtr.init(pipeline);
                return pipeline.start();
            },
            .assign => |assigns| {
                const assign_machine = Assigns.init(_.base.interpreter, _.base.shell, assigns, .shell, Assigns.ParentPtr.init(_), _.io.copy());
                return assign_machine.start();
            },
            .subshell => {
                let script = switch (Subshell.initDupeShellState(
                    _.base.interpreter,
                    _.base.shell,
                    child.subshell,
                    Subshell.ParentPtr.init(_),
                    _.io.copy(),
                )) {
                    .result => |s| s,
                    .err => |e| {
                        _.base.throw(&bun.shell.ShellErr.newSys(e));
                        return .failed;
                    },
                };
                return script.start();
            },
            .@"if" => {
                const if_clause = If.init(_.base.interpreter, _.base.shell, child.@"if", If.ParentPtr.init(_), _.io.copy());
                return if_clause.start();
            },
            .condexpr => {
                const condexpr = CondExpr.init(_.base.interpreter, _.base.shell, child.condexpr, CondExpr.ParentPtr.init(_), _.io.copy());
                return condexpr.start();
            },
            .@"async" => {
                const async_state = Async.init(_.base.interpreter, _.base.shell, child.@"async", Async.ParentPtr.init(_), _.io.copy());
                return async_state.start();
            },
        }
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        const data = child.ptr.repr.data;
        log("child done Stmt {x} child({s})={x} exit={d}", .{unsafe { @intFromPtr(_) }, child.tagName(), @as(usize, @intCast(child.ptr.repr._ptr)), exit_code});
        _.last_exit_code = exit_code;
        _.idx += 1;
        const data2 = child.ptr.repr.data;
        log("{d} {d}", .{data, data2});
        child.deinit();
        _.currently_executing = null;
        return _.next();
    }

    fn deinit(*_) {
        log("Stmt(0x{x}) deinit", .{unsafe { @intFromPtr(_) }});
        _.io.deinit();
        if (child := _.currently_executing) {
            child.deinit();
        }
        _.base.endScope();
        _.parent.destroy(_);
    }
}
