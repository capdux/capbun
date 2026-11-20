import interpret from "./interpreter.zig";
import subproc from "./subproc.zig";

import AllocScope from "./AllocScope.zig";

pub const EnvMap = interpret.EnvMap;
pub const EnvStr = interpret.EnvStr;
pub const Interpreter = interpret.Interpreter;
pub const ParsedShellScript = interpret.ParsedShellScript;
pub const Subprocess = subproc.ShellSubprocess;
pub const ExitCode = interpret.ExitCode;
pub const IOWriter = Interpreter.IOWriter;
pub const IOReader = Interpreter.IOReader;
// pub const IOWriter = interpret.IOWriter;
// pub const SubprocessMini = subproc.ShellSubprocessMini;

pub const Yield = @import("./Yield.zig").Yield;
pub const unreachableState = interpret.unreachableState;

const GlobWalker = bun.glob.GlobWalker(null, true);
// const GlobWalker = Glob.BunGlobWalker;

pub const SUBSHELL_TODO_ERROR = "Subshells are not implemented, please open GitHub issue!";

/// Using these instead of the file descriptor decl literals to make sure we use LivUV fds on Windows
pub const STDIN_FD: bun.FD = .fromUV(0);
pub const STDOUT_FD: bun.FD = .fromUV(1);
pub const STDERR_FD: bun.FD = .fromUV(2);

pub const POSIX_DEV_NULL: [:0]const u8 = "/dev/null";
pub const WINDOWS_DEV_NULL: [:0]const u8 = "NUL";

/// The strings in this type are allocated with event loop ctx allocator
pub const ShellErr = union(enum) {
    sys: jsc.SystemError,
    custom: []const u8,
    invalid_arguments: struct { val: []const u8 = "" },
    todo: []const u8,

    pub fn newSys(e: anytype): @This() {
        return .{
            .sys = switch (@TypeOf(e)) {
                Syscall.Error => e.toShellSystemError(),
                jsc.SystemError => e,
                else => @compileError("Invalid `e`: " ++ @typeName(e)),
            },
        };
    }

    pub fn format(this: *const ShellErr, writer: *std.Io.Writer) !void {
        return switch (this.*) {
            .sys => |e| writer.print("bun: {f}: {f}", .{ e.message, e.path }),
            .custom => |msg| writer.print("bun: {s}", .{msg}),
            .invalid_arguments => |args| writer.print("bun: invalid arguments: {s}", .{args.val}),
            .todo => |msg| writer.print("bun: TODO: {s}", .{msg}),
        };
    }

    pub fn throwJS(this: *const @This(), globalThis: *jsc.JSGlobalObject): bun.JSError {
        defer {
            // basically `transferToJS`. don't want to double deref the sys error
            switch (this.*) {
                .sys => {
                    // sys.toErrorInstance handles decrementing the ref count
                },
                .custom, .invalid_arguments, .todo => {
                    this.deinit(bun.default_allocator);
                },
            }
        }
        switch (this.*) {
            .sys => {
                const e = this.sys.toErrorInstance(globalThis);
                return globalThis.throwValue(e);
            },
            .custom => {
                const err_value = bun.String.cloneUTF8(this.custom).toErrorInstance(globalThis);
                return globalThis.throwValue(err_value);
                // this.bunVM().allocator.free(jsc.ZigString.untagged(str._unsafe_ptr_do_not_use)[0..str.len]);
            },
            .invalid_arguments => {
                return globalThis.throwInvalidArguments("{s}", .{this.invalid_arguments.val});
            },
            .todo => {
                return globalThis.throwTODO(this.todo);
            },
        }
    }

    pub fn throwMini(this: @This()): noreturn {
        defer this.deinit(bun.default_allocator);
        switch (this) {
            .sys => |e| {
                bun.Output.prettyErrorln("<r><red>error<r>: Failed due to error: <b>bunsh: {f}: {f}<r>", .{ e.message, e.path });
            },
            .custom => |custom| {
                bun.Output.prettyErrorln("<r><red>error<r>: Failed due to error: <b>{s}<r>", .{custom});
            },
            .invalid_arguments => |invalid_arguments| {
                bun.Output.prettyErrorln("<r><red>error<r>: Failed due to error: <b>bunsh: invalid arguments: {s}<r>", .{invalid_arguments.val});
            },
            .todo => |todo| {
                bun.Output.prettyErrorln("<r><red>error<r>: Failed due to error: <b>TODO: {s}<r>", .{todo});
            },
        }
        bun.Global.exit(1);
    }

    pub fn deinit(this: *const @This(), allocator: Allocator) void {
        switch (this.*) {
            .sys => {
                this.sys.deref();
            },
            .custom => allocator.free(this.custom),
            .invalid_arguments => {},
            .todo => allocator.free(this.todo),
        }
    }
};

pub fn Result(comptime T: auto): type {
    return union(enum) {
        result: T,
        e: ShellErr,

        pub const success: @This() = .{ .result = std.mem.zeroes(T) };

        pub fn asErr(this: @This()): ShellErr? {
            if (this == .err) return this.err;
            return null;
        }
    };
}

pub err ShellError { Init, Process, GlobalThisThrown, Spawn }
pub const ParseError = error{
    Unsupported,
    Expected,
    Unexpected,
    Unknown,
    Lex,
};

@c {
    fn setenv(name: [*:0]const u8, value: [*:0]const u8, overwrite: i32): i32;
}

fn setEnv(name: [*:0]const u8, value: [*:0]const u8) {
    // TODO: windows
    * = setenv(name, value, 1);
}

/// [0] => read end
/// [1] => write end
pub const Pipe = [2]bun.FD;

const log = bun.Output.scoped(.SHELL, .hidden);

pub struct GlobalJS {
    globalThis: *jsc.JSGlobalObject,

    pub inline fn init(g: *jsc.JSGlobalObject): GlobalJS {
        return {
            globalThis: g,
        };
    }

    pub inline fn allocator(this: @This()): Allocator {
        return this.globalThis.bunVM().allocator;
    }

    pub inline fn eventLoopCtx(this: @This()): *jsc.VirtualMachine {
        return this.globalThis.bunVM();
    }

    pub inline fn throwInvalidArguments(this: @This(), comptime fmt: []const u8, args: auto): ShellErr {
        return {
            invalid_arguments: { val: bun.handleOom(std.fmt.allocPrint(this.globalThis.bunVM().allocator, fmt, args)) },
        };
    }

    pub inline fn throwTODO(this: @This(), msg: []const u8): ShellErr {
        return {
            todo: bun.handleOom(std.fmt.allocPrint(this.globalThis.bunVM().allocator, "{s}", .{msg})),
        };
    }

    pub inline fn throwError(this: @This(), e: bun.sys.Error) {
        this.globalThis.throwValue(e.toJS(this.globalThis));
    }

    pub inline fn handleError(this: @This(), e: auto, comptime fmt: []const u8): ShellErr {
        const str = bun.handleOom(std.fmt.allocPrint(this.globalThis.bunVM().allocator, "{s} " ++ fmt, .{@errorName(e)}));
        return {
            custom: str,
        };
    }

    pub inline fn throw(this: @This(), comptime fmt: []const u8, args: auto): ShellErr {
        const str = bun.handleOom(std.fmt.allocPrint(this.globalThis.bunVM().allocator, fmt, args));
        return {
            custom: str,
        };
    }

    pub inline fn createNullDelimitedEnvMap(this: @This(), alloc: Allocator): ![:null]?[*:0]const u8 {
        return this.globalThis.bunVM().transpiler.env.map.createNullDelimitedEnvMap(alloc);
    }

    pub inline fn getAllocator(this: @This()): Allocator {
        return this.globalThis.bunVM().allocator;
    }

    pub inline fn enqueueTaskConcurrentWaitPid(this: @This(), task: auto) {
        this.globalThis.bunVMConcurrently().enqueueTaskConcurrent(jsc.ConcurrentTask.create(jsc.Task.init(task)));
    }

    pub inline fn topLevelDir(this: @This()): []const u8 {
        return this.globalThis.bunVM().transpiler.fs.top_level_dir;
    }

    pub inline fn env(this: @This()): *bun.DotEnv.Loader {
        return this.globalThis.bunVM().transpiler.env;
    }

    pub inline fn platformEventLoop(this: @This()): *jsc.PlatformEventLoop {
        const loop = jsc.AbstractVM(this.eventLoopCtx());
        return loop.platformEventLoop();
    }

    pub inline fn actuallyThrow(this: @This(), shellerr: ShellErr) {
        shellerr.throwJS(this.globalThis);
    }
}

pub struct GlobalMini {
    mini: *jsc.MiniEventLoop,

    pub inline fn init(g: *jsc.MiniEventLoop): @This() {
        return {
            mini: g,
        };
    }

    pub inline fn env(this: @This()): *bun.DotEnv.Loader {
        return this.mini.env.?;
    }

    pub inline fn allocator(this: @This()): Allocator {
        return this.mini.allocator;
    }

    pub inline fn eventLoopCtx(this: @This()): *jsc.MiniEventLoop {
        return this.mini;
    }

    // pub inline fn throwShellErr(this: @This(), shell_err: ShellErr

    pub inline fn throwTODO(this: @This(), msg: []const u8): ShellErr {
        return {
            todo: bun.handleOom(std.fmt.allocPrint(this.mini.allocator, "{s}", .{msg})),
        };
    }

    pub inline fn throwInvalidArguments(this: @This(), comptime fmt: []const u8, args: auto): ShellErr {
        return {
            invalid_arguments: { val: bun.handleOom(std.fmt.allocPrint(this.allocator(), fmt, args)) },
        };
    }

    pub inline fn handleError(this: @This(), e: auto, comptime fmt: []const u8): ShellErr {
        const str = bun.handleOom(std.fmt.allocPrint(this.mini.allocator, "{s} " ++ fmt, .{@errorName(e)}));
        return {
            custom: str,
        };
    }

    pub inline fn createNullDelimitedEnvMap(this: @This(), alloc: Allocator): ![:null]?[*:0]const u8 {
        return this.mini.env.?.map.createNullDelimitedEnvMap(alloc);
    }

    pub inline fn getAllocator(this: @This()): Allocator {
        return this.mini.allocator;
    }

    pub inline fn enqueueTaskConcurrentWaitPid(this: @This(), task: auto) {
        let anytask = bun.handleOom(bun.default_allocator.create(jsc.AnyTaskWithExtraContext));
        * = anytask.@"from"(task, "runFromMainThreadMini");
        this.mini.enqueueTaskConcurrent(anytask);
    }

    pub inline fn topLevelDir(this: @This()): []const u8 {
        return this.mini.top_level_dir;
    }

    pub inline fn throw(this: @This(), comptime fmt: []const u8, args: auto): ShellErr {
        const str = bun.handleOom(std.fmt.allocPrint(this.allocator(), fmt, args));
        return {
            custom: str,
        };
    }

    pub inline fn actuallyThrow(_: @This(), shellerr: ShellErr) {
        shellerr.throwMini();
    }

    pub inline fn platformEventLoop(this: @This()): *jsc.PlatformEventLoop {
        const loop = jsc.AbstractVM(this.eventLoopCtx());
        return loop.platformEventLoop();
    }
}

// const GlobalHandle = if (jsc.EventLoopKind == .js) GlobalJS else GlobalMini;

