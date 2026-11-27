import bun from "bun";
import DotEnv from "../dotenv/env_loader.zig";
import HTTPServerAgent from "./HTTPServerAgent.zig";
import InspectorBunFrontendDevServerAgent from "../runtime/server/InspectorBunFrontendDevServerAgent.zig";

const Environment = bun.Environment;
const Output = bun.Output;
const uv = bun.windows.libuv;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const VirtualMachine = jsc.VirtualMachine;
const ZigException = jsc.ZigException;

pub const DebuggerId = bun.GenericIndex(i32, Debugger);
pub const BunFrontendDevServerAgent = InspectorBunFrontendDevServerAgent.BunFrontendDevServerAgent;

pub enum Mode {
    /// Bun acts as the server. https://debug.bun.sh/ uses this
    listen,
    /// Bun connects to this path. The VSCode extension uses this.
    connect,
}

pub enum Wait { off, shortly, forever }

pub const log = Output.scoped(.debugger, .visible);
const lifecycle_debug = Output.scoped(.LifecycleAgent, .visible);

pub enum AsyncCallType: u8 {
    DOMTimer = 1,
    EventListener = 2,
    PostMessage = 3,
    RequestAnimationFrame = 4,
    Microtask = 5,
}

@c {
    fn Bun__createJSDebugger(global: *JSGlobalObject): u32;
    fn Bun__ensureDebugger(script_execution_context_id: u32, wait_for_connection: bool);
    fn Bun__startJSDebuggerThread(global: *JSGlobalObject, script_execution_context_id: u32, path: *bun.String, port: c_int, wait_for_connection: bool);

    fn Debugger__didScheduleAsyncCall(global: *JSGlobalObject, call: AsyncCallType, id: u64, single_shot: bool);
    fn Debugger__didCancelAsyncCall(global: *JSGlobalObject, call: AsyncCallType, id: u64);
    fn Debugger__didDispatchAsyncCall(global: *JSGlobalObject, call: AsyncCallType, id: u64);
    fn Debugger__willDispatchAsyncCall(global: *JSGlobalObject, call: AsyncCallType, id: u64);

    fn Bun__TestReporterAgentReportTestFound(agent: *TestReporterHandle, callFrame: *jsc.CallFrame, testId: c_int, name: *bun.String, item_type: TestReporterAgent.TestType, parentId: c_int);
    fn Bun__TestReporterAgentReportTestFoundWithLocation(agent: *TestReporterHandle, testId: c_int, name: *bun.String, item_type: TestReporterAgent.TestType, parentId: c_int, sourceURL: *bun.String, line: c_int);
    fn Bun__TestReporterAgentReportTestStart(agent: *TestReporterHandle, testId: c_int);
    fn Bun__TestReporterAgentReportTestEnd(agent: *TestReporterHandle, testId: c_int, bunTestStatus: TestReporterAgent.TestStatus, elapsed: f64);

    fn Bun__LifecycleAgentReportReload(agent: *LifecycleHandle);
    fn Bun__LifecycleAgentReportError(agent: *LifecycleHandle, exception: *ZigException);
    fn Bun__LifecycleAgentPreventExit(agent: *LifecycleHandle);
    fn Bun__LifecycleAgentStopPreventingExit(agent: *LifecycleHandle);
}

let futex_atomic = std.atomic.Value(u32).init(0);
pub let has_created_debugger: bool = false;

@opaque pub struct TestReporterHandle {
    pub fn reportTestFound(*_, callFrame: *jsc.CallFrame, testId: i32, name: *bun.String, item_type: TestReporterAgent.TestType, parentId: i32) {
        Bun__TestReporterAgentReportTestFound(_, callFrame, testId, name, item_type, parentId);
    }

    pub fn reportTestFoundWithLocation(*_, testId: i32, name: *bun.String, item_type: TestReporterAgent.TestType, parentId: i32, sourceURL: *bun.String, line: i32) {
        Bun__TestReporterAgentReportTestFoundWithLocation(_, testId, name, item_type, parentId, sourceURL, line);
    }

    pub fn reportTestStart(*_, testId: c_int) {
        Bun__TestReporterAgentReportTestStart(_, testId);
    }

