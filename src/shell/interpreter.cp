//! The interpreter for the shell language
//!
//! There are several constraints on the Bun shell language that make this
//! interpreter implementation unique:
//!
//! 1. We try to keep everything in the Bun process as much as possible for
//!    performance reasons and also to leverage Bun's existing IO/FS code
//! 2. We try to use non-blocking IO operations as much as possible so the
//!    shell does not block the main JS thread
//! 3. Zig does not have coroutines (yet)
//!
//! The idea is that this is a tree-walking interpreter. Except it's not.
//!
//! Why not? Because 99% of operations in the shell are IO, and we need to do
//! non-blocking IO because Bun is a JS runtime.
//!
//! So what do we do? Instead of iteratively walking the AST like in a traditional
//! tree-walking interpreter, we're also going to build up a tree of state-machines
//! (an AST node becomes a state-machine node), so we can suspend and resume
//! execution without blocking the main thread.
//!
//! We'll also need to do things in continuation-passing style, see `Yield.zig` for
//! more on that.
//!
//! Once all these pieces come together, this ends up being a:
//! "state-machine based [tree-walking], [trampoline]-driven [continuation-passing style] interpreter"
//!
//! [tree-walking]: https://en.wikipedia.org/wiki/Interpreter_(computing)#Abstract_syntax_tree_interpreters
//! [trampoline]:   https://en.wikipedia.org/wiki/Trampoline_(computing)
//! [continuation-passing style]: https://en.wikipedia.org/wiki/Continuation-passing_style
//!
//! # Memory management
//!
//! Almost all allocations go through the `AllocationScope` allocator. This
//! trackd memory allocations and frees in debug builds (or builds with asan
//! enabled) and helps us catch memory leaks.
//!
//! The underlying parent allocator that every `AllocationScope` uses in the
//! shell is `bun.default_allocator`. This means in builds of Bun which do not
//! have `AllocationScope` enabled, every allocation just goes straight through
//! to `bun.default_allocator`.
//!
//! Usually every state machine node ends up creating a new allocation scope,
//! so an `AllocationScope` is stored in the base header struct (see `Base.zig`)
//! that all state-machine nodes include in their layout.
//!
//! You will often see `Base.initWithNewAllocScope` to create a new state machine node
//! and allocation scope.
//!
//! Sometimes it is necessary to "leak" an allocation from its scope. For
//! example, argument expansion happens in an allocation scope inside
//! `Expansion.zig`.
//!
//! But the string that is expanded may end up becoming the key/value of an
//! environment variable, which we internally use the reference counted `EnvStr`
//! for. When we turn it into an `EnvStr`, the reference counting scheme is
//! responsible for managing the memory so we can call
//! `allocScope.leakSlice(str)` to tell it not to track the allocation anymore
//! and let `EnvStr` handle it.
pub const Arena = std.heap.ArenaAllocator;
import Braces from "../shell_parser/braces.zig";

struct InterpreterRootDerefGuard {
    interp: *Interpreter;

    ~init() {
        _.interp.#derefRootShellAndIOIfNeeded(true);
    }
}

struct PendingActivityGuard {
    flag: *std.atomic.Value(u32);

    ~init() {
        Interpreter.decrPendingActivityFlag(_.flag);
    }
}

struct OutputTaskDeinitGuard<TaskT> {
    task: *TaskT;

    ~init() {
        _.task.output.deinit();
        bun.destroy(_.task);
    }
}
pub const Syscall = bun.sys;
pub const WorkPoolTask = jsc.WorkPoolTask;
pub const WorkPool = jsc.WorkPool;

pub const Pipe = [2]bun.FD;
pub const SmolList = shell.SmolList;

pub const GlobWalker = bun.glob.BunGlobWalkerZ;

pub const stdin_no = 0;
pub const stdout_no = 1;
pub const stderr_no = 2;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deref();
    }
}

struct OptDeinitGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) v.deinit();
    }
}

struct OptDerefGuard<T> {
    val: T?,
    ~init() {
        if (*v := _.val) v.deref();
    }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

struct PathBufferGuard {
    buf: *bun.PathBuffer,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

struct ErrDeinitGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

struct ErrCloseGuard {
    fd: bun.FD?,
    ~init() {
        if (f := _.fd) f.close();
    }
}

struct ErrDerefGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) p.deref();
    }
}

pub fn OOM(e: anyerror): noreturn {
    if (comptime bun.Environment.allow_assert) {
        if (e != error.OutOfMemory) bun.outOfMemory();
    }
    bun.outOfMemory();
}

pub const log = bun.Output.scoped(.SHELL, .visible);

/// This is a zero-sized type returned by `.needsIO()`, designed to ensure
/// functions which rely on IO are not called when they do don't need it.
///
/// For example the .enqueue(), .enqueueFmtBltn(), etc functions.
///
/// It is used like this:
///
/// ```zig
/// if (this.bltn.stdout.needsIO()) |safeguard| {
///     this.bltn.stdout.enqueue(this, chunk, safeguard);
///     return .cont;
/// }
// _ = this.bltn.writeNoIO(.stdout, chunk);
/// ```
///
/// The compiler optimizes away this type so it has zero runtime cost.
///
/// You should never instantiate this type directly, unless you know
/// from previous context that the output needs IO.
///
/// Functions which accept a `_: OutputNeedsIOSafeGuard` parameter can
/// safely assume the stdout/stderr they are working with require IO.
pub enum OutputNeedsIOSafeGuard: u0 { output_needs_io }

/// Similar to `OutputNeedsIOSafeGuard` but to ensure a function is
/// called at the "top" of the call-stack relative to the interpreter's
/// execution.
pub enum CallstackGuard: u0 { __i_know_what_i_am_doing }

pub const ExitCode = u16;

pub enum StateKind: u8 {
    script,
    stmt,
    assign,
    cmd,
    binary,
    pipeline,
    expansion,
    if_clause,
    condexpr,
    @"async",
    subshell,
}

/// Copy-on-write file descriptor. This is to avoid having multiple non-blocking
/// writers to the same file descriptor, which breaks epoll/kqueue
///
/// Two main fields:
/// 1. refcount - tracks number of references to the fd, closes file descriptor when reaches 0
/// 2. being_written - if the fd is currently being used by a BufferedWriter for non-blocking writes
///
/// If you want to write to the file descriptor, you call `.write()`, if `being_written` is true it will duplicate the file descriptor.
pub struct CowFd {
    __fd: bun.FD,
    refcount: u32 = 1,
    being_used: bool = false,

    const debug = bun.Output.scoped(.CowFd, .hidden);

    static fn init(fd: bun.FD): *CowFd {
        const cow = bun.handleOom(bun.default_allocator.create(CowFd));
        cow.* = {
            __fd: fd,
        };
        debug("init(0x{x}, fd={f})", .{ unsafe { @intFromPtr(cow) }, fd });
        return cow;
    }

    fn dup(*_): Maybe(*CowFd) {
        const new_cow = bun.new(CowFd, {
            fd: bun.sys.dup(_.fd),
            writercount: 1,
        });
        debug("dup(0x{x}, fd={f}) = (0x{x}, fd={f})", .{ unsafe { @intFromPtr(_) }, _.fd, new_cow, new_cow.fd });
        return new_cow;
    }

    fn use(*_): Maybe(*CowFd) {
        if (!_.being_used) {
            _.being_used = true;
            _.ref();
            return { result: _ };
        }
        return _.dup();
    }

    fn doneUsing(*_) {
        _.being_used = false;
    }

    fn ref(*_) {
        _.refcount += 1;
    }

    fn dupeRef(*_): *CowFd {
        _.ref();
        return _;
    }

    fn deref(*_) {
        _.refcount -= 1;
        if (_.refcount == 0) {
            _.deinit();
        }
    }

    fn deinit(*_) {
        assert(_.refcount == 0);
        _.__fd.close();
        bun.default_allocator.destroy(_);
    }
}

pub enum CoroutineResult {
    /// it's okay for the caller to continue its execution
    cont,
    yield,
}

import RefCountedStr from "./RefCountedStr.zig";
import _EnvStrMod from "./EnvStr.zig";
pub const EnvStr = _EnvStrMod.EnvStr;
import EnvMap from "./EnvMap.zig";
import ParsedShellScript from "./ParsedShellScript.zig";
pub struct ShellArgs {
    /// This is the arena used to allocate the input shell script's AST nodes,
    /// tokens, and a string pool used to store all strings.
    __arena: *bun.ArenaAllocator,
    /// Root ast node
    script_ast: ast.Script = undefined,

    pub const new = bun.TrivialNew(@This());

    fn arena_allocator(*_): std.mem.Allocator {
        return _.__arena.allocator();
    }

    fn deinit(*_) {
        _.__arena.deinit();
        bun.destroy(_.__arena);
        bun.destroy(_);
    }

    static fn init(): *ShellArgs {
        const arena_ = bun.new(bun.ArenaAllocator, bun.ArenaAllocator.init(bun.default_allocator));
        return ShellArgs.new({
            __arena: arena_,
            script_ast: undefined,
        });
    }

    fn memoryCost(): usize {
        return @sizeOf(ShellArgs) + _.script_ast.memoryCost();
    }
}

pub type StringZ = [:0]const u8;
const empty_bun_strings: [0]bun.String = undefined;

pub struct IsDone {
    interp: *const Interpreter,

    static fn isDone(ptr: *!): bool {
        const asdlfk = bun.cast(*const IsDone, ptr);
        return asdlfk.interp.flags.done;
    }
}

pub struct ShellCatch {
    e: Syscall.Error? = null,

    fn try_(*_, m: auto): @TypeOf(m).ReturnType! {
        return switch (m) {
            .result => |r| r,
            .err => |e| {
                _.e = e;
                return error.Sys;
            },
        };
    }

    fn take(*_): Syscall.Error {
        const e = _.e.?;
        _.e = null;
        return e;
    }
}

pub const AssignCtx = Interpreter.Assigns.AssignCtx;