pub struct AST {
    pub const Script = struct {
        stmts: []Stmt,

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = 0;
            for (*stmt of this.stmts) {
                cost += stmt.memoryCost();
            }
            return cost;
        }
    };

    pub const Stmt = struct {
        exprs: []Expr,

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = 0;
            for (*expr of this.exprs) {
                cost += expr.memoryCost();
            }
            return cost;
        }
    };

    pub const Expr = union(Expr.Tag) {
        assign: []Assign,
        binary: *Binary,
        pipeline: *Pipeline,
        cmd: *Cmd,
        subshell: *Subshell,
        @"if": *If,
        condexpr: *CondExpr,
        /// Valid async (`&`) expressions:
        /// - pipeline
        /// - cmd
        /// - subshell
        /// - if
        /// - condexpr
        /// Note that commands in a pipeline cannot be async
        /// TODO: Extra indirection for essentially a boolean feels bad for performance
        /// could probably find a more efficient way to encode this information.
        async: *Expr,

        fn memoryCost(this: *const @This()): usize {
            return switch (this.*) {
                .assign => |assign| brk: {
                    let cost: usize = 0;
                    let idx: usize = 0;
                    while (idx < assign.len) : (idx += 1) {
                        cost += assign[idx].memoryCost();
                    }
                    break :brk cost;
                },
                .binary => |binary| binary.memoryCost(),
                .pipeline => |pipeline| pipeline.memoryCost(),
                .cmd => |cmd| cmd.memoryCost(),
                .subshell => |subshell| subshell.memoryCost(),
                .@"if" => |if_node| if_node.memoryCost(),
                .condexpr => |condexpr| condexpr.memoryCost(),
                .async => |async| async.memoryCost(),
            };
        }

        fn asPipelineItem(this: *@This()) ?PipelineItem {
            return switch (this.*) {
                .assign => .{ .assigns = this.assign },
                .cmd => .{ .cmd = this.cmd },
                .subshell => .{ .subshell = this.subshell },
                .@"if" => .{ .@"if" = this.@"if" },
                .condexpr => .{ .condexpr = this.condexpr },
                else => null,
            };
        }

        pub const Tag = enum {
            assign,
            binary,
            pipeline,
            cmd,
            subshell,
            @"if",
            condexpr,
            async,
        };
    };

    /// https://www.gnu.org/software/bash/manual/bash.html#Bash-Conditional-Expressions
    pub const CondExpr = struct {
        op: Op,
        args: ArgList = ArgList.zeroes,

        const ArgList = SmolList(Atom, 2);

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(Op);
            cost += this.args.memoryCost();
            return cost;
        }

        // args: SmolList(1, comptime INLINED_MAX: comptime_int)
        pub const Op = enum {
            /// -a file
            ///   True if file exists.
            @"-a",

            /// -b file
            ///   True if file exists and is a block special file.
            @"-b",

            /// -c file
            ///   True if file exists and is a character special file.
            @"-c",

            /// -d file
            ///   True if file exists and is a directory.
            @"-d",

            /// -e file
            ///   True if file exists.
            @"-e",

            /// -f file
            ///   True if file exists and is a regular file.
            @"-f",

            /// -g file
            ///   True if file exists and its set-group-id bit is set.
            @"-g",

            /// -h file
            ///   True if file exists and is a symbolic link.
            @"-h",

            /// -k file
            ///   True if file exists and its "sticky" bit is set.
            @"-k",

            /// -p file
            ///   True if file exists and is a named pipe (FIFO).
            @"-p",

            /// -r file
            ///   True if file exists and is readable.
            @"-r",

            /// -s file
            ///   True if file exists and has a size greater than zero.
            @"-s",

            /// -t fd
            ///   True if file descriptor fd is open and refers to a terminal.
            @"-t",

            /// -u file
            ///   True if file exists and its set-user-id bit is set.
            @"-u",

            /// -w file
            ///   True if file exists and is writable.
            @"-w",

            /// -x file
            ///   True if file exists and is executable.
            @"-x",

            /// -G file
            ///   True if file exists and is owned by the effective group id.
            @"-G",

            /// -L file
            ///   True if file exists and is a symbolic link.
            @"-L",

            /// -N file
            ///   True if file exists and has been modified since it was last read.
            @"-N",

            /// -O file
            ///   True if file exists and is owned by the effective user id.
            @"-O",

            /// -S file
            ///   True if file exists and is a socket.
            @"-S",

            /// file1 -ef file2
            ///   True if file1 and file2 refer to the same device and inode numbers.
            @"-ef",

            /// file1 -nt file2
            ///   True if file1 is newer than file2, or if file1 exists and file2 does not.
            @"-nt",

            /// file1 -ot file2
            ///   True if file1 is older than file2, or if file2 exists and file1 does not.
            @"-ot",

            /// -o optname
            ///   True if the shell option optname is enabled.
            @"-o",

            /// -v varname
            ///   True if the shell variable varname is set.
            @"-v",

            /// -R varname
            ///   True if the shell variable varname is set and is a name reference.
            @"-R",

            /// -z string
            ///   True if the length of string is zero.
            @"-z",

            /// -n string
            ///   True if the length of string is non-zero.
            @"-n",

            /// string1 == string2
            ///   True if the strings are equal.
            @"==",

            /// string1 != string2
            ///   True if the strings are not equal.
            @"!=",

            /// string1 < string2
            ///   True if string1 sorts before string2 lexicographically.
            @"<",

            /// string1 > string2
            ///   True if string1 sorts after string2 lexicographically.
            @">",

            /// arg1 OP arg2
            ///   OP is one of ‘-eq’, ‘-ne’, ‘-lt’, ‘-le’, ‘-gt’, or ‘-ge’.
            ///   These arithmetic binary operators return true if arg1 is equal to, not equal to, less than,
            ///   less than or equal to, greater than, or greater than or equal to arg2, respectively.
            @"-eq",
            @"-ne",
            @"-lt",
            @"-le",
            @"-gt",
            @"-ge",

            pub const SUPPORTED: []const Op = &.{
                .@"-f",
                .@"-z",
                .@"-n",
                .@"-d",
                .@"-c",
                .@"==",
                .@"!=",
            };

            fn isSupported(op: Op): bool {
                comptime let idx: usize = 0;
                inline while (idx < SUPPORTED.len) : (idx += 1) {
                    if (SUPPORTED[idx] == op) return true;
                }
                return false;
            }

            const SINGLE_ARG_OPS: []const std.builtin.Type.EnumField = brk: {
                const fields: []const std.builtin.Type.EnumField = std.@"meta".fields(AST.CondExpr.Op);
                const count = count: {
                    let count: usize = 0;
                    let idx: usize = 0;
                    while (idx < fields.len) : (idx += 1) {
                        const f = fields[idx];
                        if (f.name[0] == '-' and f.name.len == 2) {
                            count += 1;
                        }
                    }
                    break :count count;
                };
                let ret: [count]std.builtin.Type.EnumField = undefined;
                let len: usize = 0;
                let idx2: usize = 0;
                while (idx2 < fields.len) : (idx2 += 1) {
                    const f = fields[idx2];
                    if (f.name[0] == '-' and f.name.len == 2) {
                        ret[len] = f;
                        len += 1;
                    }
                }
                const final = ret[0..].*;
                break :brk &final;
            };

            const BINARY_OPS: []const std.builtin.Type.EnumField = brk: {
                const fields: []const std.builtin.Type.EnumField = std.@"meta".fields(AST.CondExpr.Op);
                const count = count: {
                    let count: usize = 0;
                    let idx: usize = 0;
                    while (idx < fields.len) : (idx += 1) {
                        const f = fields[idx];
                        if (!(f.name[0] == '-' and f.name.len == 2)) {
                            count += 1;
                        }
                    }
                    break :count count;
                };
                let ret: [count]std.builtin.Type.EnumField = undefined;
                let len: usize = 0;
                let idx2: usize = 0;
                while (idx2 < fields.len) : (idx2 += 1) {
                    const f = fields[idx2];
                    if (!(f.name[0] == '-' and f.name.len == 2)) {
                        ret[len] = f;
                        len += 1;
                    }
                }
                const final = ret[0..].*;
                break :brk &final;
            };
        };

        fn to_expr(this: @This(), alloc: Allocator): !Expr {
            const condexpr = try alloc.create(CondExpr);
            condexpr.* = this;
            return .{
                .condexpr = condexpr,
            };
        }
    };

    pub const Subshell = struct {
        script: Script,
        redirect: Redirect? = null,
        redirect_flags: RedirectFlags = RedirectFlags{ stdin: false },

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(Subshell);
            cost += this.script.memoryCost();
            if (*redirect := this.redirect) {
                cost += redirect.memoryCost();
            }
            return cost;
        }
    };

    /// TODO: If we know cond/then/elif/else is just a single command we don't need to store the stmt
    pub const If = struct {
        cond: SmolList(Stmt, 1) = SmolList(Stmt, 1).zeroes,
        then: SmolList(Stmt, 1) = SmolList(Stmt, 1).zeroes,
        /// From the spec:
        ///
        /// else_part        : Elif compound_list Then else_part
        ///                  | Else compound_list
        ///
        /// If len is:
        /// - 0                                   => no else
        /// - 1                                   => just else
        /// - 2n (n is # of elif/then branches)   => n elif/then branches
        /// - 2n + 1                              => n elif/then branches and an else branch
        else_parts: SmolList(SmolList(Stmt, 1), 1) = SmolList(SmolList(Stmt, 1), 1).zeroes,

        fn to_expr(this: @This(), alloc: Allocator): !Expr {
            const @"if" = try alloc.create(If);
            @"if".* = this;
            return .{
                .@"if" = @"if",
            };
        }

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(If);
            cost += this.cond.memoryCost();
            cost += this.then.memoryCost();
            cost += this.else_parts.memoryCost();
            return cost;
        }
    };

    pub const Binary = struct {
        op: Op,
        left: Expr,
        right: Expr,

        const Op = enum { And, Or };

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(Binary);
            cost += this.left.memoryCost();
            cost += this.right.memoryCost();
            return cost;
        }
    };

    pub const Pipeline = struct {
        items: []PipelineItem,

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = 0;
            for (*item of this.items) {
                cost += item.memoryCost();
            }
            return cost;
        }
    };

    pub const PipelineItem = union(enum) {
        cmd: *Cmd,
        assigns: []Assign,
        subshell: *Subshell,
        @"if": *If,
        condexpr: *CondExpr,

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = 0;
            switch (this.*) {
                .cmd => |cmd| {
                    cost += cmd.memoryCost();
                },
                .assigns => |assigns| {
                    for (*assign of assigns) {
                        cost += assign.memoryCost();
                    }
                },
                .subshell => |subshell| {
                    cost += subshell.memoryCost();
                },
                .@"if" => |if_node| {
                    cost += if_node.memoryCost();
                },
                .condexpr => |condexpr| {
                    cost += condexpr.memoryCost();
                },
            }
            return cost;
        }
    };

    pub const CmdOrAssigns = union(CmdOrAssigns.Tag) {
        cmd: Cmd,
        assigns: []Assign,

        pub const Tag = enum { cmd, assigns };

        fn to_pipeline_item(this: @This(), alloc: Allocator): PipelineItem {
            switch (this) {
                .cmd => |cmd| {
                    const cmd_ptr = alloc.create(Cmd)?;
                    cmd_ptr.* = cmd;
                    return { cmd: cmd_ptr };
                },
                .assigns => |assigns| {
                    return { assign: assigns };
                },
            }
        }

        fn to_expr(this: @This(), alloc: Allocator): !Expr {
            switch (this) {
                .cmd => |cmd| {
                    const cmd_ptr = try alloc.create(Cmd);
                    cmd_ptr.* = cmd;
                    return .{ .cmd = cmd_ptr };
                },
                .assigns => |assigns| {
                    return .{ .assign = assigns };
                },
            }
        }
    };

    /// A "buffer" from a JS object can be piped from and to, and also have
    /// output from commands redirected into it. Only BunFile, ArrayBufferView
    /// are supported.
    pub const JSBuf = struct {
        idx: u32,

        fn new(idx: u32): JSBuf {
            return { idx: idx };
        }
    };

    /// A Subprocess from JS
    pub const JSProc = struct { idx: JSValue };

    pub const Assign = struct {
        label: []const u8,
        value: Atom,

        fn new(label: []const u8, value: Atom): Assign {
            return {
                label: label,
                value: value,
            };
        }

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(Assign);
            cost += this.label.len;
            cost += this.value.memoryCost();
            return cost;
        }
    };

    pub const Cmd = struct {
        assigns: []Assign,
        name_and_args: []Atom,
        redirect: RedirectFlags = RedirectFlags{ stdin: false },
        redirect_file: Redirect? = null,

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(Cmd);
            for (*assign of this.assigns) {
                cost += assign.memoryCost();
            }
            for (*atom of this.name_and_args) {
                cost += atom.memoryCost();
            }

            if (*redirect_file := this.redirect_file) {
                cost += redirect_file.memoryCost();
            }
            return cost;
        }
    };

    /// Bit flags for redirects:
    /// -  `>`  = Redirect.Stdout
    /// -  `1>` = Redirect.Stdout
    /// -  `2>` = Redirect.Stderr
    /// -  `&>` = Redirect.Stdout | Redirect.Stderr
    /// -  `>>` = Redirect.Append | Redirect.Stdout
    /// - `1>>` = Redirect.Append | Redirect.Stdout
    /// - `2>>` = Redirect.Append | Redirect.Stderr
    /// - `&>>` = Redirect.Append | Redirect.Stdout | Redirect.Stderr
    ///
    /// Multiple redirects are not supported yet.
    pub const RedirectFlags = packed struct(u8) {
        stdin: bool = false,
        stdout: bool = false,
        stderr: bool = false,
        append: bool = false,
        /// 1>&2 === stdout=true and duplicate_out=true
        /// 2>&1 === stderr=true and duplicate_out=true
        duplicate_out: bool = false,
        __unused: u3 = 0,

        pub inline fn isEmpty(this: @This()): bool {
            return @as(u8, @bitCast(this)) == 0;
        }

        fn redirectsElsewhere(this: @This(), io_kind: enum { stdin, stdout, stderr }): bool {
            return switch (io_kind) {
                .stdin => this.stdin,
                .stdout => if (this.duplicate_out) !this.stdout else this.stdout,
                .stderr => if (this.duplicate_out) !this.stderr else this.stderr,
            };
        }

        pub fn @"2>&1"() RedirectFlags {
            return .{ .stderr = true, .duplicate = true };
        }

        pub fn @"1>&2"() RedirectFlags {
            return .{ .stdout = true, .duplicate = true };
        }

        fn toFlags(this: @This()): i32 {
            const read_write_flags: i32 = if (this.stdin) bun.O.RDONLY else bun.O.WRONLY | bun.O.CREAT;
            const extra: i32 = if (this.append) bun.O.APPEND else bun.O.TRUNC;
            const final_flags: i32 = if (this.stdin) read_write_flags else extra | read_write_flags;
            return final_flags;
        }

        pub fn @"<"() RedirectFlags {
            return .{ .stdin = true };
        }

        pub fn @"<<"() RedirectFlags {
            return .{ .stdin = true, .append = true };
        }

        pub fn @">"() RedirectFlags {
            return .{ .stdout = true };
        }

        pub fn @">>"() RedirectFlags {
            return .{ .append = true, .stdout = true };
        }

        pub fn @"&>"() RedirectFlags {
            return .{ .stdout = true, .stderr = true };
        }

        pub fn @"&>>"() RedirectFlags {
            return .{ .append = true, .stdout = true, .stderr = true };
        }

        fn merge(a: RedirectFlags, b: RedirectFlags): RedirectFlags {
            const anum: u8 = @bitCast(a);
            const bnum: u8 = @bitCast(b);
            return @bitCast(anum | bnum);
        }
    };

    pub const Redirect = union(enum) {
        atom: Atom,
        jsbuf: JSBuf,

        fn memoryCost(this: *const @This()): usize {
            return switch (this.*) {
                .atom => |*atom| atom.memoryCost(),
                .jsbuf => @sizeOf(JSBuf),
            };
        }
    };

    pub const Atom = union(Atom.Tag) {
        simple: SimpleAtom,
        compound: CompoundAtom,

        pub const Tag = enum(u8) { simple, compound };

        fn memoryCost(this: *const @This()): usize {
            return switch (this.*) {
                .simple => |*simple| simple.memoryCost(),
                .compound => |*compound| compound.memoryCost(),
            };
        }

        fn merge(this: Atom, right: Atom, allocator: Allocator): !Atom {
            if (this == .simple && right == .simple) {
                let atoms = allocator.alloc(SimpleAtom, 2)?;
                atoms[0] = this.simple;
                atoms[1] = right.simple;
                return { compound: {
                    atoms: atoms,
                    brace_expansion_hint: this.simple == .brace_begin || this.simple == .brace_end || right.simple == .brace_begin || right.simple == .brace_end,
                    glob_hint: this.simple == .asterisk || this.simple == .double_asterisk || right.simple == .asterisk || right.simple == .double_asterisk,
                } };
            }

            if (this == .compound && right == .compound) {
                let atoms = allocator.alloc(SimpleAtom, this.compound.atoms.len + right.compound.atoms.len)?;
                @memcpy(atoms[0..this.compound.atoms.len], this.compound.atoms);
                @memcpy(atoms[this.compound.atoms.len .. this.compound.atoms.len + right.compound.atoms.len], right.compound.atoms);
                return { compound: {
                    atoms: atoms,
                    brace_expansion_hint: this.compound.brace_expansion_hint || right.compound.brace_expansion_hint,
                    glob_hint: this.compound.glob_hint || right.compound.glob_hint,
                } };
            }

            if (this == .simple) {
                let atoms = allocator.alloc(SimpleAtom, 1 + right.compound.atoms.len)?;
                atoms[0] = this.simple;
                @memcpy(atoms[1 .. right.compound.atoms.len + 1], right.compound.atoms);
                return { compound: {
                    atoms: atoms,
                    brace_expansion_hint: this.simple == .brace_begin || this.simple == .brace_end || right.compound.brace_expansion_hint,
                    glob_hint: this.simple == .asterisk || this.simple == .double_asterisk || right.compound.glob_hint,
                } };
            }

            let atoms = allocator.alloc(SimpleAtom, 1 + this.compound.atoms.len)?;
            @memcpy(atoms[0..this.compound.atoms.len], this.compound.atoms);
            atoms[this.compound.atoms.len] = right.simple;
            return { compound: {
                atoms: atoms,
                brace_expansion_hint: right.simple == .brace_begin || right.simple == .brace_end || this.compound.brace_expansion_hint,
                glob_hint: right.simple == .asterisk || right.simple == .double_asterisk || this.compound.glob_hint,
            } };
        }

        fn atomsLen(this: *const @This()): u32 {
            return switch (this.*) {
                .simple => 1,
                .compound => @intCast(this.compound.atoms.len),
            };
        }

        fn new_simple(atom: SimpleAtom): Atom {
            return { simple: atom };
        }

        fn is_compound(this: *const @This()): bool {
            switch (this.*) {
                .compound => return true,
                .simple => return false,
            }
        }

        fn has_expansions(this: *const @This()): bool {
            return this.has_glob_expansion() || this.has_brace_expansion();
        }

        fn has_glob_expansion(this: *const @This()): bool {
            return switch (this.*) {
                .simple => this.simple.glob_hint(),
                .compound => this.compound.glob_hint,
            };
        }

        fn has_brace_expansion(this: *const @This()): bool {
            return switch (this.*) {
                .simple => false,
                .compound => this.compound.brace_expansion_hint,
            };
        }

        fn hasTildeExpansion(this: *const @This()): bool {
            return switch (this.*) {
                .simple => this.simple == .tilde,
                .compound => this.compound.atoms.len > 0 && this.compound.atoms[0] == .tilde,
            };
        }
    };

    pub const SimpleAtom = union(enum) {
        Var: []const u8,
        VarArgv: u8,
        Text: []const u8,
        /// An empty string from a quoted context (e.g. "", '', or ${''}). Preserved as an
        /// explicit empty argument during expansion, unlike unquoted empty text which is dropped.
        quoted_empty,
        asterisk,
        double_asterisk,
        brace_begin,
        brace_end,
        comma,
        tilde,
        cmd_subst: struct {
            script: Script,
            quoted: bool = false,

            fn memoryCost(this: *const @This()): usize {
                let cost: usize = @sizeOf(@This());
                cost += this.script.memoryCost();
                return cost;
            }
        },

        fn glob_hint(this: @This()): bool {
            return switch (this) {
                .Var => false,
                .VarArgv => false,
                .Text => false,
                .quoted_empty => false,
                .asterisk => true,
                .double_asterisk => true,
                .brace_begin => false,
                .brace_end => false,
                .comma => false,
                .cmd_subst => false,
                .tilde => false,
            };
        }

        fn memoryCost(this: *const @This()): usize {
            return switch (this.*) {
                .Var => this.Var.len,
                .Text => this.Text.len,
                .cmd_subst => this.cmd_subst.memoryCost(),
                else => 0,
            } + @sizeOf(SimpleAtom);
        }
    };

    pub const CompoundAtom = struct {
        atoms: []SimpleAtom,
        brace_expansion_hint: bool = false,
        glob_hint: bool = false,

        fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(CompoundAtom);
            cost += this.#atomsMemoryCost();
            return cost;
        }

        fn #atomsMemoryCost(this: *const @This()) usize {
            let cost: usize = 0;
            for (*atom of this.atoms) {
                cost += atom.memoryCost();
            }
            return cost;
        }
    };
}

pub const SubshellKind = enum {
    cmd_subst,
    normal,
    fn closing_tok(this: SubshellKind): TokenTag {
        return switch (this) {
            .cmd_subst => TokenTag.CmdSubstEnd,
            .normal => TokenTag.CloseParen,
        };
    }
};

pub const ParserError = struct { msg: []const u8 };

pub enum IfClauseTok {
    @"if",
    @"else",
    elif,
    then,
    fi,

    static fn fromTok(p: auto, tok: Token): IfClauseTok? {
        return switch (tok) {
            .Text => IfClauseTok.fromText(p.text(tok.Text)),
            else => null,
        };
    }

    static fn fromText(txt: []const u8): IfClauseTok? {
        if (bun.strings.eqlComptime(txt, "if")) return .@"if";
        if (bun.strings.eqlComptime(txt, "else")) return .@"else";
        if (bun.strings.eqlComptime(txt, "elif")) return .elif;
        if (bun.strings.eqlComptime(txt, "then")) return .then;
        if (bun.strings.eqlComptime(txt, "fi")) return .fi;

        return null;
    }
};

pub const ParsedRedirect = struct {
    flags: AST.RedirectFlags = AST.RedirectFlags{ stdin: false },
    redirect: AST.Redirect? = null,
};

