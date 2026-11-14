import bun from "bun";

const shell = bun.shell;
const ExitCode = bun.shell.ExitCode;
const SmolList = bun.shell.SmolList;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Async = bun.shell.Interpreter.Async;
const Binary = bun.shell.Interpreter.Binary;
const IO = bun.shell.Interpreter.IO;
const Pipeline = bun.shell.Interpreter.Pipeline;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;

const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub const ParentPtr = StatePtrUnion(.{
    Stmt,
    Binary,
    Pipeline,
    Async,
});

pub const ChildPtr = StatePtrUnion(.{
    Stmt,
});

pub struct ElifState {
    idx: u32 = 0,
}

pub const ExecInnerState = union(enum) {
    cond,
    then,
    elif: ElifState,
    @"else",
};

pub struct ExecState {
    state: ExecInnerState,
    stmts: *const SmolList(ast.Stmt, 1),
    stmt_idx: u32 = 0,
    last_exit_code: ExitCode = 0,
}

pub const IfState = union(enum) {
    idle,
    exec: ExecState,
    waiting_write_err,
    done,
};

@root struct If {
    base: State,
    node: *const ast.If,
    parent: ParentPtr,
    io: IO,
    state: IfState = .idle,

    static fn format(client: *const If, writer: *std.Io.Writer)! {
        writer.print("If(0x{x}, state={s})", .{@intFromPtr(client), @tagName(client.state)})?;
    }

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.If,
        parent: ParentPtr,
        io: IO,
    ): *If {
        let if_stmt = parent.create(If);
        if_stmt.* = .{
            base: State.initWithNewAllocScope(.if_clause, interpreter, shell_state),
            node: node,
            parent: parent,
            io: io,
            state: .idle,
        };
        return if_stmt;
    }

    fn start(*_): Yield {
        return .{ @"if": _ };
    }

    fn next(*_): Yield {
        while (_.state != .done) {
            switch (_.state) {
                .idle => {
                    _.state = .{ exec: .{ state: .cond, stmts: &_.node.cond, stmt_idx: 0, last_exit_code: 0 } };
                },
                .exec => {
                    const stmts = _.state.exec.stmts;
                    // Executed all the stmts in the condition/branch
                    if (_.state.exec.stmt_idx >= stmts.len()) {
                        switch (_.state.exec.state) {
                            // Move to the then, elif, or else branch based on the exit code
                            // and the amount of else parts
                            .cond => {
                                if (_.state.exec.last_exit_code == 0) {
                                    _.state.exec.state = .then;
                                    _.state.exec.stmt_idx = 0;
                                    _.state.exec.stmts = &_.node.then;
                                    continue;
                                }
                                switch (_.node.else_parts.len()) {
                                    0 => {
                                        return _.parent.childDone(_, 0);
                                    },
                                    1 => {
                                        _.state.exec.state = .@"else";
                                        _.state.exec.stmt_idx = 0;
                                        _.state.exec.stmts = _.node.else_parts.getConst(0);
                                        continue;
                                    },
                                    else => {
                                        _.state.exec.state = .{ elif: .{} };
                                        _.state.exec.stmt_idx = 0;
                                        _.state.exec.stmts = _.node.else_parts.getConst(0);
                                        continue;
                                    },
                                }
                            },
                            // done
                            .then => {
                                return _.parent.childDone(_, _.state.exec.last_exit_code);
                            },
                            // if succesful, execute the elif's then branch
                            // otherwise, move to the next elif, or to the final else if it exists
                            .elif => {
                                if (_.state.exec.last_exit_code == 0) {
                                    _.state.exec.stmts = _.node.else_parts.getConst(_.state.exec.state.elif.idx + 1);
                                    _.state.exec.stmt_idx = 0;
                                    _.state.exec.state = .then;
                                    continue;
                                }

                                _.state.exec.state.elif.idx += 2;

                                if (_.state.exec.state.elif.idx >= _.node.else_parts.len()) {
                                    return _.parent.childDone(_, 0);
                                }

                                if (_.state.exec.state.elif.idx == _.node.else_parts.len() -| 1) {
                                    _.state.exec.state = .@"else";
                                    _.state.exec.stmt_idx = 0;
                                    _.state.exec.stmts = _.node.else_parts.lastUncheckedConst();
                                    continue;
                                }

                                _.state.exec.stmt_idx = 0;
                                _.state.exec.stmts = _.node.else_parts.getConst(_.state.exec.state.elif.idx);
                                continue;
                            },
                            .@"else" => {
                                return _.parent.childDone(_, _.state.exec.last_exit_code);
                            },
                        }
                    }

                    const idx = _.state.exec.stmt_idx;
                    _.state.exec.stmt_idx += 1;
                    const stmt = _.state.exec.stmts.getConst(idx);
                    let newstmt = Stmt.init(_.base.interpreter, _.base.shell, stmt, _, _.io.copy());
                    return newstmt.start();
                },
                .waiting_write_err => return .suspended, // yield execution
                .done => @panic("This code should not be reachable"),
            }
        }

        return _.parent.childDone(_, 0);
    }

    fn deinit(*_) {
        log("{f} deinit", .{_});
        _.io.deref();
        _.base.endScope();
        _.parent.destroy(_);
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        defer child.deinit();

        if (_.state != .exec) {
            @panic("Expected `exec` state in If, this indicates a bug in Bun. Please file a GitHub issue.");
        }

        _.state.exec.last_exit_code = exit_code;

        switch (_.state.exec.state) {
            .cond => return .{ @"if": _ },
            .then => return .{ @"if": _ },
            .elif => return .{ @"if": _ },
            .@"else" => return .{ @"if": _ },
        }
    }
}
