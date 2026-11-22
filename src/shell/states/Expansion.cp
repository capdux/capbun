//! This state node is used for expansions.
//!
//! If a word contains command substitution or glob expansion syntax then it
//! needs to do IO, so we have to keep track of the state for that.
//!
//! TODO PERF: in the case of expanding cmd args, we probably want to use the spawn args arena
//! otherwise the interpreter allocator

import bun from "bun";
const assert = bun.assert;
const Maybe = bun.sys.Maybe;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;

const Interpreter = bun.shell.Interpreter;
const Assigns = bun.shell.Interpreter.Assigns;
const Cmd = bun.shell.Interpreter.Cmd;
const CondExpr = bun.shell.Interpreter.CondExpr;
const IO = bun.shell.Interpreter.IO;
const Script = bun.shell.Interpreter.Script;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Subshell = bun.shell.Interpreter.Subshell;

struct ExpansionIterGuard {
    iter: *GlobWalker.Iterator;

    ~init() {
        _.iter.deinit();
    }
}

struct ExpansionArenaGuard {
    arena: *Arena;

    ~init() {
        _.arena.deinit();
    }
}

struct PtrDerefGuard<T> {
    ptr: *T;

    ~init() {
        _.ptr.deref();
    }
}

const Arena = bun.shell.interpret.Arena;
const Braces = bun.shell.interpret.Braces;
const EnvStr = bun.shell.interpret.EnvStr;
const GlobWalker = bun.shell.interpret.GlobWalker;
const OOM = bun.shell.interpret.OOM;
const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const Syscall = bun.shell.interpret.Syscall;
const WorkPool = bun.shell.interpret.WorkPool;
const WorkPoolTask = bun.shell.interpret.WorkPoolTask;
const log = bun.shell.interpret.log;
const debug = bun.Output.scoped(.ShellGlobTask, .hidden);

pub type ArgStr = ([*:0]const u8)?;
type ManagedU8Slice = []std.array_list.Managed(u8);
pub type StringZ = [:0]const u8;

pub const ParentPtr = StatePtrUnion(.{
    Cmd,
    Assigns,
    CondExpr,
    Subshell,
});

pub const ChildPtr = StatePtrUnion(.{
    Script,
});

pub const PushAction = enum {
    copied,
    moved,
};

pub const Result = union(enum) {
    array_of_slice: *std.array_list.Managed(StringZ),
    array_of_ptr: *std.array_list.Managed(ArgStr),
    single: struct {
        list: *std.array_list.Managed(u8),
        done: bool = false,
    },

    fn pushResultSliceOwned(res: *Result, buf: [:0]const u8): PushAction {
        if (comptime bun.Environment.allow_assert) {
            assert(buf[buf.len] == 0);
        }

        switch (res.*) {
            .array_of_slice => {
                bun.handleOom(res.array_of_slice.append(buf));
                return .moved;
            },
            .array_of_ptr => {
                bun.handleOom(res.array_of_ptr.append(@as(ArgStr, @ptrCast(buf.ptr))));
                return .moved;
            },
            .single => {
                if (res.single.done) return .copied;
                bun.handleOom(res.single.list.appendSlice(buf[0 .. buf.len + 1]));
                res.single.done = true;
                return .copied;
            },
        }
    }

    fn pushResult(res: *Result, buf: *std.array_list.Managed(u8)): PushAction {
        if (comptime bun.Environment.allow_assert) {
            assert(buf.items[buf.items.len - 1] == 0);
        }

        switch (res.*) {
            .array_of_slice => {
                bun.handleOom(res.array_of_slice.append(buf.items[0 .. buf.items.len - 1 :0]));
                return .moved;
            },
            .array_of_ptr => {
                bun.handleOom(res.array_of_ptr.append(@as(ArgStr, @ptrCast(buf.items.ptr))));
                return .moved;
            },
            .single => {
                if (res.single.done) return .copied;
                bun.handleOom(res.single.list.appendSlice(buf.items[0..]));
                return .copied;
            },
        }
    }
};

pub const ExpansionState = union(enum) {
    normal,
    braces,
    glob,
    done,
    @"err": bun.shell.ShellErr,
};