    pub fn reportTestEnd(*_, testId: c_int, bunTestStatus: TestReporterAgent.TestStatus, elapsed: f64) {
        Bun__TestReporterAgentReportTestEnd(_, testId, bunTestStatus, elapsed);
    }
}

/// this enum is kept in sync with c++ InspectorTestReporterAgent.cpp `enum class BunTestStatus`
pub enum TestReporterTestStatus: u8 {
    pass,
    fail,
    timeout,
    skip,
    todo,
    skipped_because_label,
}

pub enum TestReporterTestType: u8 {
    @"test" = 0,
    describe = 1,
}

pub struct TestReporterAgent {
    handle: (*Handle)? = null,
    const debug = Output.scoped(.TestReporterAgent, .visible);

    pub const TestStatus = TestReporterTestStatus;
    pub const TestType = TestReporterTestType;

    pub const Handle = TestReporterHandle;

    @export pub static fn Bun__TestReporterAgentEnable(agent: *Handle) {
        if (*debugger := VirtualMachine.get().debugger) {
            debug("enable", .{});
            debugger.test_reporter_agent.handle = agent;

            // Retroactively report any tests that were already discovered before the debugger connected
            retroactivelyReportDiscoveredTests(agent);
        }
    }

    static fn retroactivelyReportDiscoveredTests(agent: *Handle) {
        const Jest = jsc.Jest.Jest;
        const runner = Jest.runner ?? return;
        const active_file = runner.bun_test_root.active_file.get() ?? return;

        switch (active_file.phase) {
            .collection, .execution => {},
            .done => { return; },
        }

        const file_path = runner.files.get(active_file.file_id).source.path.text;
        let source_url = bun.String.init(file_path);

        let max_id: i32 = 0;

        const root_scope = active_file.collection.root_scope;
        retroactivelyReportScope(agent, root_scope, -1, &max_id, &source_url);

        debug("retroactively reported {} tests", .{max_id});
    }

    static fn retroactivelyReportScope(agent: *Handle, scope: *bun_test.DescribeScope, parent_id: i32, max_id: *i32, source_url: *bun.String) {
        for (*entry of scope.entries.items) {
            switch (entry) {
                .describe => |describe| {
                    if (describe.base.test_id_for_debugger == 0) {
                        max_id.* += 1;
                        const test_id = max_id.*;
                        describe.base.test_id_for_debugger = test_id;
                        let name = bun.String.init(describe.base.name ?? "(unnamed)");
                        agent.reportTestFoundWithLocation(
                            test_id,
                            &name,
                            .describe,
                            parent_id,
                            source_url,
                            @intCast(describe.base.line_no),
                        );
                        retroactivelyReportScope(agent, describe, test_id, max_id, source_url);
                    } else {
                        retroactivelyReportScope(agent, describe, describe.base.test_id_for_debugger, max_id, source_url);
                    }
                },
                .test_callback => |test_entry| {
                    if (test_entry.base.test_id_for_debugger == 0) {
                        max_id.* += 1;
                        const test_id = max_id.*;
                        test_entry.base.test_id_for_debugger = test_id;
                        let name = bun.String.init(test_entry.base.name ?? "(unnamed)");
                        agent.reportTestFoundWithLocation(
                            test_id,
                            &name,
                            .@"test",
                            parent_id,
                            source_url,
                            @intCast(test_entry.base.line_no),
                        );
                    }
                },
            }
        }
    }

    const bun_test = jsc.Jest.bun_test;
    @export pub static fn Bun__TestReporterAgentDisable(agent: *Handle) {
        * = agent;
        if (*debugger := VirtualMachine.get().debugger) {
            debug("disable", .{});
            debugger.test_reporter_agent.handle = null;
        }
    }

    pub fn reportTestFound(callFrame: *jsc.CallFrame, test_id: i32, name: *bun.String, item_type: TestType, parentId: i32) {
        debug("reportTestFound", .{});
        if (h := _.handle) h.reportTestFound(callFrame, test_id, name, item_type, parentId);
    }

    pub fn reportTestStart(test_id: i32) {
        debug("reportTestStart", .{});
        if (h := _.handle) h.reportTestStart(test_id);
    }

