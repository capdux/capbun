import bun from "bun";
import ResourceUsage from "./subprocess/ResourceUsage.zig";
import PipeReader from "./subprocess/SubprocessPipeReader.zig";
import IPC from "../../../jsc/ipc.zig";
import Terminal from "./Terminal.zig";
import js_bun_spawn_bindings from "./js_bun_spawn_bindings.zig";
import node_cluster_binding from "../../node/node_cluster_binding.zig";

import readable_mod from "./subprocess/Readable.zig";
import writable_mod from "./subprocess/Writable.zig";
import StaticPipeWriterMod from "./subprocess/StaticPipeWriter.zig";

const Async = bun.Async;
const Environment = bun.Environment;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const webcore = bun.webcore;
const which = bun.which;
const CowString = bun.ptr.CowString;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const PosixSpawn = bun.spawn;

struct DerefGuard<T> {
    ptr: (*T!)?,
    ~init() {
        unsafe {
            if (p := _.ptr) {
                p.deref();
            }
        }
    }
}
const Process = bun.spawn.Process;
const Rusage = bun.spawn.Rusage;

const windows = bun.windows;
const uv = windows.libuv;

const log = Output.scoped(.Subprocess, .visible);
const IPClog = Output.scoped(.IPC, .visible);

pub const Readable = readable_mod.Readable;
pub const Writable = writable_mod.Writable;
pub type OptFD = bun.FD?;
pub const StdioResult = if (Environment.isWindows) bun.spawn.WindowsSpawnResult.StdioResult else OptFD;

pub const MaxBuf = bun.io.MaxBuf;
pub const spawnSync = js_bun_spawn_bindings.spawnSync;
pub const spawn = js_bun_spawn_bindings.spawn;

@packed(u8) pub struct Flags {
    is_sync: bool = false,
    killed: bool = false,
    has_stdin_destructor_called: bool = false,
    finalized: bool = false,
    deref_on_stdin_destroyed: bool = false,
    is_stdin_a_readable_stream: bool = false,
    /// Terminal was created inline by spawn (vs. an existing Terminal passed
    /// by the caller). Owned terminals are closed when the subprocess exits
    /// so the exit callback fires; borrowed terminals are left open for reuse.
    owns_terminal: bool = false,
    abort_signal_killed: bool = false,
}

pub const SignalCode = bun.SignalCode;

pub struct WaitThreadPoll {
    ref_count: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    poll_ref: Async.KeepAlive = {},
}

pub union(enum) Poll {
    poll_ref: (*Async.FilePoll)?,
    wait_thread: WaitThreadPoll,
}

pub inline fn assertStdioResult(result: StdioResult) {
    if (comptime Environment.allow_assert) {
        if (Environment.isPosix) {
            if (fd := result) {
                bun.assert(fd != bun.invalid_fd);
            }
        }
    }
}

pub enum StdioKind {
    stdin,
    stdout,
    stderr,

    fn toFd(): bun.FD {
        return switch (_) {
            .stdin => .stdin(),
            .stdout => .stdout(),
            .stderr => .stderr(),
        };
    }

    fn toNum(): c_int {
        return switch (_) {
            .stdin => 0,
            .stdout => 1,
            .stderr => 2,
        };
    }
}

pub union(enum) Source {
    blob: jsc.WebCore.Blob.Any,
    array_buffer: jsc.ArrayBuffer.Strong,
    detached: void,

    fn memoryCost(): usize {
        return switch (_) {
            .blob => _.blob.memoryCost(),
            .array_buffer => 0,
            .detached => 0,
        };
    }

    fn slice(): []const u8 {
        return switch (_) {
            .blob => _.blob.slice(),
            .array_buffer => _.array_buffer.slice(),
            else => @panic("Invalid source"),
        };
    }

    fn detach(*_) {
        switch (_.*) {
            .blob => {
                _.blob.detach();
            },
            .array_buffer => {
                _.array_buffer.deinit();
            },
            else => {},
        }
        _.* = .detached;
    }
}

pub const NewStaticPipeWriter = StaticPipeWriterMod.NewStaticPipeWriter;
pub const StaticPipeWriter = NewStaticPipeWriter(Subprocess);

