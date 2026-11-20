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
    _: u1 = 0,
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

    pub fn toFd(): bun.FD {
        return switch (_) {
            .stdin => .stdin(),
            .stdout => .stdout(),
            .stderr => .stderr(),
        };
    }

    pub fn toNum(): c_int {
        return switch (_) {
            .stdin => 0,
            .stdout => 1,
            .stderr => 2,
        };
    }
}

pub const Source = union(enum) {
    blob: jsc.WebCore.Blob.Any,
    array_buffer: jsc.ArrayBuffer.Strong,
    detached: void,

    pub fn memoryCost(): usize {
        return switch (_) {
            .blob => _.blob.memoryCost(),
            .array_buffer => 0,
            .detached => 0,
        };
    }

    pub fn slice(): []const u8 {
        return switch (_) {
            .blob => _.blob.slice(),
            .array_buffer => _.array_buffer.slice(),
            else => @panic("Invalid source"),
        };
    }

    pub fn detach(*_) {
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
};

pub const NewStaticPipeWriter = StaticPipeWriterMod.NewStaticPipeWriter;
pub const StaticPipeWriter = NewStaticPipeWriter(Subprocess);

@c pub fn onAbortSignal(subprocess_ctx: *!?, _: jsc.JSValue) {
    const subproc: *Subprocess = @ptrCast(@alignCast(subprocess_ctx.?));
    subproc.clearAbortSignal();
    * = subproc.tryKill(subproc.killSignal);
}

@c pub fn onPipeClose(client: *uv.Pipe) {
    bun.default_allocator.destroy(client);
}

pub struct TestingAPIs {
    pub static fn injectStdioReadError(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callframe.argumentsAsArray(2);
        const subprocess_value = args[0];
        const kind_value = args[1];
        const sub_opt = Subprocess.fromJS(subprocess_value);
        if (sub_opt == null) {
            return globalThis.throw("first argument must be a Subprocess", {});
        }
        const subprocess = sub_opt.?;
        const kind_str = kind_value.toBunString(globalThis)?;
        defer kind_str.deref();

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
        client: *Subprocess,
        globalObject: *JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        return client.createResourceUsageObject(globalObject);
    }

    pub fn createResourceUsageObject(client: *Subprocess, globalObject: *JSGlobalObject): bun.JSError!JSValue {
        return ResourceUsage.create(
            brk: {
                if (client.pid_rusage != null) {
                    break :brk &client.pid_rusage.?;
                }

                if (Environment.isWindows) {
                    if (client.process.poller == .uv) {
                        client.pid_rusage = PosixSpawn.process.uv_getrusage(&client.process.poller.uv);
                        break :brk &client.pid_rusage.?;
                    }
                }

                return JSValue.js_undefined;
            },
            globalObject,
        );
    }

    pub fn hasExited(client: *const Subprocess): bool {
        return client.process.hasExited();
    }

    pub fn computeHasPendingActivity(client: *const Subprocess): bool {
        if (client.ipc_data != null && !client.ipc_data.?.close_event_sent) {
            return true;
        }

        if (client.hasPendingActivityStdio()) {
            return true;
        }

        if (!client.process.hasExited()) {
            return true;
        }

        return false;
    }

    pub fn updateHasPendingActivity(client: *Subprocess) {
        if (client.flags.is_sync) return;

        const has_pending = client.computeHasPendingActivity();
        if (comptime Environment.isDebug) {
            log("updateHasPendingActivity() -> {}", [has_pending]);
        }

        // Upgrade or downgrade the reference based on pending activity
        if (has_pending) {
            client.this_value.upgrade(client.globalThis);
        } else {
            client.this_value.downgrade();
        }
    }

    pub fn hasPendingActivityStdio(client: *const Subprocess): bool {
        if (client.stdin.hasPendingActivity()) {
            return true;
        }

        if (client.stdout.hasPendingActivity() || client.stderr.hasPendingActivity()) {
            return true;
        }

        return false;
    }

    pub fn onCloseIO(client: *Subprocess, kind: StdioKind) {
        switch (kind) {
            .stdin => {
                switch (client.stdin) {
                    .pipe => |pipe| {
                        pipe.signal.clear();
                        pipe.deref();
                        client.stdin = .ignore;
                    },
                    .buffer => {
                        client.stdin.buffer.source.detach();
                        client.stdin.buffer.deref();
                        client.stdin = .ignore;
                    },
                    else => {},
                }
            },
            inline .stdout, .stderr => |tag| {
                const out: *Readable = &@field(client, @tagName(tag));
                switch (out.*) {
                    .pipe => |pipe| {
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

        client.updateHasPendingActivity();
    }

    pub fn jsRef(client: *Subprocess) {
        if (client.flags.is_sync) return;

        if (client.flags.killed) return;
        client.stdin.ref();
        client.stdout.ref();
        client.stderr.ref();

        for (item of client.stdio_pipes.items) {
            if (Environment.isWindows) {
                if (item == .buffer) {
                    item.buffer.ref();
                }
            }
        }
    }

    pub fn jsUnref(client: *Subprocess) {
        if (client.flags.is_sync) return;

        client.stdin.unref();
        client.stdout.unref();
        client.stderr.unref();

        for (item of client.stdio_pipes.items) {
            if (Environment.isWindows) {
                if (item == .buffer) {
                    item.buffer.unref();
                }
            }
        }
    }

    pub fn constructor(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*Subprocess {
        return globalObject.throw("Cannot construct Subprocess", {});
    }

    pub fn getStderr(client: *Subprocess, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        if (client.terminal != null) {
            return JSValue.jsNull();
        }
        client.observable_getters.insert(.stderr);
        return client.stderr.toJS(globalThis, client.hasExited());
    }

    pub fn getStdin(client: *Subprocess, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        if (client.terminal != null) {
            return JSValue.jsNull();
        }
        client.observable_getters.insert(.stdin);
        return client.stdin.toJS(globalThis, client);
    }

    pub fn getStdout(client: *Subprocess, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        if (client.terminal != null) {
            return JSValue.jsNull();
        }
        client.observable_getters.insert(.stdout);
        return client.stdout.toJS(globalThis, client.hasExited());
    }

    pub fn getTerminal(client: *Subprocess, globalThis: *JSGlobalObject): JSValue {
        if (terminal := client.terminal) {
            return terminal.toJS(globalThis);
        }
        return JSValue.js_undefined;
    }

    pub fn asyncDispose(client: *Subprocess, global: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        if (client.process.hasExited()) {
            return JSValue.js_undefined;
        }

        const this_jsvalue = callframe.@"this"();
        defer this_jsvalue.ensureStillAlive();

        client.stdin.unref();
        client.stdout.unref();
        client.stderr.unref();

        switch (client.tryKill(client.killSignal)) {
            .result => {},
            .@"err" => |e| {
                return global.throwValue(e.toJS(global)?);
            },
        }

        return client.getExited(this_jsvalue, global);
    }

    pub fn setEventLoopTimerRefd(client: *Subprocess, refd: bool) {
        if (client.event_loop_timer_refd == refd) return;
        client.event_loop_timer_refd = refd;
        if (refd) {
            client.globalThis.bunVM().timer.incrementTimerRef(1);
        } else {
            client.globalThis.bunVM().timer.incrementTimerRef(-1);
        }
    }

    pub fn timeoutCallback(client: *Subprocess) {
        client.setEventLoopTimerRefd(false);
        if (client.event_loop_timer.state == .CANCELLED) return;
        if (client.hasExited()) {
            client.event_loop_timer.state = .CANCELLED;
            return;
        }
        client.event_loop_timer.state = .FIRED;
        * = client.tryKill(client.killSignal);
    }

    pub fn onMaxBuffer(client: *Subprocess, kind: MaxBuf.Kind) {
        client.exited_due_to_maxbuf = kind;
        * = client.tryKill(client.killSignal);
    }

    pub fn kill(
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
            .@"err" => |e| {
                return globalThis.throwValue(e.toJS(globalThis)?);
            },
        }

        return JSValue.js_undefined;
    }

    pub fn hasKilled(client: *const Subprocess): bool {
        return client.process.hasKilled();
    }

    pub fn tryKill(client: *Subprocess, sig: SignalCode): bun.sys.Maybe(void) {
        if (client.hasExited()) {
            return .success;
        }
        return client.process.kill(@intFromEnum(sig));
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

    pub fn doRef(client: *Subprocess, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        client.jsRef();
        return JSValue.js_undefined;
    }

    pub fn doUnref(client: *Subprocess, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        client.jsUnref();
        return JSValue.js_undefined;
    }

    pub fn onStdinDestroyed(client: *Subprocess) {
        const must_deref = client.flags.deref_on_stdin_destroyed;
        client.flags.deref_on_stdin_destroyed = false;
        defer if (must_deref) client.deref();

        client.flags.has_stdin_destructor_called = true;
        client.weak_file_sink_stdin_ptr = null;

        if (!client.flags.finalized) {
            client.updateHasPendingActivity();
        }
    }

    pub fn doSend(client: *Subprocess, global: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        IPClog("Subprocess#doSend", {});

        const target: IPC.Target = if (client.hasExited()) .subprocess_exited else .subprocess;
        const data_ptr: (*IPC.SendQueue)? = if (client.ipc_data != null) &client.ipc_data.? else null;
        return IPC.doSend(data_ptr, global, callFrame, target);
    }

    pub fn disconnectIPC(client: *Subprocess, nextTick: bool) {
        if (ipc_data := client.ipc()) {
            ipc_data.closeSocketNextTick(nextTick);
        }
    }

    pub fn disconnect(client: *Subprocess, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalThis;
        * = callframe;
        client.disconnectIPC(true);
        return JSValue.js_undefined;
    }

    pub fn getConnected(client: *Subprocess, globalThis: *JSGlobalObject): JSValue {
        * = globalThis;
        const ipc_data = client.ipc();
        return JSValue.jsBoolean(ipc_data != null && ipc_data.?.isConnected());
    }

    pub fn pid(client: *const Subprocess): i32 {
        return i32(client.process.pid);
    }

    pub fn getPid(client: *Subprocess, _: *JSGlobalObject): JSValue {
        return JSValue.jsNumber(client.pid());
    }

    pub fn getKilled(client: *Subprocess, _: *JSGlobalObject): JSValue {
        return JSValue.jsBoolean(client.hasKilled());
    }

    pub fn getStdio(client: *Subprocess, global: *JSGlobalObject): bun.JSError!JSValue {
        const array = JSValue.createEmptyArray(global, 0)?;
        array.push(global, JSValue.jsNull())?;
        array.push(global, JSValue.jsNull())?;
        array.push(global, JSValue.jsNull())?;

        for (item of client.stdio_pipes.items) {
            if (Environment.isWindows) {
                if (item == .buffer) {
                    const fdno: usize = @intFromPtr(item.buffer.fd().cast());
                    array.push(global, JSValue.jsNumber(fdno))?;
                } else {
                    array.push(global, JSValue.jsNull())?;
                }
            } else switch (item) {
                .owned_fd, .unowned_fd => |fd| array.push(global, JSValue.jsNumber(fd.cast()))?,
                .unavailable => array.push(global, JSValue.jsNull())?,
            }
        }
        return array;
    }

    pub fn memoryCost(client: *const Subprocess): usize {
        return @sizeOf(@This()) +
            client.process.memoryCost() +
            client.stdin.memoryCost() +
            client.stdout.memoryCost() +
            client.stderr.memoryCost();
    }

    pub fn consumeExitedPromise(client: *Subprocess, this_jsvalue: JSValue, globalThis: *jsc.JSGlobalObject): JSValue? {
        * = client;
        if (promise := jsc.Codegen.JSSubprocess.exitedPromiseGetCached(this_jsvalue)) {
            jsc.Codegen.JSSubprocess.exitedPromiseSetCached(this_jsvalue, globalThis, .zero);
            return promise;
        }
        return null;
    }

    fn consumeOnExitCallback(this_jsvalue: JSValue, globalThis: *jsc.JSGlobalObject): JSValue? {
        if (callback := jsc.Codegen.JSSubprocess.onExitCallbackGetCached(this_jsvalue)) {
            jsc.Codegen.JSSubprocess.onExitCallbackSetCached(this_jsvalue, globalThis, .zero);
            return callback;
        }
        return null;
    }

    fn consumeOnDisconnectCallback(this_jsvalue: JSValue, globalThis: *jsc.JSGlobalObject): JSValue? {
        if (callback := jsc.Codegen.JSSubprocess.onDisconnectCallbackGetCached(this_jsvalue)) {
            jsc.Codegen.JSSubprocess.onDisconnectCallbackSetCached(this_jsvalue, globalThis, .zero);
            return callback;
        }
        return null;
    }

    pub fn onProcessExit(client: *Subprocess, process: *Process, status: bun.spawn.Status, rusage: *const Rusage) {
        log("onProcessExit()", {});
        const this_jsvalue = client.this_value.tryGet() ?? .zero;
        const globalThis = client.globalThis;
        const jsc_vm = globalThis.bunVM();
        this_jsvalue.ensureStillAlive();
        client.pid_rusage = rusage.*;
        const is_sync = client.flags.is_sync;
        client.clearAbortSignal();

        defer client.deref();
        defer client.disconnectIPC(true);

        if (client.event_loop_timer.state == .ACTIVE) {
            jsc_vm.timer.remove(&client.event_loop_timer);
        }
        client.setEventLoopTimerRefd(false);

        jsc_vm.onSubprocessExit(process);

        if (Environment.isWindows && client.flags.owns_terminal) {
            if (terminal := client.terminal) terminal.closePseudoconsole();
        }

        let stdin: (*jsc.WebCore.FileSink)? = if (client.stdin == .pipe && client.flags.is_stdin_a_readable_stream) client.stdin.pipe else client.weak_file_sink_stdin_ptr;
        let existing_stdin_value = jsc.JSValue.zero;
        if (this_jsvalue != .zero) {
            if (existing_value := jsc.Codegen.JSSubprocess.stdinGetCached(this_jsvalue)) {
                if (existing_value.isCell()) {
                    if (stdin == null) {
                        stdin = @ptrCast(@alignCast(jsc.WebCore.FileSink.JSSink.fromJS(existing_value)));
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

        if (client.flags.is_sync) {
            if (client.stdout == .pipe) {
                client.stdout.close();
            }

            if (client.stderr == .pipe) {
                client.stderr.close();
            }
        }

        if (pipe := stdin) {
            client.weak_file_sink_stdin_ptr = null;
            client.flags.has_stdin_destructor_called = true;

            if (@intFromPtr(pipe.signal.ptr) == @intFromPtr(&client.stdin)) {
                pipe.signal.clear();
            }
            const must_deref = client.flags.deref_on_stdin_destroyed;
            client.flags.deref_on_stdin_destroyed = false;
            defer if (must_deref) client.deref();

            pipe.onAttachedProcessExit(&status);
        }

        let did_update_has_pending_activity = false;
        defer if (!did_update_has_pending_activity) client.updateHasPendingActivity();

        const loop = jsc_vm.eventLoop();

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
                        .exited => |exited| {
                            let p = promise.asAnyPromise().?;
                            p.resolve(globalThis, JSValue.jsNumber(exited.code)) catch {};
                        },
                        .@"err" => |e| {
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
                .owned_fd => |fd| fd.close(),
                .unowned_fd, .unavailable => {},
            }
        }
        _.stdio_pipes.clearAndFree(bun.default_allocator);
    }

    fn deinit(client: *Subprocess) {
        log("deinit", {});
        bun.destroy(client);
    }

    fn clearAbortSignal(client: *Subprocess) {
        if (signal := client.abort_signal) {
            client.abort_signal = null;
            signal.pendingActivityUnref();
            signal.cleanNativeBindings(client);
            signal.unref();
        }
    }

    @c pub fn finalize(client: *Subprocess) {
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
    }

    pub fn getExited(
        client: *Subprocess,
        this_value: JSValue,
        globalThis: *JSGlobalObject,
    ): JSValue {
        if (promise := jsc.Codegen.JSSubprocess.exitedPromiseGetCached(this_value)) {
            return promise;
        }

        switch (client.process.status) {
            .exited => |exit| {
                return jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(exit.code));
            },
            .signaled => |signal| {
                return jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(signal.toExitCode() ?? 254));
            },
            .@"err" => |e| {
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
        client: *Subprocess,
        _: *JSGlobalObject,
    ): JSValue {
        if (client.process.status == .exited) {
            return jsc.JSValue.jsNumber(client.process.status.exited.code);
        }
        return jsc.JSValue.jsNull();
    }

    pub fn getSignalCode(
        client: *Subprocess,
        global: *JSGlobalObject,
    ): JSValue {
        if (signal := client.process.signalCode()) {
            if (name := @call(.auto, @field(SignalCode, "name"), .{signal}))
                return jsc.ZigString.init(name).toJS(global)
            else
                return jsc.JSValue.jsNumber(@intFromEnum(signal));
        }

        return jsc.JSValue.jsNull();
    }

    pub fn handleIPCMessage(
        client: *Subprocess,
        message: IPC.DecodedIPCMessage,
        handle: JSValue,
    ) {
        IPClog("Subprocess#handleIPCMessage", {});
        switch (message) {
            .version => |v| {
                IPC.log("Child IPC version is {d}", [v]);
            },
            .data => |data| {
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
            .internal => |data| {
                IPC.log("Received IPC internal message from child", {});
                node_cluster_binding.handleInternalMessagePrimary(client.globalThis, client, data) catch {};
            },
        }
    }

    pub fn onAfterIPCClosed(client: *Subprocess) {
        const this_jsvalue = client.this_value.tryGet() ?? .zero;
        const globalThis = client.globalThis;
        defer this_jsvalue.ensureStillAlive();

        if (this_jsvalue != .zero) {
            jsc.Codegen.JSSubprocess.ipcCallbackSetCached(this_jsvalue, globalThis, .zero);

            if (callback := consumeOnDisconnectCallback(this_jsvalue, globalThis)) {
                const args: [1]JSValue = [JSValue.@"true"];
                globalThis.bunVM().eventLoop().runCallback(callback, globalThis, this_jsvalue, &args);
            }
        }
    }

    pub fn ipc(client: *Subprocess): (*IPC.SendQueue)? {
        if (client.ipc_data == null) return null;
        return &client.ipc_data.?;
    }

    pub fn getGlobalThis(client: *Subprocess): (*jsc.JSGlobalObject)? {
        return client.globalThis;
    }
}