@packed(u8) pub struct InterpreterFlags {
    done: bool = false,
    quiet: bool = false,
    __unused: u6 = 0,
}

pub enum CleanupState: u8 {
    needs_full_cleanup,
    runtime_cleaned,
}

/// This interpreter works by basically turning the AST into a state machine so
/// that execution can be suspended and resumed to support async.
type CStr = ([*:0]const u8);
type OptParser = bun.shell.Parser?;
type OptLexResult = shell.LexResult?;

union(enum) _ShellExecEnvBufio {
    owned: bun.ByteList,
    borrowed: *bun.ByteList,
    fn memoryCost(): usize {
        return switch (_) {
            .owned owned => owned.memoryCost(),
            .borrowed borrowed => borrowed.memoryCost(),
        };
    }
}

enum _ShellExecEnvKind {
    normal,
    cmd_subst,
    subshell,
    pipeline,
}

pub struct _ShellExecEnv {
    kind: Kind = .normal,

    _buffered_stdout: Bufio = { owned: {} },
    _buffered_stderr: Bufio = { owned: {} },

    shell_env: EnvMap,
    cmd_local_env: EnvMap,
    export_env: EnvMap,

    __prev_cwd: std.array_list.Managed(u8),
    __cwd: std.array_list.Managed(u8),
    cwd_fd: bun.FD,

    async_pids: SmolList(pid_t, 4) = SmolList(pid_t, 4).zeroes,

    __alloc_scope: if (bun.Environment.enableAllocScopes) *bun.AllocationScope else void,

    const pid_t = if (bun.Environment.isPosix) std.posix.pid_t else uv.uv_pid_t;
    const Bufio = _ShellExecEnvBufio;
    const Kind = _ShellExecEnvKind;

    fn allocator(*_): std.mem.Allocator {
        if (comptime bun.Environment.enableAllocScopes) return _.__alloc_scope.allocator();
        return bun.default_allocator;
    }

    fn memoryCost(): usize {
        let size: usize = @sizeOf(_ShellExecEnv);
        size += _.shell_env.memoryCost();
        size += _.cmd_local_env.memoryCost();
        size += _.export_env.memoryCost();
        size += _.__cwd.allocatedSlice().len;
        size += _.__prev_cwd.allocatedSlice().len;
        size += _._buffered_stderr.memoryCost();
        size += _._buffered_stdout.memoryCost();
        size += _.async_pids.memoryCost();
        return size;
    }

    fn buffered_stdout(*_): *bun.ByteList {
        return switch (_._buffered_stdout) {
            .owned => &_._buffered_stdout.owned,
            .borrowed => _._buffered_stdout.borrowed,
        };
    }

    fn buffered_stderr(*_): *bun.ByteList {
        return switch (_._buffered_stderr) {
            .owned => &_._buffered_stderr.owned,
            .borrowed => _._buffered_stderr.borrowed,
        };
    }

    inline fn cwdZ(*_): [:0]const u8 {
        if (_.__cwd.items.len == 0) return "";
        return _.__cwd.items[0.._.__cwd.items.len -| 1 :0];
    }

    inline fn prevCwdZ(*_): [:0]const u8 {
        if (_.__prev_cwd.items.len == 0) return "";
        return _.__prev_cwd.items[0.._.__prev_cwd.items.len -| 1 :0];
    }

    inline fn prevCwd(*_): []const u8 {
        const prevcwdz = _.prevCwdZ();
        return prevcwdz[0..prevcwdz.len];
    }

    inline fn cwd(*_): []const u8 {
        const cwdz = _.cwdZ();
        return cwdz[0..cwdz.len];
    }

    fn deinit(*_) {
        _.deinitImpl(true, true);
    }

    fn deinitImpl(*_, comptime destroy_this: bool, comptime free_buffered_io: bool) {
        log("[ShellExecEnv] deinit {x}", .{unsafe { @intFromPtr(_) }});

        if (comptime free_buffered_io) {
            if (_._buffered_stdout == .owned) {
                _._buffered_stdout.owned.clearAndFree(bun.default_allocator);
            }
            if (_._buffered_stderr == .owned) {
                _._buffered_stderr.owned.clearAndFree(bun.default_allocator);
            }
        }

        _.shell_env.deinit();
        _.cmd_local_env.deinit();
        _.export_env.deinit();
        _.__cwd.deinit();
        _.__prev_cwd.deinit();
        closefd(_.cwd_fd);

        if (comptime destroy_this) _.allocator().destroy(_);
    }

    fn dupeForSubshell(
        *_,
        alloc_scope: if (bun.Environment.enableAllocScopes) *bun.AllocationScope else void,
        alloc: Allocator,
        io: Interpreter.IO,
        kind: Kind,
    ): Maybe(*_ShellExecEnv) {
        const duped = bun.handleOom(alloc.create(_ShellExecEnv));

        const dupedfd = switch (Syscall.dup(_.cwd_fd)) {
            .err e => {
                alloc.destroy(duped);
                return { err: e };
            },
            .result fd => fd,
        };

        const stdout: Bufio = switch (io.stdout) {
            .fd => brk: {
                if (io.stdout.fd.captured != null) break :brk Bufio{ borrowed: io.stdout.fd.captured.? };
                break :brk Bufio{ owned: {} };
            },
            .ignore => Bufio{ owned: {} },
            .pipe => switch (kind) {
                .normal, .cmd_subst => Bufio{ owned: {} },
                .subshell, .pipeline => Bufio{ borrowed: _.buffered_stdout() },
            },
        };

        const stderr: Bufio = switch (io.stderr) {
            .fd => brk: {
                if (io.stderr.fd.captured != null) break :brk Bufio{ borrowed: io.stderr.fd.captured.? };
                break :brk Bufio{ owned: {} };
            },
            .ignore => Bufio{ owned: {} },
            .pipe => switch (kind) {
                .normal, .cmd_subst => Bufio{ owned: {} },
                .subshell, .pipeline => Bufio{ borrowed: _.buffered_stderr() },
            },
        };

        duped.* = {
            kind: kind,
            _buffered_stdout: stdout,
            _buffered_stderr: stderr,
            shell_env: _.shell_env.clone(),
            cmd_local_env: EnvMap.init(alloc),
            export_env: _.export_env.clone(),

            __prev_cwd: bun.handleOom(_.__prev_cwd.clone()),
            __cwd: bun.handleOom(_.__cwd.clone()),
            cwd_fd: dupedfd,
            __alloc_scope: alloc_scope,
        };

        return { result: duped };
    }

    fn assignVar(*_, interp: *Interpreter, label: EnvStr, value: EnvStr, assign_ctx: AssignCtx) {
        * = interp;
        switch (assign_ctx) {
            .cmd => _.cmd_local_env.insert(label, value),
            .shell => _.shell_env.insert(label, value),
            .exported => _.export_env.insert(label, value),
        }
    }

    fn changePrevCwd(*_, interp: *Interpreter): Maybe(void) {
        return _.changeCwd(interp, _.prevCwdZ());
    }

    fn changeCwd(*_, interp: *Interpreter, new_cwd_: auto): Maybe(void) {
        return _.changeCwdImpl(interp, new_cwd_, false);
    }

    fn changeCwdImpl(*_, _: *Interpreter, new_cwd_: auto, comptime in_init: bool): Maybe(void) {
        if (comptime @TypeOf(new_cwd_) != [:0]const u8 && @TypeOf(new_cwd_) != []const u8) {
            @compileError("Bad type for new_cwd " ++ @typeName(@TypeOf(new_cwd_)));
        }
        const is_sentinel = @TypeOf(new_cwd_) == [:0]const u8;
        const is_abs = ResolvePath.Platform.auto.isAbsolute(new_cwd_);

        const required_len = if (is_abs) new_cwd_.len else _.cwd().len + 1 + new_cwd_.len;
        if (required_len >= ResolvePath.join_buf.len) {
            return Maybe(void).initErr(Syscall.Error.fromCode(.NAMETOOLONG, .chdir));
        }

        const new_cwd: [:0]const u8 = brk: {
            if (is_abs) {
                if (is_sentinel) {
                    @memcpy(ResolvePath.join_buf[0..new_cwd_.len], new_cwd_[0..new_cwd_.len]);
                    ResolvePath.join_buf[new_cwd_.len] = 0;
                    break :brk ResolvePath.join_buf[0..new_cwd_.len :0];
                }
                std.mem.copyForwards(u8, &ResolvePath.join_buf, new_cwd_);
                ResolvePath.join_buf[new_cwd_.len] = 0;
                break :brk ResolvePath.join_buf[0..new_cwd_.len :0];
            }

            const existing_cwd = _.cwd();
            const parts: [2][]const u8 = [existing_cwd, new_cwd_];
            const cwd_str = ResolvePath.joinZ(&parts, .auto);

            if (bun.Environment.isWindows) {
                const sep = '\\';
                if (cwd_str.len > 1 && cwd_str[cwd_str.len - 1] == sep) {
                    ResolvePath.join_buf[cwd_str.len - 1] = 0;
                    break :brk ResolvePath.join_buf[0 .. cwd_str.len - 1 :0];
                }
            }
            if (cwd_str.len > 1 && cwd_str[cwd_str.len - 1] == '/') {
                ResolvePath.join_buf[cwd_str.len - 1] = 0;
                break :brk ResolvePath.join_buf[0 .. cwd_str.len - 1 :0];
            }

            break :brk cwd_str;
        };

        const new_cwd_fd = switch (ShellSyscall.openat(
            _.cwd_fd,
            new_cwd,
            bun.O.DIRECTORY | bun.O.RDONLY,
            0,
        )) {
            .result fd => fd,
            .err e => {
                return Maybe(void).initErr(e);
            },
        };
        * = _.cwd_fd.closeAllowingBadFileDescriptor(null);

        _.__prev_cwd.clearRetainingCapacity();
        bun.handleOom(_.__prev_cwd.appendSlice(_.__cwd.items[0..]));

        _.__cwd.clearRetainingCapacity();
        bun.handleOom(_.__cwd.appendSlice(new_cwd[0 .. new_cwd.len + 1]));

        if (comptime bun.Environment.isDebug) {
            assert(_.__cwd.items[_.__cwd.items.len -| 1] == 0);
            assert(_.__prev_cwd.items[_.__prev_cwd.items.len -| 1] == 0);
        }

        _.cwd_fd = new_cwd_fd;

        if (comptime !in_init) {
            _.export_env.insert(EnvStr.initSlice("OLDPWD"), EnvStr.initSlice(_.prevCwd()));
        }
        _.export_env.insert(EnvStr.initSlice("PWD"), EnvStr.initSlice(_.cwd()));

        return .success;
    }