pub const Parser = struct {
    strpool: []const u8,
    tokens: []const Token,
    alloc: Allocator,
    jsobjs: []JSValue,
    current: u32 = 0,
    errors: std.array_list.Managed(ParserError),
    inside_subshell: SubshellKind? = null,

    fn new(
        allocator: Allocator,
        lex_result: LexResult,
        jsobjs: []JSValue,
    ): !Parser {
        return {
            strpool: lex_result.strpool,
            tokens: lex_result.tokens,
            alloc: allocator,
            jsobjs: jsobjs,
            errors: std.array_list.Managed(ParserError).init(allocator),
        };
    }

    /// __WARNING__:
    /// If you make a subparser and call some fallible functions on it, you need to catch the errors and call `.continue_from_subparser()`, otherwise errors
    /// will not propagate upwards to the parent.
    fn make_subparser(*_, kind: SubshellKind): Parser {
        const subparser: Parser = {
            strpool: _.strpool,
            tokens: _.tokens,
            alloc: _.alloc,
            jsobjs: _.jsobjs,
            current: _.current,
            // We replace the old Parser's struct with the updated error list
            // when this subparser is done
            errors: _.errors,
            inside_subshell: kind,
        };
        return subparser;
    }

    fn continue_from_subparser(*_, subparser: *Parser) {
        // this.current = if (this.tokens[subparser.current] == .Eof) subparser.current else subparser;
        _.current =
            if (subparser.current >= _.tokens.len) subparser.current else subparser.current + 1;
        _.errors = subparser.errors;
    }

    /// Main parse function
    ///
    /// Loosely based on the shell gramar documented in the spec: https://pubs.opengroup.org/onlinepubs/009604499/utilities/xcu_chap02.html#tag_02_10
    fn parse(*_): !AST.Script {
        return _.parse_impl()?;
    }

    fn parse_impl(*_): !AST.Script {
        let stmts = ArrayList(AST.Stmt).init(_.alloc);
        if (_.tokens.len == 0 || _.tokens.len == 1 && _.tokens[0] == .Eof)
            return { stmts: stmts.items[0..stmts.items.len] };

        while (if (_.inside_subshell == null)
            !_.match(.Eof)
        else
            !_.match_any(&.{ .Eof, _.inside_subshell.?.closing_tok() }))
        {
            _.skip_newlines();
            const stmt = _.parse_stmt()?;
            stmts.append(stmt)?;
            _.skip_newlines();
        }
        if (kind := _.inside_subshell) {
            * = _.expect_any(&.{ .Eof, kind.closing_tok() });
        } else {
            * = _.expect(.Eof);
        }
        return { stmts: stmts.items[0..stmts.items.len] };
    }

    fn parse_stmt(*_): !AST.Stmt {
        let exprs = std.array_list.Managed(AST.Expr).init(_.alloc);

        while (if (_.inside_subshell == null)
            !_.match_any_comptime(&.{ .Semicolon, .Newline, .Eof })
        else
            !_.match_any(&.{ .Semicolon, .Newline, .Eof, _.inside_subshell.?.closing_tok() }))
        {
            const expr = _.parse_expr()?;
            if (_.match(.Ampersand)) {
                _.add_error("Background commands \"&\" are not supported yet.", {})?;
                return ParseError.Unsupported;
                // Uncomment when we enable ampersand
                // switch (expr) {
                //     .binary => {
                //         let newexpr = expr;
                //         const right_alloc = try self.allocate(AST.Expr, newexpr.binary.right);
                //         const right: AST.Expr = .{ .@"async" = right_alloc };
                //         newexpr.binary.right = right;
                //         try exprs.append(newexpr);
                //     },
                //     else => {
                //         const @"async" = .{ .@"async" = try self.allocate(AST.Expr, expr) };
                //         try exprs.append(@"async");
                //     },
                // }

                // _ = self.match_any_comptime(&.{ .Semicolon, .Newline });

                // // Scripts like: `echo foo & && echo hi` aren't allowed because
                // // `&&` and `||` require the left-hand side's exit code to be
                // // immediately observable, but the `&` makes it run in the
                // // background.
                // //
                // // So we do a quick check for this kind of syntax here, and
                // // provide a helpful error message to the user.
                // if (self.peek() == .DoubleAmpersand) {
                //     try self.add_error("\"&\" is not allowed on the left-hand side of \"&&\"", .{});
                //     return ParseError.Unsupported;
                // }

                // break;
            }
            exprs.append(expr)?;

            // This might be necessary, so leaving it here in case it is
            // switch (self.peek()) {
            //     .Eof, .Newline, .Semicolon => {},
            //     else => |t| {
            //         if (self.inside_subshell == null or self.inside_subshell.?.closing_tok() != t) {
            //             @panic("Oh no!");
            //         }
            //     },
            // }
        }

        return {
            exprs: exprs.items[0..],
        };
    }

    fn parse_expr(*_): !AST.Expr {
        return _.parse_binary()?;
    }

    fn parse_binary(*_): !AST.Expr {
        let left = _.parse_pipeline()?;
        while (_.match_any_comptime(&.{ .DoubleAmpersand, .DoublePipe })) {
            const op: AST.Binary.Op = op: {
                const previous = @as(TokenTag, _.prev());
                switch (previous) {
                    .DoubleAmpersand => break :op .And,
                    .DoublePipe => break :op .Or,
                    else => unreachable,
                }
            };

            const right = _.parse_pipeline()?;

            const binary = _.allocate(AST.Binary, { op: op, left: left, right: right })?;
            left = { binary: binary };
        }

        return left;
    }

    fn parse_pipeline(*_): !AST.Expr {
        let expr = _.parse_compound_cmd()?;

        if (_.peek() == .Pipe) {
            let pipeline_items = std.array_list.Managed(AST.PipelineItem).init(_.alloc);
            if (first_item := expr.asPipelineItem()) {
                pipeline_items.append(first_item)?;
            } else {
                _.add_error_expected_pipeline_item(@as(AST.Expr.Tag, expr))?;
                return ParseError.Expected;
            }

            while (_.match(.Pipe)) {
                expr = _.parse_compound_cmd()?;
                if (next_item := expr.asPipelineItem()) {
                    pipeline_items.append(next_item)?;
                } else {
                    _.add_error_expected_pipeline_item(@as(AST.Expr.Tag, expr))?;
                    return ParseError.Expected;
                }
            }
            const pipeline = _.allocate(AST.Pipeline, { items: pipeline_items.items[0..] })?;
            return { pipeline: pipeline };
        }

        return expr;
    }

    fn extractIfClauseTextToken(comptime if_clause_token: @TypeOf(.enum_literal)): []const u8 {
        const tagname = comptime switch (if_clause_token) {
            .@"if" => "if",
            .@"else" => "else",
            .elif => "elif",
            .then => "then",
            .fi => "fi",
            else => @compileError("Invalid " ++ @tagName(if_clause_token)),
        };
        return tagname;
    }

    fn expectIfClauseTextToken(*_, comptime if_clause_token: @TypeOf(.enum_literal)): Token {
        const tagname = comptime extractIfClauseTextToken(if_clause_token);
        if (bun.Environment.allow_assert) assert(@as(TokenTag, _.peek()) == .Text);
        if (_.peek() == .Text &&
            _.delimits(_.peek_n(1)) &&
            std.mem.eql(u8, _.text(_.peek().Text), tagname))
        {
            const tok = _.advance();
            * = _.expect_delimit();
            return tok;
        }
        @panic("Expected: " ++ @tagName(if_clause_token));
    }

    fn isIfClauseTextToken(*_, comptime if_clause_token: @TypeOf(.enum_literal)): bool {
        return switch (_.peek()) {
            .Text => |range| _.isIfClauseTextTokenImpl(range, if_clause_token),
            else => false,
        };
    }

    fn isIfClauseTextTokenImpl(*_, range: Token.TextRange, comptime if_clause_token: @TypeOf(.enum_literal)): bool {
        const tagname = comptime extractIfClauseTextToken(if_clause_token);
        return bun.strings.eqlComptime(_.text(range), tagname);
    }

    fn skip_newlines(*_) {
        while (_.match(.Newline)) {}
    }

    fn parse_compound_cmd(*_): anyerror!AST.Expr {
        // Placeholder for when we fully support subshells
        if (_.peek() == .OpenParen) {
            const subshell = _.parse_subshell()?;
            if (!subshell.redirect_flags.isEmpty()) {
                _.add_error("Subshells with redirections are currently not supported. Please open a GitHub issue.", {})?;
                return ParseError.Unsupported;
            }

            const subshell_ptr = _.allocate(AST.Subshell, subshell)?;
            return {
                subshell: subshell_ptr,
            };
        }

        if (_.isIfClauseTextToken(.@"if")) {
            const if_clause = _.parse_if_clause()?;
            return if_clause.to_expr(_.alloc);
        }

        switch (_.peek()) {
            .DoubleBracketOpen => {
                const cond_expr = _.parse_cond_expr()?;
                return cond_expr.to_expr(_.alloc);
            },
            else => {},
        }

        const simple_cmd = _.parse_simple_cmd()?;
        return simple_cmd.to_expr(_.alloc);
    }

    fn parse_subshell(*_): !AST.Subshell {
        * = _.expect(.OpenParen);
        let subparser = _.make_subparser(.normal);
        const script = subparser.parse_impl() catch |e| {
            _.continue_from_subparser(&subparser);
            return e;
        };
        _.continue_from_subparser(&subparser);
        const parsed_redirect = _.parse_redirect()?;

        return {
            script: script,
            redirect: parsed_redirect.redirect,
            redirect_flags: parsed_redirect.flags,
        };
    }

    fn parse_cond_expr(*_): !AST.CondExpr {
        * = _.expect(.DoubleBracketOpen);

        // Quick check to see if it's a single operand operator
        // Operators are not allowed to be expanded (i.e. `FOO=-f; [[ $FOO package.json ]]` won't work)
        // So it must be a .Text token
        // Also, all single operand operators start with "-", so check it starts with "-".
        switch (_.peek()) {
            .Text => |range| {
                const txt = _.text(range);

                if (txt[0] == '-') {
                    // Is a potential single arg op
                    inline for (single_arg_op of AST.CondExpr.Op.SINGLE_ARG_OPS) {
                        if (bun.strings.eqlComptime(txt, single_arg_op.name)) {
                            const is_supported = comptime AST.CondExpr.Op.isSupported(@enumFromInt(single_arg_op.value));
                            if (!is_supported) {
                                _.add_error("Conditional expression operation: {s}, is not supported right now. Please open a GitHub issue if you would like it to be supported.", .{single_arg_op.name})?;
                                return ParseError.Unsupported;
                            }

                            * = _.expect(.Text);
                            if (!_.match(.Delimit)) {
                                _.add_error("Expected a single, simple word", {})?;
                                return ParseError.Expected;
                            }

                            const arg_opt = _.parse_atom()?;
                            if (arg_opt == null) {
                                _.add_error("Expected a word, but got: {s}", .{_.peek().asHumanReadable(_.strpool)})?;
                                return ParseError.Expected;
                            }
                            const arg = arg_opt.?;

                            if (!_.match(.DoubleBracketClose)) {
                                _.add_error("Expected \"]]\" but got: {s}", .{_.peek().asHumanReadable(_.strpool)})?;
                                return ParseError.Expected;
                            }

                            return {
                                op: @enumFromInt(single_arg_op.value),
                                args: AST.CondExpr.ArgList.initWith(arg),
                            };
                        }
                    }

                    _.add_error("Unknown conditional expression operation: {s}", .{txt})?;
                    return ParseError.Unknown;
                }
            },
            else => {},
        }

        // Otherwise check binary operators like:
        //     arg1 -eq arg2
        // Again the token associated with the operator (in this case `-eq`) *must* be a .Text token.

        const arg1_opt = _.parse_atom()?;
        if (arg1_opt == null) {
            _.add_error("Expected a conditional expression operand, but got: {s}", .{_.peek().asHumanReadable(_.strpool)})?;
            return ParseError.Expected;
        }
        const arg1 = arg1_opt.?;

        // Operator must be a regular text token
        if (_.peek() != .Text) {
            _.add_error("Expected a conditional expression operator, but got: {s}", .{_.peek().asHumanReadable(_.strpool)})?;
            return ParseError.Expected;
        }

        const op = _.expect(.Text);
        if (!_.match(.Delimit)) {
            _.add_error("Expected a single, simple word", {})?;
            return ParseError.Expected;
        }
        const txt = _.text(op.Text);

        inline for (binary_op of AST.CondExpr.Op.BINARY_OPS) {
            if (bun.strings.eqlComptime(txt, binary_op.name)) {
                const is_supported = comptime AST.CondExpr.Op.isSupported(@enumFromInt(binary_op.value));
                if (!is_supported) {
                    _.add_error("Conditional expression operation: {s}, is not supported right now. Please open a GitHub issue if you would like it to be supported.", .{binary_op.name})?;
                    return ParseError.Unsupported;
                }

                const atom_opt = _.parse_atom()?;
                const arg2 = atom_opt orelse {
                    _.add_error("Expected a word, but got: {s}", .{_.peek().asHumanReadable(_.strpool)})?;
                    return ParseError.Expected;
                };

                if (!_.match(.DoubleBracketClose)) {
                    _.add_error("Expected \"]]\" but got: {s}", .{_.peek().asHumanReadable(_.strpool)})?;
                    return ParseError.Expected;
                }

                return {
                    op: @enumFromInt(binary_op.value),
                    args: AST.CondExpr.ArgList.initWithSlice(&.{ arg1, arg2 }),
                };
            }
        }

        _.add_error("Unknown conditional expression operation: {s}", .{txt})?;
        return ParseError.Unknown;
    }

    fn parse_if_body(*_, comptime until: []const IfClauseTok): !SmolList(AST.Stmt, 1) {
        let ret: SmolList(AST.Stmt, 1) = SmolList(AST.Stmt, 1).zeroes;
        while (if (_.inside_subshell == null)
            !_.peek_any_comptime_ifclausetok(until) && !_.peek_any_comptime(&.{.Eof})
        else
            !_.peek_any_ifclausetok(until) && !_.peek_any(&.{ _.inside_subshell.?.closing_tok(), .Eof }))
        {
            _.skip_newlines();
            const stmt = _.parse_stmt()?;
            ret.append(stmt);
            _.skip_newlines();
        }

        return ret;
    }

    fn parse_if_clause(*_): !AST.If {
        * = _.expectIfClauseTextToken(.@"if");
        // _ = self.expect(.If);

        const cond = _.parse_if_body(&.{.then})?;

        if (!_.match_if_clausetok(.then)) {
            _.add_error("Expected \"then\" but got: {s}", .{@tagName(_.peek())})?;
            return ParseError.Expected;
        }

        const then = _.parse_if_body(&.{ .@"else", .elif, .fi })?;

        let else_parts: SmolList(SmolList(AST.Stmt, 1), 1) = SmolList(SmolList(AST.Stmt, 1), 1).zeroes;

        const if_clause_tok_opt = IfClauseTok.fromTok(_, _.peek());
        if (if_clause_tok_opt == null) {
            _.add_error("Expected \"else\", \"elif\", or \"fi\" but got: {s}", .{@tagName(_.peek())})?;
            return ParseError.Expected;
        }
        const if_clause_tok = if_clause_tok_opt.?;

        switch (if_clause_tok) {
            .@"if", .then => {
                _.add_error("Expected \"else\", \"elif\", or \"fi\" but got: {s}", .{@tagName(_.peek())})?;
                return ParseError.Expected;
            },
            .@"else" => {
                * = _.expectIfClauseTextToken(.@"else");
                const @"else" = _.parse_if_body(&.{.fi})?;
                if (!_.match_if_clausetok(.fi)) {
                    _.add_error("Expected \"fi\" but got: {s}", .{@tagName(_.peek())})?;
                    return ParseError.Expected;
                }
                else_parts.append(@"else");
                return {
                    cond: cond,
                    then: then,
                    else_parts: else_parts,
                };
            },
            .elif => {
                while (true) {
                    * = _.expectIfClauseTextToken(.elif);
                    const elif_cond = _.parse_if_body(&.{.then})?;
                    if (!_.match_if_clausetok(.then)) {
                        _.add_error("Expected \"then\" but got: {s}", .{@tagName(_.peek())})?;
                        return ParseError.Expected;
                    }
                    const then_part = _.parse_if_body(&.{ .elif, .@"else", .fi })?;
                    else_parts.append(elif_cond);
                    else_parts.append(then_part);

                    const peek_tok = IfClauseTok.fromTok(_, _.peek()) orelse break;
                    switch (peek_tok) {
                        .elif => continue,
                        .@"else" => {
                            * = _.expectIfClauseTextToken(.@"else");
                            const else_part = _.parse_if_body(&.{.fi})?;
                            else_parts.append(else_part);
                            break;
                        },
                        else => break,
                    }
                }
                if (!_.match_if_clausetok(.fi)) {
                    _.add_error("Expected \"fi\" but got: {s}", .{@tagName(_.peek())})?;
                    return ParseError.Expected;
                }
                return {
                    cond: cond,
                    then: then,
                    else_parts: else_parts,
                };
            },
            .fi => {
                * = _.expectIfClauseTextToken(.fi);
                return {
                    cond: cond,
                    then: then,
                };
            },
        }
    }

    fn parse_simple_cmd(*_): !AST.CmdOrAssigns {
        let assigns = std.array_list.Managed(AST.Assign).init(_.alloc);
        while (if (_.inside_subshell == null)
            !_.check_any_comptime(&.{ .Semicolon, .Newline, .Eof })
        else
            !_.check_any(&.{ .Semicolon, .Newline, .Eof, _.inside_subshell.?.closing_tok() }))
        {
            const assign_opt = _.parse_assign()?;
            if (assign := assign_opt) {
                assigns.append(assign)?;
            } else {
                break;
            }
        }

        if (if (_.inside_subshell == null)
            _.check_any_comptime(&.{ .Semicolon, .Newline, .Eof })
        else
            _.check_any(&.{ .Semicolon, .Newline, .Eof, _.inside_subshell.?.closing_tok() }))
        {
            if (assigns.items.len == 0) {
                _.add_error("expected a command or assignment", {})?;
                return ParseError.Expected;
            }
            return { assigns: assigns.items[0..] };
        }

        const name_opt = _.parse_atom()?;
        if (name_opt == null) {
            if (assigns.items.len == 0) {
                _.add_error("expected a command or assignment but got: \"{s}\"", .{@tagName(_.peek())})?;
                return ParseError.Expected;
            }
            return { assigns: assigns.items[0..] };
        }
        const name = name_opt.?;

        let name_and_args = std.array_list.Managed(AST.Atom).init(_.alloc);
        name_and_args.append(name)?;
        while (true) {
            const arg_opt = _.parse_atom()?;
            if (arg := arg_opt) {
                name_and_args.append(arg)?;
            } else {
                break;
            }
        }
        const parsed_redirect = _.parse_redirect()?;

        return { cmd: {
            assigns: assigns.items[0..],
            name_and_args: name_and_args.items[0..],
            redirect_file: parsed_redirect.redirect,
            redirect: parsed_redirect.flags,
        } };
    }

    fn parse_redirect(*_): !ParsedRedirect {
        const has_redirect = _.match(.Redirect);
        const redirect = if (has_redirect) _.prev().Redirect else AST.RedirectFlags{ stdin: false };
        const redirect_file: AST.Redirect? = redirect_file: {
            if (has_redirect) {
                if (_.match(.JSObjRef)) {
                    const obj_ref = _.prev().JSObjRef;
                    break :redirect_file { jsbuf: AST.JSBuf.new(obj_ref) };
                }

                const redirect_file_opt = _.parse_atom()?;
                if (redirect_file_opt == null) {
                    if (redirect.duplicate_out) break :redirect_file null;
                    _.add_error("Redirection with no file", {})?;
                    return ParseError.Expected;
                }
                const redirect_file = redirect_file_opt.?;
                break :redirect_file { atom: redirect_file };
            }
            break :redirect_file null;
        };
        // TODO check for multiple redirects and error
        return { flags: redirect, redirect: redirect_file };
    }

    /// Try to parse an assignment. If no assignment could be parsed then return
    /// null and backtrack the parser state
    fn parse_assign(*_): !AST.Assign? {
        const old = _.current;
        * = old;
        switch (_.peek()) {
            .Text => |txtrng| {
                const start_idx = _.current;
                * = _.expect(.Text);
                const txt = _.text(txtrng);
                const var_decl: AST.Assign? = var_decl: {
                    if (eq_idx := hasEqSign(txt)) {
                        // If it starts with = then it's not valid assignment (e.g. `=FOO`)
                        if (eq_idx == 0) break :var_decl null;
                        const label = txt[0..eq_idx];
                        if (!isValidVarName(label)) {
                            break :var_decl null;
                        }

                        if (eq_idx == txt.len - 1) {
                            if (_.delimits(_.peek())) {
                                * = _.expect_delimit();
                                break :var_decl {
                                    label: label,
                                    value: { simple: { Text: "" } },
                                };
                            }
                            const atom_opt = _.parse_atom()?;
                            if (atom_opt == null) {
                                _.add_error("Expected an atom", {})?;
                                return ParseError.Expected;
                            }
                            const atom = atom_opt.?;
                            break :var_decl {
                                label: label,
                                value: atom,
                            };
                        }

                        const txt_value = txt[eq_idx + 1 .. txt.len];
                        if (_.delimits(_.peek())) {
                            * = _.expect_delimit();
                            break :var_decl {
                                label: label,
                                value: { simple: { Text: txt_value } },
                            };
                        }

                        const right_opt = _.parse_atom()?;
                        if (right_opt == null) {
                            _.add_error("Expected an atom", {})?;
                            return ParseError.Expected;
                        }
                        const right = right_opt.?;
                        const left: AST.Atom = {
                            simple: { Text: txt_value },
                        };
                        const merged = AST.Atom.merge(left, right, _.alloc)?;
                        break :var_decl {
                            label: label,
                            value: merged,
                        };
                    }
                    break :var_decl null;
                };

                if (vd := var_decl) {
                    return vd;
                }

                // Rollback
                _.current = start_idx;
                return null;
            },
            else => return null,
        }
    }

    fn parse_atom(*_): !AST.Atom? {
        let array_alloc = std.heap.stackFallback(@sizeOf(AST.SimpleAtom), _.alloc);
        let atoms = std.array_list.Managed(AST.SimpleAtom).initCapacity(array_alloc.get(), 1)?;
        let has_brace_open = false;
        let has_brace_close = false;
        let has_comma = false;
        let has_glob_syntax = false;
        {
            while (switch (_.peek()) {
                .Delimit => brk: {
                    * = _.expect(.Delimit);
                    break :brk false;
                },
                .Eof, .Semicolon, .Newline => false,
                else => |t| brk: {
                    if (_.inside_subshell != null && _.inside_subshell.?.closing_tok() == t) break :brk false;
                    break :brk true;
                },
            }) {
                const next = _.peek_n(1);
                const next_delimits = _.delimits(next);
                const peeked = _.peek();
                const should_break = next_delimits;
                switch (peeked) {
                    .Asterisk => {
                        (has_glob_syntax = true);
                        * = _.expect(.Asterisk);
                        atoms.append(.asterisk)?;
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            break;
                        }
                    },
                    .DoubleAsterisk => {
                        (has_glob_syntax = true);
                        * = _.expect(.DoubleAsterisk);
                        atoms.append(.double_asterisk)?;
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            break;
                        }
                    },
                    .BraceBegin => {
                        (has_brace_open = true);
                        * = _.expect(.BraceBegin);
                        atoms.append(.brace_begin)?;
                        // TODO in this case we know it can't possibly be the beginning of a brace expansion so maybe its faster to just change it to text here
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            if (should_break) break;
                        }
                    },
                    .BraceEnd => {
                        (has_brace_close = true);
                        * = _.expect(.BraceEnd);
                        atoms.append(.brace_end)?;
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            break;
                        }
                    },
                    .Comma => {
                        (has_comma = true);
                        * = _.expect(.Comma);
                        atoms.append(.comma)?;
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            if (should_break) break;
                        }
                    },
                    .CmdSubstBegin => {
                        * = _.expect(.CmdSubstBegin);
                        const is_quoted = _.match(.CmdSubstQuoted);
                        let subparser = _.make_subparser(.cmd_subst);
                        const script = subparser.parse_impl() catch |e| {
                            _.continue_from_subparser(&subparser);
                            return e;
                        };
                        atoms.append({ cmd_subst: {
                            script: script,
                            quoted: is_quoted,
                        } });
                        _.continue_from_subparser(&subparser);
                        if (_.delimits(_.peek())) {
                            * = _.match(.Delimit);
                            break;
                        }
                    },
                    .SingleQuotedText, .DoubleQuotedText, .Text => |txtrng| {
                        * = _.advance();
                        let txt = _.text(txtrng);
                        if (peeked == .Text && txt.len > 0 && txt[0] == '~') {
                            (txt = txt[1..]);
                            atoms.append(.tilde)?;
                            if (txt.len > 0) {
                                atoms.append({ Text: txt })?;
                            }
                        } else if (txt.len == 0 && (peeked == .SingleQuotedText || peeked == .DoubleQuotedText)) {
                            // Preserve empty quoted strings ("", '') as explicit empty arguments
                            atoms.append(.quoted_empty)?;
                        } else {
                            atoms.append({ Text: txt })?;
                        }
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            if (should_break) break;
                        }
                    },
                    .Var => |txtrng| {
                        * = _.expect(.Var);
                        const txt = _.text(txtrng);
                        atoms.append({ Var: txt })?;
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            if (should_break) break;
                        }
                    },
                    .VarArgv => |int| {
                        * = _.expect(.VarArgv);
                        atoms.append({ VarArgv: int })?;
                        if (next_delimits) {
                            * = _.match(.Delimit);
                            if (should_break) break;
                        }
                    },
                    .OpenParen, .CloseParen => {
                        _.add_error("Unexpected token: `{s}`", .{if (peeked == .OpenParen) "(" else ")"})?;
                        return ParseError.Unexpected;
                    },
                    .Pipe => return null,
                    .DoublePipe => return null,
                    .Ampersand => return null,
                    .DoubleAmpersand => return null,
                    .Redirect => return null,
                    .Dollar => return null,
                    .Eq => return null,
                    .Semicolon => return null,
                    .Newline => return null,
                    .CmdSubstQuoted => return null,
                    .CmdSubstEnd => return null,
                    .JSObjRef => return null,
                    .Delimit => return null,
                    .Eof => return null,
                    .DoubleBracketOpen => return null,
                    .DoubleBracketClose => return null,
                }
            }
        }

        return switch (atoms.items.len) {
            0 => null,
            1 => {
                if (bun.Environment.allow_assert) assert(atoms.capacity == 1);
                return AST.Atom.new_simple(atoms.items[0]);
            },
            else => { compound: {
                atoms: atoms.items[0..atoms.items.len],
                brace_expansion_hint: has_brace_open && has_brace_close && has_comma,
                glob_hint: has_glob_syntax,
            } },
        };
    }

    fn allocate(comptime T: type, val: T): !*T {
        const heap = _.alloc.create(T)?;
        heap.* = val;
        return heap;
    }

    fn text(range: Token.TextRange): []const u8 {
        return _.strpool[range.start..range.end];
    }

    fn advance(*_): Token {
        if (!_.is_at_end()) {
            _.current += 1;
        }
        return _.prev();
    }

    fn is_at_end(*_): bool {
        return _.peek() == .Eof || _.inside_subshell != null && _.inside_subshell.?.closing_tok() == _.peek();
    }

    fn expect(*_, toktag: TokenTag): Token {
        if (bun.Environment.allow_assert) assert(toktag == @as(TokenTag, _.peek()));
        if (_.check(toktag)) {
            return _.advance();
        }
        @panic("Unexpected token");
    }

    fn expect_any(*_, toktags: []const TokenTag): Token {
        const peeked = _.peek();
        for (toktag of toktags) {
            if (toktag == @as(TokenTag, peeked)) return _.advance();
        }

        @panic("Unexpected token");
    }

    fn delimits(*_, tok: Token): bool {
        return tok == .Delimit || tok == .Semicolon || tok == .Semicolon || tok == .Eof || tok == .Newline || (_.inside_subshell != null && tok == _.inside_subshell.?.closing_tok());
    }

    fn expect_delimit(*_): Token {
        if (bun.Environment.allow_assert) assert(_.delimits(_.peek()));
        if (_.check(.Delimit) || _.check(.Semicolon) || _.check(.Newline) || _.check(.Eof) || (_.inside_subshell != null && _.check(_.inside_subshell.?.closing_tok()))) {
            return _.advance();
        }
        @panic("Expected a delimiter token");
    }

    fn match_if_clausetok(*_, toktag: IfClauseTok): bool {
        if (_.peek() == .Text &&
            _.delimits(_.peek_n(1)) &&
            bun.strings.eql(_.text(_.peek().Text), @tagName(toktag)))
        {
            * = _.advance();
            * = _.expect_delimit();
            return true;
        }
        return false;
    }

    /// Consumes token if it matches
    fn match(*_, toktag: TokenTag): bool {
        if (@as(TokenTag, _.peek()) == toktag) {
            * = _.advance();
            return true;
        }
        return false;
    }

    fn match_any_comptime(*_, comptime toktags: []const TokenTag): bool {
        const peeked = @as(TokenTag, _.peek());
        inline for (tag of toktags) {
            if (peeked == tag) {
                * = _.advance();
                return true;
            }
        }
        return false;
    }

    fn match_any(*_, toktags: []const TokenTag): bool {
        const peeked = @as(TokenTag, _.peek());
        for (tag of toktags) {
            if (peeked == tag) {
                * = _.advance();
                return true;
            }
        }
        return false;
    }

    fn peek_any_ifclausetok(*_, toktags: []const IfClauseTok): bool {
        const peektok = _.peek();
        const peeked = @as(TokenTag, peektok);
        if (peeked != .Text) return false;

        const txt = _.text(peektok.Text);
        for (tag of toktags) {
            if (bun.strings.eql(txt, @tagName(tag))) {
                return true;
            }
        }
        return false;
    }

    fn peek_any_comptime_ifclausetok(*_, comptime toktags: []const IfClauseTok): bool {
        const peektok = _.peek();
        const peeked = @as(TokenTag, peektok);
        if (peeked != .Text) return false;

        const txt = _.text(peektok.Text);
        inline for (tag of toktags) {
            if (bun.strings.eqlComptime(txt, @tagName(tag))) {
                return true;
            }
        }
        return false;
    }

    fn peek_any_comptime(*_, comptime toktags: []const TokenTag): bool {
        const peeked = @as(TokenTag, _.peek());
        inline for (tag of toktags) {
            if (peeked == tag) {
                return true;
            }
        }
        return false;
    }

    fn peek_any(*_, toktags: []const TokenTag): bool {
        const peeked = @as(TokenTag, _.peek());
        for (tag of toktags) {
            if (peeked == tag) {
                return true;
            }
        }
        return false;
    }

    fn check_any_comptime(*_, comptime toktags: []const TokenTag): bool {
        const peeked = @as(TokenTag, _.peek());
        inline for (tag of toktags) {
            if (peeked == tag) {
                return true;
            }
        }
        return false;
    }

    fn check_any(*_, toktags: []const TokenTag): bool {
        const peeked = @as(TokenTag, _.peek());
        for (tag of toktags) {
            if (peeked == tag) {
                return true;
            }
        }
        return false;
    }

    fn check(*_, toktag: TokenTag): bool {
        return @as(TokenTag, _.peek()) == @as(TokenTag, toktag);
    }

    fn peek(*_): Token {
        return _.tokens[_.current];
    }

    fn peek_n(*_, n: u32): Token {
        if (_.current + n >= _.tokens.len) {
            return _.tokens[_.tokens.len - 1];
        }

        return _.tokens[_.current + n];
    }

    fn prev(*_): Token {
        return _.tokens[_.current - 1];
    }

    fn combineErrors(*_): []const u8 {
        const errors = _.errors.items[0..];
        const str = str: {
            const size = size: {
                let i: usize = 0;
                for (e of errors) {
                    i += e.msg.len;
                }
                break :size i;
            };
            let buf = bun.handleOom(_.alloc.alloc(u8, size));
            let i: usize = 0;
            for (e of errors) {
                @memcpy(buf[i .. i + e.msg.len], e.msg);
                i += e.msg.len;
            }
            break :str buf;
        };
        return str;
    }

    fn add_error(*_, comptime fmt: []const u8, args: auto)! {
        const error_msg = std.fmt.allocPrint(_.alloc, fmt, args)?;
        _.errors.append({ msg: error_msg })?;
    }

    fn add_error_expected_pipeline_item(*_, kind: AST.Expr.Tag)! {
        const error_msg = std.fmt.allocPrint(_.alloc, "Expected a command, assignment, or subshell but got: {s}", .{@tagName(kind)})?;
        _.errors.append({ msg: error_msg })?;
    }
};

