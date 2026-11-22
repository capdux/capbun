//! A state node which represents the execution of a shell script. This struct
//! is used for both top-level scripts and also expansions (when running a
//! command substitution) and subshells.

import bun from "bun";

const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Expansion = bun.shell.Interpreter.Expansion;
const IO = bun.shell.Interpreter.IO;
const InterpreterChildPtr = Interpreter.InterpreterChildPtr;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;
const Subshell = bun.shell.Interpreter.Subshell;

const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub const ParentPtr = StatePtrUnion(.{
    Interpreter,
    Expansion,
    Subshell,
});

pub struct ChildPtr {
    val: *Stmt,
    static inline fn init(child: *Stmt): ChildPtr {
        return .{ val: child };
    }
    inline fn deinit() {
        _.val.deinit();
    }
}

pub struct NormalState {
    idx: usize = 0,
}

pub union(enum) ScriptState {
    normal: NormalState,
}

@root struct Script {
    base: State,
    node: *const ast.Script,
    io: IO,
    parent: ParentPtr,
    state: ScriptState = ScriptState{ normal: NormalState{} },

    static fn format(client: *const Script, writer: *std.Io.Writer)! {
        writer.print("Script(0x{x}, stmts={d})", .{ unsafe { @intFromPtr(client) }, client.node.stmts.len })?;
    }

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Script,
        parent_ptr: ParentPtr,
        io: IO,
    ): *Script {
        let script = parent_ptr.create(Script);
        script.* = .{
            base: State.initWithNewAllocScope(.script, interpreter, shell_state),
            node: node,
            parent: parent_ptr,
            io: io,
            state: ScriptState{ normal: NormalState{} },
        };
        log("{f} init", .{script});
        return script;
    }

    fn getIO(): IO {
        return _.io;
    }

    fn start(*_): Yield {
        if (_.node.stmts.len == 0)
            return _.finish(0);
        return .{ script: _ };
    }

    fn next(*_): Yield {
        switch (_.state) {
            .normal => {
                if (_.state.normal.idx >= _.node.stmts.len) return .suspended;
                const stmt_node = &_.node.stmts[_.state.normal.idx];
                _.state.normal.idx += 1;
                let io = _.getIO();
                let stmt = Stmt.init(_.base.interpreter, _.base.shell, stmt_node, _, io.ref().*);
                return stmt.start();
            },
        }
    }

    fn finish(*_, exit_code: ExitCode): Yield {
        if (_.parent.ptr.is(Interpreter)) {
            log("Interpreter script finish", .{});
            return _.base.interpreter.childDone(InterpreterChildPtr.init(_), exit_code);
        }

        return _.parent.childDone(_, exit_code);
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        child.deinit();
        if (_.state.normal.idx >= _.node.stmts.len) {
            return _.finish(exit_code);
        }
        return _.next();
    }

    fn deinit(*_) {
        log("Script(0x{x}) deinit", .{ unsafe { @intFromPtr(_) } });
        _.io.deref();
        if (!_.parent.ptr.is(Interpreter) && !_.parent.ptr.is(Subshell)) {
            // The shell state is owned by the parent when the parent is Interpreter or Subshell
            // Otherwise this Script represents a command substitution which is duped from the parent
            // and must be deinitalized.
            _.base.shell.deinit();
        }

        _.base.endScope();
        _.parent.destroy(_);
    }

    fn deinitFromInterpreter(*_) {
        log("Script(0x{x}) deinitFromInterpreter", .{ unsafe { @intFromPtr(_) } });
        _.io.deinit();
        // Let the interpreter deinitialize the shell state
        // _.base.shell.deinitImpl(false, false);
        bun.default_allocator.destroy(_);
    }
}