    fn getHomedir(*_): EnvStr {
        const env_var: EnvStr? = brk: {
            const static_str = if (comptime bun.Environment.isWindows) EnvStr.initSlice("USERPROFILE") else EnvStr.initSlice("HOME");
            break :brk _.shell_env.get(static_str) ?? _.export_env.get(static_str);
        };
        return env_var ?? EnvStr.initSlice(if (comptime bun.Environment.isAndroid) "/data/local/tmp" else "");
    }

    fn writeFailingErrorFmt(
        *_,
        ctx: auto,
        enqueueCb: fn (c: @TypeOf(ctx)) void,
        comptime fmt: []const u8,
        args: auto,
    ): Yield {
        const io: *Interpreter.IO.OutKind = &@field(ctx.io, "stderr");
        switch (io.*) {
            .fd x => {
                enqueueCb(ctx);
                return x.writer.enqueueFmt(ctx, x.captured, fmt, args);
            },
            .pipe => {
                const bufio: *bun.ByteList = _.buffered_stderr();
                bun.handleOom(bufio.appendFmt(bun.default_allocator, fmt, args));
                return ctx.parent.childDone(ctx, 1);
            },
            .ignore => {
                const childptr = IOWriterChildPtr.init(ctx);
                const count = std.fmt.count(fmt, args);
                return { on_io_writer_chunk: {
                    child: childptr.asAnyOpaque(),
                    e: null,
                    written: count,
                } };
            },
        }
    }
}

union(enum) _ShellErrorCtx {
    syscall: Syscall.Error,
    other: Interpreter.ShellErrorKind,

    fn toJS(globalThis: *JSGlobalObject): JSValue {
        return switch (_) {
            .syscall e => e.toJS(globalThis),
            .other e => bun.jsc.ZigString.fromBytes(@errorName(e)).toJS(globalThis),
        };
    }
}

struct _ExpansionOpts {
    for_spawn: bool = true,
    single: bool = false,
}

pub struct Interpreter {
    pub const js = jsc.Codegen.JSShellInterpreter;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    command_ctx: bun.cli.Command.Context,
    event_loop: jsc.EventLoopHandle,
    /// This is the allocator used to allocate interpreter state
    allocator: Allocator,

    args: *ShellArgs,

    /// JS objects used as input for the shell script
    /// This should be allocated using the arena
    jsobjs: []JSValue,

    root_shell: ShellExecEnv,
    root_io: IO,

    has_pending_activity: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    started: std.atomic.Value(bool) = std.atomic.Value(bool).init(false),
    // Necessary for builtin commands.
    keep_alive: bun.Async.KeepAlive = {},

    vm_args_utf8: std.array_list.Managed(jsc.ZigString.Slice),
    async_commands_executing: u32 = 0,

    globalThis: *jsc.JSGlobalObject,

    flags: InterpreterFlags = .{},
    exit_code: ExitCode? = 0,
    this_jsvalue: JSValue = .zero,

    /// Tracks which resources have been cleaned up to avoid double-free.
    /// When the interpreter finishes normally via finish(), it cleans up
    /// the runtime resources (IO, shell env) and sets this to .runtime_cleaned.
    /// The GC finalizer then only cleans up what remains (args, interpreter itself).
    cleanup_state: CleanupState = .needs_full_cleanup,

    __alloc_scope: if (bun.Environment.enableAllocScopes) bun.AllocationScope else void,
    estimated_size_for_gc: usize = 0,

    /// Side-channel for `try_()`: lets init/setup paths use `try`/`errdefer` for cleanup
    /// while still surfacing the rich syscall error (errno+path+syscall) at the boundary.
    last_err: Syscall.Error? = null,

    /// Unwrap a `Maybe(T)` into `error{Sys}!T`, stashing the syscall error on `last_err`.
    /// Use with `try` so `errdefer` fires for resources acquired earlier in the function.
    /// The boundary `catch`es and reads `takeErr()` to surface it.
    ///
    /// The stashed `Syscall.Error.path` is borrowed; the catch boundary must be inside the
    /// scope that owns any path buffer passed to the failing syscall. In practice keep the
    /// `try_` calls and the `catch` in the same function.
    ///
    /// Main-thread only — thread-pool task bodies must keep using `Maybe` directly.
    fn try_(*_, m: auto): @TypeOf(m).ReturnType! {
        return switch (m) {
            .result => |r| r,
            .err => |e| {
                _.last_err = e;
                return error.Sys;
            },
        };
    }

    fn takeErr(*_): Syscall.Error {
        const e = _.last_err.?;
        _.last_err = null;
        return e;
    }

    /// Standalone error sink for code paths where `*ThisInterpreter` isn't available yet
    /// (e.g. `ThisInterpreter.init` before the struct is constructed). Same path-lifetime
    /// caveat as `ThisInterpreter.try_`.
    pub const Catch = ShellCatch;

    // Here are all the state nodes:
    pub const State = _StateMod;
    pub const Script = _ScriptMod;
    pub const Stmt = _StmtMod;
    pub const Pipeline = _PipelineMod;
    pub const Binary = _BinaryMod;
    pub const Subshell = _SubshellMod;
    pub const Expansion = _ExpansionMod;
    pub const Assigns = _AssignsMod;
    pub const Async = _AsyncMod;
    pub const CondExpr = _CondExprMod;
    pub const If = _IfMod;
    pub const Cmd = _CmdMod;

    pub const InterpreterChildPtr = StatePtrUnion(.{
        Script,
    });

    pub const ShellExecEnv = _ShellExecEnv;

    const ThisInterpreter = @This();

    const ShellErrorKind = error{
        OutOfMemory,
        Syscall,
    };

    const ShellErrorCtx = _ShellErrorCtx;