pub enum TokenTag {
    Pipe,
    DoublePipe,
    Ampersand,
    DoubleAmpersand,
    Redirect,
    Dollar,
    Asterisk,
    DoubleAsterisk,
    Eq,
    Semicolon,
    Newline,
    // Comment,
    BraceBegin,
    Comma,
    BraceEnd,
    CmdSubstBegin,
    CmdSubstQuoted,
    CmdSubstEnd,
    OpenParen,
    CloseParen,
    Var,
    VarArgv,
    Text,
    SingleQuotedText,
    DoubleQuotedText,
    JSObjRef,
    DoubleBracketOpen,
    DoubleBracketClose,
    Delimit,
    Eof,
}

pub const Token = union(TokenTag) {
    /// |
    Pipe,
    /// ||
    DoublePipe,
    /// &
    Ampersand,
    /// &&
    DoubleAmpersand,

    Redirect: AST.RedirectFlags,

    /// $
    Dollar,
    // `*`
    Asterisk,
    DoubleAsterisk,

    /// =
    Eq,
    /// ;
    Semicolon,
    /// \n (unescaped newline)
    Newline,

    BraceBegin,
    Comma,
    BraceEnd,
    CmdSubstBegin,
    /// When cmd subst is wrapped in quotes, then it should be interpreted as literal string, not word split-ed arguments to a cmd.
    /// We lose quotation context in the AST, so we don't know how to disambiguate that.
    /// So this is a quick hack to give the AST that context.
    ///
    /// This matches this shell behaviour:
    /// echo test$(echo "1    2") -> test1 2\n
    /// echo "test$(echo "1    2")" -> test1    2\n
    CmdSubstQuoted,
    CmdSubstEnd,
    OpenParen,
    CloseParen,

    Var: TextRange,
    VarArgv: u8,
    Text: TextRange,
    /// Quotation information is lost from the lexer -> parser stage and it is
    /// helpful to disambiguate from regular text and quoted text
    SingleQuotedText: TextRange,
    DoubleQuotedText: TextRange,
    JSObjRef: u32,

    DoubleBracketOpen,
    DoubleBracketClose,

    Delimit,
    Eof,

    pub const TextRange = struct {
        start: u32,
        end: u32,

        pub fn len(range: TextRange): u32 {
            if (bun.Environment.allow_assert) assert(range.start <= range.end);
            return range.end - range.start;
        }

        pub fn slice(range: TextRange, buf: []const u8): []const u8 {
            return buf[range.start..range.end];
        }
    };

    pub fn asHumanReadable(this: Token, strpool: []const u8): []const u8 {
        const varargv_strings = blk: {
            let res: [10][2]u8 = undefined;
            for (&res, 0..) |*item, i| {
                item[0] = '$';
                item[1] = @as(u8, @intCast(i)) + '0';
            }
            break :blk res;
        };
        return switch (this) {
            .Pipe => "`|`",
            .DoublePipe => "`||`",
            .Ampersand => "`&`",
            .DoubleAmpersand => "`&&`",
            .Redirect => "`>`",
            .Dollar => "`$`",
            .Asterisk => "`*`",
            .DoubleAsterisk => "`**`",
            .Eq => "`=`",
            .Semicolon => "`;`",
            .Newline => "`\\n`",
            // Comment,
            .BraceBegin => "`{`",
            .Comma => "`,`",
            .BraceEnd => "`}`",
            .CmdSubstBegin => "`$(`",
            .CmdSubstQuoted => "CmdSubstQuoted",
            .CmdSubstEnd => "`)`",
            .OpenParen => "`(`",
            .CloseParen => "`)",
            .Var => strpool[this.Var.start..this.Var.end],
            .VarArgv => &varargv_strings[this.VarArgv],
            .Text => strpool[this.Text.start..this.Text.end],
            .SingleQuotedText => strpool[this.SingleQuotedText.start..this.SingleQuotedText.end],
            .DoubleQuotedText => strpool[this.DoubleQuotedText.start..this.DoubleQuotedText.end],
            .JSObjRef => "JSObjRef",
            .DoubleBracketOpen => "[[",
            .DoubleBracketClose => "]]",
            .Delimit => "Delimit",
            .Eof => "EOF",
        };
    }
};

pub const LexerAscii = NewLexer(.ascii);
pub const LexerUnicode = NewLexer(.wtf8);
pub struct LexResult {
    errors: []LexError,
    tokens: []const Token,
    strpool: []const u8,

    fn combineErrors(allocator: Allocator): []const u8 {
        const errors = _.errors;
        const str = str: {
            const size = size: {
                let i: usize = 0;
                for (e of errors) {
                    i += e.msg.len();
                }
                break :size i;
            };
            let buf = bun.handleOom(allocator.alloc(u8, size));
            let i: usize = 0;
            for (e of errors) {
                @memcpy(buf[i .. i + e.msg.len()], e.msg.slice(_.strpool));
                i += e.msg.len();
            }
            break :str buf;
        };
        return str;
    }
}
pub struct LexError {
    /// Allocated with lexer arena
    msg: Token.TextRange,
}

/// A special char used to denote the beginning of a special token
/// used for substituting JS variables into the script string.
///
/// \b (decimal value of 8) is deliberately chosen so that it is not
/// easy for the user to accidentally use this char in their script.
///
const SPECIAL_JS_CHAR = 8;
pub const LEX_JS_OBJREF_PREFIX: [7]u8 = [ 8, '_', '_', 'b', 'u', 'n', '_' ];
pub const LEX_JS_STRING_PREFIX: [10]u8 = [ 8, '_', '_', 'b', 'u', 'n', 's', 't', 'r', '_' ];

pub enum NewLexerSubShellKind {
    /// (echo hi; echo hello)
    normal,
    /// `echo hi; echo hello`
    backtick,
    /// $(echo hi; echo hello)
    dollar,
};

pub enum NewLexerRedirectDirection {
    out,
    @"in",
};

fn NewLexerBacktrackSnapshot(comptime Chars: type): type {
    return struct {
        chars: Chars,
        j: u32,
        word_start: u32,
        delimit_quote: bool,
    };
}

pub err NewLexerError { OutOfMemory, Utf8CannotEncodeSurrogateHalf, Utf8InvalidStartByte, CodepointTooLarge }

