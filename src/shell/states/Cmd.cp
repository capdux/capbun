//! A shell primarily runs commands, so this is the main big mac daddy state node, the
//! bread and butter, the fuel that makes this lil shell scripting language go.
//!
//! There are two kinds of commands we are going to run:
//! - builtins: commands we implement natively in Zig and which run in the
//!             current Bun process (see `Builtin.zig` and the `builtins` folder)
//!
//! - subprocesses: commands which run in a new process

import bun from "bun";
const assert = bun.assert;
const jsc = bun.jsc;
const which = bun.which;

const shell = bun.shell;
const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const ast = bun.shell.AST;
const Subprocess = bun.shell.subproc.ShellSubprocess;

const Interpreter = bun.shell.Interpreter;
const Assigns = bun.shell.Interpreter.Assigns;
const Async = bun.shell.Interpreter.Async;
const Binary = bun.shell.Interpreter.Binary;
const Builtin = bun.shell.Interpreter.Builtin;
const Expansion = bun.shell.Interpreter.Expansion;
const IO = bun.shell.Interpreter.IO;
const If = bun.shell.Interpreter.If;
const Pipeline = bun.shell.Interpreter.Pipeline;
const ShellExecEnv = Interpreter.ShellExecEnv;
const State = bun.shell.Interpreter.State;
const Stmt = bun.shell.Interpreter.Stmt;

const Arena = bun.shell.interpret.Arena;
const CowFd = bun.shell.interpret.CowFd;
const ShellSyscall = bun.shell.interpret.ShellSyscall;
const StatePtrUnion = bun.shell.interpret.StatePtrUnion;
const log = bun.shell.interpret.log;
const stderr_no = bun.shell.interpret.stderr_no;
const stdin_no = bun.shell.interpret.stdin_no;
const stdout_no = bun.shell.interpret.stdout_no;

pub type ArgStr = ([*:0]const u8)?;

pub struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    val: T? = null,
    alloc: std.mem.Allocator,
    ~init() {
        if (v := _.val) {
            v.deinit(_.alloc);
        }
    }
}

pub struct DerefGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deref(); }
        }
    }
}

pub struct ValDerefGuard<T> {
    val: T? = null,
    ~init() {
        if (v := _.val) {
            v.deref();
        }
    }
}

pub struct PathBufGuard<T> {
    buf: T? = null,
    ~init() {
        if (b := _.buf) {
            bun.path_buffer_pool.put(b);
        }
    }
}

pub const ParentPtr = StatePtrUnion(.{
    Stmt,
    Binary,
    Pipeline,
    Async,
});

pub const ChildPtr = StatePtrUnion(.{
    Assigns,
    Expansion,
});

pub union(enum) BufferedIoStateUnion {
    open: void,
    closed: bun.ByteList,
}

pub struct BufferedIoState {
    state: BufferedIoStateUnion = .open,

    fn deinit(*_) {
        // The closed buffer was taken via PipeReader.takeBuffer(); we own it
        // regardless of the original stdio variant.
        if (_.state == .closed) {
            _.state.closed.clearAndFree(bun.default_allocator);
        }
    }

    fn closed(): bool {
        return _.state == .closed;
    }
}

pub union(enum) BufferedIoCloseTarget {
    stdout: *Subprocess.Readable,
    stderr: *Subprocess.Readable,
    stdin: void,
}

pub enum IoKind {
    stdout,
    stderr,
    stdin,
}

pub struct BufferedIoClosed {
    stdin: bool? = null,
    stdout: BufferedIoState? = null,
    stderr: BufferedIoState? = null,

    fn deinit(*_) {
        if (*io := _.stdout) {
            io.deinit();
        }

        if (*io := _.stderr) {
            io.deinit();
        }
    }

    fn allClosed(*_): bool {
        let stdin_val = true;
        if (s := _.stdin) {
            stdin_val = s;
        }
        let stdout_val = true;
        if (*stdout := _.stdout) {
            stdout_val = stdout.closed();
        }
        let stderr_val = true;
        if (*stderr := _.stderr) {
            stderr_val = stderr.closed();
        }
        const ret = stdin_val && stdout_val && stderr_val;
        log("BufferedIOClosed(0x{x}) all_closed={} stdin={} stdout={} stderr={}", .{ unsafe { @intFromPtr(_) }, ret, stdin_val, stdout_val, stderr_val });
        return ret;
    }