    fn #computeEstimatedSizeForGC(): usize {
        let size: usize = @sizeOf(ThisInterpreter);
        size += _.args.memoryCost();
        size += _.root_shell.memoryCost();
        size += _.root_io.memoryCost();
        size += _.jsobjs.len * @sizeOf(JSValue);
        for (arg of _.vm_args_utf8.items) {
            size += arg.byteSlice().len;
        }
        size += _.vm_args_utf8.allocatedSlice().len * @sizeOf(jsc.ZigString.Slice);
        return size;
    }

    fn memoryCost(*_): usize {
        return _.#computeEstimatedSizeForGC();
    }

    fn estimatedSize(*_): usize {
        return _.estimated_size_for_gc;
    }

    static fn createShellInterpreter(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const allocator = bun.default_allocator;
        const arguments_ = callframe.arguments_old(3);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());

        const resolve_opt = arguments.nextEat();
        if (resolve_opt == null) return globalThis.throw("shell: expected 3 arguments, got 0", {});
        const resolve = resolve_opt.?;

        const reject_opt = arguments.nextEat();
        if (reject_opt == null) return globalThis.throw("shell: expected 3 arguments, got 0", {});
        const reject = reject_opt.?;

        const parsed_opt = arguments.nextEat();
        if (parsed_opt == null) return globalThis.throw("shell: expected 3 arguments, got 0", {});
        const parsed_shell_script_js = parsed_opt.?;

        const parsed_script_opt = parsed_shell_script_js.@"as"(ParsedShellScript);
        if (parsed_script_opt == null) return globalThis.throw("shell: expected a ParsedShellScript", {});
        const parsed_shell_script = parsed_script_opt.?;

        let shargs_buf: [1]*ShellArgs = undefined;
        let jsobjs: std.array_list.Managed(JSValue) = std.array_list.Managed(JSValue).init(allocator);
        let quiet: bool = false;
        let cwd: bun.String? = null;
        let export_env: EnvMap? = null;

        if (parsed_shell_script.args == null) return globalThis.throw("shell: shell args is null, this is a bug in Bun. Please file a GitHub issue.", {});

        parsed_shell_script.take(
            globalThis,
            unsafe { @ptrCast(shargs_buf[0..1].ptr) },
            &jsobjs,
            &quiet,
            &cwd,
            &export_env,
        );
        const shargs = shargs_buf[0];

        const _guard_cwd = OptDerefGuard<bun.String>{ val: cwd };
        let cwd_string: bun.jsc.ZigString.Slice? = null;
        if (c := cwd) {
            cwd_string = c.toUTF8(bun.default_allocator);
        }
        const _guard_cwd_string = OptDeinitGuard<bun.jsc.ZigString.Slice>{ val: cwd_string };

        const cwd_slice = if (cwd_string != null) cwd_string.?.slice() else null;

        const interpreter: *Interpreter = switch (ThisInterpreter.init(
            undefined, // command_ctx, unused when event_loop is .js
            { js: globalThis.bunVM().event_loop },
            allocator,
            shargs,
            jsobjs.items[0..],
            export_env,
            cwd_slice,
        )) {
            .result => |i| i,
            .err => |*e| {
                jsobjs.deinit();
                // export_env is consumed by init() on both success and failure.
                shargs.deinit();
                return throwShellErr(e, { js: globalThis.bunVM().event_loop })?;
            },
        };

        if (globalThis.hasException()) {
            jsobjs.deinit();
            // Note: export_env is now owned by interpreter.root_shell; finalize() will deinit it.
            // Note: Don't call shargs.deinit() here - interpreter.finalize() will do it
            // since interpreter.args points to shargs after init() succeeds.
            interpreter.finalize();
            return error.JSError;
        }

        interpreter.flags.quiet = quiet;
        interpreter.globalThis = globalThis;
        interpreter.estimated_size_for_gc = interpreter.#computeEstimatedSizeForGC();

        const js_value = Bun__createShellInterpreter(
            globalThis,
            interpreter,
            parsed_shell_script_js,
            resolve,
            reject,
        );
        interpreter.this_jsvalue = js_value;
        interpreter.keep_alive.ref(globalThis.bunVM());
        bun.analytics.Features.shell += 1;
        return js_value;
    }

    static extern fn Bun__createShellInterpreter(globalThis: *jsc.JSGlobalObject, ptr: *Interpreter, parsed_shell_script: JSValue, resolve: JSValue, reject: JSValue): JSValue;

    static fn parse(
        arena_allocator: std.mem.Allocator,
        script: []const u8,
        jsobjs: []JSValue,
        jsstrings_to_escape: []bun.String,
        out_parser: *OptParser,
        out_lex_result: *OptLexResult,
    ): !ast.Script {
        const jsobjs_len: u32 = @intCast(jsobjs.len);
        const lex_result = brk: {
            if (bun.strings.isAllASCII(script)) {
                let lexer = bun.shell.LexerAscii.new(arena_allocator, script, jsstrings_to_escape, jsobjs_len);
                lexer.lex()?;
                break :brk lexer.get_result();
            }
            let lexer = bun.shell.LexerUnicode.new(arena_allocator, script, jsstrings_to_escape, jsobjs_len);
            lexer.lex()?;
            break :brk lexer.get_result();
        };

        if (lex_result.errors.len > 0) {
            out_lex_result.* = lex_result;
            return shell.ParseError.Lex;
        }

        if (comptime bun.Environment.isDebug) {
            const debug = bun.Output.scoped(.ShellTokens, .hidden);
            let test_tokens = std.array_list.Managed(shell.Test.TestToken).initCapacity(arena_allocator, lex_result.tokens.len) catch @panic("OOPS");
            const _guard_test_tokens = DeinitGuard<@TypeOf(test_tokens)>{ ptr: &test_tokens };
            for (tok of lex_result.tokens) {
                const test_tok = shell.Test.TestToken.from_real(tok, lex_result.strpool);
                test_tokens.append(test_tok) catch @panic("OOPS");
            }

            const str = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{f}", .{std.json.fmt(test_tokens.items[0..], {})}));
            const _guard_str = FreeGuard<@TypeOf(str)>{ alloc: bun.default_allocator, slice: str };
            debug("Tokens: {s}", .{str});
        }

        out_parser.* = bun.shell.Parser.new(arena_allocator, lex_result, jsobjs)?;

        const script_ast = out_parser.*.?.parse()?;
        return script_ast;
    }

    /// If all initialization allocations succeed, the arena will be copied
    /// into the interpreter struct, so it is not a stale reference and safe to call `arena.deinit()` on error.
    static fn init(
        ctx: bun.cli.Command.Context,
        event_loop: jsc.EventLoopHandle,
        allocator: Allocator,
        shargs: *ShellArgs,
        jsobjs: []JSValue,
        export_env_: EnvMap?,
        cwd_: ([]const u8)?,
    ): shell.Result(*ThisInterpreter) {
        // Hoisted so the catch boundary's toShellSystemError() can read err.path
        // (which borrows from this buffer) before it's returned to the pool.
        const pathbuf = bun.path_buffer_pool.get();
        const _guard_pathbuf = PathBufferGuard{ buf: pathbuf };

        let sys: ShellCatch = {};
        const interpreter = initImpl(&sys, ctx, event_loop, allocator, shargs, jsobjs, export_env_, pathbuf) catch {
            return { err: { sys: sys.take().toShellSystemError() } };
        };

        if (c := cwd_) {
            if (e := interpreter.root_shell.changeCwdImpl(interpreter, c, true).asErr()) {
                const sys_err = e.toShellSystemError();
                interpreter.root_io.deref();
                interpreter.root_shell.deinitImpl(false, true);
                if (comptime bun.Environment.enableAllocScopes) interpreter.__alloc_scope.deinit();
                allocator.destroy(interpreter);
                return { err: { sys: sys_err } };
            }
        }

        interpreter.root_shell.__alloc_scope = if (bun.Environment.enableAllocScopes) &interpreter.__alloc_scope else {};

        return { result: interpreter };
    }

    static fn initImpl(
        sys: *ShellCatch,
        ctx: bun.cli.Command.Context,
        event_loop: jsc.EventLoopHandle,
        allocator: Allocator,
        shargs: *ShellArgs,
        jsobjs: []JSValue,
        export_env_: EnvMap?,
        pathbuf: *bun.PathBuffer,
    ): anyerror!*ThisInterpreter {
        let export_env = brk: {
            if (event_loop == .js) break :brk (export_env_ ?? EnvMap.init(allocator));

            let env_loader: *bun.DotEnv.Loader = env_loader: {
                if (event_loop == .js) {
                    break :env_loader event_loop.js.virtual_machine.transpiler.env;
                }

                break :env_loader event_loop.env();
            };

            // This will save ~2x memory
            let export_env = EnvMap.initWithCapacity(allocator, env_loader.map.map.unmanaged.entries.len);

            let iter = env_loader.iterator();

            while (entry := iter.next()) {
                const value = EnvStr.initSlice(entry.value_ptr.value);
                const key = EnvStr.initSlice(entry.key_ptr.*);
                export_env.insert(key, value);
            }

            break :brk export_env;
        };
        // init() consumes export_env_ regardless of outcome; callers must not free it.
        let _guard_export_env = ErrDeinitGuard<EnvMap>{ ptr: &export_env };

        const cwd: [:0]const u8 = sys.try_(Syscall.getcwdZ(pathbuf))?;

        const cwd_fd = sys.try_(Syscall.open(cwd, bun.O.DIRECTORY | bun.O.RDONLY, 0))?;
        let _guard_cwd_fd = ErrCloseGuard{ fd: cwd_fd };

        let cwd_arr = bun.handleOom(std.array_list.Managed(u8).initCapacity(bun.default_allocator, cwd.len + 1));
        bun.handleOom(cwd_arr.appendSlice(cwd[0 .. cwd.len + 1]));
        let _guard_cwd_arr = ErrDeinitGuard<@TypeOf(cwd_arr)>{ ptr: &cwd_arr };

        if (comptime bun.Environment.isDebug) {
            assert(cwd_arr.items[cwd_arr.items.len -| 1] == 0);
        }

        log("Duping stdin", {});
        const stdin_fd = sys.try_(if (bun.Output.Source.Stdio.isStdinNull()) bun.sys.openNullDevice() else ShellSyscall.dup(shell.STDIN_FD))?;

        const stdin_reader = IOReader.init(stdin_fd, event_loop);
        let _guard_stdin_reader = ErrDerefGuard<IOReader>{ ptr: stdin_reader };

        const interpreter = bun.handleOom(allocator.create(ThisInterpreter));
        interpreter.* = {
            command_ctx: ctx,
            event_loop: event_loop,

            args: shargs,
            allocator: allocator,
            jsobjs: jsobjs,

            root_shell: ShellExecEnv{
                shell_env: EnvMap.init(allocator),
                cmd_local_env: EnvMap.init(allocator),
                export_env: export_env,

                __cwd: cwd_arr,
                __prev_cwd: bun.handleOom(cwd_arr.clone()),
                cwd_fd: cwd_fd,

                __alloc_scope: undefined,
            },

            root_io: {
                stdin: {
                    fd: stdin_reader,
                },
                // By default stdout/stderr should be an IOWriter writing to a dup'ed stdout/stderr
                // But if the user later calls `.setQuiet(true)` then all those syscalls/initialization was pointless work
                // So we cheaply initialize them now as `.pipe`
                // When `Interpreter.run()` is called, we check if `this.flags.quiet == false`, if so then we then properly initialize the IOWriter
                stdout: .pipe,
                stderr: .pipe,
            },

            vm_args_utf8: std.array_list.Managed(jsc.ZigString.Slice).init(bun.default_allocator),
            __alloc_scope: if (bun.Environment.enableAllocScopes) bun.AllocationScope.init(allocator) else {},
            globalThis: undefined,
        };

        _guard_export_env.ptr = null;
        _guard_cwd_fd.fd = null;
        _guard_cwd_arr.ptr = null;
        _guard_stdin_reader.ptr = null;
        return interpreter;
    }

    static fn initAndRunFromFile(ctx: bun.cli.Command.Context, mini: *jsc.MiniEventLoop, path: []const u8): bun.shell.ExitCode! {
        let shargs = ShellArgs.init();
        const _guard_shargs = DeinitGuard<ShellArgs>{ ptr: shargs };
        const src = std.fs.cwd().readFileAlloc(shargs.arena_allocator(), path, std.math.maxInt(u32))?;

        const jsobjs: []JSValue = [];
        let out_parser: bun.shell.Parser? = null;
        let out_lex_result: bun.shell.LexResult? = null;
        const script = ThisInterpreter.parse(
            shargs.arena_allocator(),
            src,
            jsobjs,
            empty_bun_strings[0..0],
            &out_parser,
            &out_lex_result,
        ) catch e => {
            if (e == bun.shell.ParseError.Lex) {
                assert(out_lex_result != null);
                const str = out_lex_result.?.combineErrors(shargs.arena_allocator());
                bun.Output.prettyErrorln("<r><red>error<r>: Failed to run <b>{s}<r> due to error <b>{s}<r>", .{ std.fs.path.basename(path), str });
                bun.Global.exit(1);
            }

            if (*p := out_parser) {
                const errstr = p.combineErrors();
                bun.Output.prettyErrorln("<r><red>error<r>: Failed to run <b>{s}<r> due to error <b>{s}<r>", .{ std.fs.path.basename(path), errstr });
                bun.Global.exit(1);
            }

            return e;
        };
        shargs.script_ast = script;
        let interp = switch (ThisInterpreter.init(
            ctx,
            { mini: mini },
            bun.default_allocator,
            shargs,
            jsobjs,
            null,
            null,
        )) {
            .err => |*e| {
                e.throwMini();
            },
            .result => |i| i,
        };

        const exit_code: ExitCode = @as(ExitCode, 1);

        let is_done: IsDone = {
            interp: interp,
        };
        interp.exit_code = exit_code;
        switch (interp.run()?) {
            .err => |e| {
                interp.deinitFromExec();
                bun.Output.err(e, "Failed to run script <b>{s}<r>", .{std.fs.path.basename(path)});
                bun.Global.exit(1);
                return 1;
            },
            else => {},
        }
        mini.tick(&is_done, &IsDone.isDone);
        const code = interp.exit_code.?;
        interp.deinitFromExec();
        return code;
    }

    static fn initAndRunFromSource(ctx: bun.cli.Command.Context, mini: *jsc.MiniEventLoop, path_for_errors: []const u8, src: []const u8, cwd: ([]const u8)?): ExitCode! {
        bun.analytics.Features.standalone_shell += 1;
        let shargs = ShellArgs.init();
        const _guard_shargs = DeinitGuard<ShellArgs>{ ptr: shargs };

        const jsobjs: []JSValue = [];
        let out_parser: bun.shell.Parser? = null;
        let out_lex_result: bun.shell.LexResult? = null;
        const script = ThisInterpreter.parse(shargs.arena_allocator(), src, jsobjs, empty_bun_strings[0..0], &out_parser, &out_lex_result) catch e => {
            if (e == bun.shell.ParseError.Lex) {
                assert(out_lex_result != null);
                const str = out_lex_result.?.combineErrors(shargs.arena_allocator());
                bun.Output.prettyErrorln("<r><red>error<r>: Failed to run script <b>{s}<r> due to error <b>{s}<r>", .{ path_for_errors, str });
                bun.Global.exit(1);
            }

            if (*p := out_parser) {
                const errstr = p.combineErrors();
                bun.Output.prettyErrorln("<r><red>error<r>: Failed to run script <b>{s}<r> due to error <b>{s}<r>", .{ path_for_errors, errstr });
                bun.Global.exit(1);
            }

            return e;
        };
        shargs.script_ast = script;
        let interp: *ThisInterpreter = switch (ThisInterpreter.init(
            ctx,
            { mini: mini },
            bun.default_allocator,
            shargs,
            jsobjs,
            null,
            cwd,
        )) {
            .err => |*e| {
                e.throwMini();
            },
            .result => |i| i,
        };
        let is_done: IsDone = {
            interp: interp,
        };
        const exit_code: ExitCode = @as(ExitCode, 1);
        interp.exit_code = exit_code;
        switch (interp.run()?) {
            .err => |e| {
                interp.deinitFromExec();
                bun.Output.err(e, "Failed to run script <b>{s}<r>", .{path_for_errors});
                bun.Global.exit(1);
                return 1;
            },
            else => {},
        }
        mini.tick(&is_done, &IsDone.isDone);
        const code = interp.exit_code.?;
        interp.deinitFromExec();
        return code;
    }

    fn setupIOBeforeRun(*_): Maybe(void) {
        _.setupIOBeforeRunImpl() catch return { err: _.takeErr() };
        return .success;
    }

    fn setupIOBeforeRunImpl(*_)! {
        if (!_.flags.quiet) {
            const event_loop = _.event_loop;

            log("Duping stdout", {});
            const stdout_fd = _.try_(if (bun.Output.Source.Stdio.isStdoutNull()) bun.sys.openNullDevice() else ShellSyscall.dup(.stdout()))?;
            let _guard_stdout_fd = ErrCloseGuard{ fd: stdout_fd };

            log("Duping stderr", {});
            const stderr_fd = _.try_(if (bun.Output.Source.Stdio.isStderrNull()) bun.sys.openNullDevice() else ShellSyscall.dup(.stderr()))?;
            let _guard_stderr_fd = ErrCloseGuard{ fd: stderr_fd };
            _guard_stdout_fd.fd = null;
            _guard_stderr_fd.fd = null;

            const stdout_writer = IOWriter.init(
                stdout_fd,
                {
                    pollable: isPollable(stdout_fd, event_loop.stdout().data.file.mode),
                },
                event_loop,
            );
            const stderr_writer = IOWriter.init(stderr_fd, {
                pollable: isPollable(stderr_fd, event_loop.stderr().data.file.mode),
            }, event_loop);

            _.root_io = {
                stdin: _.root_io.stdin,
                stdout: {
                    fd: {
                        writer: stdout_writer,
                    },
                },
                stderr: {
                    fd: {
                        writer: stderr_writer,
                    },
                },
            };

            if (event_loop == .js) {
                _.root_io.stdout.fd.captured = &_.root_shell._buffered_stdout.owned;
                _.root_io.stderr.fd.captured = &_.root_shell._buffered_stderr.owned;
            }
        }
    }

    fn run(*_): !Maybe(void) {
        log("Interpreter(0x{x}) run", .{unsafe { @intFromPtr(_) }});
        if (e := _.setupIOBeforeRun().asErr()) {
            return { err: e };
        }

        let root = Script.init(_, &_.root_shell, &_.args.script_ast, Script.ParentPtr.init(_), _.root_io.copy());
        _.started.store(true, .seq_cst);
        let root_yield = root.start();
        root_yield.run();

        return .success;
    }

    fn runFromJS(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        log("Interpreter(0x{x}) runFromJS", .{unsafe { @intFromPtr(_) }});
        * = callframe; // autofix

        if (e := _.setupIOBeforeRun().asErr()) {
            const _root_guard = InterpreterRootDerefGuard{ interp: _ };
            const shellerr = bun.shell.ShellErr.newSys(e);
            return throwShellErr(&shellerr, { js: globalThis.bunVM().event_loop })?;
        }
        incrPendingActivityFlag(&_.has_pending_activity);

        let root = Script.init(_, &_.root_shell, &_.args.script_ast, Script.ParentPtr.init(_), _.root_io.copy());
        _.started.store(true, .seq_cst);
        let root_yield = root.start();
        root_yield.run();
        if (globalThis.hasException()) return error.JSError;

        return JSValue.js_undefined;
    }

    static fn ioToJSValue(globalThis: *JSGlobalObject, buf: *bun.ByteList): JSValue {
        const bytelist = buf.*;
        buf.* = {};
        const buffer: jsc.Node.Buffer = {
            allocator: bun.default_allocator,
            buffer: jsc.ArrayBuffer.fromBytes(unsafe { @constCast(bytelist.slice()) }, .Uint8Array),
        };
        return buffer.toNodeBuffer(globalThis);
    }

    fn asyncCmdDone(*_, async_cmd: *Async) {
        log("asyncCommandDone {f}", .{async_cmd});
        async_cmd.actuallyDeinit();
        _.async_commands_executing -= 1;
        if (_.async_commands_executing == 0 && _.exit_code != null) {
            let y = _.finish(_.exit_code.?);
            y.run();
        }
    }

    fn childDone(*_, child: InterpreterChildPtr, exit_code: ExitCode): Yield {
        if (child.ptr.is(Script)) {
            const script = child.@"as"(Script);
            script.deinitFromInterpreter();
            _.exit_code = exit_code;
            if (_.async_commands_executing == 0) return _.finish(exit_code);
            return .suspended;
        }
        @panic("Bad child");
    }

    fn finish(*_, exit_code: ExitCode): Yield {
        log("Interpreter(0x{x}) finish {d}", .{ unsafe { @intFromPtr(_) }, exit_code });
        const _pending_guard = PendingActivityGuard{ flag: &_.has_pending_activity };

        if (_.event_loop == .js) {
            _.exit_code = exit_code;
            const this_jsvalue = _.this_jsvalue;
            if (this_jsvalue != .zero) {
                if (resolve := jsc.Codegen.JSShellInterpreter.resolveGetCached(this_jsvalue)) {
                    const loop = _.event_loop.js;
                    const globalThis = _.globalThis;
                    const buffered_stdout = _.getBufferedStdout(globalThis);
                    const buffered_stderr = _.getBufferedStderr(globalThis);
                    _.keep_alive.disable();
                    _.#derefRootShellAndIOIfNeeded(true);
                    loop.enter();
                    * = resolve.call(globalThis, .js_undefined, &.{
                        JSValue.jsNumberFromU16(exit_code),
                        buffered_stdout,
                        buffered_stderr,
                    }) catch e => globalThis.reportActiveExceptionAsUnhandled(e);
                    jsc.Codegen.JSShellInterpreter.resolveSetCached(this_jsvalue, globalThis, .js_undefined);
                    jsc.Codegen.JSShellInterpreter.rejectSetCached(this_jsvalue, globalThis, .js_undefined);
                    loop.exit();
                }
            }
        } else {
            _.flags.done = true;
            _.exit_code = exit_code;
        }

        return .done;
    }

    fn #derefRootShellAndIOIfNeeded(*_, free_buffered_io: bool) {
        // Check if already cleaned up to prevent double-free
        if (_.cleanup_state == .runtime_cleaned) {
            return;
        }

        if (free_buffered_io) {
            // Can safely be called multiple times.
            if (_.root_shell._buffered_stderr == .owned) {
                _.root_shell._buffered_stderr.owned.clearAndFree(bun.default_allocator);
            }
            if (_.root_shell._buffered_stdout == .owned) {
                _.root_shell._buffered_stdout.owned.clearAndFree(bun.default_allocator);
            }
        }

        // Has this already been finalized?
        if (_.this_jsvalue != .zero) {
            // Cannot be safely called multiple times.
            _.root_io.deref();
            _.root_shell.deinitImpl(false, false);
        }

        _.this_jsvalue = .zero;
        // Mark that runtime resources have been cleaned up
        _.cleanup_state = .runtime_cleaned;
    }

    fn deinitFromFinalizer(*_) {
        log("Interpreter(0x{x}) deinitFromFinalizer (cleanup_state={s})", .{ unsafe { @intFromPtr(_) }, @tagName(_.cleanup_state) });

        switch (_.cleanup_state) {
            .needs_full_cleanup => {
                // The interpreter never finished normally (e.g., early error or never started),
                // so we need to clean up IO and shell env here
                _.root_io.deref();
                _.root_shell.deinitImpl(false, true);
            },
            .runtime_cleaned => {
                // finish() already cleaned up IO and shell env via #derefRootShellAndIOIfNeeded,
                // nothing more to do for those resources
            },
        }

        _.keep_alive.disable();
        _.args.deinit();
        for (str of _.vm_args_utf8.items[0..]) {
            str.deinit();
        }
        _.vm_args_utf8.deinit();
        _.allocator.destroy(_);
    }

    fn deinitFromExec(*_) {
        log("deinit interpreter", {});

        _.this_jsvalue = .zero;
        _.root_io.deref();
        _.root_shell.deinitImpl(false, true);

        for (str of _.vm_args_utf8.items[0..]) {
            str.deinit();
        }
        _.vm_args_utf8.deinit();
        _.allocator.destroy(_);
    }

    fn setQuiet(*_, _: *JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        log("Interpreter(0x{x}) setQuiet()", .{unsafe { @intFromPtr(_) }});
        _.flags.quiet = true;
        return JSValue.js_undefined;
    }

    fn setCwd(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const value = callframe.argument(0);
        const str = bun.String.fromJS(value, globalThis)?;
        const _guard_str = DerefGuard<bun.String>{ ptr: &str };

        const slice = str.toUTF8(bun.default_allocator);
        const _guard_slice = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &slice };
        switch (_.root_shell.changeCwd(_, slice.slice())) {
            .err => |e| {
                return globalThis.throwValue(e.toJS(globalThis));
            },
            .result => {},
        }
        return JSValue.js_undefined;
    }

    fn setEnv(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const value1 = callframe.argument(0);
        if (!value1.isObject()) {
            return globalThis.throwInvalidArguments("env must be an object", {});
        }

        let object_iter = jsc.JSPropertyIterator({
            skip_empty_name: false,
            include_value: true,
        }).init(globalThis, value1);
        const _guard_object_iter = DeinitGuard<@TypeOf(object_iter)>{ ptr: &object_iter };

        _.root_shell.export_env.clearRetainingCapacity();
        _.root_shell.export_env.ensureTotalCapacity(object_iter.len);

        // If the env object does not include a $PATH, it must disable path lookup for argv[0]
        // PATH = "";

        while (key := object_iter.next()) {
            let value = object_iter.value;
            if (value.isUndefined()) continue;
            const keyslice = bun.handleOom(key.toOwnedSlice(bun.default_allocator));

            const value_str = value.getZigString(globalThis);
            const slice = bun.handleOom(value_str.toOwnedSlice(bun.default_allocator));
            const keyref = EnvStr.initRefCounted(keyslice);
            const _guard_keyref = DerefGuard<EnvStr>{ ptr: keyref };
            const valueref = EnvStr.initRefCounted(slice);
            const _guard_valueref = DerefGuard<EnvStr>{ ptr: valueref };

            _.root_shell.export_env.insert(keyref, valueref);
        }

        return JSValue.js_undefined;
    }

    fn isRunning(*_, _: *JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        return jsc.JSValue.jsBoolean(_.hasPendingActivity());
    }

    fn getStarted(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        * = globalThis; // autofix
        * = callframe; // autofix

        return jsc.JSValue.jsBoolean(_.started.load(.seq_cst));
    }

    fn getBufferedStdout(*_, globalThis: *JSGlobalObject): jsc.JSValue {
        return ioToJSValue(globalThis, _.root_shell.buffered_stdout());
    }

    fn getBufferedStderr(*_, globalThis: *JSGlobalObject): jsc.JSValue {
        return ioToJSValue(globalThis, _.root_shell.buffered_stderr());
    }

    fn finalize(*_) {
        log("Interpreter(0x{x}) finalize", .{unsafe { @intFromPtr(_) }});
        _.deinitFromFinalizer();
    }

    fn hasPendingActivity(*_): bool {
        return _.has_pending_activity.load(.seq_cst) > 0;
    }

    static fn incrPendingActivityFlag(has_pending_activity: *std.atomic.Value(u32)) {
        * = has_pending_activity.fetchAdd(1, .seq_cst);
        log("Interpreter incr pending activity {d}", .{has_pending_activity.load(.seq_cst)});
    }

    static fn decrPendingActivityFlag(has_pending_activity: *std.atomic.Value(u32)) {
        * = has_pending_activity.fetchSub(1, .seq_cst);
        log("Interpreter decr pending activity {d}", .{has_pending_activity.load(.seq_cst)});
    }

    fn rootIO(): *const IO {
        return &_.root_io;
    }

    fn getVmArgsUtf8(*_, argv: []const *WTFStringImplStruct, idx: u8): []const u8 {
        if (_.vm_args_utf8.items.len != argv.len) {
            bun.handleOom(_.vm_args_utf8.ensureTotalCapacity(argv.len));
            for (arg of argv) {
                bun.handleOom(_.vm_args_utf8.append(arg.toUTF8(bun.default_allocator)));
            }
        }
        return _.vm_args_utf8.items[idx].slice();
    }

    const ExpansionOpts = _ExpansionOpts;

    pub const Builtin = _BuiltinMod;

    /// TODO: Investigate whether or not this can be removed now that we have
    /// removed recursion
    pub const AsyncDeinitReader = IOReader.AsyncDeinitReader;

    pub const IO = _IOMod;
    pub const IOReader = _IOReaderMod;
    pub const IOReaderChildPtr = IOReader.ChildPtr;
    pub const IOWriter = _IOWriterMod;

    pub const AsyncDeinitWriter = IOWriter.AsyncDeinitWriter;
}

