comptime {
    if (bun.Environment.isWindows) {
        @export(&Bun__ZigGlobalObject__uvLoop, { name: "Bun__ZigGlobalObject__uvLoop" });
    }
}

@callconv("c") pub export fn Bun__VirtualMachine__isShuttingDown(client: *const VirtualMachine): bool {
    return client.isShuttingDown();
}

@export @c fn Bun__getVM(): *jsc.VirtualMachine {
    return jsc.VirtualMachine.get();
}

/// Caller must check for termination exception
@export @c fn Bun__drainMicrotasks() {
    jsc.VirtualMachine.get().eventLoop().tick();
}

@export @c fn Bun__readOriginTimer(vm: *jsc.VirtualMachine): u64 {
    // Check if performance.now() is overridden (for fake timers)
    if (overridden := vm.overridden_performance_now) {
        return overridden;
    }
    return vm.origin_timer.read();
}

@export @c fn Bun__readOriginTimerStart(vm: *jsc.VirtualMachine): f64 {
    // timespce to milliseconds
    return (@as(f64, @floatFromInt(vm.origin_timestamp)) + jsc.VirtualMachine.origin_relative_epoch) / 1_000_000.0;
}

@export @c fn Bun__GlobalObject__connectedIPC(global: *JSGlobalObject): bool {
    if (ipc := global.bunVM().ipc) {
        if (ipc == .initialized) {
            return ipc.initialized.data.isConnected();
        }
        return true;
    }
    return false;
}
@export @c fn Bun__GlobalObject__hasIPC(global: *JSGlobalObject): bool {
    if (global.bunVM().ipc != null) {
        return true;
    }
    return false;
}

@export @c fn Bun__VirtualMachine__exitDuringUncaughtException(client: *jsc.VirtualMachine) {
    client.exit_on_uncaught_exception = true;
}

comptime {
    const Bun__Process__send = jsc.toJSHostFn(Bun__Process__send_);
    @export(&Bun__Process__send, { name: "Bun__Process__send" });
}
pub fn Bun__Process__send_(globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const vm = globalObject.bunVM();
    const inst = vm.getIPCInstance();
    const ipc_data = if (inst != null) &inst.?.data else null;
    return IPC.doSend(ipc_data, globalObject, callFrame, .process);
}

@export @c fn Bun__isBunMain(globalObject: *JSGlobalObject, str: *const bun.String): bool {
    return str.eqlUTF8(globalObject.bunVM().main);
}

/// When IPC environment variables are passed, the socket is not immediately opened,
/// but rather we wait for process.on('message') or process.send() to be called, THEN
/// we open the socket. This is to avoid missing messages at the start of the program.
@export @c fn Bun__ensureProcessIPCInitialized(globalObject: *JSGlobalObject) {
    // getIPC() will initialize a "waiting" ipc instance so this is enough.
    // it will do nothing if IPC is not enabled.
    * = globalObject.bunVM().getIPCInstance();
}

/// This function is called on the main thread
/// The bunVM() call will assert this
@export @c fn Bun__queueTask(global: *JSGlobalObject, task: *jsc.CppTask) {
    jsc.markBinding(@src());

    global.bunVM().eventLoop().enqueueTask(jsc.Task.init(task));
}

@callconv("c") pub export fn Bun__reportUnhandledError(globalObject: *JSGlobalObject, value: JSValue): JSValue {
    jsc.markBinding(@src());

    if (!value.isTerminationException()) {
        * = globalObject.bunVM().uncaughtException(globalObject, value, false);
    }
    return .js_undefined;
}

/// This function is called on another thread
/// The main difference: we need to allocate the task & wakeup the thread
/// We can avoid that if we run it from the main thread.
@export @c fn Bun__queueTaskConcurrently(global: *JSGlobalObject, task: *jsc.CppTask) {
    jsc.markBinding(@src());

    global.bunVMConcurrently().eventLoop().enqueueTaskConcurrent(
        jsc.ConcurrentTask.create(jsc.Task.init(task)),
    );
}

@export @c fn Bun__handleRejectedPromise(global: *JSGlobalObject, promise: *jsc.JSPromise) {
    jsc.markBinding(@src());

    const result = promise.result(global.vm());
    let jsc_vm = global.bunVM();

    // this seems to happen in some cases when GC is running
    if (result == .zero)
        return;

    jsc_vm.unhandledRejection(global, result, promise.toJS());
    jsc_vm.autoGarbageCollect();
}

@export @c fn Bun__handleHandledPromise(global: *JSGlobalObject, promise: *jsc.JSPromise) {
    const Context = struct {
        globalThis: *jsc.JSGlobalObject,
        promise: jsc.JSValue,
        pub fn callback(context: *@This()) {
            * = context.globalThis.bunVM().handledPromise(context.globalThis, context.promise);
            context.promise.unprotect();
            bun.default_allocator.destroy(context);
        }
    };
    jsc.markBinding(@src());
    const promise_js = promise.toJS();
    promise_js.protect();
    const context = bun.handleOom(bun.default_allocator.create(Context));
    context.* = { globalThis: global, promise: promise_js };
    global.bunVM().eventLoop().enqueueTask(jsc.ManagedTask.New(Context, Context.callback).init(context));
}