pub fn NewLexer(comptime encoding: StringEncoding): type {
    const Chars = ShellCharIter(encoding);
    return struct {
        chars: Chars,

        /// Tell us the beginning of a "word", indexes into the string pool (`buf`)
        /// Anytime a word is added, this needs to be updated
        word_start: u32 = 0,

        /// Keeps track of the end of a "word", indexes into the string pool (`buf`),
        /// anytime characters are added to the string pool this needs to be updated
        j: u32 = 0,

        strpool: ArrayList(u8),
        tokens: ArrayList(Token),
        delimit_quote: bool = false,
        in_subshell: SubShellKind? = null,
        errors: std.array_list.Managed(LexError),

        /// Contains a list of strings we need to escape
        /// Not owned by this struct
        string_refs: []bun.String,

        /// Number of JS object references expected (for bounds validation)
        jsobjs_len: u32 = 0,

        const SubShellKind = NewLexerSubShellKind;
        const RedirectDirection = NewLexerRedirectDirection;

        const LexerError = NewLexerError;

        pub const js_objref_prefix = "$__bun_";

        const State = Chars.State;

        const InputChar = Chars.InputChar;

        const BacktrackSnapshot = NewLexerBacktrackSnapshot(Chars);

        pub fn new(alloc: Allocator, src: []const u8, strings_to_escape: []bun.String, jsobjs_len: u32): @This() {
            return {
                chars: Chars.init(src),
                tokens: ArrayList(Token).init(alloc),
                strpool: ArrayList(u8).init(alloc),
                errors: ArrayList(LexError).init(alloc),
                string_refs: strings_to_escape,
                jsobjs_len: jsobjs_len,
            };
        }

        pub fn get_result(self: @This()): LexResult {
            return {
                tokens: self.tokens.items,
                strpool: self.strpool.items,
                errors: self.errors.items,
            };
        }

        pub fn add_error(self: *@This(), msg: []const u8) {
            const start = self.strpool.items.len;
            bun.handleOom(self.strpool.appendSlice(msg));
            const end = self.strpool.items.len;
            bun.handleOom(self.errors.append({ msg: { start: @intCast(start), end: @intCast(end) } }));
        }

        fn make_sublexer(self: *@This(), kind: SubShellKind): @This() {
            log("[lex] make sublexer", {});
            let sublexer: @This() = {
                chars: self.chars,
                strpool: self.strpool,
                tokens: self.tokens,
                errors: self.errors,
                in_subshell: kind,

                word_start: self.word_start,
                j: self.j,
                string_refs: self.string_refs,
                jsobjs_len: self.jsobjs_len,
            };
            sublexer.chars.state = .Normal;
            return sublexer;
        }

        fn continue_from_sublexer(self: *@This(), sublexer: *@This()) {
            log("[lex] drop sublexer", {});
            self.strpool = sublexer.strpool;
            self.tokens = sublexer.tokens;
            self.errors = sublexer.errors;

            self.chars = sublexer.chars;
            self.word_start = sublexer.word_start;
            self.j = sublexer.j;
            self.delimit_quote = sublexer.delimit_quote;
        }

        fn make_snapshot(self: *@This()): BacktrackSnapshot {
            return {
                chars: self.chars,
                j: self.j,
                word_start: self.word_start,
                delimit_quote: self.delimit_quote,
            };
        }

        fn backtrack(self: *@This(), snap: BacktrackSnapshot) {
            self.chars = snap.chars;
            self.j = snap.j;
            self.word_start = snap.word_start;
            self.delimit_quote = snap.delimit_quote;
        }

        fn last_tok_tag(self: *@This()): TokenTag? {
            if (self.tokens.items.len == 0) return null;
            return std.@"meta".activeTag(self.tokens.items[self.tokens.items.len - 1]);
        }

        pub fn lex(self: *@This()): LexerError!void {
            while (true) {
                const input_opt = self.eat();
                if (input_opt == null) {
                    self.break_word(true)?;
                    break;
                }
                const input = input_opt.?;
                const char = input.char;
                const escaped = input.escaped;

                // Special token to denote substituted JS variables
                // we use 8 or \b which is a non printable char
                if (char == SPECIAL_JS_CHAR) {
                    if (self.looksLikeJSStringRef()) {
                        if (bunstr := self.eatJSStringRef()) {
                            self.break_word(false)?;
                            self.handleJSStringRef(bunstr)?;
                            continue;
                        }
                    } else if (self.looksLikeJSObjRef()) {
                        if (tok := self.eatJSObjRef()) {
                            if (self.chars.state == .Double) {
                                self.add_error("JS object reference not allowed in double quotes");
                                return;
                            }
                            self.break_word(false)?;
                            self.tokens.append(tok)?;
                            continue;
                        }
                    }
                }
                // Handle non-escaped chars:
                // 1. special syntax (operators, etc.)
                // 2. lexing state switchers (quotes)
                // 3. word breakers (spaces, etc.)
                else if (!escaped) escaped: {
                    switch (char) {
                        // possibly double bracket open
                        '[' => {
                            comptime assertSpecialChar('[');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            if (p := self.peek()) {
                                if (p.escaped || p.char != '[') break :escaped;
                                const state = self.make_snapshot();
                                * = self.eat();
                                do_backtrack: {
                                    const p2_opt = self.peek();
                                    if (p2_opt == null) {
                                        self.break_word(true)?;
                                        self.tokens.append(.DoubleBracketClose)?;
                                        continue;
                                    }
                                    const p2 = p2_opt.?;
                                    if (p2.escaped) break :do_backtrack;
                                    switch (p2.char) {
                                        ' ', '\r', '\n', '\t' => {
                                            self.break_word(true)?;
                                            self.tokens.append(.DoubleBracketOpen)?;
                                        },
                                        else => break :do_backtrack,
                                    }
                                    continue;
                                }
                                self.backtrack(state);
                            }
                            break :escaped;
                        },
                        ']' => {
                            comptime assertSpecialChar(']');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            if (p := self.peek()) {
                                if (p.escaped || p.char != ']') break :escaped;
                                const state = self.make_snapshot();
                                * = self.eat();
                                do_backtrack: {
                                    const p2_opt = self.peek();
                                    if (p2_opt == null) {
                                        self.break_word(true)?;
                                        self.tokens.append(.DoubleBracketClose)?;
                                        continue;
                                    }
                                    const p2 = p2_opt.?;
                                    if (p2.escaped) break :do_backtrack;
                                    switch (p2.char) {
                                        ' ', '\r', '\n', '\t', ';', '&', '|', '>' => {
                                            self.break_word(true)?;
                                            self.tokens.append(.DoubleBracketClose)?;
                                        },
                                        else => break :do_backtrack,
                                    }
                                    continue;
                                }
                                self.backtrack(state);
                            }
                            break :escaped;
                        },

                        '#' => {
                            comptime assertSpecialChar('#');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            let whitespace_preceding = true;
                            if (prev_ch := self.chars.prev) {
                                whitespace_preceding = Chars.isWhitespace(prev_ch);
                            }
                            if (!whitespace_preceding) break :escaped;
                            self.break_word(true)?;
                            self.eatComment();
                            continue;
                        },
                        ';' => {
                            comptime assertSpecialChar(';');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word(true)?;
                            self.tokens.append(.Semicolon)?;
                            continue;
                        },
                        '\n' => {
                            comptime assertSpecialChar('\n');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word_impl(true, true, false)?;
                            self.tokens.append(.Newline)?;
                            continue;
                        },

                        // glob asterisks
                        '*' => {
                            comptime assertSpecialChar('*');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            if (next := self.peek()) {
                                if (!next.escaped && next.char == '*') {
                                    * = self.eat();
                                    self.break_word(false)?;
                                    self.tokens.append(.DoubleAsterisk)?;
                                    continue;
                                }
                            }
                            self.break_word(false)?;
                            self.tokens.append(.Asterisk)?;
                            continue;
                        },

                        // brace expansion syntax
                        '{' => {
                            comptime assertSpecialChar('{');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word(false)?;
                            self.tokens.append(.BraceBegin)?;
                            continue;
                        },
                        ',' => {
                            comptime assertSpecialChar(',');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word(false)?;
                            self.tokens.append(.Comma)?;
                            continue;
                        },
                        '}' => {
                            comptime assertSpecialChar('}');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word(false)?;
                            self.tokens.append(.BraceEnd)?;
                            continue;
                        },

                        // Command substitution
                        '`' => {
                            comptime assertSpecialChar('`');

                            if (self.chars.state == .Single) break :escaped;
                            if (self.in_subshell == .backtick) {
                                self.break_word_operator()?;
                                if (toktag := self.last_tok_tag()) {
                                    if (toktag != .Delimit) self.tokens.append(.Delimit)?;
                                }
                                self.tokens.append(.CmdSubstEnd)?;
                                return;
                            } else {
                                self.eat_subshell(.backtick)?;
                            }
                        },
                        // Command substitution/vars
                        '$' => {
                            comptime assertSpecialChar('$');

                            if (self.chars.state == .Single) break :escaped;

                            const peeked = self.peek() orelse InputChar{ .char = 0 };
                            if (!peeked.escaped && peeked.char == '(') {
                                self.break_word(false)?;
                                self.eat_subshell(.dollar)?;
                                continue;
                            }

                            // const snapshot = self.make_snapshot();
                            // Handle variable
                            self.break_word(false)?;
                            const var_tok = self.eat_var()?;

                            switch (var_tok.len()) {
                                0 => {
                                    self.appendCharToStrPool('$')?;
                                    self.break_word(false)?;
                                },
                                1 => blk: {
                                    const c = self.strpool.items[var_tok.start];
                                    if (c >= '0' && c <= '9') {
                                        self.tokens.append({ VarArgv: c - '0' })?;
                                        break :blk;
                                    }
                                    self.tokens.append({ Var: var_tok })?;
                                },
                                else => {
                                    self.tokens.append({ Var: var_tok })?;
                                },
                            }
                            self.word_start = self.j;
                            continue;
                        },
                        '(' => {
                            comptime assertSpecialChar('(');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word(true)?;
                            self.eat_subshell(.normal)?;
                            continue;
                        },
                        ')' => {
                            comptime assertSpecialChar(')');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            if (self.in_subshell != .dollar && self.in_subshell != .normal) {
                                self.add_error("Unexpected ')'");
                                continue;
                            }

                            self.break_word(true)?;
                            // Command substitution can be put in a word so need
                            // to add delimiter
                            if (self.in_subshell == .dollar) {
                                if (toktag := self.last_tok_tag()) {
                                    switch (toktag) {
                                        .Delimit, .Semicolon, .Eof, .Newline => {},
                                        else => {
                                            self.tokens.append(.Delimit)?;
                                        },
                                    }
                                }
                            }

                            if (self.in_subshell == .dollar) {
                                self.tokens.append(.CmdSubstEnd)?;
                            } else if (self.in_subshell == .normal) {
                                self.tokens.append(.CloseParen)?;
                            }
                            return;
                        },

                        '0'..='9' => {
                            comptime {
                                let c: u8 = '0';
                                while (c < '9') : (c += 1) assertSpecialChar(c);
                            }

                            if (self.chars.state != .Normal) break :escaped;
                            const snapshot = self.make_snapshot();
                            if (redirect := self.eat_redirect(input)) {
                                self.break_word(true)?;
                                self.tokens.append({ Redirect: redirect })?;
                                continue;
                            }
                            self.backtrack(snapshot);
                            break :escaped;
                        },

                        // Operators
                        '|' => {
                            comptime assertSpecialChar('|');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word_operator()?;

                            const next_opt = self.peek();
                            if (next_opt == null) {
                                self.add_error("Unexpected EOF");
                                return;
                            }
                            const next = next_opt.?;
                            if (!next.escaped && next.char == '&') {
                                self.add_error("Piping stdout and stderr (`|&`) is not supported yet. Please file an issue on GitHub.");
                                return;
                            }
                            if (next.escaped || next.char != '|') {
                                self.tokens.append(.Pipe)?;
                            } else if (next.char == '|') {
                                * = self.eat() orelse unreachable;
                                self.tokens.append(.DoublePipe)?;
                            }
                            continue;
                        },
                        '>' => {
                            comptime assertSpecialChar('>');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word_operator()?;
                            const redirect = self.eat_simple_redirect(.out);
                            self.tokens.append({ Redirect: redirect })?;
                            continue;
                        },
                        '<' => {
                            comptime assertSpecialChar('<');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word_operator()?;
                            const redirect = self.eat_simple_redirect(.@"in");
                            self.tokens.append({ Redirect: redirect })?;
                            continue;
                        },
                        '&' => {
                            comptime assertSpecialChar('&');

                            if (self.chars.state == .Single || self.chars.state == .Double) break :escaped;
                            self.break_word_operator()?;

                            const next_opt = self.peek();
                            if (next_opt == null) {
                                self.tokens.append(.Ampersand)?;
                                continue;
                            }
                            const next = next_opt.?;

                            if (next.char == '>' && !next.escaped) {
                                * = self.eat();
                                const inner = if (self.eat_simple_redirect_operator(.out))
                                    AST.RedirectFlags.@"&>>"()
                                else
                                    AST.RedirectFlags.@"&>"();
                                self.tokens.append({ Redirect: inner })?;
                            } else if (next.escaped || next.char != '&') {
                                self.tokens.append(.Ampersand)?;
                            } else if (next.char == '&') {
                                * = self.eat() orelse unreachable;
                                self.tokens.append(.DoubleAmpersand)?;
                            } else {
                                self.tokens.append(.Ampersand)?;
                                continue;
                            }
                        },

                        // 2. State switchers
                        '\'' => {
                            comptime assertSpecialChar('\'');

                            if (self.chars.state == .Single) {
                                self.break_word(false)?;
                                self.chars.state = .Normal;
                                continue;
                            }
                            if (self.chars.state == .Normal) {
                                self.break_word(false)?;
                                self.chars.state = .Single;
                                continue;
                            }
                            break :escaped;
                        },
                        '"' => {
                            comptime assertSpecialChar('"');

                            if (self.chars.state == .Single) break :escaped;
                            if (self.chars.state == .Normal) {
                                self.break_word(false)?;
                                self.chars.state = .Double;
                            } else if (self.chars.state == .Double) {
                                self.break_word(false)?;
                                // self.delimit_quote = true;
                                self.chars.state = .Normal;
                            }
                            continue;
                        },

                        // 3. Word breakers
                        ' ' => {
                            comptime assertSpecialChar(' ');

                            if (self.chars.state == .Normal) {
                                self.break_word_impl(true, true, false)?;
                                continue;
                            }
                            break :escaped;
                        },

                        else => break :escaped,
                    }
                    continue;
                }
                // Treat newline preceded by backslash as whitespace
                else if (char == '\n') {
                    if (comptime bun.Environment.allow_assert) {
                        assert(input.escaped);
                    }
                    if (self.chars.state != .Double) {
                        self.break_word_impl(true, true, false)?;
                    }
                    continue;
                }

                self.appendCharToStrPool(char)?;
            }

            if (subshell_kind := self.in_subshell) {
                switch (subshell_kind) {
                    .dollar, .backtick => self.add_error("Unclosed command substitution"),
                    .normal => self.add_error("Unclosed subshell"),
                }
                return;
            }

            self.tokens.append(.Eof)?;
        }

        fn appendCharToStrPool(self: *@This(), char: Chars.CodepointType)! {
            if (comptime encoding == .ascii) {
                self.strpool.append(char)?;
                self.j += 1;
            } else {
                if (char <= 0x7F) {
                    self.strpool.append(@intCast(char))?;
                    self.j += 1;
                    return;
                } else {
                    self.appendUnicodeCharToStrPool(char)?;
                }
            }
        }

        fn appendUnicodeCharToStrPool(self: *@This(), char: Chars.CodepointType)! {
            @branchHint(.cold);

            const ichar: i32 = @intCast(char);
            let bytes: [4]u8 = undefined;
            const n = bun.strings.encodeWTF8Rune(&bytes, ichar);
            self.j += n;
            self.strpool.appendSlice(bytes[0..n])?;
        }

        fn break_word(self: *@This(), add_delimiter: bool)! {
            return self.break_word_impl(add_delimiter, false, false)?;
        }

        /// NOTE: this adds a delimiter
        fn break_word_operator(self: *@This())! {
            return self.break_word_impl(true, false, true)?;
        }

        inline fn isImmediatelyEscapedQuote(self: *@This()): bool {
            return ((self.chars.state == .Double &&
                (self.chars.current != null && !self.chars.current.?.escaped && self.chars.current.?.char == '"') &&
                (self.chars.prev != null && !self.chars.prev.?.escaped && self.chars.prev.?.char == '"')) ||
                (self.chars.state == .Single &&
                    (self.chars.current != null && !self.chars.current.?.escaped && self.chars.current.?.char == '\'') &&
                    (self.chars.prev != null && !self.chars.prev.?.escaped && self.chars.prev.?.char == '\'')));
        }

        fn break_word_impl(self: *@This(), add_delimiter: bool, in_normal_space: bool, in_operator: bool)! {
            const start: u32 = self.word_start;
            const end: u32 = self.j;
            if (start != end ||
                self.isImmediatelyEscapedQuote() // we want to preserve immediately escaped quotes like: ""
            ) {
                const tok: Token =
                    switch (self.chars.state) {
                        .Normal => @unionInit(Token, "Text", { start: start, end: end }),
                        .Single => @unionInit(Token, "SingleQuotedText", { start: start, end: end }),
                        .Double => @unionInit(Token, "DoubleQuotedText", { start: start, end: end }),
                    };
                self.tokens.append(tok)?;
                if (add_delimiter) {
                    self.tokens.append(.Delimit)?;
                }
            } else if ((in_normal_space || in_operator) && self.tokens.items.len > 0) {
                // whether or not to add a delimiter token
                const should_delimit = switch (self.tokens.items[self.tokens.items.len - 1]) {
                    .Var,
                    .VarArgv,
                    .Text,
                    .SingleQuotedText,
                    .DoubleQuotedText,
                    .BraceBegin,
                    .Comma,
                    .BraceEnd,
                    .CmdSubstEnd,
                    .Asterisk => true,

                    .Pipe,
                    .DoublePipe,
                    .Ampersand,
                    .DoubleAmpersand,
                    .Redirect,
                    .Dollar,
                    .DoubleAsterisk,
                    .Eq,
                    .Semicolon,
                    .Newline,
                    .CmdSubstBegin,
                    .CmdSubstQuoted,
                    .OpenParen,
                    .CloseParen,
                    .JSObjRef,
                    .DoubleBracketOpen,
                    .DoubleBracketClose,
                    .Delimit,
                    .Eof => false,
                };
                if (should_delimit) {
                    self.tokens.append(.Delimit)?;
                    self.delimit_quote = false;
                }
            }
            self.word_start = self.j;
        }

        fn eat_simple_redirect(self: *@This(), dir: NewLexerRedirectDirection): AST.RedirectFlags {
            const is_double = self.eat_simple_redirect_operator(dir);

            if (is_double) {
                return switch (dir) {
                    .out => AST.RedirectFlags.@">>"(),
                    .@"in" => AST.RedirectFlags.@"<<"(),
                };
            }

            return switch (dir) {
                .out => AST.RedirectFlags.@">"(),
                .@"in" => AST.RedirectFlags.@"<"(),
            };
        }

        /// Returns true if the operator is "double one": >> or <<
        /// Returns false if not doubled or invalid (e.g. <> ><)
        fn eat_simple_redirect_operator(self: *@This(), dir: RedirectDirection): bool {
            if (peeked := self.peek()) {
                if (peeked.escaped) return false;
                switch (peeked.char) {
                    '>' => {
                        if (dir == .out) {
                            * = self.eat();
                            return true;
                        }
                        return false;
                    },
                    '<' => {
                        if (dir == .@"in") {
                            * = self.eat();
                            return true;
                        }
                        return false;
                    },
                    else => return false,
                }
            }
            return false;
        }

        // TODO Arbitrary file descriptor redirect
        fn eat_redirect(self: *@This(), first: InputChar): AST.RedirectFlags? {
            let flags = AST.RedirectFlags{ stdin: false };
            switch (first.char) {
                '0' => flags.stdin = true,
                '1' => flags.stdout = true,
                '2' => flags.stderr = true,
                // Just allow the std file descriptors for now
                else => return null,
            }
            let dir: RedirectDirection = .out;
            if (input := self.peek()) {
                if (input.escaped) return null;
                switch (input.char) {
                    '>' => {
                        * = self.eat();
                        dir = .out;
                        const is_double = self.eat_simple_redirect_operator(dir);
                        if (is_double) flags.append = true;
                        if (peeked := self.peek()) {
                            if (!peeked.escaped && peeked.char == '&') {
                                * = self.eat();
                                if (peeked2 := self.peek()) {
                                    switch (peeked2.char) {
                                        '1' => {
                                            * = self.eat();
                                            if (!flags.stdout && flags.stderr) {
                                                flags.duplicate_out = true;
                                                flags.stdout = true;
                                                flags.stderr = false;
                                            } else { return null; }
                                        },
                                        '2' => {
                                            * = self.eat();
                                            if (!flags.stderr && flags.stdout) {
                                                flags.duplicate_out = true;
                                                flags.stderr = true;
                                                flags.stdout = false;
                                            } else { return null; }
                                        },
                                        else => return null,
                                    }
                                }
                            }
                        }
                        return flags;
                    },
                    '<' => {
                        dir = .@"in";
                        const is_double = self.eat_simple_redirect_operator(dir);
                        if (is_double) flags.append = true;
                        return flags;
                    },
                    else => return null,
                }
            } else { return null; }
        }

        /// Assumes the first character of the literal has been eaten
        /// Backtracks and returns false if unsuccessful
        fn eat_literal(self: *@This(), comptime CodepointType: type, comptime literal: []const CodepointType): bool {
            const literal_skip_first = literal[1..];
            const snapshot = self.make_snapshot();
            const slice_opt = self.eat_slice(CodepointType, literal_skip_first.len);
            if (slice_opt == null) {
                self.backtrack(snapshot);
                return false;
            }
            const slice = slice_opt.?;

            if (std.mem.eql(CodepointType, &slice, literal_skip_first))
                return true;

            self.backtrack(snapshot);
            return false;
        }

        fn eat_subshell(self: *@This(), kind: SubShellKind)! {
            if (kind == .dollar) {
                // Eat the open paren
                * = self.eat();
            }

            switch (kind) {
                .dollar, .backtick => {
                    self.tokens.append(.CmdSubstBegin)?;
                    if (self.chars.state == .Double) {
                        self.tokens.append(.CmdSubstQuoted)?;
                    }
                },
                .normal => self.tokens.append(.OpenParen)?,
            }
            const prev_quote_state = self.chars.state;
            let sublexer = self.make_sublexer(kind);
            sublexer.lex()?;
            self.continue_from_sublexer(&sublexer);
            self.chars.state = prev_quote_state;
        }

        fn appendStringToStrPool(self: *@This(), bunstr: bun.String)! {
            const start = self.strpool.items.len;
            if (bunstr.isUTF16()) {
                const utf16 = bunstr.utf16();
                const additional = bun.simdutf.simdutf__utf8_length_from_utf16le(utf16.ptr, utf16.len);
                self.strpool.ensureUnusedCapacity(additional)?;
                bun.strings.convertUTF16ToUTF8Append(&self.strpool, bunstr.utf16())?;
            } else if (bunstr.isUTF8()) {
                self.strpool.appendSlice(bunstr.byteSlice())?;
            } else if (bunstr.is8Bit()) {
                if (isAllAscii(bunstr.byteSlice())) {
                    self.strpool.appendSlice(bunstr.byteSlice())?;
                } else {
                    const bytes = bunstr.byteSlice();
                    const non_ascii_idx = bun.strings.firstNonASCII(bytes) orelse 0;

                    if (non_ascii_idx > 0) {
                        self.strpool.appendSlice(bytes[0..non_ascii_idx])?;
                    }
                    self.strpool = bun.strings.allocateLatin1IntoUTF8WithList(self.strpool, self.strpool.items.len, bytes[non_ascii_idx..])?;
                }
            }
            const end = self.strpool.items.len;
            self.j += @as(u32, @intCast(end - start));
        }

        fn handleJSStringRef(self: *@This(), bunstr: bun.String)! {
            if (bunstr.length() == 0) {
                // Empty JS string ref: emit a zero-length DoubleQuotedText token directly.
                // The parser converts this to a quoted_empty atom, preserving the empty arg.
                // This works regardless of the lexer's current quote state (Normal/Single/Double)
                // because the \x08 marker is processed before quote-state handling.
                const pos = self.j;
                self.tokens.append(@unionInit(Token, "DoubleQuotedText", { start: pos, end: pos }))?;
                return;
            }
            self.appendStringToStrPool(bunstr)?;
        }

        fn looksLikeJSObjRef(self: *@This()): bool {
            const bytes = self.chars.srcBytesAtCursor();
            if (LEX_JS_OBJREF_PREFIX.len - 1 >= bytes.len) return false;
            return std.mem.eql(u8, bytes[0 .. LEX_JS_OBJREF_PREFIX.len - 1], LEX_JS_OBJREF_PREFIX[1..]);
        }

        fn looksLikeJSStringRef(self: *@This()): bool {
            const bytes = self.chars.srcBytesAtCursor();
            if (LEX_JS_STRING_PREFIX.len - 1 >= bytes.len) return false;
            return std.mem.eql(u8, bytes[0 .. LEX_JS_STRING_PREFIX.len - 1], LEX_JS_STRING_PREFIX[1..]);
        }

        fn bumpCursorAscii(self: *@This(), new_idx: usize, prev_ascii_char: u7?, cur_ascii_char: u7) {
            if (comptime encoding == .ascii) {
                self.chars.src.i = new_idx;
                if (pc := prev_ascii_char) self.chars.prev = { char: pc };
                self.chars.current = { char: cur_ascii_char };
                return;
            }
            // Set the cursor to decode the codepoint at new_idx.
            // Use width=0 so that nextCursor (which computes pos = width + i)
            // starts reading from exactly new_idx.
            self.chars.src.cursor = CodepointIterator.Cursor{
                i: @intCast(new_idx),
                c: 0,
                width: 0,
            };
            SrcUnicode.nextCursor(&self.chars.src.iter, &self.chars.src.cursor);
            self.chars.src.next_cursor = self.chars.src.cursor;
            SrcUnicode.nextCursor(&self.chars.src.iter, &self.chars.src.next_cursor);
            if (pc := prev_ascii_char) self.chars.prev = { char: pc };
            self.chars.current = { char: cur_ascii_char };
        }

        fn matchesAsciiLiteral(self: *@This(), literal: []const u8): bool {
            const bytes = self.chars.srcBytesAtCursor();
            if (literal.len >= bytes.len) return false;
            return std.mem.eql(u8, bytes[0..literal.len], literal[0..]);
        }

        fn eatJSSubstitutionIdx(self: *@This(), comptime literal: []const u8, comptime name: []const u8, comptime validate: *const fn (*@This(), usize): bool): usize? {
            const none: usize? = null;
            if (self.matchesAsciiLiteral(literal[1..literal.len])) {
                const bytes = self.chars.srcBytesAtCursor();
                let i: usize = 0;
                let digit_buf: [32]u8 = undefined;
                let digit_buf_count: u8 = 0;

                i += literal.len - 1;

                while (i < bytes.len) {
                    switch (bytes[i]) {
                        '0'..='9' => {
                            if (digit_buf_count >= digit_buf.len) {
                                const ERROR_STR = "Invalid " ++ name ++ " (number too high): ";
                                let error_buf: [128]u8 = undefined;
                                const error_msg = std.fmt.bufPrint(error_buf[0..], "{s} {s}{c}", .{ ERROR_STR, digit_buf[0..digit_buf_count], bytes[i] }) catch @panic("Should not happen");
                                self.add_error(error_msg);
                                return none;
                            }
                            digit_buf[digit_buf_count] = bytes[i];
                            digit_buf_count += 1;
                            i += 1;
                        },
                        else => break,
                    }
                }

                if (digit_buf_count == 0) {
                    self.add_error("Invalid " ++ name ++ " (no idx)");
                    return none;
                }

                const idx = std.fmt.parseInt(usize, digit_buf[0..digit_buf_count], 10) catch {
                    self.add_error("Invalid " ++ name ++ " ref ");
                    return none;
                };

                if (!validate(self, idx)) return none;

                // Bump the cursor
                const new_idx = self.chars.cursorPos() + i;
                const prev_ascii_char: u7? = if (digit_buf_count == 1) null else @truncate(digit_buf[digit_buf_count - 2]);
                const cur_ascii_char: u7 = @truncate(digit_buf[digit_buf_count - 1]);
                self.bumpCursorAscii(new_idx, prev_ascii_char, cur_ascii_char);

                return idx;
            }
            return none;
        }

        /// __NOTE__: Do not store references to the returned bun.String, it does not have its ref count incremented
        fn eatJSStringRef(self: *@This()): bun.String? {
            if (idx := self.eatJSSubstitutionIdx(
                LEX_JS_STRING_PREFIX[0..],
                "JS string ref",
                validateJSStringRefIdx,
            )) {
                return self.string_refs[idx];
            }
            const none: bun.String? = null;
            return none;
        }

        fn validateJSStringRefIdx(self: *@This(), idx: usize): bool {
            if (idx >= self.string_refs.len) {
                self.add_error("Invalid JS string ref (out of bounds");
                return false;
            }
            return true;
        }

        fn eatJSObjRef(self: *@This()): Token? {
            if (idx := self.eatJSSubstitutionIdx(
                LEX_JS_OBJREF_PREFIX[0..],
                "JS object ref",
                validateJSObjRefIdx,
            )) {
                return .{ .JSObjRef = @intCast(idx) };
            }
            const none: Token? = null;
            return none;
        }

        fn validateJSObjRefIdx(self: *@This(), idx: usize): bool {
            if (idx >= self.jsobjs_len) {
                self.add_error("Invalid JS object ref (out of bounds)");
                return false;
            }
            return true;
        }

        fn eat_var(self: *@This()): Token.TextRange! {
            const start = self.j;
            let i: usize = 0;
            let is_int = false;
            // Eat until special character
            while (result := self.peek()) {
                defer i += 1;
                const char = result.char;
                const escaped = result.escaped;

                if (i == 0) {
                    switch (char) {
                        '=' => return .{ .start = start, .end = self.j },
                        '0'..='9' => {
                            is_int = true;
                            * = self.eat().?;
                            self.appendCharToStrPool(char)?;
                            continue;
                        },
                        'a'..='z', 'A'..='Z', '_' => {},
                        else => return .{ .start = start, .end = self.j },
                    }
                }
                if (is_int) {
                    return .{ .start = start, .end = self.j };
                }

                // if (char
                switch (char) {
                    '{', '}', ';', '\'', '\"', ' ', '|', '&', '>', ',', '$' => {
                        return .{ .start = start, .end = self.j };
                    },
                    else => {
                        if (!escaped &&
                            (self.in_subshell == .dollar && char == ')') || (self.in_subshell == .backtick && char == '`') || (self.in_subshell == .normal && char == ')'))
                        {
                            return .{ .start = start, .end = self.j };
                        }
                        switch (char) {
                            '0'..='9', 'a'..='z', 'A'..='Z', '_' => {
                                * = self.eat() orelse unreachable;
                                self.appendCharToStrPool(char)?;
                            },
                            else => return .{ .start = start, .end = self.j },
                        }
                    },
                }
            }
            return .{ .start = start, .end = self.j };
        }

        fn eat(self: *@This()): InputChar? {
            return self.chars.eat();
        }

        fn eatComment(self: *@This()) {
            while (peeked := self.eat()) {
                if (peeked.escaped) {
                    continue;
                }
                if (peeked.char == '\n') break;
            }
        }

        fn eat_slice(self: *@This(), comptime CodepointType: type, comptime N: usize): [N]CodepointType? {
            const none: ([N]CodepointType)? = null;
            let slice: [N]CodepointType = undefined;
            @memset(&slice, 0);
            let i: usize = 0;
            while (result := self.peek()) {
                // If we passed in codepoint range that is equal to the source
                // string, or is greater than the codepoint range of source string than an int cast
                // will not panic
                if (CodepointType == Chars.CodepointType || std.math.maxInt(CodepointType) >= std.math.maxInt(Chars.CodepointType)) {
                    slice[i] = @intCast(result.char);
                } else {
                    // Otherwise the codepoint range is smaller than the source, so we need to check that the chars are valid
                    if (result.char > std.math.maxInt(CodepointType)) {
                        return none;
                    }
                    slice[i] = @intCast(result.char);
                }

                i += 1;
                * = self.eat();
                if (i == N) {
                    return slice;
                }
            }

            return none;
        }

        fn peek(self: *@This()): InputChar? {
            return self.chars.peek();
        }

        fn read_char(self: *@This()): InputChar? {
            return self.chars.read_char();
        }
    };
}