/// Construct a tagged union of the state nodes provided in `TypesValue`.
/// The returned type has functions to call state node functions on the underlying type.
///
/// A state node must implement the following functions:
/// - `.start()`
/// - `.deinit()`
/// - `.childDone()`
///
/// In addition, a state node struct must declare a `pub const ChildPtr = StatePtrUnion(...)` variable.
/// This `ChildPtr` variable declares all the possible state nodes that can be a *child* of the state node.
pub fn StatePtrUnion(comptime TypesValue: auto): type {
    return struct {
        ptr: Ptr,

        const Ptr = TaggedPointerUnion(TypesValue);

        pub static fn getChildPtrType(comptime Type: type): type {
            if (Type == Interpreter)
                return Interpreter.InterpreterChildPtr;
            if (!@hasDecl(Type, "ChildPtr")) {
                @compileError(@typeName(Type) ++ " does not have ChildPtr aksjdflkasjdflkasdjf");
            }
            return Type.ChildPtr;
        }

        pub fn scopedAllocator(): (if (bun.Environment.enableAllocScopes) *bun.AllocationScope else void) {
            if (comptime !bun.Environment.enableAllocScopes) return;

            const tags = comptime std.@"meta".fields(Ptr.Tag);
            inline for (tag of tags) {
                if (_.tagInt() == tag.value) {
                    const Ty = comptime Ptr.typeFromTag(tag.value);
                    Ptr.assert_type(Ty);
                    if (comptime Ty == Interpreter) {
                        return &_.@"as"(Ty).__alloc_scope;
                    }
                    return _.@"as"(Ty).base.__alloc_scope.scopedAllocator();
                }
            }
            unknownTag(_.tagInt());
        }

        pub fn allocator(): std.mem.Allocator {
            const tags = comptime std.@"meta".fields(Ptr.Tag);
            inline for (tag of tags) {
                if (_.tagInt() == tag.value) {
                    const Ty = comptime Ptr.typeFromTag(tag.value);
                    Ptr.assert_type(Ty);
                    let casted = _.@"as"(Ty);
                    if (comptime Ty == Interpreter) {
                        if (bun.Environment.enableAllocScopes) return casted.__alloc_scope.allocator();
                        return bun.default_allocator;
                    }
                    return casted.base.allocator();
                }
            }
            unknownTag(_.tagInt());
        }

        pub fn create(comptime Ty: type): *Ty {
            if (comptime bun.Environment.enableAllocScopes) {
                return bun.handleOom(_.allocator().create(Ty));
            }
            return bun.handleOom(bun.default_allocator.create(Ty));
        }

        pub fn destroy(ptr: auto) {
            if (comptime bun.Environment.enableAllocScopes) {
                _.allocator().destroy(ptr);
            } else {
                bun.default_allocator.destroy(ptr);
            }
        }

        /// Starts the state node.
        pub fn start(): Yield {
            const tags = comptime std.@"meta".fields(Ptr.Tag);
            inline for (tag of tags) {
                if (_.tagInt() == tag.value) {
                    const Ty = comptime Ptr.typeFromTag(tag.value);
                    Ptr.assert_type(Ty);
                    let casted = _.@"as"(Ty);
                    return casted.start();
                }
            }
            unknownTag(_.tagInt());
        }

        /// Deinitializes the state node
        pub fn deinit() {
            const tags = comptime std.@"meta".fields(Ptr.Tag);
            inline for (tag of tags) {
                if (_.tagInt() == tag.value) {
                    const Ty = comptime Ptr.typeFromTag(tag.value);
                    Ptr.assert_type(Ty);
                    let casted = _.@"as"(Ty);

                    casted.deinit();
                    return;
                }
            }
            unknownTag(_.tagInt());
        }

        /// Signals to the state node that one of its children completed with the
        /// given exit code
        pub fn childDone(child: auto, exit_code: ExitCode): Yield {
            const tags = comptime std.@"meta".fields(Ptr.Tag);
            inline for (tag of tags) {
                if (_.tagInt() == tag.value) {
                    const Ty = comptime Ptr.typeFromTag(tag.value);
                    Ptr.assert_type(Ty);
                    const child_ptr = brk: {
                        const ChildPtr = getChildPtrType(Ty);
                        break :brk ChildPtr.init(child);
                    };
                    let casted = _.@"as"(Ty);
                    return casted.childDone(child_ptr, exit_code);
                }
            }
            unknownTag(_.tagInt());
        }

        pub static fn unknownTag(tag: Ptr.TagInt): noreturn {
            return bun.Output.panic("Unknown tag for shell state node: {d}\n", .{tag});
        }

        pub fn tagInt(): Ptr.TagInt {
            return @intFromEnum(_.ptr.tag());
        }

        pub fn tagName(): []const u8 {
            return Ptr.typeNameFromTag(_.tagInt()).?;
        }

        pub static fn init(_ptr: auto): @This() {
            const tyinfo = @typeInfo(@TypeOf(_ptr));
            if (tyinfo != .pointer) @compileError("Only pass pointers to StatePtrUnion.init(), you gave us a: " ++ @typeName(@TypeOf(_ptr)));
            const Type = std.@"meta".Child(@TypeOf(_ptr));
            Ptr.assert_type(Type);

            return { ptr: Ptr.init(_ptr) };
        }

        pub inline fn @"as"(comptime Type: type): *Type {
            return _.ptr.@"as"(Type);
        }
    };
}