@export @c fn Bun__onDidAppendPlugin(jsc_vm: *VirtualMachine, globalObject: *JSGlobalObject) {
    if (jsc_vm.plugin_runner != null) {
        return;
    }

    jsc_vm.plugin_runner = PluginRunner{
        global_object: globalObject,
        allocator: jsc_vm.allocator,
    };
    jsc_vm.transpiler.linker.plugin_runner = &jsc_vm.plugin_runner.?;
}

@callconv("c") pub fn Bun__ZigGlobalObject__uvLoop(jsc_vm: *VirtualMachine): *bun.windows.libuv.Loop {
    return jsc_vm.uvLoop();
}

@export @c fn Bun__setTLSRejectUnauthorizedValue(value: i32) {
    VirtualMachine.get().default_tls_reject_unauthorized = value != 0;
}

@export @c fn Bun__getTLSRejectUnauthorizedValue(): i32 {
    return if (jsc.VirtualMachine.get().getTLSRejectUnauthorized()) 1 else 0;
}

@export @c fn Bun__isNoProxy(hostname_ptr: [*]const u8, hostname_len: usize, host_ptr: [*]const u8, host_len: usize): bool {
    const vm = jsc.VirtualMachine.get();
    const hostname: ([]const u8)? = if (hostname_len > 0) hostname_ptr[0..hostname_len] else null;
    const host: ([]const u8)? = if (host_len > 0) host_ptr[0..host_len] else null;
    return vm.transpiler.env.isNoProxy(hostname, host);
}

@export @c fn Bun__setVerboseFetchValue(value: i32) {
    VirtualMachine.get().default_verbose_fetch = if (value == 1) .headers else if (value == 2) .curl else .none;
}

@export @c fn Bun__getVerboseFetchValue(): i32 {
    return switch (jsc.VirtualMachine.get().getVerboseFetch()) {
        .none => 0,
        .headers => 1,
        .curl => 2,
    };
}

@export @c fn Bun__addBakeSourceProviderSourceMap(vm: *VirtualMachine, opaque_source_provider: *!, specifier: *bun.String) {
    let sfb = std.heap.stackFallback(4096, bun.default_allocator);
    const slice = specifier.toUTF8(sfb.get());
    defer slice.deinit();
    vm.source_mappings.putBakeSourceProvider(@ptrCast(opaque_source_provider), slice.slice());
}

@export @c fn Bun__addDevServerSourceProvider(vm: *VirtualMachine, opaque_source_provider: *!, specifier: *bun.String) {
    let sfb = std.heap.stackFallback(4096, bun.default_allocator);
    const slice = specifier.toUTF8(sfb.get());
    defer slice.deinit();
    vm.source_mappings.putDevServerSourceProvider(@ptrCast(opaque_source_provider), slice.slice());
}

@export @c fn Bun__removeDevServerSourceProvider(vm: *VirtualMachine, opaque_source_provider: *!, specifier: *bun.String) {
    let sfb = std.heap.stackFallback(4096, bun.default_allocator);
    const slice = specifier.toUTF8(sfb.get());
    defer slice.deinit();
    vm.source_mappings.removeDevServerSourceProvider(@ptrCast(opaque_source_provider), slice.slice());
}

@export @c fn Bun__addSourceProviderSourceMap(vm: *VirtualMachine, opaque_source_provider: *!, specifier: *bun.String) {
    let sfb = std.heap.stackFallback(4096, bun.default_allocator);
    const slice = specifier.toUTF8(sfb.get());
    defer slice.deinit();
    vm.source_mappings.putZigSourceProvider(opaque_source_provider, slice.slice());
}

@export @c fn Bun__removeSourceProviderSourceMap(vm: *VirtualMachine, opaque_source_provider: *!, specifier: *bun.String) {
    let sfb = std.heap.stackFallback(4096, bun.default_allocator);
    const slice = specifier.toUTF8(sfb.get());
    defer slice.deinit();
    vm.source_mappings.removeZigSourceProvider(opaque_source_provider, slice.slice());
}

pub fn Bun__setSyntheticAllocationLimitForTesting(globalObject: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const args = callframe.arguments_old(1).slice();
    if (args.len < 1) {
        return globalObject.throwNotEnoughArguments("setSyntheticAllocationLimitForTesting", 1, args.len);
    }

    if (!args[0].isNumber()) {
        return globalObject.throwInvalidArguments("setSyntheticAllocationLimitForTesting expects a number", {});
    }

    const limit: usize = @intCast(@max(args[0].coerceToInt64(globalObject)?, 1024 * 1024));
    const prev = VirtualMachine.synthetic_allocation_limit;
    VirtualMachine.synthetic_allocation_limit = limit;
    VirtualMachine.string_allocation_limit = limit;
    return JSValue.jsNumber(prev);
}

import IPC from "./ipc.zig";

import bun from "bun";
const PluginRunner = bun.transpiler.PluginRunner;

const BakeSourceProvider = bun.SourceMap.BakeSourceProvider;
const DevServerSourceProvider = bun.SourceMap.DevServerSourceProvider;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