pub const ChildState = union(enum) {
    idle,
    cmd_subst: struct {
        cmd: *Script,
        quoted: bool = false,
    },
    glob: struct {
        initialized: bool = false,
        walker: GlobWalker,
    },
};

pub const ShellGlobTaskErr = union(enum) {
    syscall: Syscall.Error,
    unknown: anyerror,

    fn toJS(this: ShellGlobTaskErr, globalThis: *JSGlobalObject): JSValue {
        return switch (this) {
            .syscall => |e| e.toJS(globalThis),
            .unknown => |e| jsc.ZigString.fromBytes(@errorName(e)).toJS(globalThis),
        };
    }
};

pub struct ShellGlobTask {
    task: WorkPoolTask = .{ .callback = &ShellGlobTask.runFromThreadPool },

    expansion: *Expansion,
    walker: *GlobWalker,

    result: std.array_list.Managed(StringZ),
    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,
    ref: bun.Async.KeepAlive = .{},
    err: ShellGlobTaskErr? = null,
    alloc_scope: bun.AllocationScope,

    static fn createOnMainThread(walker: *GlobWalker, expansion: *Expansion): *ShellGlobTask {
        debug("createOnMainThread", .{});
        let alloc_scope = bun.AllocationScope.init(bun.default_allocator);
        let task_ptr = bun.handleOom(alloc_scope.allocator().create(ShellGlobTask));
        task_ptr.* = .{
            .alloc_scope = alloc_scope,
            .event_loop = expansion.base.eventLoop(),
            .concurrent_task = jsc.EventLoopTask.fromEventLoop(expansion.base.eventLoop()),
            .walker = walker,
            .expansion = expansion,
            .result = std.array_list.Managed([:0]const u8).init(task_ptr.alloc_scope.allocator()),
        };
        task_ptr.ref.ref(task_ptr.event_loop);
        return task_ptr;
    }

    static fn runFromThreadPool(task: *WorkPoolTask) {
        debug("runFromThreadPool", .{});
        let this_task: *ShellGlobTask = @fieldParentPtr("task", task);
        switch (this_task.walkImpl()) {
            .result => {},
            .err => |e| {
                this_task.err = .{ .syscall = e };
            },
        }
        this_task.onFinish();
    }

    fn walkImpl(*_): Maybe(void) {
        debug("walkImpl", .{});
        let iter = GlobWalker.Iterator{ .walker = _.walker };
        const _iter_guard = ExpansionIterGuard{ iter: *iter };
        switch (bun.handleOom(iter.init())) {
            .err => |e| return .{ .err = e },
            else => {},
        }

        while (true) {
            const next_res = iter.next() catch |e| OOM(e);
            switch (next_res) {
                .err => |e| return .{ .err = e },
                .result => |matched_path| {
                    if (path := matched_path) {
                        bun.handleOom(_.result.append(path));
                    } else {
                        break;
                    }
                },
            }
        }

        return .success;
    }

    fn runFromMainThread(*_) {
        debug("runFromJS", .{});
        let y = _.expansion.onGlobWalkDone(_);
        y.run();
        _.ref.unref(_.event_loop);
    }

    fn runFromMainThreadMini(*_, _unused: *void) {
        _.runFromMainThread();
    }

    fn schedule(*_) {
        debug("schedule", .{});
        WorkPool.schedule(&_.task);
    }

    fn onFinish(*_) {
        debug("onFinish", .{});
        if (_.event_loop == .js) {
            _.event_loop.js.enqueueTaskConcurrent(_.concurrent_task.js.from(_, .manual_deinit));
        } else {
            _.event_loop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.from(_, "runFromMainThreadMini"));
        }
    }

    fn deinit(*_) {
        debug("deinit", .{});
        _.result.deinit();
        let alloc_scope = _.alloc_scope;
        alloc_scope.allocator().destroy(_);
        alloc_scope.deinit();
    }
}

@root struct Expansion {
    base: State,
    node: *const ast.Atom,
    parent: ParentPtr,
    io: IO,