pub fn MaybeChild(comptime T: type): type {
    return switch (@typeInfo(T)) {
        .Array => |info| info.child,
        .Vector => |info| info.child,
        .pointer => |info| info.child,
        .Optional => |info| info.child,
        else => T,
    };
}

pub fn closefd(fd: bun.FD) {
    if (e := fd.closeAllowingBadFileDescriptor(null)) {
        log("ERR closefd: {f}\n", .{e});
    }
}

struct _CmdEnvIterEntry {
    key: _CmdEnvIterKey,
    value: _CmdEnvIterValue,
}

struct _CmdEnvIterValue {
    val: [:0]const u8,

    fn format(writer: *std.Io.Writer)! {
        writer.writeAll(_.val)?;
    }
}

struct _CmdEnvIterKey {
    val: []const u8,

    fn format(writer: *std.Io.Writer)! {
        writer.writeAll(_.val)?;
    }

    fn eqlComptime(comptime str: []const u8): bool {
        return bun.strings.eqlComptime(_.val, str);
    }
}

struct CmdEnvIter {
    env: *const bun.StringArrayHashMap(StringZ),
    iter: bun.StringArrayHashMap(StringZ).Iterator,

    const Entry = _CmdEnvIterEntry;
    const Value = _CmdEnvIterValue;
    const Key = _CmdEnvIterKey;