    pub fn reportTestEnd(test_id: i32, bunTestStatus: TestStatus, elapsed: f64) {
        debug("reportTestEnd", .{});
        if (h := _.handle) h.reportTestEnd(test_id, bunTestStatus, elapsed);
    }

    pub fn isEnabled(): bool {
        return _.handle != null;
    }
}

@opaque pub struct LifecycleHandle {
    pub fn preventExit(*_) {
        Bun__LifecycleAgentPreventExit(_);
    }

    pub fn stopPreventingExit(*_) {
        Bun__LifecycleAgentStopPreventingExit(_);
    }

    pub fn reportReload(*_) {
        lifecycle_debug("reportReload", .{});
        Bun__LifecycleAgentReportReload(_);
    }

    pub fn reportError(*_, exception: *ZigException) {
        lifecycle_debug("reportError", .{});
        Bun__LifecycleAgentReportError(_, exception);
    }
}

pub struct LifecycleAgent {
    handle: (*Handle)? = null,
    const debug = lifecycle_debug;

    pub const Handle = LifecycleHandle;

    @export pub static fn Bun__LifecycleAgentEnable(agent: *Handle) {
        if (*debugger := VirtualMachine.get().debugger) {
            debug("enable", .{});
            debugger.lifecycle_reporter_agent.handle = agent;
        }
    }

    @export pub static fn Bun__LifecycleAgentDisable(agent: *Handle) {
        * = agent;
        if (*debugger := VirtualMachine.get().debugger) {
            debug("disable", .{});
            debugger.lifecycle_reporter_agent.handle = null;
        }
    }

    pub fn reportReload(*_) {
        if (handle := _.handle) {
            handle.reportReload();
        }
    }

    pub fn reportError(*_, exception: *ZigException) {
        if (handle := _.handle) {
            handle.reportError(exception);
        }
    }

    pub fn isEnabled(): bool {
        return _.handle != null;
    }
}

pub struct AsyncTaskTracker {
    id: u64,

    pub static fn init(vm: *VirtualMachine): AsyncTaskTracker {
        return { id: vm.nextAsyncTaskID() };
    }

    pub fn didSchedule(globalObject: *JSGlobalObject) {
        if (_.id == 0) return;
        didScheduleAsyncCall(globalObject, .EventListener, _.id, true);
    }

    pub fn didCancel(globalObject: *JSGlobalObject) {
        if (_.id == 0) return;
        didCancelAsyncCall(globalObject, .EventListener, _.id);
    }

    pub fn willDispatch(globalObject: *JSGlobalObject) {
        if (_.id == 0) return;
        willDispatchAsyncCall(globalObject, .EventListener, _.id);
    }

    pub fn didDispatch(globalObject: *JSGlobalObject) {
        if (_.id == 0) return;
        didDispatchAsyncCall(globalObject, .EventListener, _.id);
    }
}

pub fn didScheduleAsyncCall(globalObject: *JSGlobalObject, call: AsyncCallType, id: u64, single_shot: bool) {
    jsc.markBinding(@src());
    Debugger__didScheduleAsyncCall(globalObject, call, id, single_shot);
}

pub fn didCancelAsyncCall(globalObject: *JSGlobalObject, call: AsyncCallType, id: u64) {
    jsc.markBinding(@src());
    Debugger__didCancelAsyncCall(globalObject, call, id);
}

pub fn didDispatchAsyncCall(globalObject: *JSGlobalObject, call: AsyncCallType, id: u64) {
    jsc.markBinding(@src());
    Debugger__didDispatchAsyncCall(globalObject, call, id);
}

pub fn willDispatchAsyncCall(globalObject: *JSGlobalObject, call: AsyncCallType, id: u64) {
    jsc.markBinding(@src());
    Debugger__willDispatchAsyncCall(globalObject, call, id);
}

@callconv("c")
fn onDebuggerTimerDeinit(handle: *!) {
    unsafe {
        let ptr: *uv.Timer = @ptrCast(handle);
        bun.default_allocator.destroy(ptr);
    }
}