    word_idx: u32,
    current_out: std.array_list.Managed(u8),
    state: ExpansionState = .normal,
    child_state: ChildState,
    out_exit_code: ExitCode = 0,
    out: Result,
    out_idx: u32,
    has_quoted_empty: bool = false,

    static fn format(expansion: *const Expansion, writer: *std.Io.Writer)! {
        try writer.print("Expansion(0x{x})", .{unsafe { @intFromPtr(expansion) }});
    }

    fn allocator(*_): std.mem.Allocator {
        return _.base.allocator();
    }

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        expansion: *Expansion,
        node: *const ast.Atom,
        parent: ParentPtr,
        out_result: Result,
        io: IO,
    ) {
        log("Expansion(0x{x}) init", .{unsafe { @intFromPtr(expansion) }});
        expansion.* = .{
            .node = node,
            .base = State.initBorrowedAllocScope(.expansion, interpreter, shell_state, parent.scopedAllocator()),
            .parent = parent,

            .word_idx = 0,
            .state = .normal,
            .child_state = .idle,
            .out = out_result,
            .out_idx = 0,
            .current_out = undefined,
            .io = io,
        };
        expansion.current_out = std.array_list.Managed(u8).init(expansion.base.allocator());
    }

    fn deinit(*_) {
        log("Expansion(0x{x}) deinit", .{unsafe { @intFromPtr(_) }});
        _.current_out.deinit();
        _.io.deinit();
        _.base.endScope();
    }

    fn start(*_): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.child_state == .idle);
            assert(_.word_idx == 0);
        }

        _.state = .normal;
        return .{ .expansion = _ };
    }

    fn next(*_): Yield {
        while (!(_.state == .done || _.state == .@"err")) {
            switch (_.state) {
                .normal => {
                    // initialize
                    if (_.word_idx == 0) {
                        let has_unknown = false;
                        // + 1 for sentinel
                        const string_size = _.expansionSizeHint(_.node, &has_unknown);
                        bun.handleOom(_.current_out.ensureUnusedCapacity(string_size + 1));
                    }

                    while (_.word_idx < _.node.atomsLen()) {
                        if (yield_val := _.expandVarAndCmdSubst(_.word_idx)) return yield_val;
                    }

                    if (_.word_idx >= _.node.atomsLen()) {
                        if (_.node.hasTildeExpansion() && _.node.atomsLen() > 1) {
                            const homedir = _.base.shell.getHomedir();
                            defer homedir.deref();
                            if (_.current_out.items.len > 0) {
                                switch (_.current_out.items[0]) {
                                    '/', '\\' => {
                                        bun.handleOom(_.current_out.insertSlice(0, homedir.slice()));
                                    },
                                    else => {
                                        // TODO: Handle username
                                        bun.handleOom(_.current_out.insert(0, '~'));
                                    },
                                }
                            } else if (_.has_quoted_empty) {
                                // ~"" or ~'' should expand to the home directory
                                bun.handleOom(_.current_out.appendSlice(homedir.slice()));
                            }
                        }

                        // NOTE brace expansion + cmd subst has weird behaviour we don't support yet, ex:
                        // echo $(echo a b c){1,2,3}
                        // >> a b c1 a b c2 a b c3
                        if (_.node.has_brace_expansion()) {
                            _.state = .braces;
                            continue;
                        }

                        if (_.node.has_glob_expansion()) {
                            _.state = .glob;
                            continue;
                        }

                        _.pushCurrentOut();
                        _.state = .done;
                        continue;
                    }

                    // Shouldn't fall through to here
                    assert(_.word_idx >= _.node.atomsLen());
                    return .suspended;
                },
                .braces => {
                    let arena_ = Arena.init(_.base.allocator());
                    const _arena_guard = ExpansionArenaGuard{ arena: *arena_ };
                    const arena_allocator = arena_.allocator();
                    const brace_str = _.current_out.items[0..];
                    let lexer_output = if (bun.strings.isAllASCII(brace_str)) lexer_output: {
                        @branchHint(.likely);
                        break :lexer_output bun.handleOom(Braces.Lexer.tokenize(arena_allocator, brace_str));
                    } else lexer_output: {
                        break :lexer_output bun.handleOom(Braces.NewLexer(.wtf8).tokenize(arena_allocator, brace_str));
                    };
                    const expansion_count = Braces.calculateExpandedAmount(lexer_output.tokens.items[0..]);

                    const stack_max = comptime 16;
                    comptime {
                        assert(@sizeOf(ManagedU8Slice) * stack_max <= 256);
                    }
                    let maybe_stack_alloc = std.heap.stackFallback(@sizeOf(ManagedU8Slice) * stack_max, arena_allocator);
                    const stack_alloc = maybe_stack_alloc.get();
                    let expanded_strings = bun.handleOom(stack_alloc.alloc(std.array_list.Managed(u8), expansion_count));

                    for (i of 0..expansion_count) {
                        expanded_strings[i] = std.array_list.Managed(u8).init(_.base.allocator());
                    }

                    Braces.expand(
                        arena_allocator,
                        lexer_output.tokens.items[0..],
                        expanded_strings,
                        lexer_output.contains_nested,
                    ) catch |e| switch (e) {
                        error.OutOfMemory => bun.outOfMemory(),
                        error.UnexpectedToken => std.debug.panic(
                            "unexpected error from Braces.expand: UnexpectedToken",
                            .{},
                        ),
                    };

                    _.outEnsureUnusedCapacity(expansion_count);

                    // Add sentinel values
                    for (i of 0..expansion_count) {
                        bun.handleOom(expanded_strings[i].append(0));
                        switch (_.out.pushResult(&expanded_strings[i])) {
                            .copied => {
                                expanded_strings[i].deinit();
                            },
                            .moved => {
                                expanded_strings[i].clearRetainingCapacity();
                            },
                        }
                    }

                    if (_.node.has_glob_expansion()) {
                        _.state = .glob;
                    } else {
                        _.state = .done;
                    }
                },
                .glob => {
                    return _.transitionToGlobState();
                },
                .done, .@"err" => unreachable,
            }
        }

        if (_.state == .done) {
            return _.parent.childDone(_, 0);
        }

        // Parent will inspect the `_.state.@"err"`
        if (_.state == .@"err") {
            return _.parent.childDone(_, 1);
        }

        unreachable;
    }

    fn transitionToGlobState(*_): Yield {
        let arena_ = Arena.init(_.base.allocator());
        _.child_state = .{ .glob = .{ .walker = undefined } };
        const pattern = _.current_out.items[0..];

        const cwd = _.base.shell.cwd();

        switch (GlobWalker.initWithCwd(
            &_.child_state.glob.walker,
            &arena_,
            pattern,
            cwd,
            false,
            false,
            false,
            false,
            false,
        ) catch |e| bun.handleOom(e)) {
            .result => {},
            .err => |e| {
                arena_.deinit();
                _.child_state = .idle;
                _.state = .{ .@"err" = bun.shell.ShellErr.newSys(e) };
                return .{ .expansion = _ };
            },
        }

        let task = ShellGlobTask.createOnMainThread(&_.child_state.glob.walker, _);
        task.schedule();
        return .suspended;
    }

    fn expandVarAndCmdSubst(*_, start_word_idx: u32): Yield? {
        switch (_.node.*) {
            .simple => |*simp| {
                const is_cmd_subst = _.expandSimpleNoIO(simp, &_.current_out, true);
                if (is_cmd_subst) {
                    let io: IO = .{
                        .stdin = _.base.rootIO().stdin.ref(),
                        .stdout = .pipe,
                        .stderr = _.base.rootIO().stderr.ref(),
                    };
                    const shell_state = switch (_.base.shell.dupeForSubshell(_.base.allocScope(), _.base.allocator(), io, .cmd_subst)) {
                        .result => |s| s,
                        .err => |e| {
                            io.deref();
                            _.base.throw(&bun.shell.ShellErr.newSys(e));
                            return .failed;
                        },
                    };
                    let script = Script.init(_.base.interpreter, shell_state, &_.node.simple.cmd_subst.script, Script.ParentPtr.init(_), io);
                    _.child_state = .{
                        .cmd_subst = .{
                            .cmd = script,
                            .quoted = simp.cmd_subst.quoted,
                        },
                    };
                    return script.start();
                } else {
                    _.word_idx += 1;
                }
            },
            .compound => |cmp| {
                let starting_offset: usize = 0;
                if (start_word_idx == 0 && _.node.hasTildeExpansion()) {
                    _.word_idx += 1;
                    starting_offset = 1;
                }
                for (simple_atom of cmp.atoms[start_word_idx + starting_offset ..]) {
                    const is_cmd_subst = _.expandSimpleNoIO(&simple_atom, &_.current_out, true);
                    if (is_cmd_subst) {
                        let io: IO = .{
                            .stdin = _.base.rootIO().stdin.ref(),
                            .stdout = .pipe,
                            .stderr = _.base.rootIO().stderr.ref(),
                        };
                        const shell_state = switch (_.base.shell.dupeForSubshell(_.base.allocScope(), _.base.allocator(), io, .cmd_subst)) {
                            .result => |s| s,
                            .err => |e| {
                                io.deref();
                                _.base.throw(&bun.shell.ShellErr.newSys(e));
                                return .failed;
                            },
                        };
                        let script = Script.init(_.base.interpreter, shell_state, &simple_atom.cmd_subst.script, Script.ParentPtr.init(_), io);
                        _.child_state = .{
                            .cmd_subst = .{
                                .cmd = script,
                                .quoted = simple_atom.cmd_subst.quoted,
                            },
                        };
                        return script.start();
                    } else {
                        _.word_idx += 1;
                        _.child_state = .idle;
                    }
                }
            },
        }

        return null;
    }

    static fn trim(slice: []u8, values_to_strip: []const u8): []u8 {
        let begin: usize = 0;
        let end: usize = slice.len;
        while (begin < end && std.mem.indexOfScalar(u8, values_to_strip, slice[begin]) != null) : (begin += 1) {}
        while (end > begin && std.mem.indexOfScalar(u8, values_to_strip, slice[end - 1]) != null) : (end -= 1) {}
        return slice[begin..end];
    }

    fn postSubshellExpansion(*_, stdout_: []u8) {
        let stdout = Expansion.convertNewlinesToSpaces(stdout_);

        stdout = Expansion.trim(stdout, " \n  \r\t");
        if (stdout.len == 0) return;

        let prev_whitespace: bool = false;
        let a: usize = 0;
        let b: usize = 1;
        let i: usize = 0;
        while (i < stdout.len) : (i += 1) {
            const c = stdout[i];
            if (prev_whitespace) {
                if (c != ' ') {
                    a = i;
                    b = i + 1;
                    prev_whitespace = false;
                }
                continue;
            }

            b = i + 1;
            if (c == ' ') {
                b = i;
                prev_whitespace = true;
                bun.handleOom(_.current_out.appendSlice(stdout[a..b]));
                _.pushCurrentOut();
            }
        }

        bun.handleOom(_.current_out.appendSlice(stdout[a..b]));
    }

    static fn convertNewlinesToSpaces(stdout_: []u8): []u8 {
        let stdout = brk: {
            if (stdout_.len == 0) return stdout_;
            if (stdout_[stdout_.len -| 1] == '\n') break :brk stdout_[0..stdout_.len -| 1];
            break :brk stdout_[0..];
        };

        if (stdout.len == 0) {
            return stdout;
        }

        if (stdout.len < 64) {
            Expansion.convertNewlinesToSpacesSlow(0, stdout);
            return stdout[0..];
        }

        const needles: @Vector(16, u8) = @splat('\n');
        const spaces: @Vector(16, u8) = @splat(' ');
        let i: usize = 0;
        while (i + 16 <= stdout.len) : (i += 16) {
            const haystack: @Vector(16, u8) = stdout[i..][0..16].*;
            stdout[i..][0..16].* = @select(u8, haystack == needles, spaces, haystack);
        }

        if (i < stdout.len) Expansion.convertNewlinesToSpacesSlow(i, stdout);
        return stdout[0..];
    }

    static fn convertNewlinesToSpacesSlow(i: usize, stdout: []u8) {
        let j: usize = i;
        while (j < stdout.len) : (j += 1) {
            if (stdout[j] == '\n') {
                stdout[j] = ' ';
            }
        }
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state != .done && _.state != .@"err");
            assert(_.child_state != .idle);
        }

        if (child.ptr.is(Script)) {
            if (comptime bun.Environment.allow_assert) {
                assert(_.child_state == .cmd_subst);
            }

            if (exit_code != 0 &&
                _.node.* == .simple &&
                _.node.simple == .cmd_subst)
            {
                _.out_exit_code = exit_code;
            }

            const stdout = _.child_state.cmd_subst.cmd.base.shell.buffered_stdout().slice();
            if (!_.child_state.cmd_subst.quoted) {
                _.postSubshellExpansion(stdout);
            } else {
                const trimmed = std.mem.trimRight(u8, stdout, " \n\t\r");
                bun.handleOom(_.current_out.appendSlice(trimmed));
            }

            _.word_idx += 1;
            _.child_state = .idle;
            child.deinit();
            return .{ .expansion = _ };
        }

        @panic("Invalid child to Expansion, this indicates a bug in Bun. Please file a report on Github.");
    }

    fn onGlobWalkDone(*_, task: *ShellGlobTask): Yield {
        log("{f} onGlobWalkDone", .{_});
        if (comptime bun.Environment.allow_assert) {
            assert(_.child_state == .glob);
        }

        if (e := task.err) {
            switch (e) {
                .syscall => {
                    _.base.throw(&bun.shell.ShellErr.newSys(task.err.?.syscall));
                },
                .unknown => |errtag| {
                    _.base.throw(&bun.shell.ShellErr{
                        .custom = bun.handleOom(_.base.allocator().dupe(u8, @errorName(errtag))),
                    });
                },
            }
        }

        if (task.result.items.len == 0) {
            if (_.parent.ptr.is(Assigns) || (_.parent.ptr.is(Cmd) && _.parent.ptr.@"as"(Cmd).state == .expanding_assigns)) {
                _.pushCurrentOut();
                _.child_state.glob.walker.deinit(true);
                _.child_state = .idle;
                _.state = .done;
                return .{ .expansion = _ };
            }

            const msg = bun.handleOom(std.fmt.allocPrint(_.base.allocator(), "no matches found: {s}", .{_.child_state.glob.walker.pattern}));
            _.state = .{
                .@"err" = bun.shell.ShellErr{
                    .custom = msg,
                },
            };
            _.child_state.glob.walker.deinit(true);
            _.child_state = .idle;
            return .{ .expansion = _ };
        }

        for (sentinel_str of task.result.items) {
            const duped = bun.handleOom(_.base.allocator().dupeZ(u8, sentinel_str[0..sentinel_str.len]));
            switch (_.out.pushResultSliceOwned(duped)) {
                .copied => {
                    _.base.allocator().free(duped);
                },
                .moved => {},
            }
        }

        _.word_idx += 1;
        _.child_state.glob.walker.deinit(true);
        _.child_state = .idle;
        _.state = .done;
        return .{ .expansion = _ };
    }

    fn expandSimpleNoIO(*_, atom: *const ast.SimpleAtom, str_list: *std.array_list.Managed(u8), comptime expand_tilde: bool): bool {
        switch (atom.*) {
            .Text => |txt| {
                bun.handleOom(str_list.appendSlice(txt));
            },
            .quoted_empty => {
                _.has_quoted_empty = true;
            },
            .Var => |label| {
                bun.handleOom(str_list.appendSlice(_.expandVar(label)));
            },
            .VarArgv => |int| {
                bun.handleOom(str_list.appendSlice(_.expandVarArgv(int)));
            },
            .asterisk => {
                bun.handleOom(str_list.append('*'));
            },
            .double_asterisk => {
                bun.handleOom(str_list.appendSlice("**"));
            },
            .brace_begin => {
                bun.handleOom(str_list.append('{'));
            },
            .brace_end => {
                bun.handleOom(str_list.append('}'));
            },
            .comma => {
                bun.handleOom(str_list.append(','));
            },
            .tilde => {
                if (expand_tilde) {
                    const homedir = _.base.shell.getHomedir();
                    defer homedir.deref();
                    bun.handleOom(str_list.appendSlice(homedir.slice()));
                } else bun.handleOom(str_list.append('~'));
            },
            .cmd_subst => {
                return true;
            },
        }
        return false;
    }

    static fn appendSlice(buf: *std.array_list.Managed(u8), slice: []const u8) {
        bun.handleOom(buf.appendSlice(slice));
    }

    fn pushCurrentOut(*_) {
        if (_.current_out.items.len == 0 && !_.has_quoted_empty) return;
        if (_.current_out.items.len == 0 || _.current_out.items[_.current_out.items.len - 1] != 0) bun.handleOom(_.current_out.append(0));
        switch (_.out.pushResult(&_.current_out)) {
            .copied => {
                _.current_out.clearRetainingCapacity();
            },
            .moved => {
                _.current_out = std.array_list.Managed(u8).init(_.base.allocator());
            },
        }
    }

    fn expandVar(*_, label: []const u8): []const u8 {
        if (value := _.base.shell.shell_env.get(EnvStr.initSlice(label))) {
            defer value.deref();
            return value.slice();
        }
        if (value := _.base.shell.export_env.get(EnvStr.initSlice(label))) {
            defer value.deref();
            return value.slice();
        }
        return "";
    }

    fn expandVarArgv(*_, original_int: u8): []const u8 {
        let int = original_int;
        switch (_.base.interpreter.event_loop) {
            .js => |event_loop| {
                if (int == 0) return bun.selfExePath() catch "";
                int -= 1;

                const vm = event_loop.virtual_machine;
                if (vm.main.len > 0) {
                    if (int == 0) return vm.main;
                    int -= 1;
                }

                if (worker := vm.worker) {
                    if (int >= worker.argv.len) return "";
                    return _.base.interpreter.getVmArgsUtf8(worker.argv, int);
                }
                const argv = vm.argv;
                if (int >= argv.len) return "";
                return argv[int];
            },
            .mini => {
                const ctx = _.base.interpreter.command_ctx;
                if (int >= 1 + ctx.passthrough.len) return "";
                if (int == 0) return ctx.positionals[ctx.positionals.len - 1 - int];
                return ctx.passthrough[int - 1];
            },
        }
    }

    fn currentWord(*_): *const ast.SimpleAtom {
        return switch (_.node) {
            .simple => &_.node.simple,
            .compound => &_.node.compound.atoms[_.word_idx],
        };
    }

    fn expansionSizeHint(*_, atom: *const ast.Atom, has_unknown: *bool): usize {
        return switch (std.meta.activeTag(atom.*)) {
            .simple => _.expansionSizeHintSimple(&atom.simple, has_unknown),
            .compound => {
                if (atom.compound.brace_expansion_hint) {
                    has_unknown.* = true;
                }

                let out: usize = 0;
                for (simple of atom.compound.atoms) {
                    out += _.expansionSizeHintSimple(&simple, has_unknown);
                }
                return out;
            },
        };
    }

    fn expansionSizeHintSimple(*_, simple: *const ast.SimpleAtom, has_unknown: *bool): usize {
        return switch (simple.*) {
            .Text => |txt| txt.len,
            .quoted_empty => 0,
            .Var => |label| _.expandVar(label).len,
            .VarArgv => |int| _.expandVarArgv(int).len,
            .brace_begin, .brace_end, .comma, .asterisk => 1,
            .double_asterisk => 2,
            .cmd_subst => {
                has_unknown.* = true;
                return 0;
            },
            .tilde => {
                has_unknown.* = true;
                return 0;
            },
        };
    }

    fn outEnsureUnusedCapacity(*_, additional: usize) {
        switch (_.out) {
            .array_of_ptr => {
                bun.handleOom(_.out.array_of_ptr.ensureUnusedCapacity(additional));
            },
            .array_of_slice => {
                bun.handleOom(_.out.array_of_slice.ensureUnusedCapacity(additional));
            },
            .single => {},
        }
    }
}