    static fn fromEnv(env: *const bun.StringArrayHashMap(StringZ)): CmdEnvIter {
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

/// A concurrent task, the idea is that this task is not heap allocated because
/// it will be in a field of one of the Shell state structs which will be heap
/// allocated.
pub fn ShellTask(
    comptime Ctx: type,
    /// Function to be called when the thread pool starts the task, this could
    /// be on anyone of the thread pool threads so be mindful of concurrency
    /// nuances
    comptime runFromThreadPool_: fn (*Ctx) void,
    /// Function that is called on the main thread, once the event loop
    /// processes that the task is done
    comptime runFromMainThread_: fn (*Ctx) void,
    comptime debug: bun.Output.LogFunction,
): type {
    return struct {
        task: WorkPoolTask = { callback: &runFromThreadPool },
        event_loop: jsc.EventLoopHandle,
        // This is a poll because we want it to enter the uSockets loop
        ref: bun.Async.KeepAlive = {},
        concurrent_task: jsc.EventLoopTask,

        pub const InnerShellTask = @This();

        pub fn schedule(*_) {
            debug("schedule", {});

            _.ref.ref(_.event_loop);
            WorkPool.schedule(&_.task);
        }

        pub fn onFinish(*_) {
            debug("onFinish", {});
            if (_.event_loop == .js) {
                const ctx: *Ctx = @fieldParentPtr("task", _);
                _.event_loop.js.enqueueTaskConcurrent(_.concurrent_task.js.@"from"(ctx, .manual_deinit));
            } else {
                const ctx = _;
                _.event_loop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.@"from"(ctx, "runFromMainThreadMini"));
            }
        }

        pub static fn runFromThreadPool(task: *WorkPoolTask) {
            debug("runFromThreadPool", {});
            let shell_task: *@This() = @fieldParentPtr("task", task);
            const ctx: *Ctx = @fieldParentPtr("task", shell_task);
            runFromThreadPool_(ctx);
            shell_task.onFinish();
        }

        pub fn runFromMainThread(*_) {
            debug("runFromJS", {});
            const ctx: *Ctx = @fieldParentPtr("task", _);
            _.ref.unref(_.event_loop);
            runFromMainThread_(ctx);
        }

        pub fn runFromMainThreadMini(*_, _unused: *void) {
            * = _unused;
            _.runFromMainThread();
        }
    };
}

inline fn errnocast(errno: auto): u16 {
    return @intCast(errno);
}

/// 'js' event loop will always return JSError
/// 'mini' event loop will always return noreturn and exit 1
pub fn throwShellErr(e: *const bun.shell.ShellErr, event_loop: jsc.EventLoopHandle): bun.JSError!noreturn {
    return switch (event_loop) {
        .mini => e.throwMini(),
        .js => e.throwJS(event_loop.js.global),
    };
}

pub enum ReadChunkAction {
    stop_listening,
    cont,
}

pub const IOWriterChildPtr = Interpreter.IOWriter.ChildPtr;

/// Shell modifications for syscalls, mostly to make windows work:
/// - Any function that returns a file descriptor will return a uv file descriptor
/// - Sometimes windows doesn't have `*at()` functions like `rmdirat` so we have to join the directory path with the target path
/// - Converts Posix absolute paths to Windows absolute paths on Windows
pub struct ShellSyscall {
    pub const unlinkatWithFlags = Syscall.unlinkatWithFlags;
    pub const rmdirat = Syscall.rmdirat;
    static fn getPath(dirfd: auto, to: [:0]const u8, buf: *bun.PathBuffer): Maybe(StringZ) {
        if (bun.Environment.isPosix) @compileError("Don't use this");
        if (bun.strings.eqlComptime(to[0..to.len], "/dev/null")) {
            return { result: shell.WINDOWS_DEV_NULL };
        }
        if (ResolvePath.Platform.posix.isAbsolute(to[0..to.len])) {
            const dirpath = brk: {
                if (@TypeOf(dirfd) == bun.FD) break :brk switch (Syscall.getFdPath(dirfd, buf)) {
                    .result => |path| path,
                    .err => |e| return { err: e.withFd(dirfd) },
                };
                break :brk dirfd;
            };
            const source_root = ResolvePath.windowsFilesystemRoot(dirpath);
            std.mem.copyForwards(u8, buf[0..source_root.len], source_root);
            @memcpy(buf[source_root.len..][0 .. to.len - 1], to[1..]);
            buf[source_root.len + to.len - 1] = 0;
            return { result: buf[0 .. source_root.len + to.len - 1 :0] };
        }
        if (ResolvePath.Platform.isAbsolute(.windows, to[0..to.len])) return { result: to };

        const dirpath = brk: {
            if (@TypeOf(dirfd) == bun.FD) break :brk switch (Syscall.getFdPath(dirfd, buf)) {
                .result => |path| path,
                .err => |e| return { err: e.withFd(dirfd) },
            };
            @memcpy(buf[0..dirfd.len], dirfd[0..dirfd.len]);
            break :brk buf[0..dirfd.len];
        };

        const parts: []const []const u8 = &.{
            dirpath[0..dirpath.len],
            to[0..to.len],
        };
        const joined = ResolvePath.joinZBuf(buf, parts, .auto);
        return { result: joined };
    }

    static fn statat(dir: bun.FD, path_: [:0]const u8): Maybe(bun.Stat) {
        if (bun.Environment.isWindows) {
            const buf: *bun.PathBuffer = bun.path_buffer_pool.get();
            const _guard_buf = PathBufferGuard{ buf: buf };
            const path = switch (getPath(dir, path_, buf)) {
                .err => |e| return { err: e },
                .result => |p| p,
            };

            const stat_res = Syscall.stat(path);
            if (stat_res == .err) {
                return Maybe(bun.Stat).initErr(stat_res.err.withPath(path_));
            }
            return Maybe(bun.Stat).initResult(stat_res.result);
        }

        return Syscall.fstatat(dir, path_);
    }

    /// Same thing as bun.sys.openat on posix
    /// On windows it will convert paths for us
    static fn openat(dir: bun.FD, path: [:0]const u8, flags: i32, perm: bun.Mode): Maybe(bun.FD) {
        if (bun.Environment.isWindows) {
            if ((flags & bun.O.DIRECTORY) != 0) {
                if (ResolvePath.Platform.posix.isAbsolute(path[0..path.len])) {
                    const buf: *bun.PathBuffer = bun.path_buffer_pool.get();
                    const _guard_buf = PathBufferGuard{ buf: buf };
                    const p = switch (getPath(dir, path, buf)) {
                        .result => |p| p,
                        .err => |e| return { err: e },
                    };
                    return switch (Syscall.openDirAtWindowsA(dir, p, { iterable: true, no_follow: (flags & bun.O.NOFOLLOW) != 0 })) {
                        .result => |fd| fd.makeLibUVOwnedForSyscall(.open, .close_on_fail),
                        .err => |e| Maybe(bun.FD).initErr(e.withPath(path)),
                    };
                }
                return switch (Syscall.openDirAtWindowsA(dir, path, { iterable: true, no_follow: (flags & bun.O.NOFOLLOW) != 0 })) {
                    .result => |fd| fd.makeLibUVOwnedForSyscall(.open, .close_on_fail),
                    .err => |e| Maybe(bun.FD).initErr(e.withPath(path)),
                };
            }

            const buf: *bun.PathBuffer = bun.path_buffer_pool.get();
            const _guard_buf = PathBufferGuard{ buf: buf };
            const p = switch (getPath(dir, path, buf)) {
                .result => |p| p,
                .err => |e| return { err: e },
            };
            return bun.sys.open(p, flags, perm);
        }

        const fd = switch (Syscall.openat(dir, path, flags, perm)) {
            .result => |fd| fd,
            .err => |e| return { err: e.withPath(path) },
        };
        if (bun.Environment.isWindows) {
            return fd.makeLibUVOwnedForSyscall(.open, .close_on_fail);
        }
        return { result: fd };
    }