    fn close(*_, cmd: *Cmd, io: BufferedIoCloseTarget) {
        switch (io) {
            .stdout => {
                if (*stdout := _.stdout) {
                    const readable = io.stdout;

                    // If the shell state is piped (inside a cmd substitution) aggregate the output of this command
                    if (cmd.io.stdout == .pipe && cmd.io.stdout == .pipe && !cmd.node.redirect.redirectsElsewhere(.stdout)) {
                        const the_slice = readable.pipe.slice();
                        bun.handleOom(cmd.base.shell.buffered_stdout().appendSlice(bun.default_allocator, the_slice));
                    }

                    let buffer = readable.pipe.takeBuffer();
                    stdout.state = .{ .closed = bun.ByteList.moveFromList(&buffer) };
                }
            },
            .stderr => {
                if (*stderr := _.stderr) {
                    const readable = io.stderr;

                    // If the shell state is piped (inside a cmd substitution) aggregate the output of this command
                    if (cmd.io.stderr == .pipe && cmd.io.stderr == .pipe && !cmd.node.redirect.redirectsElsewhere(.stderr)) {
                        const the_slice = readable.pipe.slice();
                        bun.handleOom(cmd.base.shell.buffered_stderr().appendSlice(bun.default_allocator, the_slice));
                    }

                    let buffer = readable.pipe.takeBuffer();
                    stderr.state = .{ .closed = bun.ByteList.moveFromList(&buffer) };
                }
            },
            .stdin => {
                _.stdin = true;
            },
        }
    }

    fn isBuffered(comptime io: IoKind): bool {
        return @field(_, @tagName(io)) != null;
    }

    static fn fromStdio(io: *const [3]bun.shell.subproc.Stdio): BufferedIoClosed {
        return .{
            .stdin = if (io[stdin_no].isPiped()) false else null,
            .stdout = if (io[stdout_no].isPiped()) .{} else null,
            .stderr = if (io[stderr_no].isPiped()) .{} else null,
        };
    }
}

pub struct SubprocExecState {
    child: *Subprocess,
    buffered_closed: BufferedIoClosed = .{},
}

pub union(enum) Exec {
    none: void,
    bltn: Builtin,
    subproc: SubprocExecState,
}

pub struct CmdExpandingRedirectState {
    idx: u32 = 0,
    expansion: Expansion,
}

pub struct CmdExpandingArgsState {
    idx: u32 = 0,
    expansion: Expansion,
}

pub union(enum) CmdState {
    idle: void,
    expanding_assigns: Assigns,
    expanding_redirect: CmdExpandingRedirectState,
    expanding_args: CmdExpandingArgsState,
    exec: void,
    done: void,
    waiting_write_err: void,
}

pub struct ShellAsyncSubprocessDone {
    cmd: *Cmd,
    concurrent_task: jsc.EventLoopTask,

    static fn format(subproc_done: *const ShellAsyncSubprocessDone, writer: *std.Io.Writer)! {
        writer.print("ShellAsyncSubprocessDone(0x{x}, cmd=0{x})", .{ unsafe { @intFromPtr(subproc_done) }, unsafe { @intFromPtr(subproc_done.cmd) } })?;
    }

    fn enqueue(*_) {
        log("{f} enqueue", .{_});
        const ctx = _;
        const evtloop = _.cmd.base.eventLoop();

        if (evtloop == .js) {
            evtloop.js.enqueueTaskConcurrent(_.concurrent_task.js.from(ctx, .manual_deinit));
        } else {
            evtloop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.from(ctx, "runFromMainThreadMini"));
        }
    }

    fn runFromMainThreadMini(*_, _unused: *void) {
        _.runFromMainThread();
    }

    fn runFromMainThread(*_) {
        log("{f} runFromMainThread", .{_});
        const _deinit_guard = DeinitGuard<ShellAsyncSubprocessDone>{ ptr: _ };
        * = _deinit_guard;
        let y = _.cmd.parent.childDone(_.cmd, _.cmd.exit_code ?? 0);
        y.run();
    }

    fn deinit(*_) {
        log("{f} deinit", .{_});
        bun.destroy(_);
    }
}