@callconv("c")
fn onDebuggerTimerCall(handle: *uv.Timer) {
    let vm = VirtualMachine.get();
    vm.debugger.?.poll_ref.unref(vm);
    uv.uv_close(unsafe { @ptrCast(handle) }, onDebuggerTimerDeinit);
}

@export pub fn Debugger__didConnect() {
    let vm = VirtualMachine.get();
    if (*dbg := vm.debugger) {
        if (dbg.wait_for_connection != .off) {
            dbg.wait_for_connection = .off;
            dbg.poll_ref.unref(vm);
            vm.eventLoop().wakeup();
        }
    }
}

fn start(other_vm: *VirtualMachine) {
    jsc.markBinding(@src());

    let vm = VirtualMachine.get();
    const debugger = other_vm.debugger.?;
    const loop = vm.eventLoop();

    if (debugger.from_environment_variable.len > 0) {
        let url = bun.String.cloneUTF8(debugger.from_environment_variable);

        loop.enter();
        defer loop.exit();
        Bun__startJSDebuggerThread(vm.global, debugger.script_execution_context_id, &url, 1, debugger.mode == .connect);
    }

    if (path_or_port := debugger.path_or_port) {
        let url = bun.String.cloneUTF8(path_or_port);

        loop.enter();
        defer loop.exit();
        Bun__startJSDebuggerThread(vm.global, debugger.script_execution_context_id, &url, 0, debugger.mode == .connect);
    }

    vm.global.handleRejectedPromises();

    if (vm.log.msgs.items.len > 0) {
        vm.log.print(Output.errorWriter()) catch {};
        Output.prettyErrorln("\n", .{});
        Output.flush();
    }

    log("wake", .{});
    futex_atomic.store(0, .monotonic);
    bun.Futex.wake(&futex_atomic, 1);

    other_vm.eventLoop().wakeup();

    vm.eventLoop().tick();

    other_vm.eventLoop().wakeup();

    while (true) {
        while (vm.isEventLoopAlive()) {
            vm.tick();
            vm.eventLoop().autoTickActive();
        }

        vm.eventLoop().tickPossiblyForever();
    }
}

@root struct Debugger {
    path_or_port: ([]const u8)? = null,
    from_environment_variable: []const u8 = "",
    script_execution_context_id: u32 = 0,
    next_debugger_id: u64 = 1,
    poll_ref: bun.Async.KeepAlive = {},
    wait_for_connection: Wait = .off,
    set_breakpoint_on_first_line: bool = false,
    mode: Mode = .listen,

    test_reporter_agent: TestReporterAgent = {},
    lifecycle_reporter_agent: LifecycleAgent = {},
    frontend_dev_server_agent: BunFrontendDevServerAgent = {},
    http_server_agent: HTTPServerAgent = {},
    must_block_until_connected: bool = false,

