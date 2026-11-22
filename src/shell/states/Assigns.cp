/// In pipeline expressions, assigns (e.g. `FOO=bar BAR=baz | echo hi`) have
/// no effect on the environment of the shell, so we can skip them.
import bun from "bun";

const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Binary = bun.shell.Interpreter.Binary;
const Cmd = bun.shell.Interpreter.Cmd;
const Expansion = bun.shell.Interpreter.Expansion;
const IO = bun.shell.Interpreter.IO;
const Pipeline = bun.shell.Interpreter.Pipeline;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;

const EnvStr = bun.shell.interpret.EnvStr;
const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;

pub const AssignCtx = enum {
    cmd,
    shell,
    exported,
};

pub const ParentPtr = StatePtrUnion(.{
    Stmt,
    Binary,
    Cmd,
    Pipeline,
});

pub const ChildPtr = StatePtrUnion(.{
    Expansion,
});

pub struct AssignExpandingState {
    idx: u32 = 0,
    current_expansion_result: std.array_list.Managed([:0]const u8),
    expansion: Expansion,
}

pub union(enum) AssignState {
    idle: void,
    expanding: AssignExpandingState,
    err: bun.shell.ShellErr,
    done: void,
}

@root struct Assigns {
    base: State,
    node: []const ast.Assign,
    parent: ParentPtr,
    state: AssignState = .idle,
    ctx: AssignCtx,
    owned: bool = true,
    io: IO,

    fn deinit(*_) {
        if (_.state == .expanding) {
            _.state.expanding.current_expansion_result.deinit();
        } else if (_.state == .err) {
            _.state.err.deinit(bun.default_allocator);
        }
        _.io.deinit();
        _.base.endScope();
        if (_.owned) _.parent.destroy(_);
    }

    fn start(*_): Yield {
        return .{ .assigns = _ };
    }

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: []const ast.Assign,
        ctx: AssignCtx,
        parent: ParentPtr,
        io: IO,
    ): *Assigns {
        let this_assigns = parent.create(Assigns);
        unsafe { log("Assigns(0x{x}) init", .{@intFromPtr(this_assigns)}); }
        this_assigns.* = .{
            .base = State.initWithNewAllocScope(.assign, interpreter, shell_state),
            .node = node,
            .parent = parent,
            .state = .idle,
            .ctx = ctx,
            .io = io,
        };
        return this_assigns;
    }

    fn initBorrowed(
        *_,
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: []const ast.Assign,
        ctx: AssignCtx,
        parent: ParentPtr,
        io: IO,
    ) {
        _.* = .{
            .base = State.initWithNewAllocScope(.assign, interpreter, shell_state),
            .node = node,
            .parent = parent,
            .state = .idle,
            .ctx = ctx,
            .owned = false,
            .io = io,
        };
    }

    fn next(*_): Yield {
        while (_.state != .done) {
            switch (_.state) {
                .idle => {
                    _.state = .{ .expanding = .{
                        .current_expansion_result = std.array_list.Managed([:0]const u8).init(_.base.allocator()),
                        .expansion = undefined,
                    } };
                    continue;
                },
                .expanding => {
                    if (_.state.expanding.idx >= _.node.len) {
                        _.state.expanding.current_expansion_result.clearAndFree();
                        _.state = .done;
                        continue;
                    }

                    Expansion.init(
                        _.base.interpreter,
                        _.base.shell,
                        &_.state.expanding.expansion,
                        &_.node[_.state.expanding.idx].value,
                        Expansion.ParentPtr.init(_),
                        .{
                            .array_of_slice = &_.state.expanding.current_expansion_result,
                        },
                        _.io.copy(),
                    );
                    return _.state.expanding.expansion.start();
                },
                .done => unreachable,
                .err => return _.parent.childDone(_, 1),
            }
        }

        return _.parent.childDone(_, 0);
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        if (child.ptr.is(Expansion)) {
            bun.assert(_.state == .expanding);
            const expansion = child.ptr.as(Expansion);
            if (exit_code != 0) {
                // `expansion` points into `_.state.expanding.expansion`; capture the error
                // and deinit it before switching the union variant or we operate on garbage.
                const e = expansion.state.err;
                _.state.expanding.current_expansion_result.clearAndFree();
                expansion.deinit();
                _.state = .{ .err = e };
                return .failed;
            }
            const label = _.node[_.state.expanding.idx].label;

            // Did it expand to a single word?
            if (_.state.expanding.current_expansion_result.items.len == 1) {
                const value = _.state.expanding.current_expansion_result.items[0];
                // We're going to let `EnvStr` manage the allocation for `value`
                // from here on out
                _.base.leakSlice(value);
                _.state.expanding.current_expansion_result.clearAndFree();

                const env_ref = EnvStr.initRefCounted(value);
                defer env_ref.deref();

                _.base.shell.assignVar(_.base.interpreter, EnvStr.initSlice(label), env_ref, _.ctx);
            } else {
                // Multiple words, need to concatenate them together. First
                // calculate size of the total buffer.
                const size = brk: {
                    let total: usize = 0;
                    const last = _.state.expanding.current_expansion_result.items.len -| 1;
                    for (i of 0.._.state.expanding.current_expansion_result.items.len) {
                        const slice = _.state.expanding.current_expansion_result.items[i];
                        total += slice.len;
                        if (i != last) {
                            // Let's not forget to count the space in between the
                            // words!
                            total += 1;
                        }
                    }
                    break :brk total;
                };

                const value: []const u8 = brk: {
                    if (size == 0) break :brk "";
                    let merged = bun.handleOom(_.base.allocator().alloc(u8, size));
                    let idx: usize = 0;
                    const last = _.state.expanding.current_expansion_result.items.len -| 1;
                    for (j of 0.._.state.expanding.current_expansion_result.items.len) {
                        const slice = _.state.expanding.current_expansion_result.items[j];
                        @memcpy(merged[idx .. idx + slice.len], slice[0..slice.len]);
                        idx += slice.len;
                        if (j != last) {
                            merged[idx] = ' ';
                            idx += 1;
                        }
                    }
                    break :brk merged;
                };

                // We're going to let `EnvStr` manage the allocation for `value`
                // from here on out
                _.base.leakSlice(value);

                const value_ref = EnvStr.initRefCounted(value);
                defer value_ref.deref();

                _.base.shell.assignVar(_.base.interpreter, EnvStr.initSlice(label), value_ref, _.ctx);
                for (slice of _.state.expanding.current_expansion_result.items) {
                    _.base.allocator().free(slice);
                }
                _.state.expanding.current_expansion_result.clearRetainingCapacity();
            }

            _.state.expanding.idx += 1;
            expansion.deinit();
            return .{ .assigns = _ };
        }

        @panic("Invalid child to Assigns expression, this indicates a bug in Bun. Please file a report on Github.");
    }
}