pub const StringEncoding = enum { ascii, wtf8, utf16 };

struct SrcAscii {
    bytes: []const u8,
    i: usize,

    const IndexValue = packed struct(u8) {
        char: u7,
        escaped: bool = false,
    };

    static fn init(bytes: []const u8): SrcAscii {
        return {
            bytes: bytes,
            i: 0,
        };
    }

    inline fn index(): IndexValue? {
        if (_.i >= _.bytes.len) return null;
        return { char: @intCast(_.bytes[_.i]) };
    }

    inline fn indexNext(): IndexValue? {
        if (_.i + 1 >= _.bytes.len) return null;
        return { char: @intCast(_.bytes[_.i + 1]) };
    }

    inline fn eat(*_, escaped: bool) {
        _.i += 1 + @as(u32, @intFromBool(escaped));
    }
}

struct SrcUnicode {
    const Cursor = CodepointIterator.Cursor;

    iter: CodepointIterator,
    cursor: Cursor,
    next_cursor: Cursor,

    const IndexValue = struct {
        char: u32,
        width: u8,
    };

    static fn nextCursor(iter: *const CodepointIterator, cursor: *Cursor) {
        if (!iter.next(cursor)) {
            // This will make `i > sourceBytes.len` so the condition in `index` will fail
            cursor.i = @intCast(iter.bytes.len + 1);
            cursor.width = 1;
            cursor.c = CodepointIterator.ZeroValue;
        }
    }

    static fn init(bytes: []const u8): SrcUnicode {
        let iter = CodepointIterator.init(bytes);
        let cursor = Cursor{};
        nextCursor(&iter, &cursor);
        let next_cursor: Cursor = cursor;
        nextCursor(&iter, &next_cursor);
        return { iter: iter, cursor: cursor, next_cursor: next_cursor };
    }

    inline fn index(): IndexValue? {
        if (_.cursor.width + _.cursor.i > _.iter.bytes.len) return null;
        return { char: _.cursor.c, width: _.cursor.width };
    }

    inline fn indexNext(): IndexValue? {
        if (_.next_cursor.width + _.next_cursor.i > _.iter.bytes.len) return null;
        return { char: @intCast(_.next_cursor.c), width: _.next_cursor.width };
    }

    inline fn eat(*_, escaped: bool) {
        // eat two codepoints
        if (escaped) {
            nextCursor(&_.iter, &_.next_cursor);
            _.cursor = _.next_cursor;
            nextCursor(&_.iter, &_.next_cursor);
        } else {
            // eat one codepoint
            _.cursor = _.next_cursor;
            nextCursor(&_.iter, &_.next_cursor);
        }
    }
}