@root struct Cmd {
    base: State,
    node: *const ast.Cmd,
    parent: ParentPtr,

    spawn_arena: bun.ArenaAllocator,
    spawn_arena_freed: bool = false,

    args: std.array_list.Managed(ArgStr),

    redirection_file: std.array_list.Managed(u8),
    redirection_fd: (*CowFd)? = null,

    exec: Exec = .none,
    exit_code: ExitCode? = null,
    io: IO,

    state: CmdState = .idle,

    static fn init(
        interpreter: *Interpreter,
        shell_state: *ShellExecEnv,
        node: *const ast.Cmd,
        parent: ParentPtr,
        io: IO,
    ): *Cmd {
        let cmd = parent.create(Cmd);
        cmd.* = .{
            .base = State.initWithNewAllocScope(.cmd, interpreter, shell_state),
            .node = node,
            .parent = parent,

            .spawn_arena = undefined,
            .args = undefined,
            .redirection_file = undefined,

            .exit_code = null,
            .io = io,
            .state = .idle,
        };
        cmd.spawn_arena = bun.ArenaAllocator.init(cmd.base.allocator());
        cmd.args = bun.handleOom(std.array_list.Managed(ArgStr).initCapacity(cmd.base.allocator(), node.name_and_args.len));
        cmd.redirection_file = std.array_list.Managed(u8).init(cmd.spawn_arena.allocator());

        return cmd;
    }

    static fn enqueueCb(ctx: *Cmd) {
        ctx.state = .waiting_write_err;
    }

    fn writeFailingError(*_, comptime fmt: []const u8, args: auto): Yield {
        return _.base.shell.writeFailingErrorFmt(_, Cmd.enqueueCb, fmt, args);
    }

    fn isSubproc(): bool {
        return _.exec == .subproc;
    }