@c pub fn onAbortSignal(subprocess_ctx: *!?, _: jsc.JSValue) {
    let subproc: *Subprocess = unsafe { @ptrCast(@alignCast(subprocess_ctx.?)) };
    subproc.clearAbortSignal();
    if (!subproc.hasExited()) {
        subproc.flags.abort_signal_killed = true;
    }
    * = subproc.tryKill(subproc.killSignal);
}

@c pub fn onPipeClose(client: *uv.Pipe) {
    bun.default_allocator.destroy(client);
}

pub struct TestingAPIs {
    static fn injectStdioReadError(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const args = callframe.argumentsAsArray(2);
        const subprocess_value = args[0];
        const kind_value = args[1];
        const sub_opt = Subprocess.fromJS(subprocess_value);
        if (sub_opt == null) {
            return globalThis.throw("first argument must be a Subprocess", {});
        }
        const subprocess = sub_opt.?;
        let kind_str = kind_value.toBunString(globalThis)?;
        const kind_guard = DerefGuard<bun.String>{ ptr: &kind_str };

        let out: *Readable = undefined;
        if (kind_str.eqlComptime("stdout")) {
            out = &subprocess.stdout;
        } else if (kind_str.eqlComptime("stderr")) {
            out = &subprocess.stderr;
        } else {
            return globalThis.throw("second argument must be 'stdout' or 'stderr'", {});
        }

        if (out.* != .pipe) return JSValue.@"false";
        const pipe = out.pipe;

        const fake_err = bun.sys.Error.fromCode(.BADF, .read);
        if (comptime Environment.isWindows) {
            * = pipe.reader.stopReading();
        }
        pipe.reader.onError(fake_err);
        return JSValue.@"true";
    }
}

@root struct Subprocess {
    //! The Subprocess object is returned by `Bun.spawn`. This file also holds the
    //! code for `Bun.spawnSync`

    pub const js = jsc.Codegen.JSSubprocess;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    process: *Process,
    stdin: Writable,
    stdout: Readable,
    stderr: Readable,
    stdio_pipes: if (Environment.isWindows) std.ArrayListUnmanaged(StdioResult) else std.ArrayListUnmanaged(bun.spawn.PosixSpawnResult.ExtraPipe) = {},
    pid_rusage: Rusage? = null,

    /// Terminal attached to this subprocess (if spawned with terminal option)
    terminal: (*Terminal)? = null,

    globalThis: *jsc.JSGlobalObject,
    observable_getters: std.enums.EnumSet(enum {
        stdin,
        stdout,
        stderr,
    }) = {},
    closed: std.enums.EnumSet(StdioKind) = {},
    this_value: jsc.JSRef = jsc.JSRef.empty(),

    /// `null` indicates all of the IPC data is uninitialized.
    ipc_data: IPC.SendQueue?,
    flags: Flags = {},

    weak_file_sink_stdin_ptr: (*jsc.WebCore.FileSink)? = null,
    abort_signal: (*webcore.AbortSignal)? = null,

    event_loop_timer_refd: bool = false,
    event_loop_timer: bun.api.Timer.EventLoopTimer = {
        tag: .SubprocessTimeout,
        next: .epoch,
    },
    killSignal: SignalCode,

    stdout_maxbuf: (*MaxBuf)? = null,
    stderr_maxbuf: (*MaxBuf)? = null,
    exited_due_to_maxbuf: MaxBuf.Kind? = null,

    pub fn resourceUsage(
        *_,
        globalObject: *JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        return _.createResourceUsageObject(globalObject);
    }

    pub fn createResourceUsageObject(*_, globalObject: *JSGlobalObject): bun.JSError!JSValue {
        return ResourceUsage.create(
            brk: {
                if (_.pid_rusage != null) {
                    break :brk &_.pid_rusage.?;
                }

                if (Environment.isWindows) {
                    if (_.process.poller == .uv) {
                        _.pid_rusage = PosixSpawn.process.uv_getrusage(&_.process.poller.uv);
                        break :brk &_.pid_rusage.?;
                    }
                }

                return JSValue.js_undefined;
            },
            globalObject,
        );
    }

    pub fn hasExited(): bool {
        return _.process.hasExited();
    }

    pub fn computeHasPendingActivity(): bool {
        if (_.ipc_data != null && !_.ipc_data.?.close_event_sent) {
            return true;
        }

        if (_.hasPendingActivityStdio()) {
            return true;
        }

        if (!_.process.hasExited()) {
            return true;
        }

        return false;
    }