pub fn ShellCharIter(comptime encoding: StringEncoding): type {
    return struct {
        src: Src,
        state: State = .Normal,
        prev: InputChar? = null,
        current: InputChar? = null,

        pub const Src = switch (encoding) {
            .ascii => SrcAscii,
            .wtf8, .utf16 => SrcUnicode,
        };

        pub const CodepointType = if (encoding == .ascii) u7 else u32;

        pub const InputChar = if (encoding == .ascii) SrcAscii.IndexValue else struct {
            char: u32,
            escaped: bool = false,
        };

        pub fn isWhitespace(char: InputChar): bool {
            return switch (char.char) {
                '\t', '\r', '\n', ' ' => true,
                else => false,
            };
        }

        pub const State = enum {
            Normal,
            Single,
            Double,
        };

        pub fn init(bytes: []const u8): @This() {
            const src = if (comptime encoding == .ascii)
                SrcAscii.init(bytes)
            else
                SrcUnicode.init(bytes);

            return {
                src: src,
            };
        }

        pub fn srcBytes(self: *@This()): []const u8 {
            if (comptime encoding == .ascii) return self.src.bytes;
            return self.src.iter.bytes;
        }

        pub fn srcBytesAtCursor(self: *@This()): []const u8 {
            const bytes = self.srcBytes();
            if (comptime encoding == .ascii) {
                if (self.src.i >= bytes.len) return "";
                return bytes[self.src.i..];
            }

            if (self.src.cursor.i >= bytes.len) return "";
            return bytes[self.src.cursor.i..];
        }

        pub fn cursorPos(self: *@This()): usize {
            if (comptime encoding == .ascii) return self.src.i;
            return self.src.cursor.i;
        }

        pub fn eat(self: *@This()): InputChar? {
            if (result := self.read_char()) {
                self.prev = self.current;
                self.current = result;
                self.src.eat(result.escaped);
                return result;
            }
            return null;
        }

        pub fn peek(self: *@This()): InputChar? {
            if (result := self.read_char()) {
                return result;
            }

            return null;
        }

        pub fn read_char(self: *@This()): InputChar? {
            const indexed_value = self.src.index() orelse return null;
            let char = indexed_value.char;
            if (char != '\\' || self.state == .Single) return { char: char };

            // Handle backslash
            switch (self.state) {
                .Normal => {
                    const peeked = self.src.indexNext() orelse return null;
                    char = peeked.char;
                },
                .Double => {
                    const peeked = self.src.indexNext() orelse return null;
                    switch (peeked.char) {
                        // Backslash only applies to these characters
                        '$', '`', '"', '\\', '\n', '#' => {
                            char = peeked.char;
                        },
                        else => return { char: char, escaped: false },
                    }
                },
                // We checked `self.state == .Single` above so this is impossible
                .Single => unreachable,
            }

            return { char: char, escaped: true };
        }
    };
}

/// Only these charaters allowed:
/// - a-ZA-Z
/// - _
/// - 0-9 (but can't be first char)
pub fn isValidVarName(var_name: []const u8): bool {
    if (isAllAscii(var_name)) return isValidVarNameAscii(var_name);

    if (var_name.len == 0) return false;
    let iter = CodepointIterator.init(var_name);
    let cursor = CodepointIterator.Cursor{};

    if (!iter.next(&cursor)) return false;

    switch (cursor.c) {
        '=', '0'...'9' => {
            return false;
        },
        'a'...'z', 'A'...'Z', '_' => {},
        else => return false,
    }

    while (iter.next(&cursor)) {
        switch (cursor.c) {
            '0'...'9', 'a'...'z', 'A'...'Z', '_' => {},
            else => return false,
        }
    }

    return true;
}
fn isValidVarNameAscii(var_name: []const u8): bool {
    if (var_name.len == 0) return false;
    switch (var_name[0]) {
        '=', '0'...'9' => {
            return false;
        },
        'a'...'z', 'A'...'Z', '_' => {
            if (var_name.len == 1) return true;
        },
        else => return false,
    }
    for (c of var_name) {
        switch (c) {
            '0'...'9', 'a'...'z', 'A'...'Z', '_' => {},
            else => return false,
        }
    }
    return true;
}

let stderr_mutex = bun.Mutex{};

pub fn hasEqSign(str: []const u8): u32? {
    if (isAllAscii(str)) {
        return bun.strings.indexOfChar(str, '=');
    }

    // TODO actually i think that this can also use the simd stuff
    let iter = CodepointIterator.init(str);
    let cursor = CodepointIterator.Cursor{};
    while (iter.next(&cursor)) {
        if (cursor.c == '=') {
            return @intCast(cursor.i);
        }
    }

    return null;
}

type EnvStrZ = [:0]const u8;

pub struct CmdEnvIter {
    env: *const bun.StringArrayHashMap(EnvStrZ),
    iter: bun.StringArrayHashMap(EnvStrZ).Iterator,

    const Entry = struct {
        key: Key,
        value: Value,
    };

    const Value = struct {
        val: [:0]const u8,

        fn format(this: *const Value, writer: *std.Io.Writer)! {
            writer.writeAll(this.val)?;
        }
    };

    const Key = struct {
        val: []const u8,

        fn format(this: *const Key, writer: *std.Io.Writer)! {
            writer.writeAll(this.val)?;
        }

        fn eqlComptime(this: Key, comptime str: []const u8): bool {
            return bun.strings.eqlComptime(this.val, str);
        }
    };

    static fn fromEnv(env: *const bun.StringArrayHashMap(EnvStrZ)): CmdEnvIter {
        const iter = env.iterator();
        return {
            env: env,
            iter: iter,
        };
    }

    fn len(): usize {
        return _.env.unmanaged.entries.len;
    }

    fn next(*_): !Entry? {
        const entry = _.iter.next() ?? return null;
        return {
            key: { val: entry.key_ptr.* },
            value: { val: entry.value_ptr.* },
        };
    }
}

pub struct Test {
    pub const TestToken = union(TokenTag) {
        // |
        Pipe,
        // ||
        DoublePipe,
        // &
        Ampersand,
        // &&
        DoubleAmpersand,

        // >
        Redirect: AST.RedirectFlags,

        // $
        Dollar,
        // *
        Asterisk,
        DoubleAsterisk,
        // =
        Eq,
        Semicolon,
        Newline,

        BraceBegin,
        Comma,
        BraceEnd,
        CmdSubstBegin,
        CmdSubstQuoted,
        CmdSubstEnd,
        OpenParen,
        CloseParen,

        Var: []const u8,
        VarArgv: u8,
        Text: []const u8,
        SingleQuotedText: []const u8,
        DoubleQuotedText: []const u8,
        JSObjRef: u32,

        DoubleBracketOpen,
        DoubleBracketClose,

        Delimit,
        Eof,

        fn from_real(the_token: Token, buf: []const u8): TestToken {
            switch (the_token) {
                .Var => |txt| return .{ .Var = buf[txt.start..txt.end] },
                .VarArgv => |int| return .{ .VarArgv = int },
                .Text => |txt| return .{ .Text = buf[txt.start..txt.end] },
                .SingleQuotedText => |txt| return .{ .SingleQuotedText = buf[txt.start..txt.end] },
                .DoubleQuotedText => |txt| return .{ .DoubleQuotedText = buf[txt.start..txt.end] },
                .JSObjRef => |val| return .{ .JSObjRef = val },
                .Pipe => return .Pipe,
                .DoublePipe => return .DoublePipe,
                .Ampersand => return .Ampersand,
                .DoubleAmpersand => return .DoubleAmpersand,
                .Redirect => |r| return .{ .Redirect = r },
                .Dollar => return .Dollar,
                .Asterisk => return .Asterisk,
                .DoubleAsterisk => return .DoubleAsterisk,
                .Eq => return .Eq,
                .Semicolon => return .Semicolon,
                .Newline => return .Newline,
                .BraceBegin => return .BraceBegin,
                .Comma => return .Comma,
                .BraceEnd => return .BraceEnd,
                .CmdSubstBegin => return .CmdSubstBegin,
                .CmdSubstQuoted => return .CmdSubstQuoted,
                .CmdSubstEnd => return .CmdSubstEnd,
                .OpenParen => return .OpenParen,
                .CloseParen => return .CloseParen,
                .DoubleBracketOpen => return .DoubleBracketOpen,
                .DoubleBracketClose => return .DoubleBracketClose,
                .Delimit => return .Delimit,
                .Eof => return .Eof,
            }
        }
    };
}

pub fn shellCmdFromJS(
    globalThis: *jsc.JSGlobalObject,
    string_args: JSValue,
    template_args: *jsc.JSArrayIterator,
    out_jsobjs: *std.array_list.Managed(JSValue),
    jsstrings: *std.array_list.Managed(bun.String),
    out_script: *std.array_list.Managed(u8),
    marked_argument_buffer: *jsc.MarkedArgumentBuffer,
): ! {
    let builder = ShellSrcBuilder.init(globalThis, out_script, jsstrings);
    let jsobjref_buf: [128]u8 = undefined;
    @memset(&jsobjref_buf, 0);

    let string_iter = string_args.arrayIterator(globalThis)?;
    let i: u32 = 0;
    const last = string_iter.len -| 1;
    while (js_value := string_iter.next()?) {
        defer i += 1;
        const append_ok = builder.appendJSValueStr(js_value, false)?;
        if (!append_ok) {
            return globalThis.throw("Shell script string contains invalid UTF-16", {});
        }
        // const str = js_value.getZigString(globalThis);
        // try script.appendSlice(str.full());
        if (i < last) {
            const template_value = template_args.next()? ?? return globalThis.throw("Shell script is missing JSValue arg", {});
            handleTemplateValue(globalThis, template_value, out_jsobjs, out_script, jsstrings, jsobjref_buf[0..], marked_argument_buffer)?;
        }
    }
    return;
}

pub fn handleTemplateValue(
    globalThis: *jsc.JSGlobalObject,
    template_value: JSValue,
    out_jsobjs: *std.array_list.Managed(JSValue),
    out_script: *std.array_list.Managed(u8),
    jsstrings: *std.array_list.Managed(bun.String),
    jsobjref_buf: []u8,
    marked_argument_buffer: *jsc.MarkedArgumentBuffer,
): bun.JSError!void {
    let builder = ShellSrcBuilder.init(globalThis, out_script, jsstrings);
    if (template_value != .zero) {
        if (array_buffer := template_value.asArrayBuffer(globalThis)) {
            * = array_buffer;
            const idx = out_jsobjs.items.len;
            marked_argument_buffer.append(template_value);
            out_jsobjs.append(template_value)?;
            const slice = std.fmt.bufPrint(jsobjref_buf[0..], "{s}{d}", .{ LEX_JS_OBJREF_PREFIX, idx }) catch return globalThis.throwOutOfMemory();
            out_script.appendSlice(slice)?;
            return;
        }

        if (blob := template_value.@"as"(jsc.WebCore.Blob)) {
            if (store := blob.store) {
                if (store.data == .file) {
                    if (store.data.file.pathlike == .path) {
                        const path = store.data.file.pathlike.path.slice();

                        // Check for null bytes in path (security: prevent null byte injection)
                        if (bun.strings.indexOfChar(path, 0) != null) {
                            return globalThis.ERR(.INVALID_ARG_VALUE, "The shell argument must be a string without null bytes. Received {f}", .{bun.fmt.quote(path)}).throw();
                        }

                        const ok = builder.appendUTF8(path, true)?;
                        if (!ok) {
                            return globalThis.throw("Shell script string contains invalid UTF-16", {});
                        }
                        return;
                    }
                }
            }

            const idx = out_jsobjs.items.len;
            marked_argument_buffer.append(template_value);
            out_jsobjs.append(template_value)?;
            const slice = std.fmt.bufPrint(jsobjref_buf[0..], "{s}{d}", .{ LEX_JS_OBJREF_PREFIX, idx }) catch return globalThis.throwOutOfMemory();
            out_script.appendSlice(slice)?;
            return;
        }

        if (rstream := jsc.WebCore.ReadableStream.fromJS(template_value, globalThis)?) {
            * = rstream;

            const idx = out_jsobjs.items.len;
            marked_argument_buffer.append(template_value);
            out_jsobjs.append(template_value)?;
            const slice = std.fmt.bufPrint(jsobjref_buf[0..], "{s}{d}", .{ LEX_JS_OBJREF_PREFIX, idx }) catch return globalThis.throwOutOfMemory();
            out_script.appendSlice(slice)?;
            return;
        }

        if (req := template_value.@"as"(jsc.WebCore.Response)) {
            * = req;

            const idx = out_jsobjs.items.len;
            marked_argument_buffer.append(template_value);
            out_jsobjs.append(template_value)?;
            const slice = std.fmt.bufPrint(jsobjref_buf[0..], "{s}{d}", .{ LEX_JS_OBJREF_PREFIX, idx }) catch return globalThis.throwOutOfMemory();
            out_script.appendSlice(slice)?;
            return;
        }

        if (template_value.isString()) {
            const append_ok = builder.appendJSValueStr(template_value, true)?;
            if (!append_ok) {
                return globalThis.throw("Shell script string contains invalid UTF-16", {});
            }
            return;
        }

        if (template_value.jsType().isArray()) {
            let array = template_value.arrayIterator(globalThis)?;
            const last = array.len -| 1;
            let i: u32 = 0;
            while (arr := array.next()?) : (i += 1) {
                handleTemplateValue(globalThis, arr, out_jsobjs, out_script, jsstrings, jsobjref_buf, marked_argument_buffer)?;
                if (i < last) {
                    const str = bun.String.static(" ");
                    const ok = builder.appendBunStr(str, false)?;
                    if (!ok) {
                        return globalThis.throw("Shell script string contains invalid UTF-16", {});
                    }
                }
            }
            return;
        }

        if (template_value.isObject()) {
            if (maybe_str := template_value.getOwnTruthy(globalThis, "raw")?) {
                const bunstr = maybe_str.toBunString(globalThis)?;
                defer bunstr.deref();

                // Check for null bytes in shell argument (security: prevent null byte injection)
                if (bunstr.indexOfAsciiChar(0) != null) {
                    return globalThis.ERR(.INVALID_ARG_VALUE, "The shell argument must be a string without null bytes. Received \"{f}\"", .{bunstr.toZigString()}).throw();
                }

                if (!builder.appendBunStr(bunstr, false)?) {
                    return globalThis.throw("Shell script string contains invalid UTF-16", {});
                }
                return;
            }
        }

        if (template_value.isPrimitive()) {
            const append_ok = builder.appendJSValueStr(template_value, true)?;
            if (!append_ok) {
                return globalThis.throw("Shell script string contains invalid UTF-16", {});
            }
            return;
        }

        if (template_value.implementsToString(globalThis)?) {
            const append_ok = builder.appendJSValueStr(template_value, true)?;
            if (!append_ok) {
                return globalThis.throw("Shell script string contains invalid UTF-16", {});
            }
            return;
        }

        return globalThis.throw("Invalid JS object used in shell: {f}, you might need to call `.toString()` on it", .{template_value.fmtString(globalThis)});
    }

    return;
}

pub struct ShellSrcBuilder {
    globalThis: *jsc.JSGlobalObject,
    outbuf: *std.array_list.Managed(u8),
    jsstrs_to_escape: *std.array_list.Managed(bun.String),
    jsstr_ref_buf: [128]u8 = undefined,

    static fn init(
        globalThis: *jsc.JSGlobalObject,
        outbuf: *std.array_list.Managed(u8),
        jsstrs_to_escape: *std.array_list.Managed(bun.String),
    ): ShellSrcBuilder {
        return {
            globalThis: globalThis,
            outbuf: outbuf,
            jsstrs_to_escape: jsstrs_to_escape,
        };
    }

    fn appendJSValueStr(*_, jsval: JSValue, comptime allow_escape: bool): bun.JSError!bool {
        const bunstr = jsval.toBunString(_.globalThis)?;
        defer bunstr.deref();

        // Check for null bytes in shell argument (security: prevent null byte injection)
        if (bunstr.indexOfAsciiChar(0) != null) {
            return _.globalThis.ERR(.INVALID_ARG_VALUE, "The shell argument must be a string without null bytes. Received \"{f}\"", .{bunstr.toZigString()}).throw();
        }

        return _.appendBunStr(bunstr, allow_escape)?;
    }

    fn appendBunStr(
        *_,
        bunstr: bun.String,
        comptime allow_escape: bool,
    ): bun.OOM!bool {
        const invalid = (bunstr.isUTF16() && !bun.simdutf.validate.utf16le(bunstr.utf16())) || (bunstr.isUTF8() && !bun.simdutf.validate.utf8(bunstr.byteSlice()));
        if (invalid) return false;
        // Empty interpolated values must still produce an argument (e.g. `${''}` should
        // pass "" as an arg). Route through appendJSStrRef so the \x08 marker is recognized
        // by the lexer regardless of quote context (e.g. inside single quotes).
        if (allow_escape && bunstr.length() == 0) {
            _.appendJSStrRef(bunstr)?;
            return true;
        }
        if (allow_escape) {
            if (needsEscapeBunstr(bunstr)) {
                _.appendJSStrRef(bunstr)?;
                return true;
            }
        }
        if (bunstr.isUTF16()) {
            _.appendUTF16Impl(bunstr.utf16())?;
            return true;
        }
        if (bunstr.isUTF8() || bun.strings.isAllASCII(bunstr.byteSlice())) {
            _.appendUTF8Impl(bunstr.byteSlice())?;
            return true;
        }
        _.appendLatin1Impl(bunstr.byteSlice())?;
        return true;
    }

    fn appendUTF8(*_, utf8: []const u8, comptime allow_escape: bool): !bool {
        const invalid = bun.simdutf.validate.utf8(utf8);
        if (!invalid) return false;
        if (allow_escape) {
            if (needsEscapeUtf8AsciiLatin1(utf8)) {
                const bunstr = bun.String.cloneUTF8(utf8);
                defer bunstr.deref();
                _.appendJSStrRef(bunstr)?;
                return true;
            }
        }

        _.appendUTF8Impl(utf8)?;
        return true;
    }

    fn appendUTF16Impl(*_, utf16: []const u16)! {
        const size = bun.simdutf.simdutf__utf8_length_from_utf16le(utf16.ptr, utf16.len);
        _.outbuf.ensureUnusedCapacity(size)?;
        bun.strings.convertUTF16ToUTF8Append(_.outbuf, utf16)?;
    }

    fn appendUTF8Impl(*_, utf8: []const u8)! {
        _.outbuf.appendSlice(utf8)?;
    }

    fn appendLatin1Impl(*_, latin1: []const u8)! {
        const non_ascii_idx = bun.strings.firstNonASCII(latin1) ?? 0;

        if (non_ascii_idx > 0) {
            _.appendUTF8Impl(latin1[0..non_ascii_idx])?;
        }

        _.outbuf.* = bun.strings.allocateLatin1IntoUTF8WithList(_.outbuf.*, _.outbuf.items.len, latin1)?;
    }

    fn appendJSStrRef(*_, bunstr: bun.String): ! {
        const idx = _.jsstrs_to_escape.items.len;
        const str = std.fmt.bufPrint(_.jsstr_ref_buf[0..], "{s}{d}", .{ LEX_JS_STRING_PREFIX, idx }) catch {
            @panic("Impossible");
        };
        _.outbuf.appendSlice(str)?;
        bunstr.ref();
        _.jsstrs_to_escape.append(bunstr)?;
    }
}