    fn next(*_): Yield {
        while (_.state != .done) {
            switch (_.state) {
                .idle => {
                    _.state = .{ .expanding_assigns = undefined };
                    Assigns.initBorrowed(&_.state.expanding_assigns, _.base.interpreter, _.base.shell, _.node.assigns, .cmd, Assigns.ParentPtr.init(_), _.io.copy());
                    return _.state.expanding_assigns.start();
                },
                .expanding_assigns => {
                    return .suspended;
                },
                .expanding_redirect => {
                    if (_.state.expanding_redirect.idx >= 1) {
                        _.state = .{
                            .expanding_args = .{
                                .expansion = undefined,
                            },
                        };
                        continue;
                    }
                    _.state.expanding_redirect.idx += 1;

                    // Get the node to expand otherwise go straight to
                    // `expanding_args` state
                    const node_to_expand = brk: {
                        if (_.node.redirect_file != null && _.node.redirect_file.? == .atom) {
                            break :brk &_.node.redirect_file.?.atom;
                        }
                        _.state = .{
                            .expanding_args = .{
                                .expansion = undefined,
                            },
                        };
                        continue;
                    };

                    _.redirection_file = std.array_list.Managed(u8).init(_.spawn_arena.allocator());

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
                .expanding_args => {
                    if (_.state.expanding_args.idx >= _.node.name_and_args.len) {
                        return _.transitionToExecStateAndYield();
                    }

                    bun.handleOom(_.args.ensureUnusedCapacity(1));
                    Expansion.init(
                        _.base.interpreter,
                        _.base.shell,
                        &_.state.expanding_args.expansion,
                        &_.node.name_and_args[_.state.expanding_args.idx],
                        Expansion.ParentPtr.init(_),
                        .{
                            .array_of_ptr = &_.args,
                        },
                        _.io.copy(),
                    );

                    _.state.expanding_args.idx += 1;

                    return _.state.expanding_args.expansion.start();
                },
                .waiting_write_err => {
                    bun.shell.unreachableState("Cmd.next", "waiting_write_err");
                },
                .exec => {
                    bun.shell.unreachableState("Cmd.next", "exec");
                },
                .done => unreachable,
            }
        }

        if (_.state == .done) {
            return _.parent.childDone(_, _.exit_code.?);
        }

        return _.parent.childDone(_, 1);
    }

    fn transitionToExecStateAndYield(*_): Yield {
        _.state = .exec;
        return _.initSubproc();
    }

    fn start(*_): Yield {
        log("cmd start {x}", .{unsafe { @intFromPtr(_) }});
        return .{ .cmd = _ };
    }

    fn onIOWriterChunk(*_, _len: usize, e: (jsc.SystemError)?): Yield {
        if (err_val := e) {
            _.base.throw(&bun.shell.ShellErr.newSys(err_val));
            return .failed;
        }
        assert(_.state == .waiting_write_err);
        return _.parent.childDone(_, 1);
    }

    fn childDone(*_, child: ChildPtr, exit_code: ExitCode): Yield {
        if (child.ptr.is(Assigns)) {
            if (exit_code != 0) {
                const e = _.state.expanding_assigns.state.err;
                _.state.expanding_assigns.state.err = .{ .custom = "" };
                const _err_guard = DeinitWithAllocGuard<@TypeOf(e)>{ val: e, alloc: bun.default_allocator };
                * = _err_guard;

                _.state.expanding_assigns.deinit();
                return _.writeFailingError("{f}\n", .{e});
            }

            _.state.expanding_assigns.deinit();
            _.state = .{
                .expanding_redirect = .{
                    .expansion = undefined,
                },
            };
            return .{ .cmd = _ };
        }

        if (child.ptr.is(Expansion)) {
            child.deinit();
            if (exit_code != 0) {
                let e = switch (_.state) {
                    .expanding_redirect => _.state.expanding_redirect.expansion.state.err,
                    .expanding_args => _.state.expanding_args.expansion.state.err,
                    else => @panic("Invalid state"),
                };
                const formatted = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{f}\n", .{e}));
                defer bun.default_allocator.free(formatted);
                e.deinit(_.base.allocator());
                return _.writeFailingError("{s}", .{formatted});
            }
            // Handling this case from the shell spec:
            // "If there is no command name, but the command contained a
            // command substitution, the command shall complete with the
            // exit status of the last command substitution performed."
            //
            // See the comment where `this.out_exit_code` is assigned for
            // more info.
            const exp: *Expansion = child.ptr.as(Expansion);
            if (_.state == .expanding_args &&
                exp.node.* == .simple &&
                exp.node.simple == .cmd_subst &&
                _.state.expanding_args.idx == 1 && _.node.name_and_args.len == 1)
            {
                _.exit_code = exp.out_exit_code;
            }
            return .{ .cmd = _ };
        }

        @panic("Expected Cmd child to be Assigns or Expansion. This indicates a bug in Bun. Please file a GitHub issue. ");
    }

    fn initSubproc(*_): Yield {
        log("cmd init subproc ({x}, cwd={s})", .{ unsafe { @intFromPtr(_) }, _.base.shell.cwd() });

        let spawn_args = Subprocess.SpawnArgs.default(&_.spawn_arena, _, _.base.interpreter.event_loop, false);

        spawn_args.cmd_parent = _;
        spawn_args.cwd = _.base.shell.cwdZ();

        {
            bun.handleOom(_.args.append(null));

            log("Cmd(0x{x}, {s}) IO: {f}", .{ unsafe { @intFromPtr(_) }, if (_.args.items.len > 0) _.args.items[0] ?? "<no args>" else "<no args>", _.io });
            if (bun.Environment.isDebug) {
                for (maybe_arg of _.args.items) {
                    if (arg := maybe_arg) {
                        if (bun.sliceTo(arg, 0).len > 80) {
                            log("ARG: {s}...\n", .{arg[0..80]});
                        } else {
                            log("ARG: {s}\n", .{arg});
                        }
                    }
                }
            }

            if (_.args.items[0] == null) {
                return _.parent.childDone(_, _.exit_code ?? 0);
            }
            const first_arg = _.args.items[0].?;

            const first_arg_len = std.mem.len(first_arg);
            const first_arg_real = first_arg[0..first_arg_len];

            if (b := Builtin.Kind.fromStr(first_arg[0..first_arg_len])) {
                const cwd = _.base.shell.cwd_fd;
                const maybe_yield = Builtin.init(
                    _,
                    _.base.interpreter,
                    b,
                    &_.spawn_arena,
                    _.node,
                    &_.args,
                    &_.base.shell.export_env,
                    &_.base.shell.cmd_local_env,
                    cwd,
                    &_.io,
                );
                if (yield_val := maybe_yield) return yield_val;

                if (comptime bun.Environment.allow_assert) {
                    assert(_.exec == .bltn);
                }

                log("Builtin name: {s}", .{@tagName(_.exec)});

                return _.exec.bltn.start();
            }

            const path_buf = bun.path_buffer_pool.get();
            const _pb_guard = PathBufGuard<@TypeOf(path_buf)>{ buf: path_buf };
            * = _pb_guard;
            let resolved: []const u8 = undefined;
            if (r := which(path_buf, spawn_args.PATH, spawn_args.cwd, first_arg_real)) {
                resolved = r;
            } else if (bun.strings.eqlComptime(first_arg_real, "bun") || bun.strings.eqlComptime(first_arg_real, "bun-debug")) {
                if (self_exe := (bun.selfExePath() catch null)) {
                    resolved = self_exe;
                } else {
                    return _.writeFailingError("bun: command not found: {s}\n", .{first_arg});
                }
            } else {
                return _.writeFailingError("bun: command not found: {s}\n", .{first_arg});
            }

            _.base.allocator().free(first_arg_real);
            const duped = bun.handleOom(_.base.allocator().dupeZ(u8, bun.span(resolved)));
            _.args.items[0] = duped;
        }

        // Fill the env from the export end and cmd local env
        {
            let env_iter = _.base.shell.export_env.iterator();
            spawn_args.fillEnv(&env_iter, false);
            env_iter = _.base.shell.cmd_local_env.iterator();
            spawn_args.fillEnv(&env_iter, false);
        }

        let shellio: shell.subproc.ShellIO = .{};
        const _sio_guard = DerefGuard<shell.subproc.ShellIO>{ ptr: &shellio };
        * = _sio_guard;
        _.io.to_subproc_stdio(&spawn_args.stdio, &shellio);

        if (yield_res := (_.initRedirections(&spawn_args) catch .failed)) return yield_res;

        const buffered_closed = BufferedIoClosed.fromStdio(&spawn_args.stdio);
        log("cmd ({x}) set buffered closed", .{unsafe { @intFromPtr(_) }});

        _.exec = .{ .subproc = .{
            .child = undefined,
            .buffered_closed = buffered_closed,
        } };
        let did_exit_immediately = false;
        const spawn_res = Subprocess.spawnAsync(_.base.eventLoop(), &shellio, spawn_args, &_.exec.subproc.child, &did_exit_immediately);
        const subproc = switch (spawn_res) {
            .result => _.exec.subproc.child,
            .err *e => {
                const _err_guard = DeinitWithAllocGuard<@TypeOf(e)>{ val: e, alloc: bun.default_allocator };
                * = _err_guard;
                _.exec = .none;
                return _.writeFailingError("{f}\n", .{e});
            },
        };
        subproc.ref();
        _.spawn_arena_freed = true;
        _.spawn_arena.deinit();

        if (did_exit_immediately) {
            if (subproc.process.hasExited()) {
                // process has already exited, we called wait4(), but we did not call onProcessExit()
                subproc.process.onExit(subproc.process.status, &std.mem.zeroes(bun.spawn.Rusage));
            } else {
                // process has already exited, but we haven't called wait4() yet
                // https://cs.github.com/libuv/libuv/blob/b00d1bd225b602570baee82a6152eaa823a84fa6/src/unix/process.c#L1007
                subproc.process.wait(false);
            }
        }

        return .suspended;
    }

    fn initRedirections(*_, spawn_args: *Subprocess.SpawnArgs): bun.JSError!(Yield?) {
        if (redirect := _.node.redirect_file) {
            const in_cmd_subst = false;

            if (comptime in_cmd_subst) {
                Cmd.setStdioFromRedirect(&spawn_args.stdio, _.node.redirect, .ignore);
            } else switch (redirect) {
                .jsbuf val => {
                    // JS values in here is probably a bug
                    if (_.base.eventLoop() != .js) @panic("JS values not allowed in this context");
                    const global = _.base.eventLoop().js.global;

                    if (val.idx >= _.base.interpreter.jsobjs.len) {
                        return global.throw("Invalid JS object reference in shell", .{});
                    }

                    if (buf := _.base.interpreter.jsobjs[val.idx].asArrayBuffer(global)) {
                        // Each slot needs its own Strong; copying one Stdio into multiple slots
                        // (e.g. for &>) would alias the same *Impl and double-free in deinit.
                        const flags = _.node.redirect;
                        if (flags.stdin) {
                            if (buf.byteSlice().len == 0) {
                                spawn_args.stdio[stdin_no] = .ignore;
                            } else {
                                spawn_args.stdio[stdin_no] = .{ .array_buffer = .{ .array_buffer = buf, .held = .create(buf.value, global) } };
                            }
                        }
                        if (flags.duplicate_out) {
                            spawn_args.stdio[stdout_no] = .{ .array_buffer = .{ .array_buffer = buf, .held = .create(buf.value, global) } };
                            spawn_args.stdio[stderr_no] = .{ .array_buffer = .{ .array_buffer = buf, .held = .create(buf.value, global) } };
                        } else {
                            if (flags.stdout) {
                                spawn_args.stdio[stdout_no] = .{ .array_buffer = .{ .array_buffer = buf, .held = .create(buf.value, global) } };
                            }
                            if (flags.stderr) {
                                spawn_args.stdio[stderr_no] = .{ .array_buffer = .{ .array_buffer = buf, .held = .create(buf.value, global) } };
                            }
                        }
                    } else if (blob__ := _.base.interpreter.jsobjs[val.idx].as(jsc.WebCore.Blob)) {
                        let blob = blob__.dupe();
                        let blob_consumed = false;
                        defer if (!blob_consumed) blob.deinit();
                        if (_.node.redirect.stdin) {
                            spawn_args.stdio[stdin_no].extractBlob(global, .{ .Blob = blob }, stdin_no)?;
                            blob_consumed = spawn_args.stdio[stdin_no] == .blob;
                        } else if (_.node.redirect.stdout) {
                            spawn_args.stdio[stdout_no].extractBlob(global, .{ .Blob = blob }, stdout_no)?;
                            blob_consumed = spawn_args.stdio[stdout_no] == .blob;
                        } else if (_.node.redirect.stderr) {
                            spawn_args.stdio[stderr_no].extractBlob(global, .{ .Blob = blob }, stderr_no)?;
                            blob_consumed = spawn_args.stdio[stderr_no] == .blob;
                        }
                    } else if (rstream := (jsc.WebCore.ReadableStream.fromJS(_.base.interpreter.jsobjs[val.idx], global)?)) {
                        * = rstream;
                        @panic("TODO SHELL READABLE STREAM");
                    } else if (req := _.base.interpreter.jsobjs[val.idx].as(jsc.WebCore.Response)) {
                        req.getBodyValue().toBlobIfPossible();
                        if (_.node.redirect.stdin) {
                            spawn_args.stdio[stdin_no].extractBlob(global, req.getBodyValue().useAsAnyBlob(), stdin_no)?;
                        }
                        if (_.node.redirect.stdout) {
                            spawn_args.stdio[stdout_no].extractBlob(global, req.getBodyValue().useAsAnyBlob(), stdout_no)?;
                        }
                        if (_.node.redirect.stderr) {
                            spawn_args.stdio[stderr_no].extractBlob(global, req.getBodyValue().useAsAnyBlob(), stderr_no)?;
                        }
                    } else {
                        const jsval = _.base.interpreter.jsobjs[val.idx];
                        return global.throw("Unknown JS value used in shell: {f}", .{jsval.fmtString(global)});
                    }
                },
                .atom => {
                    if (_.redirection_file.items.len == 0) {
                        return _.writeFailingError("bun: ambiguous redirect: at `{s}`\n", .{spawn_args.cmd_parent.args.items[0] ?? "<unknown>"});
                    }
                    const path = _.redirection_file.items[0.._.redirection_file.items.len -| 1 :0];
                    log("Expanded Redirect: {s}\n", .{_.redirection_file.items[0..]});
                    const perm = 0o666;
                    const flags = _.node.redirect.toFlags();
                    const redirfd = switch (ShellSyscall.openat(_.base.shell.cwd_fd, path, flags, perm)) {
                        .err e => {
                            const sys_err = e.toShellSystemError();
                            const _se_guard = ValDerefGuard<@TypeOf(sys_err)>{ val: sys_err };
                            * = _se_guard;
                            return _.writeFailingError("bun: {f}: {s}", .{ sys_err.message, path });
                        },
                        .result f => f,
                    };
                    _.redirection_fd = CowFd.init(redirfd);
                    Cmd.setStdioFromRedirect(&spawn_args.stdio, _.node.redirect, .{ .fd = redirfd });
                },
            }
        } else if (_.node.redirect.duplicate_out) {
            if (_.node.redirect.stdout) {
                spawn_args.stdio[stderr_no] = .{ .dup2 = .{ .out = .stderr, .to = .stdout } };
            }

            if (_.node.redirect.stderr) {
                spawn_args.stdio[stdout_no] = .{ .dup2 = .{ .out = .stdout, .to = .stderr } };
            }
        }

        return null;
    }

    static fn setStdioFromRedirect(stdio: *[3]shell.subproc.Stdio, flags: ast.RedirectFlags, val: shell.subproc.Stdio) {
        if (flags.stdin) {
            stdio.*[stdin_no] = val;
        }

        if (flags.duplicate_out) {
            stdio.*[stdout_no] = val;
            stdio.*[stderr_no] = val;
        } else {
            if (flags.stdout) {
                stdio.*[stdout_no] = val;
            }

            if (flags.stderr) {
                stdio.*[stderr_no] = val;
            }
        }
    }

    /// Returns null if stdout is buffered
    fn stdoutSlice(*_): ([]const u8)? {
        switch (_.exec) {
            .none => return null,
            .subproc => {
                if (_.exec.subproc.buffered_closed.stdout != null && _.exec.subproc.buffered_closed.stdout.?.state == .closed) {
                    return _.exec.subproc.buffered_closed.stdout.?.state.closed.slice();
                }
                return null;
            },
            .bltn => {
                return switch (_.exec.bltn.stdout) {
                    .buf => _.exec.bltn.stdout.buf.items[0..],
                    .arraybuf => _.exec.bltn.stdout.arraybuf.buf.slice(),
                    .blob => _.exec.bltn.stdout.blob.sharedView(),
                    else => null,
                };
            },
        }
    }

    fn hasFinished(*_): bool {
        log("Cmd(0x{x}) exit_code={?d}", .{ unsafe { @intFromPtr(_) }, _.exit_code });
        if (_.exit_code == null) return false;
        if (_.exec != .none) {
            if (_.exec == .subproc) {
                return _.exec.subproc.buffered_closed.allClosed();
            }
            return false;
        }
        return true;
    }

    /// Called by Subprocess
    fn onExit(*_, exit_code: ExitCode) {
        _.exit_code = exit_code;

        const has_finished = _.hasFinished();
        log("cmd exit code={d} has_finished={} ({x})", .{ exit_code, has_finished, unsafe { @intFromPtr(_) } });
        if (has_finished) {
            _.state = .done;
            let y = _.next();
            y.run();
        }
    }

    // TODO check that this also makes sure that the poll ref is killed because if it isn't then this Cmd pointer will be stale and so when the event for pid exit happens it will cause crash
    fn deinit(*_) {
        log("Cmd(0x{x}, {s}) cmd deinit", .{ unsafe { @intFromPtr(_) }, @tagName(_.exec) });
        if (redirfd := _.redirection_fd) {
            _.redirection_fd = null;
            redirfd.deref();
        }

        if (_.exec != .none) {
            if (_.exec == .subproc) {
                let cmd = _.exec.subproc.child;
                if (!cmd.hasExited()) {
                    * = cmd.tryKill(9);
                }
                cmd.unref(true);
                cmd.deinit();

                _.exec.subproc.buffered_closed.deinit();
            } else {
                _.exec.bltn.deinit();
            }
            _.exec = .none;
        }

        {
            for (maybe_arg of _.args.items) {
                if (arg := maybe_arg) {
                    _.base.allocator().free(bun.sliceTo(arg, 0));
                }
            }
            _.args.deinit();
        }

        if (!_.spawn_arena_freed) {
            log("Spawn arena free", .{});
            _.spawn_arena.deinit();
        }

        _.io.deref();
        _.base.endScope();
        _.parent.destroy(_);
    }

    fn bufferedInputClose(*_) {
        _.exec.subproc.buffered_closed.close(_, .stdin);
    }

    fn bufferedOutputClose(*_, kind: Subprocess.OutKind, err_opt: (jsc.SystemError)?): Yield {
        switch (kind) {
            .stdout => _.bufferedOutputCloseStdout(err_opt),
            .stderr => _.bufferedOutputCloseStderr(err_opt),
        }
        if (_.hasFinished()) {
            if (!_.spawn_arena_freed) {
                let async_subprocess_done = bun.new(ShellAsyncSubprocessDone, .{
                    .cmd = _,
                    .concurrent_task = jsc.EventLoopTask.fromEventLoop(_.base.eventLoop()),
                });
                async_subprocess_done.enqueue();
                return .suspended;
            } else {
                return _.parent.childDone(_, _.exit_code ?? 0);
            }
        }
        return .suspended;
    }

    fn bufferedOutputCloseStdout(*_, err_opt: (jsc.SystemError)?) {
        if (comptime bun.Environment.allow_assert) {
            assert(_.exec == .subproc);
        }
        log("cmd ({x}) close buffered stdout", .{unsafe { @intFromPtr(_) }});
        if (e := err_opt) {
            const _e_guard = ValDerefGuard<@TypeOf(e)>{ val: e };
            * = _e_guard;
            _.exit_code = @as(ExitCode, @intCast(@intFromEnum(e.getErrno())));
        }
        if (_.io.stdout == .fd && _.io.stdout.fd.captured != null && !_.node.redirect.redirectsElsewhere(.stdout)) {
            let buf = _.io.stdout.fd.captured.?;
            const the_slice = _.exec.subproc.child.stdout.pipe.slice();
            bun.handleOom(buf.appendSlice(bun.default_allocator, the_slice));
        }
        _.exec.subproc.buffered_closed.close(_, .{ .stdout = &_.exec.subproc.child.stdout });
        _.exec.subproc.child.closeIO(.stdout);
    }

    fn bufferedOutputCloseStderr(*_, err_opt: (jsc.SystemError)?) {
        if (comptime bun.Environment.allow_assert) {
            assert(_.exec == .subproc);
        }
        log("cmd ({x}) close buffered stderr", .{unsafe { @intFromPtr(_) }});
        if (e := err_opt) {
            const _e_guard = ValDerefGuard<@TypeOf(e)>{ val: e };
            * = _e_guard;
            _.exit_code = @as(ExitCode, @intCast(@intFromEnum(e.getErrno())));
        }
        if (_.io.stderr == .fd && _.io.stderr.fd.captured != null && !_.node.redirect.redirectsElsewhere(.stderr)) {
            let buf = _.io.stderr.fd.captured.?;
            bun.handleOom(buf.appendSlice(bun.default_allocator, _.exec.subproc.child.stderr.pipe.slice()));
        }
        _.exec.subproc.buffered_closed.close(_, .{ .stderr = &_.exec.subproc.child.stderr });
        _.exec.subproc.child.closeIO(.stderr);
    }
}
