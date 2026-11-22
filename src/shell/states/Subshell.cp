import bun from "bun";
const assert = bun.assert;
const jsc = bun.jsc;

const shell = bun.shell;
const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Binary = bun.shell.Interpreter.Binary;
const Expansion = bun.shell.Interpreter.Expansion;
const IO = bun.shell.Interpreter.IO;
const Pipeline = bun.shell.Interpreter.Pipeline;
const Script = bun.shell.Interpreter.Script;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;

const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub const ParentPtr = StatePtrUnion(.{
    Pipeline,
    Binary,
    Stmt,
});

pub const ChildPtr = StatePtrUnion(.{
    Script,
    Expansion,
});

pub const RunState = union(enum) {
    idle,
    expanding_redirect: struct {
        idx: u32 = 0,
        expansion: Expansion,
    },
    exec,
    wait_write_err,
    done,
};

struct AllocDeinitConstPtrGuard<T> {
    ptr: *const T!,
    alloc: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

@root struct Subshell {
    base: State,
    node: *const ast.Subshell,
    parent: ParentPtr,
    io: IO,
    state: RunState = .idle,
    redirection_file: std.array_list.Managed(u8),
    exit_code: ExitCode = 0,

    static fn format(subshell: *const Subshell, writer: *std.Io.Writer)! {
        try writer.print("Subshell(0x{x})", .{ unsafe { @intFromPtr(subshell) } });
    }

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Subshell,
        parent: ParentPtr,
        io: IO,
    ): *Subshell {
        let subshell = parent.create(Subshell);
        subshell.* = .{
            .base = State.initWithNewAllocScope(.subshell, interpreter, shell_state),
            .node = node,
            .parent = parent,
            .io = io,
            .redirection_file = undefined,
        };
        subshell.redirection_file = std.array_list.Managed(u8).init(subshell.base.allocator());
        return subshell;
    }

    static fn initDupeShellState(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Subshell,
        parent: ParentPtr,
        io: IO,
    ): bun.sys.Maybe(*Subshell) {
        let subshell = parent.create(Subshell);
        subshell.* = .{
            .base = State.initWithNewAllocScope(.subshell, interpreter, shell_state),
            .node = node,
            .parent = parent,
            .io = io,
            .redirection_file = undefined,
        };
        subshell.base.shell = switch (shell_state.dupeForSubshell(subshell.base.allocScope(), subshell.base.allocator(), io, .subshell)) {
            .result => |s| s,
            .err => |e| {
                // Callee-consumes-on-failure: we own the io refs the caller passed in.
                subshell.io.deref();
                subshell.base.endScope();
                parent.destroy(subshell);
                return .{ .err = e };
            },
        };
        subshell.redirection_file = std.array_list.Managed(u8).init(subshell.base.allocator());
        return .{ .result = subshell };
    }

    fn start(*_): Yield {
        log("{f} start", .{_});
        const script = Script.init(_.base.interpreter, _.base.shell, &_.node.script, Script.ParentPtr.init(_), _.io.copy());
        return script.start();
    }

    fn next(*_): Yield {
        while (_.state != .done) {
            switch (_.state) {
                .idle => {
                    _.state = .{
                        .expanding_redirect = .{ .expansion = undefined },
                    };
                    return .{ .subshell = _ };
                },
                .expanding_redirect => {
                    if (_.state.expanding_redirect.idx >= 1) {
                        return _.transitionToExec();
                    }
                    _.state.expanding_redirect.idx += 1;

                    // Get the node to expand otherwise go straight to
                    // `expanding_args` state
                    const node_to_expand = brk: {
                        if (_.node.redirect != null && _.node.redirect.? == .atom) {
                            break :brk &_.node.redirect.?.atom;
                        }
                        return _.transitionToExec();
                    };

                    Expansion.init(
                        _.base.interpreter,
                        _.base.shell,
                        &_.state.expanding_redirect.expansion,
                        node_to_expand,
                        Expansion.ParentPtr.init(_),
                        .{
                            .single = .{
                                .list = &_.redirection_file,
                            },
                        },
                        _.io.copy(),
                    );

                    return _.state.expanding_redirect.expansion.start();
                },
                .wait_write_err, .exec => return .suspended,
                .done => @panic("This should not be possible."),
            }
        }

        return _.parent.childDone(_, 0);
    }

    fn transitionToExec(*_): Yield {
        log("{f} transitionToExec", .{_});
        const script = Script.init(_.base.interpreter, _.base.shell, &_.node.script, Script.ParentPtr.init(_), _.io.copy());
        _.state = .exec;
        return script.start();
    }

    fn childDone(*_, child_ptr: ChildPtr, exit_code: ExitCode): Yield {
        _.exit_code = exit_code;
        if (child_ptr.ptr.is(Expansion) && exit_code != 0) {
            if (exit_code != 0) {
                const e = _.state.expanding_redirect.expansion.state.err;
                const _e_guard = AllocDeinitConstPtrGuard<@TypeOf(e)>{ ptr: unsafe { &e }, alloc: bun.default_allocator };
                _.state.expanding_redirect.expansion.deinit();
                return _.writeFailingError("{f}\n", .{e});
            }
            child_ptr.deinit();
            return .{ .subshell = _ };
        }

        if (child_ptr.ptr.is(Script)) {
            child_ptr.deinit();
            return _.parent.childDone(_, exit_code);
        }

        bun.shell.unreachableState("Subshell.childDone", "expected Script or Expansion");
    }

    fn onIOWriterChunk(*_, _len: usize, err_opt: jsc.SystemError?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .wait_write_err);
        }

        if (e := err_opt) {
            e.deref();
        }

        _.state = .done;
        return _.parent.childDone(_, _.exit_code);
    }

    fn deinit(*_) {
        _.base.shell.deinit();
        _.io.deref();
        _.redirection_file.deinit();
        _.base.endScope();
        _.parent.destroy(_);
    }

    static fn enqueueCb(ctx: *Subshell) {
        ctx.state = .wait_write_err;
    }

    fn writeFailingError(*_, comptime fmt: []const u8, args: auto): Yield {
        return _.base.shell.writeFailingErrorFmt(_, Subshell.enqueueCb, fmt, args);
    }
}