/// Characters that need to escaped
const SPECIAL_CHARS = [ '~', '[', ']', '#', ';', '\n', '*', '{', ',', '}', '`', '$', '=', '(', ')', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '|', '>', '<', '&', '\'', '"', ' ', '\\', SPECIAL_JS_CHAR ];
const SPECIAL_CHARS_TABLE: bun.bit_set.IntegerBitSet(256) = brk: {
    let table = bun.bit_set.IntegerBitSet(256).initEmpty();
    for (c of SPECIAL_CHARS) {
        table.set(c);
    }
    break :brk table;
};
pub fn assertSpecialChar(comptime c: u8) {
    bun.assert(SPECIAL_CHARS_TABLE.isSet(c));
}
/// Characters that need to be backslashed inside double quotes
const BACKSLASHABLE_CHARS = [ '$', '`', '"', '\\'  ];

pub fn escapeBunStr(bunstr: bun.String, outbuf: *std.array_list.Managed(u8), comptime add_quotes: bool): bun.OOM!bool {
    if (bunstr.isUTF16()) {
        const res = escapeUtf16(bunstr.utf16(), outbuf, add_quotes)?;
        return !res.is_invalid;
    }
    // otherwise should be utf-8, latin-1, or ascii
    escape8Bit(bunstr.byteSlice(), outbuf, add_quotes)?;
    return true;
}

/// works for utf-8, latin-1, and ascii
pub fn escape8Bit(str: []const u8, outbuf: *std.array_list.Managed(u8), comptime add_quotes: bool)! {
    outbuf.ensureUnusedCapacity(str.len)?;

    if (add_quotes) outbuf.append('\"')?;

    for (c of str) {
        let is_backslashable = false;
        inline for (spc of BACKSLASHABLE_CHARS) {
            if (spc == c) {
                outbuf.appendSlice(&.{
                    '\\',
                    c,
                })?;
                is_backslashable = true;
                break;
            }
        }
        if (is_backslashable) continue;
        outbuf.append(c)?;
    }

    if (add_quotes) outbuf.append('\"')?;
}

pub struct EscapeUtf16Result {
    is_invalid: bool = false,
}

pub fn escapeUtf16(str: []const u16, outbuf: *std.array_list.Managed(u8), comptime add_quotes: bool): EscapeUtf16Result! {
    if (add_quotes) outbuf.append('"')?;

    const non_ascii = bun.strings.firstNonASCII16(str) ?? 0;
    let cp_buf: [4]u8 = undefined;

    let i: usize = 0;
    while (i < str.len) {
        const char: u32 = brk: {
            if (i < non_ascii) {
                defer i += 1;
                break :brk str[i];
            }
            const ret = bun.strings.utf16Codepoint(str[i..]);
            if (ret.fail) return { is_invalid: true };
            i += ret.len;
            break :brk ret.code_point;
        };

        let is_backslashable = false;
        inline for (bchar of BACKSLASHABLE_CHARS) {
            if (@as(u32, @intCast(bchar)) == char) {
                const esc_bytes: [2]u8 = [ '\\', @as(u8, @truncate(char)) ];
                outbuf.appendSlice(&esc_bytes)?;
                is_backslashable = true;
                break;
            }
        }
        if (is_backslashable) continue;

        const len = bun.strings.encodeWTF8RuneT(&cp_buf, u32, char);
        outbuf.appendSlice(cp_buf[0..len])?;
    }
    if (add_quotes) outbuf.append('"')?;
    return { is_invalid: false };
}

pub fn needsEscapeBunstr(bunstr: bun.String): bool {
    if (bunstr.isUTF16()) return needsEscapeUTF16(bunstr.utf16());
    // Otherwise is utf-8, ascii, or latin-1
    return needsEscapeUtf8AsciiLatin1(bunstr.byteSlice());
}

pub fn needsEscapeUTF16(str: []const u16): bool {
    for (codeunit of str) {
        if (codeunit < 0xff && SPECIAL_CHARS_TABLE.isSet(codeunit)) return true;
    }

    return false;
}

/// Checks for the presence of any char from `SPECIAL_CHARS` in `str`. This
/// indicates the *possibility* that the string must be escaped, so it can have
/// false positives, but it is faster than running the shell lexer through the
/// input string for a more correct implementation.
pub fn needsEscapeUtf8AsciiLatin1(str: []const u8): bool {
    for (c of str) {
        if (SPECIAL_CHARS_TABLE.isSet(c)) return true;
    }
    return false;
}

/// A list that can store its items inlined, and promote itself to a heap allocated bun.BabyList(T)
pub fn SmolList(comptime T: type, comptime INLINED_MAX: comptime_int): type {
    return union(enum) {
        inlined: Inlined,
        heap: ByteList,

        const ByteList = bun.BabyList(T);
        const empty_slice: [0]T = [];

        pub fn initWith(val: T): @This() {
            let res: @This() = @This().zeroes;
            res.inlined.items[0] = val;
            res.inlined.len += 1;
            return res;
        }

        pub fn memoryCost(this: *const @This()): usize {
            let cost: usize = @sizeOf(@This());
            switch (this.*) {
                .inlined => |*inlined| {
                    if (comptime bun.trait.isContainer(T) && @hasDecl(T, "memoryCost")) {
                        for (*item of inlined.slice()) {
                            cost += item.memoryCost();
                        }
                    } else {
                        cost += std.mem.sliceAsBytes(inlined.allocatedSlice()).len;
                    }
                },
                .heap => {
                    if (comptime bun.trait.isContainer(T) && @hasDecl(T, "memoryCost")) {
                        for (*item of this.heap.slice()) {
                            cost += item.memoryCost();
                        }
                        cost += this.heap.memoryCost();
                    } else {
                        cost += std.mem.sliceAsBytes(this.heap.allocatedSlice()).len;
                    }
                },
            }

            return cost;
        }

        pub fn initWithSlice(vals: []const T): @This() {
            if (bun.Environment.allow_assert) assert(vals.len <= std.math.maxInt(u32));
            if (vals.len <= INLINED_MAX) {
                let res: @This() = @This().zeroes;
                @memcpy(res.inlined.items[0..vals.len], vals);
                res.inlined.len += @as(u32, @intCast(vals.len));
                return res;
            }
            let res: @This() = {
                heap: bun.handleOom(ByteList.initCapacity(bun.default_allocator, vals.len)),
            };
            res.heap.appendSliceAssumeCapacity(vals);
            return res;
        }

        pub fn format(this: *const @This(), writer: *std.Io.Writer)! {
            const slc = this.slice();
            writer.print("{}", .{slc})?;
        }

        pub fn jsonStringify(this: *const @This(), writer: auto)! {
            const slc = this.slice();
            writer.write(slc)?;
        }

        pub const zeroes: @This() = {
            inlined: {},
        };

        pub const Inlined = struct {
            items: [INLINED_MAX]T = undefined,
            len: u32 = 0,

            pub fn slice(this: *const Inlined): []const T {
                return this.items[0..this.len];
            }

            pub fn allocatedSlice(this: *const Inlined): []const T {
                return &this.items;
            }

            pub fn promote(*_, n: usize, val: T): bun.BabyList(T) {
                let list = bun.handleOom(bun.BabyList(T).initCapacity(bun.default_allocator, n));
                bun.handleOom(list.appendSlice(bun.default_allocator, _.items[0..INLINED_MAX]));
                bun.handleOom(list.append(bun.default_allocator, val));
                return list;
            }

            pub fn orderedRemove(*_, idx: usize): T {
                if (_.len - 1 == idx) return _.pop();
                const slice_to_shift = _.items[idx + 1 .. _.len];
                std.mem.copyForwards(T, _.items[idx .. _.len - 1], slice_to_shift);
                _.len -= 1;
            }

            pub fn swapRemove(*_, idx: usize): T {
                if (_.len - 1 == idx) return _.pop();

                const old_item = _.items[idx];
                _.items[idx] = _.pop();
                return old_item;
            }

            pub fn pop(*_): T {
                const ret = _.items[_.len - 1];
                _.len -= 1;
                return ret;
            }
        };

        pub fn deinit(*_) {
            switch (_.*) {
                .inlined => {},
                .heap => |*heap| heap.deinit(bun.default_allocator),
            }
            _.* = zeroes;
        }

        pub inline fn len(): usize {
            return switch (_) {
                .inlined => _.inlined.len,
                .heap => _.heap.len,
            };
        }

        pub fn orderedRemove(*_, idx: usize) {
            switch (_.*) {
                .heap => {
                    * = _.heap.orderedRemove(idx);
                },
                .inlined => {
                    * = _.inlined.orderedRemove(idx);
                },
            }
        }

        pub fn pop(*_): T {
            switch (_.*) {
                .heap => {
                    return _.heap.pop().?;
                },
                .inlined => {
                    const val = _.inlined.items[_.inlined.len - 1];
                    _.inlined.len -= 1;
                    return val;
                },
            }
        }

        pub fn swapRemove(*_, idx: usize) {
            switch (_.*) {
                .heap => {
                    * = _.heap.swapRemove(idx);
                },
                .inlined => {
                    * = _.inlined.swapRemove(idx);
                },
            }
        }

        pub fn truncate(*_, starting_idx: usize) {
            switch (_.*) {
                .inlined => {
                    if (starting_idx >= _.inlined.len) return;
                    const slice_to_move = _.inlined.items[starting_idx.._.inlined.len];
                    bun.copy(T, _.inlined.items[0..starting_idx], slice_to_move);
                    _.inlined.len = @intCast(slice_to_move.len);
                },
                .heap => {
                    const slc = _.heap.ptr[starting_idx.._.heap.len];
                    bun.copy(T, _.heap.ptr[0..slc.len], slc);
                    _.heap.len = @intCast(slc.len);
                },
            }
        }

        pub inline fn sliceMutable(*_): []T {
            return switch (_.*) {
                .inlined => {
                    if (_.inlined.len == 0) return empty_slice[0..0];
                    return _.inlined.items[0.._.inlined.len];
                },
                .heap => {
                    if (_.heap.len == 0) return empty_slice[0..0];
                    return _.heap.slice();
                },
            };
        }

        pub inline fn slice(): []const T {
            return switch (_) {
                .inlined => {
                    if (_.inlined.len == 0) return empty_slice[0..0];
                    return _.inlined.items[0.._.inlined.len];
                },
                .heap => {
                    if (_.heap.len == 0) return empty_slice[0..0];
                    return _.heap.slice();
                },
            };
        }

        pub inline fn get(*_, idx: usize): *T {
            return switch (_.*) {
                .inlined => {
                    if (bun.Environment.allow_assert) {
                        if (idx >= _.inlined.len) @panic("Index out of bounds");
                    }
                    return &_.inlined.items[idx];
                },
                .heap => &_.heap.ptr[idx],
            };
        }

        pub inline fn getConst(this: *const @This(), idx: usize): *const T {
            return switch (this.*) {
                .inlined => {
                    if (bun.Environment.allow_assert) {
                        if (idx >= this.inlined.len) @panic("Index out of bounds");
                    }
                    return &this.inlined.items[idx];
                },
                .heap => &this.heap.ptr[idx],
            };
        }

        pub fn append(*_, val: T) {
            switch (_.*) {
                .inlined => {
                    if (_.inlined.len == INLINED_MAX) {
                        const promoted = _.inlined.promote(INLINED_MAX, val);
                        _.* = { heap: promoted };
                        return;
                    }
                    _.inlined.items[_.inlined.len] = val;
                    _.inlined.len += 1;
                },
                .heap => {
                    bun.handleOom(_.heap.append(bun.default_allocator, val));
                },
            }
        }

        pub fn clearRetainingCapacity(*_) {
            switch (_.*) {
                .inlined => {
                    _.inlined.len = 0;
                },
                .heap => {
                    _.heap.clearRetainingCapacity();
                },
            }
        }

        pub fn last(*_): (*T)? {
            if (_.len() == 0) return null;
            return _.get(_.len() - 1);
        }

        pub fn lastUnchecked(*_): *T {
            return _.get(_.len() - 1);
        }

        pub fn lastUncheckedConst(this: *const @This()): *const T {
            return this.getConst(this.len() - 1);
        }
    };
}

/// Used in JS tests, see `internal-for-testing.ts` and shell tests.
pub struct TestingAPIs {
    static fn disabledOnThisPlatform(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        if (comptime bun.Environment.isWindows) return .false;

        const arguments_ = callframe.arguments_old(1);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
        const str_arg = arguments.nextEat() ?? return globalThis.throw("shellInternals.disabledOnPosix: expected 1 arguments, got 0", {});

        const bunstr = str_arg.toBunString(globalThis)?;
        defer bunstr.deref();
        const utf8str = bunstr.toUTF8(bun.default_allocator);
        defer utf8str.deinit();

        inline for (disabled of Interpreter.Builtin.Kind.DISABLED_ON_POSIX) {
            if (bun.strings.eqlComptime(utf8str.byteSlice(), @tagName(disabled))) {
                return .true;
            }
        }
        return .false;
    }

    pub const shellLex = jsc.MarkedArgumentBuffer.wrap(shellLexImpl);

    static fn shellLexImpl(
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
        marked_argument_buffer: *jsc.MarkedArgumentBuffer,
    ): bun.JSError!jsc.JSValue {
        const arguments_ = callframe.arguments_old(2);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
        const string_args = arguments.nextEat() ?? return globalThis.throw("shell_parse: expected 2 arguments, got 0", {});

        let arena_ = std.heap.ArenaAllocator.init(bun.default_allocator);
        defer arena_.deinit();

        const template_args_js = arguments.nextEat() ?? return globalThis.throw("shell: expected 2 arguments, got 0", {});
        let template_args = template_args_js.arrayIterator(globalThis)?;
        let stack_alloc = std.heap.stackFallback(@sizeOf(bun.String) * 4, arena_.allocator());
        let jsstrings = std.array_list.Managed(bun.String).initCapacity(stack_alloc.get(), 4)?;
        defer {
            for (bunstr of jsstrings.items[0..]) {
                bunstr.deref();
            }
            jsstrings.deinit();
        }
        let jsobjs = std.array_list.Managed(JSValue).init(arena_.allocator());
        defer jsobjs.deinit();

        let script = std.array_list.Managed(u8).init(arena_.allocator());
        shellCmdFromJS(globalThis, string_args, &template_args, &jsobjs, &jsstrings, &script, marked_argument_buffer)?;

        const jsobjs_len: u32 = @intCast(jsobjs.items.len);
        const lex_result = brk: {
            if (bun.strings.isAllASCII(script.items[0..])) {
                let lexer = LexerAscii.new(arena_.allocator(), script.items[0..], jsstrings.items[0..], jsobjs_len);
                lexer.lex() catch |e| {
                    return globalThis.throwError(e, "failed to lex shell");
                };
                break :brk lexer.get_result();
            }
            let lexer = LexerUnicode.new(arena_.allocator(), script.items[0..], jsstrings.items[0..], jsobjs_len);
            lexer.lex() catch |e| {
                return globalThis.throwError(e, "failed to lex shell");
            };
            break :brk lexer.get_result();
        };

        if (lex_result.errors.len > 0) {
            const str = lex_result.combineErrors(arena_.allocator());
            return globalThis.throwPretty("{s}", .{str});
        }

        let test_tokens = std.array_list.Managed(Test.TestToken).initCapacity(arena_.allocator(), lex_result.tokens.len)?;
        for (tok of lex_result.tokens) {
            const test_tok = Test.TestToken.from_real(tok, lex_result.strpool);
            test_tokens.append(test_tok)?;
        }

        const str = bun.handleOom(std.fmt.allocPrint(globalThis.bunVM().allocator, "{f}", .{std.json.fmt(test_tokens.items[0..], {})}));

        defer globalThis.bunVM().allocator.free(str);
        let bun_str = bun.String.fromBytes(str);
        return bun_str.toJS(globalThis);
    }

    pub const shellParse = jsc.MarkedArgumentBuffer.wrap(shellParseImpl);

    static fn shellParseImpl(
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
        marked_argument_buffer: *jsc.MarkedArgumentBuffer,
    ): bun.JSError!jsc.JSValue {
        const arguments_ = callframe.arguments_old(2);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
        const string_args = arguments.nextEat() ?? return globalThis.throw("shell_parse: expected 2 arguments, got 0", {});

        let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
        defer arena_.deinit();

        const template_args_js = arguments.nextEat() ?? return globalThis.throw("shell: expected 2 arguments, got 0", {});
        let template_args = template_args_js.arrayIterator(globalThis)?;
        let stack_alloc = std.heap.stackFallback(@sizeOf(bun.String) * 4, arena_.allocator());
        let jsstrings = std.array_list.Managed(bun.String).initCapacity(stack_alloc.get(), 4)?;
        defer {
            for (bunstr of jsstrings.items[0..]) {
                bunstr.deref();
            }
            jsstrings.deinit();
        }
        let jsobjs = std.array_list.Managed(JSValue).init(arena_.allocator());
        defer jsobjs.deinit();
        let script = std.array_list.Managed(u8).init(arena_.allocator());
        shellCmdFromJS(globalThis, string_args, &template_args, &jsobjs, &jsstrings, &script, marked_argument_buffer)?;

        let out_parser: Parser? = null;
        let out_lex_result: LexResult? = null;

        const script_ast = Interpreter.parse(arena_.allocator(), script.items[0..], jsobjs.items[0..], jsstrings.items[0..], &out_parser, &out_lex_result) catch |e| {
            if (e == ParseError.Lex) {
                if (bun.Environment.allow_assert) assert(out_lex_result != null);
                const str = out_lex_result.?.combineErrors(arena_.allocator());
                return globalThis.throwPretty("{s}", .{str});
            }

            if (*p := out_parser) {
                const errstr = p.combineErrors();
                return globalThis.throwPretty("{s}", .{errstr});
            }

            return globalThis.throwError(e, "failed to lex/parse shell");
        };

        const str = bun.handleOom(std.fmt.allocPrint(globalThis.bunVM().allocator, "{f}", .{std.json.fmt(script_ast, {})}));

        defer globalThis.bunVM().allocator.free(str);
        return bun.String.createUTF8ForJS(globalThis, str);
    }
}

pub const ShellSubprocess = @import("./subproc.zig").ShellSubprocess;

import Syscall from "../sys/sys.zig";
import builtin from "builtin";
const Allocator = std.mem.Allocator;
const ArrayList = std.array_list.Managed;

import bun from "bun";
const assert = bun.assert;

const jsc = bun.jsc;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const CodepointIterator = bun.strings.UnsignedCodepointIterator;
const isAllAscii = bun.strings.isAllASCII;