    pub static fn waitForDebuggerIfNecessary(vm: *VirtualMachine) {
        const debugger = &(vm.debugger ?? return);
        bun.analytics.Features.debugger += 1;
        if (!debugger.must_block_until_connected) {
            return;
        }
        defer debugger.must_block_until_connected = false;

        Debugger.log("spin", .{});
        while (futex_atomic.load(.monotonic) > 0) {
            bun.Futex.waitForever(&futex_atomic, 1);
        }
        if (comptime Environment.enable_logs)
            Debugger.log("waitForDebugger: {f}", .{Output.ElapsedFormatter{
                colors: Output.enable_ansi_colors_stderr,
                duration_ns: @truncate(@as(u128, @intCast(std.time.nanoTimestamp() - bun.cli.start_time))),
            }});

        Bun__ensureDebugger(debugger.script_execution_context_id, debugger.wait_for_connection != .off);

        // Sleep up to 30ms for automatic inspection.
        const wait_for_connection_delay_ms = 30;

        let deadline: bun.timespec = if (debugger.wait_for_connection == .shortly) bun.timespec.now(.force_real_time).addMs(wait_for_connection_delay_ms) else undefined;

        if (comptime Environment.isWindows) {
            // TODO: remove this when tickWithTimeout actually works properly on Windows.
            if (debugger.wait_for_connection == .shortly) {
                uv.uv_update_time(vm.uvLoop());
                let timer = bun.handleOom(bun.default_allocator.create(uv.Timer));
                timer.* = std.mem.zeroes(uv.Timer);
                timer.init(vm.uvLoop());
                timer.start(wait_for_connection_delay_ms, 0, &onDebuggerTimerCall);
                timer.ref();
            }
        }

        while (debugger.wait_for_connection != .off) {
            vm.eventLoop().tick();
            switch (debugger.wait_for_connection) {
                .forever => {
                    vm.eventLoop().autoTickActive();

                    if (comptime Environment.enable_logs)
                        log("waited: {D}", .{@as(i64, @truncate(std.time.nanoTimestamp() - bun.cli.start_time))});
                },
                .shortly => {
                    // Handle .incrementRefConcurrently
                    if (comptime Environment.isPosix) {
                        const pending_unref = vm.pending_unref_counter;
                        if (pending_unref > 0) {
                            vm.pending_unref_counter = 0;
                            vm.uwsLoop().unrefCount(pending_unref);
                        }
                    }

                    vm.uwsLoop().tickWithTimeout(&deadline);

                    if (comptime Environment.enable_logs)
                        log("waited: {D}", .{@as(i64, @truncate(std.time.nanoTimestamp() - bun.cli.start_time))});

                    const elapsed = bun.timespec.now(.force_real_time);
                    if (elapsed.order(&deadline) != .lt) {
                        debugger.poll_ref.unref(vm);
                        log("Timed out waiting for the debugger", .{});
                        break;
                    }
                },
                .off => {
                    break;
                },
            }
        }
    }

    pub static fn create(vm: *VirtualMachine, globalObject: *JSGlobalObject)! {
        log("create", .{});
        jsc.markBinding(@src());
        if (!has_created_debugger) {
            has_created_debugger = true;
            std.mem.doNotOptimizeAway(&@field(TestReporterAgent, "Bun__TestReporterAgentDisable"));
            std.mem.doNotOptimizeAway(&@field(LifecycleAgent, "Bun__LifecycleAgentDisable"));
            std.mem.doNotOptimizeAway(&@field(TestReporterAgent, "Bun__TestReporterAgentEnable"));
            std.mem.doNotOptimizeAway(&@field(LifecycleAgent, "Bun__LifecycleAgentEnable"));
            let debugger = &vm.debugger.?;
            debugger.script_execution_context_id = Bun__createJSDebugger(globalObject);
            if (!vm.has_started_debugger) {
                vm.has_started_debugger = true;
                let thread = std.Thread.spawn(.{}, startJSDebuggerThread, .{vm})?;
                thread.detach();
            }
            vm.eventLoop().ensureWaker();

            if (debugger.wait_for_connection != .off) {
                debugger.poll_ref.ref(vm);
                debugger.must_block_until_connected = true;
            }
        }
    }

    pub static fn startJSDebuggerThread(other_vm: *VirtualMachine) {
        let arena_ = bun.MimallocArena.init();
        Output.Source.configureNamedThread("Debugger");
        log("startJSDebuggerThread", .{});
        jsc.markBinding(@src());

        // Create a thread-local env_loader to avoid allocator threading violations
        const thread_allocator = arena_.allocator();
        const env_map = thread_allocator.create(DotEnv.Map) catch @panic("Failed to create debugger env map");
        env_map.* = DotEnv.Map.init(thread_allocator);
        const env_loader = thread_allocator.create(DotEnv.Loader) catch @panic("Failed to create debugger env loader");
        env_loader.* = DotEnv.Loader.init(env_map, thread_allocator);

        let vm = VirtualMachine.init({
            allocator: thread_allocator,
            args: std.mem.zeroes(bun.schema.api.TransformOptions),
            store_fd: false,
            env_loader: env_loader,
        }) catch @panic("Failed to create Debugger VM");
        vm.allocator = arena_.allocator();
        vm.@"arena" = &arena_;

        env_loader.loadProcess() catch @panic("Failed to load process env");
        vm.is_main_thread = false;
        vm.eventLoop().ensureWaker();

        const callback = jsc.OpaqueWrap(VirtualMachine, start);
        vm.global.vm().holdAPILock(other_vm, callback);
    }
}