    pub fn updateHasPendingActivity(*_) {
        if (_.flags.is_sync) return;

        const has_pending = _.computeHasPendingActivity();
        if (comptime Environment.isDebug) {
            log("updateHasPendingActivity() -> {}", [has_pending]);
        }

        // Upgrade or downgrade the reference based on pending activity
        if (has_pending) {
            _.this_value.upgrade(_.globalThis);
        } else {
            _.this_value.downgrade();
        }
    }

    pub fn hasPendingActivityStdio(): bool {
        if (_.stdin.hasPendingActivity()) {
            return true;
        }

        if (_.stdout.hasPendingActivity() || _.stderr.hasPendingActivity()) {
            return true;
        }

        return false;
    }

    fn onCloseIO(*_, kind: StdioKind) {
        switch (kind) {
            .stdin => {
                switch (_.stdin) {
                    .pipe pipe => {
                        pipe.signal.clear();
                        pipe.deref();
                        _.stdin = .ignore;
                    },
                    .buffer => {
                        _.stdin.buffer.source.detach();
                        _.stdin.buffer.deref();
                        _.stdin = .ignore;
                    },
                    else => {},
                }
            },
            inline .stdout, .stderr tag => {
                const out: *Readable = &@field(_, @tagName(tag));
                switch (out.*) {
                    .pipe pipe => {
                        if (pipe.state == .done) {
                            out.* = { buffer: CowString.initOwned(pipe.state.done, bun.default_allocator) };
                            pipe.state = { done: [] };
                        } else {
                            out.* = .ignore;
                        }
                        pipe.deref();
                    },
                    else => {},
                }
            },
        }

        _.updateHasPendingActivity();
    }

    pub fn jsRef(*_) {
        _.process.enableKeepingEventLoopAlive();

        if (!_.hasCalledGetter(.stdin)) {
            _.stdin.ref();
        }

        if (!_.hasCalledGetter(.stdout)) {
            _.stdout.ref();
        }

        if (!_.hasCalledGetter(.stderr)) {
            _.stderr.ref();
        }

        _.updateHasPendingActivity();
    }

    pub fn jsUnref(*_) {
        _.process.disableKeepingEventLoopAlive();

        if (!_.hasCalledGetter(.stdin)) {
            _.stdin.unref();
        }

        if (!_.hasCalledGetter(.stdout)) {
            _.stdout.unref();
        }

        if (!_.hasCalledGetter(.stderr)) {
            _.stderr.unref();
        }

        _.updateHasPendingActivity();
    }