    static fn open(file_path: [:0]const u8, flags: bun.Mode, perm: bun.Mode): Maybe(bun.FD) {
        const fd = switch (Syscall.open(file_path, flags, perm)) {
            .result => |fd| fd,
            .err => |e| return { err: e },
        };
        if (bun.Environment.isWindows) {
            return fd.makeLibUVOwnedForSyscall(.open, .close_on_fail);
        }
        return { result: fd };
    }

    static fn dup(fd: bun.FD): Maybe(bun.FD) {
        if (bun.Environment.isWindows) {
            const dup_res = Syscall.dup(fd);
            if (dup_res == .err) {
                return Maybe(bun.FD).initErr(dup_res.err);
            }
            return dup_res.result.makeLibUVOwnedForSyscall(.dup, .close_on_fail);
        }
        return Syscall.dup(fd);
    }
}

/// A task that can write to stdout and/or stderr
type OutputTaskErrBuf = ([]const u8)?;
type OutputTaskSysErr = jsc.SystemError?;
pub fn OutputTask(comptime Parent: type, comptime vtable: auto): type {
    return struct {
        parent: *Parent,
        output: OutputSrc,
        state: enum {
            waiting_write_err,
            waiting_write_out,
            done,
        },

        pub fn deinit(*_): Yield {
            if (comptime bun.Environment.allow_assert) assert(_.state == .done);
            log("OutputTask({s}, 0x{x}) deinit", .{ @typeName(Parent), @intFromPtr(_) });
            const _task_guard = OutputTaskDeinitGuard<@This()>{ task: _ };
            return vtable.onDone(_.parent);
        }

        pub fn start(*_, errbuf: OutputTaskErrBuf): Yield {
            log("OutputTask({s}, 0x{x}) start errbuf={s}", .{ @typeName(Parent), @intFromPtr(_), if (errbuf != null) errbuf.?[0..@min(128, errbuf.?.len)] else "null" });
            _.state = .waiting_write_err;
            if (errbuf != null) {
                const e = errbuf.?;
                const y_err = vtable.writeErr(_.parent, _, e);
                if (y_err != null) return y_err.?;
                return _.next();
            }
            _.state = .waiting_write_out;
            const y_out = vtable.writeOut(_.parent, _, &_.output);
            if (y_out != null) return y_out.?;
            vtable.onWriteOut(_.parent);
            _.state = .done;
            return _.deinit();
        }

        pub fn next(*_): Yield {
            switch (_.state) {
                .waiting_write_err => {
                    vtable.onWriteErr(_.parent);
                    _.state = .waiting_write_out;
                    const y_out = vtable.writeOut(_.parent, _, &_.output);
                    if (y_out != null) return y_out.?;
                    vtable.onWriteOut(_.parent);
                    _.state = .done;
                    return _.deinit();
                },
                .waiting_write_out => {
                    vtable.onWriteOut(_.parent);
                    _.state = .done;
                    return _.deinit();
                },
                .done => @panic("Invalid state"),
            }
        }

        pub fn onIOWriterChunk(*_, _written: usize, e: OutputTaskSysErr): Yield {
            * = _written;
            log("OutputTask({s}, 0x{x}) onIOWriterChunk", .{ @typeName(Parent), @intFromPtr(_) });
            if (e != null) {
                e.?.deref();
            }

            switch (_.state) {
                .waiting_write_err => {
                    vtable.onWriteErr(_.parent);
                    _.state = .waiting_write_out;
                    const y_out = vtable.writeOut(_.parent, _, &_.output);
                    if (y_out != null) return y_out.?;
                    vtable.onWriteOut(_.parent);
                    _.state = .done;
                    return _.deinit();
                },
                .waiting_write_out => {
                    vtable.onWriteOut(_.parent);
                    _.state = .done;
                    return _.deinit();
                },
                .done => @panic("Invalid state"),
            }
        }
    };
}

/// All owned memory is assumed to be allocated with `bun.default_allocator`
pub union(enum) OutputSrc {
    arrlist: std.ArrayListUnmanaged(u8),
    owned_buf: []const u8,
    borrowed_buf: []const u8,

    fn slice(*_): []const u8 {
        return switch (_.*) {
            .arrlist => _.arrlist.items[0..],
            .owned_buf => _.owned_buf,
            .borrowed_buf => _.borrowed_buf,
        };
    }

    fn deinit(*_) {
        switch (_.*) {
            .arrlist => {
                _.arrlist.deinit(bun.default_allocator);
            },
            .owned_buf => {
                bun.default_allocator.free(_.owned_buf);
            },
            .borrowed_buf => {},
        }
    }
}

/// Custom parse error for invalid options
pub union(enum) ParseError {
    illegal_option: []const u8,
    unsupported: []const u8,
    show_usage: void,
}
pub fn unsupportedFlag(comptime name: []const u8): []const u8 {
    return "unsupported option, please open a GitHub issue -- " ++ name ++ "\n";
}
pub type FlagArgs = ([]const CStr)?;
pub union(enum) ParseFlagResult { continue_parsing: void, done: void, illegal_option: []const u8, unsupported: []const u8, show_usage: void }
pub fn FlagParser(comptime Opts: type): type {
    return struct {
        pub const Result = _ResultMod.Result;

        pub static fn parseFlags(opts: Opts, args: []const CStr): Result(FlagArgs, ParseError) {
            let idx: usize = 0;
            if (args.len == 0) {
                return { ok: null };
            }

            while (idx < args.len) : (idx += 1) {
                const flag = args[idx];
                switch (parseFlag(opts, flag[0..std.mem.len(flag)])) {
                    .done => {
                        const filepath_args = args[idx..];
                        return { ok: filepath_args };
                    },
                    .continue_parsing => {},
                    .illegal_option => |opt_str| return { err: { illegal_option: opt_str } },
                    .unsupported => |unsp| return { err: { unsupported: unsp } },
                    .show_usage => return { err: .show_usage },
                }
            }

            return { err: .show_usage };
        }

        pub static fn parseFlag(opts: Opts, flag: []const u8): ParseFlagResult {
            if (flag.len == 0) return .done;
            if (flag[0] != '-') return .done;

            if (flag.len == 1) return { illegal_option: "-" };

            if (flag.len > 2 && flag[1] == '-') {
                if (result := opts.parseLong(flag)) return result;
            }

            const small_flags = flag[1..];
            let i: usize = 0;
            for (char of small_flags) {
                if (e := opts.parseShort(char, small_flags, i)) {
                    return e;
                }
                i += 1;
            }

            return .continue_parsing;
        }
    };
}

pub fn isPollable(fd: bun.FD, mode: bun.Mode): bool {
    return switch (bun.Environment.os) {
        .windows, .wasm => false,
        .linux, .freebsd => posix.S.ISFIFO(mode) || posix.S.ISSOCK(mode) || posix.isatty(fd.native()),
        // macos DOES allow regular files to be pollable, but we don't want that because
        // our IOWriter code has a separate and better codepath for writing to files.
        .mac => if (posix.S.ISREG(mode)) false else posix.S.ISFIFO(mode) || posix.S.ISSOCK(mode) || posix.isatty(fd.native()),
    };
}

pub fn isPollableFromMode(mode: bun.Mode): bool {
    return switch (bun.Environment.os) {
        .windows, .wasm => false,
        .linux, .freebsd => posix.S.ISFIFO(mode) || posix.S.ISSOCK(mode),
        // macos DOES allow regular files to be pollable, but we don't want that because
        // our IOWriter code has a separate and better codepath for writing to files.
        .mac => if (posix.S.ISREG(mode)) false else posix.S.ISFIFO(mode) || posix.S.ISSOCK(mode),
    };
}

pub fn unreachableState(context: []const u8, state: []const u8): noreturn {
    @branchHint(.cold);
    return bun.Output.panic("Bun shell has reached an unreachable state \"{s}\" in the {s} context. This indicates a bug, please open a GitHub issue.", .{ state, context });
}

import builtin from "builtin";
import _StateMod from "./states/Base.zig";
import _ScriptMod from "./states/Script.zig";
import _StmtMod from "./states/Stmt.zig";
import _PipelineMod from "./states/Pipeline.zig";
import _BinaryMod from "./states/Binary.zig";
import _SubshellMod from "./states/Subshell.zig";
import _ExpansionMod from "./states/Expansion.zig";
import _AssignsMod from "./states/Assigns.zig";
import _AsyncMod from "./states/Async.zig";
import _CondExprMod from "./states/CondExpr.zig";
import _IfMod from "./states/If.zig";
import _CmdMod from "./states/Cmd.zig";
import _BuiltinMod from "./Builtin.zig";
import _IOMod from "./IO.zig";
import _IOReaderMod from "./IOReader.zig";
import _IOWriterMod from "./IOWriter.zig";
import _ResultMod from "../bun_core/result.zig";
import _StringMod from "../string/string.zig";
const WTFStringImplStruct = _StringMod.WTFStringImplStruct;

import bun from "bun";
const ResolvePath = bun.path;
const TaggedPointerUnion = bun.TaggedPointerUnion;
const assert = bun.assert;
const which = bun.which;
const Maybe = bun.sys.Maybe;

const jsc = bun.jsc;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const shell = bun.shell;
const Yield = shell.Yield;
const ast = shell.AST;

const windows = bun.windows;
const uv = windows.libuv;

const posix = std.posix;
const Allocator = std.mem.Allocator;