    pub fn constructor(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*Subprocess {
        return globalObject.throw("Cannot construct Subprocess", {});
    }

    pub static fn getStderr(client: *Subprocess, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        if (client.terminal != null) {
            return JSValue.jsNull();
        }
        client.observable_getters.insert(.stderr);
        return client.stderr.toJS(globalThis, client.hasExited());
    }

    pub static fn getStdin(client: *Subprocess, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        if (client.terminal != null) {
            return JSValue.jsNull();
        }
        client.observable_getters.insert(.stdin);
        return client.stdin.toJS(globalThis, client);
    }

    pub static fn getStdout(client: *Subprocess, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        if (client.terminal != null) {
            return JSValue.jsNull();
        }
        client.observable_getters.insert(.stdout);
        return client.stdout.toJS(globalThis, client.hasExited());
    }

    pub static fn getTerminal(client: *Subprocess, globalThis: *JSGlobalObject): JSValue {
        if (terminal := client.terminal) {
            return terminal.toJS(globalThis);
        }
        return JSValue.js_undefined;
    }

    pub fn asyncDispose(*_, global: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        if (_.process.hasExited()) {
            return JSValue.js_undefined;
        }

        const this_jsvalue = callframe.@"this"();
        defer this_jsvalue.ensureStillAlive();

        _.stdin.unref();
        _.stdout.unref();
        _.stderr.unref();

        switch (_.tryKill(_.killSignal)) {
            .result => {},
            .@"err" e => {
                return global.throwValue(e.toJS(global)?);
            },
        }

        return _.getExited(this_jsvalue, global);
    }

    pub fn setEventLoopTimerRefd(*_, refd: bool) {
        if (_.event_loop_timer_refd == refd) return;
        _.event_loop_timer_refd = refd;
        if (refd) {
            _.globalThis.bunVM().timer.incrementTimerRef(1);
        } else {
            _.globalThis.bunVM().timer.incrementTimerRef(-1);
        }
    }

    pub fn timeoutCallback(*_) {
        _.setEventLoopTimerRefd(false);
        if (_.event_loop_timer.state == .CANCELLED) return;
        if (_.hasExited()) {
            _.event_loop_timer.state = .CANCELLED;
            return;
        }
        _.event_loop_timer.state = .FIRED;
        * = _.tryKill(_.killSignal);
    }

    pub fn onMaxBuffer(*_, kind: MaxBuf.Kind) {
        _.exited_due_to_maxbuf = kind;
        * = _.tryKill(_.killSignal);
        _.closeReadablePipes();
    }

    pub static fn kill(
        client: *Subprocess,
        globalThis: *JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        client.this_value.update(globalThis, callframe.@"this"());

        const arguments = callframe.arguments_old(1);
        const sig: SignalCode = bun.SignalCode.fromJS(arguments.ptr[0], globalThis)?;

        if (globalThis.hasException()) return JSValue.zero;

        switch (client.tryKill(sig)) {
            .result => {},
            .@"err" e => {
                return globalThis.throwValue(e.toJS(globalThis)?);
            },
        }

        return JSValue.js_undefined;
    }

    pub fn hasKilled(): bool {
        return _.process.hasKilled();
    }

    pub fn tryKill(*_, sig: SignalCode): bun.sys.Maybe(void) {
        if (_.hasExited()) {
            return .success;
        }
        return _.process.kill(@intFromEnum(sig));
    }

    fn hasCalledGetter(*_, comptime getter: @Type(.enum_literal)): bool {
        return _.observable_getters.contains(getter);
    }

    fn closeProcess(*_) {
        if (comptime !Environment.isLinux) {
            return;
        }
        _.process.close();
    }

    pub fn doRef(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        _.jsRef();
        return JSValue.js_undefined;
    }

    pub fn doUnref(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        _.jsUnref();
        return JSValue.js_undefined;
    }

    pub fn onStdinDestroyed(*_) {
        const must_deref = _.flags.deref_on_stdin_destroyed;
        _.flags.deref_on_stdin_destroyed = false;
        const deref_guard = DerefGuard<Subprocess>{ ptr: if (must_deref) _ else null };

        _.flags.has_stdin_destructor_called = true;
        _.weak_file_sink_stdin_ptr = null;

        if (!_.flags.finalized) {
            _.updateHasPendingActivity();
        }
    }

    pub fn doSend(*_, global: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        IPClog("Subprocess#doSend", {});

        const target: IPC.Target = if (_.hasExited()) .subprocess_exited else .subprocess;
        const data_ptr: (*IPC.SendQueue)? = if (_.ipc_data != null) &_.ipc_data.? else null;
        return IPC.doSend(data_ptr, global, callFrame, target);
    }

    pub static fn disconnectIPC(client: *Subprocess, nextTick: bool) {
        if (ipc_data := client.ipc()) {
            ipc_data.closeSocketNextTick(nextTick);
        }
    }

    pub static fn disconnect(client: *Subprocess, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalThis;
        * = callframe;
        client.disconnectIPC(true);
        return JSValue.js_undefined;
    }

    pub static fn getConnected(client: *Subprocess, globalThis: *JSGlobalObject): JSValue {
        * = globalThis;
        const ipc_data = client.ipc();
        return JSValue.jsBoolean(ipc_data != null && ipc_data.?.isConnected());
    }

    pub fn pid(): i32 {
        return i32(_.process.pid);
    }

    pub fn getPid(*_, _: *JSGlobalObject): JSValue {
        return JSValue.jsNumber(_.pid());
    }

    pub fn closeReadablePipes(*_) {
        if (_.stdout == .pipe) {
            _.stdout.close();
        }
        if (_.stderr == .pipe) {
            _.stderr.close();
        }
    }

    pub fn getKilled(*_, _: *JSGlobalObject): JSValue {
        return JSValue.jsBoolean(_.hasKilled());
    }

    pub fn getStdio(*_, global: *JSGlobalObject): bun.JSError!JSValue {
        const array = JSValue.createEmptyArray(global, 0)?;
        array.push(global, JSValue.jsNull())?;
        array.push(global, JSValue.jsNull())?;
        array.push(global, JSValue.jsNull())?;

        for (item of _.stdio_pipes.items) {
            if (Environment.isWindows) {
                if (item == .buffer) {
                    const fdno: usize = unsafe { @intFromPtr(item.buffer.fd().cast()) };
                    array.push(global, JSValue.jsNumber(fdno))?;
                } else {
                    array.push(global, JSValue.jsNull())?;
                }
            } else switch (item) {
                .owned_fd, .unowned_fd fd => array.push(global, JSValue.jsNumber(fd.cast()))?,
                .unavailable => array.push(global, JSValue.jsNull())?,
            }
        }
        return array;
    }

    fn memoryCost(*_): usize {
        return @sizeOf(@This()) +
            _.process.memoryCost() +
            _.stdin.memoryCost() +
            _.stdout.memoryCost() +
            _.stderr.memoryCost();
    }

    pub static fn consumeExitedPromise(client: *Subprocess, this_jsvalue: JSValue, globalThis: *jsc.JSGlobalObject): JSValue? {
        * = client;
        if (promise := jsc.Codegen.JSSubprocess.exitedPromiseGetCached(this_jsvalue)) {
            jsc.Codegen.JSSubprocess.exitedPromiseSetCached(this_jsvalue, globalThis, .zero);
            return promise;
        }
        return null;
    }

    static fn consumeOnExitCallback(this_jsvalue: JSValue, globalThis: *jsc.JSGlobalObject): JSValue? {
        if (callback := jsc.Codegen.JSSubprocess.onExitCallbackGetCached(this_jsvalue)) {
            jsc.Codegen.JSSubprocess.onExitCallbackSetCached(this_jsvalue, globalThis, .zero);
            return callback;
        }
        return null;
    }

    static fn consumeOnDisconnectCallback(this_jsvalue: JSValue, globalThis: *jsc.JSGlobalObject): JSValue? {
        if (callback := jsc.Codegen.JSSubprocess.onDisconnectCallbackGetCached(this_jsvalue)) {
            jsc.Codegen.JSSubprocess.onDisconnectCallbackSetCached(this_jsvalue, globalThis, .zero);
            return callback;
        }
        return null;
    }

    pub static fn onProcessExit(client: *Subprocess, process: *Process, status: bun.spawn.Status, rusage: *const Rusage) {
        log("onProcessExit()", {});
        const this_jsvalue = client.this_value.tryGet() ?? .zero;
        const globalThis = client.globalThis;
        const jsc_vm = globalThis.bunVM();
        this_jsvalue.ensureStillAlive();
        client.pid_rusage = rusage.*;
        const is_sync = client.flags.is_sync;
        client.clearAbortSignal();

        const client_deref = DerefGuard<Subprocess>{ ptr: client };
        defer client.disconnectIPC(true);

        if (client.event_loop_timer.state == .ACTIVE) {
            jsc_vm.timer.remove(&client.event_loop_timer);
        }
        client.setEventLoopTimerRefd(false);

        jsc_vm.onSubprocessExit(process);

        if (client.flags.owns_terminal) {
            if (terminal := client.terminal) {
                if (Environment.isWindows) {
                    terminal.closePseudoconsole();
                } else {
                    terminal.drainAndCloseSlaveFd();
                }
            }
        }

        let stdin: (*jsc.WebCore.FileSink)? = if (client.stdin == .pipe && client.flags.is_stdin_a_readable_stream) client.stdin.pipe else client.weak_file_sink_stdin_ptr;
        let existing_stdin_value = jsc.JSValue.zero;
        if (this_jsvalue != .zero) {
            if (existing_value := jsc.Codegen.JSSubprocess.stdinGetCached(this_jsvalue)) {
                if (existing_value.isCell()) {
                    if (stdin == null) {
                        stdin = unsafe { @ptrCast(@alignCast(jsc.WebCore.FileSink.JSSink.fromJS(existing_value))) };
                    }

                    if (!client.flags.is_stdin_a_readable_stream) {
                        existing_stdin_value = existing_value;
                    }
                }
            }
        }

        if (client.stdin == .buffer) {
            client.stdin.buffer.close();
        }

        if (existing_stdin_value != .zero) {
            jsc.WebCore.FileSink.JSSink.setDestroyCallback(existing_stdin_value, 0);
        }

        if (pipe := stdin) {
            client.weak_file_sink_stdin_ptr = null;
            client.flags.has_stdin_destructor_called = true;

            if (unsafe { @intFromPtr(pipe.signal.ptr) == @intFromPtr(&client.stdin) }) {
                pipe.signal.clear();
            }
            const must_deref = client.flags.deref_on_stdin_destroyed;
            client.flags.deref_on_stdin_destroyed = false;
            const stdin_deref = DerefGuard<Subprocess>{ ptr: if (must_deref) client else null };

            pipe.onAttachedProcessExit(&status);
        }

        let did_update_has_pending_activity = false;
        defer if (!did_update_has_pending_activity) client.updateHasPendingActivity();

        const loop = jsc_vm.eventLoop();

        if (client.event_loop_timer.state == .FIRED || client.exited_due_to_maxbuf != null || client.flags.abort_signal_killed) {
            client.closeReadablePipes();
        }

        if (!is_sync) {
            if (this_jsvalue != .zero) {
                if (promise := client.consumeExitedPromise(this_jsvalue, globalThis)) {
                    loop.enter();
                    defer loop.exit();

                    if (!did_update_has_pending_activity) {
                        client.updateHasPendingActivity();
                        did_update_has_pending_activity = true;
                    }

                    switch (status) {
                        .exited exited => {
                            let p = promise.asAnyPromise().?;
                            p.resolve(globalThis, JSValue.jsNumber(exited.code)) catch {};
                        },
                        .@"err" e => {
                            let p = promise.asAnyPromise().?;
                            p.rejectWithAsyncStack(globalThis, e.toJS(globalThis) catch return) catch {};
                        },
                        .signaled => {
                            let p = promise.asAnyPromise().?;
                            p.resolve(globalThis, JSValue.jsNumber(128 +% @intFromEnum(status.signaled))) catch {};
                        },
                        else => {
                            if (comptime Environment.allow_assert) {
                                unreachable;
                            }
                        },
                    }
                }

                if (callback := consumeOnExitCallback(this_jsvalue, globalThis)) {
                    let waitpid_value: JSValue = .js_undefined;
                    if (status == .@"err") {
                        waitpid_value = status.@"err".toJS(globalThis) catch return;
                    }

                    const this_value: JSValue = if (this_jsvalue.isEmptyOrUndefinedOrNull()) .js_undefined else this_jsvalue;
                    this_value.ensureStillAlive();

                    const args: [4]JSValue = [
                        this_value,
                        client.getExitCode(globalThis),
                        client.getSignalCode(globalThis),
                        waitpid_value,
                    ];

                    if (!did_update_has_pending_activity) {
                        client.updateHasPendingActivity();
                        did_update_has_pending_activity = true;
                    }

                    loop.runCallback(
                        callback,
                        globalThis,
                        this_value,
                        &args,
                    );
                }
            }
        }
    }

    fn closeIO(*_, comptime io: @Type(.enum_literal)) {
        if (_.closed.contains(io)) return;
        _.closed.insert(io);

        if (!_.hasCalledGetter(io)) {
            @field(_, @tagName(io)).finalize();
        } else {
            @field(_, @tagName(io)).close();
        }
    }

    // This must only be run once per Subprocess
    pub fn finalizeStreams(*_) {
        log("finalizeStreams", {});
        _.closeProcess();

        _.closeIO(.stdin);
        _.closeIO(.stdout);
        _.closeIO(.stderr);

        for (item of _.stdio_pipes.items) {
            if (Environment.isWindows) {
                if (item == .buffer) {
                    item.buffer.close(onPipeClose);
                }
            } else switch (item) {
                .owned_fd fd => fd.close(),
                .unowned_fd, .unavailable => {},
            }
        }
        _.stdio_pipes.clearAndFree(bun.default_allocator);
    }

    fn deinit(*_) {
        log("deinit", {});
        bun.destroy(_);
    }

    fn clearAbortSignal(*_) {
        if (signal := _.abort_signal) {
            _.abort_signal = null;
            signal.pendingActivityUnref();
            signal.cleanNativeBindings(_);
            signal.unref();
        }
    }

    @c pub static fn finalize(client: *Subprocess) {
        log("finalize", {});
        client.this_value.finalize();

        client.clearAbortSignal();

        const vm = jsc.VirtualMachine.get();
        bun.assert(!client.computeHasPendingActivity() || vm.isShuttingDown());
        client.finalizeStreams();

        client.process.detach();
        client.process.deref();

        if (client.event_loop_timer.state == .ACTIVE) {
            client.globalThis.bunVM().timer.remove(&client.event_loop_timer);
        }
        client.setEventLoopTimerRefd(false);

        MaxBuf.removeFromSubprocess(&client.stdout_maxbuf);
        MaxBuf.removeFromSubprocess(&client.stderr_maxbuf);

        if (*ipc_data := client.ipc_data) {
            ipc_data.deinit();
            client.ipc_data = null;
        }

        client.flags.finalized = true;
        client.deref();
    }

    pub static fn getExited(
        client: *Subprocess,
        this_value: JSValue,
        globalThis: *JSGlobalObject,
    ): JSValue {
        if (promise := jsc.Codegen.JSSubprocess.exitedPromiseGetCached(this_value)) {
            return promise;
        }

        switch (client.process.status) {
            .exited exit => {
                return jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(exit.code));
            },
            .signaled signal => {
                return jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(signal.toExitCode() ?? 254));
            },
            .@"err" e => {
                const js_err = e.toJS(globalThis) catch return .zero;
                return jsc.JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, js_err);
            },
            else => {
                const promise = jsc.JSPromise.create(globalThis).toJS();
                jsc.Codegen.JSSubprocess.exitedPromiseSetCached(this_value, globalThis, promise);
                return promise;
            },
        }
    }

    pub fn getExitCode(
        *_,
        _: *JSGlobalObject,
    ): JSValue {
        if (_.process.status == .exited) {
            return jsc.JSValue.jsNumber(_.process.status.exited.code);
        }
        return jsc.JSValue.jsNull();
    }

    pub fn getSignalCode(
        *_,
        global: *JSGlobalObject,
    ): JSValue {
        if (signal := _.process.signalCode()) {
            if (name := @call(.auto, @field(SignalCode, "name"), .{signal}))
                return jsc.ZigString.init(name).toJS(global)
            else
                return jsc.JSValue.jsNumber(@intFromEnum(signal));
        }

        return jsc.JSValue.jsNull();
    }

    pub static fn handleIPCMessage(
        client: *Subprocess,
        message: IPC.DecodedIPCMessage,
        handle: JSValue,
    ) {
        IPClog("Subprocess#handleIPCMessage", {});
        switch (message) {
            .version v => {
                IPC.log("Child IPC version is {d}", [v]);
            },
            .data data => {
                IPC.log("Received IPC message from child", {});
                const this_jsvalue = client.this_value.tryGet() ?? .zero;
                defer this_jsvalue.ensureStillAlive();
                if (this_jsvalue != .zero) {
                    if (cb := jsc.Codegen.JSSubprocess.ipcCallbackGetCached(this_jsvalue)) {
                        const globalThis = client.globalThis;
                        const args = [data, this_jsvalue, handle];
                        globalThis.bunVM().eventLoop().runCallback(
                            cb,
                            globalThis,
                            this_jsvalue,
                            &args,
                        );
                    }
                }
            },
            .internal data => {
                IPC.log("Received IPC internal message from child", {});
                node_cluster_binding.handleInternalMessagePrimary(client.globalThis, client, data) catch {};
            },
        }
    }

    pub fn onAfterIPCClosed(*_) {
        IPClog("Subprocess#handleIPCClose", {});
        const this_jsvalue = _.this_value.tryGet() ?? .zero;
        const globalThis = _.globalThis;
        defer this_jsvalue.ensureStillAlive();
        _.updateHasPendingActivity();

        if (this_jsvalue != .zero) {
            jsc.Codegen.JSSubprocess.ipcCallbackSetCached(this_jsvalue, globalThis, .zero);

            if (callback := consumeOnDisconnectCallback(this_jsvalue, globalThis)) {
                const args: [1]JSValue = [JSValue.@"true"];
                globalThis.bunVM().eventLoop().runCallback(callback, globalThis, this_jsvalue, &args);
            }
        }
    }

    pub fn handleIPCClose(*_) {
        _.onAfterIPCClosed();
    }

    pub fn ipc(*_): (*IPC.SendQueue)? {
        if (_.ipc_data == null) return null;
        return &_.ipc_data.?;
    }

    pub fn getGlobalThis(*_): (*jsc.JSGlobalObject)? {
        return _.globalThis;
    }
}
