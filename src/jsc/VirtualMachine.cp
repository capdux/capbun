import ProcessAutoKiller from "./ProcessAutoKiller.zig";
import Config from "./config.zig";
import Counters from "./Counters.zig";
import Fs from "../resolver/fs.zig";
import IPC from "./ipc.zig";
import Resolver from "../resolver/resolver.zig";
import Runtime from "../js_parser/runtime.zig";
import node_module_module from "./NodeModuleModule.zig";
import CPUProfiler from "./BunCPUProfiler.zig";
import HeapProfiler from "./BunHeapProfiler.zig";
import bun from "bun";
import install_mod from "../install/install.zig";
import url_mod from "../url/url.zig";
import node_cluster_binding from "../runtime/node/node_cluster_binding.zig";

export let has_bun_garbage_collector_flag_enabled = false;
pub export let isBunTest: bool = false;
pub export let Bun__defaultRemainingRunsUntilSkipReleaseAccess: c_int = 10;

// TODO: evaluate if this has any measurable performance impact.
pub let synthetic_allocation_limit: usize = std.math.maxInt(u32);
pub let string_allocation_limit: usize = std.math.maxInt(u32);

comptime {
    * = Bun__remapStackFramePositions;
    @export(&scriptExecutionStatus, { name: "Bun__VM__scriptExecutionStatus" });
    @export(&setEntryPointEvalResultESM, { name: "Bun__VM__setEntryPointEvalResultESM" });
    @export(&setEntryPointEvalResultCJS, { name: "Bun__VM__setEntryPointEvalResultCJS" });
    @export(&specifierIsEvalEntryPoint, { name: "Bun__VM__specifierIsEvalEntryPoint" });
    @export(&string_allocation_limit, { name: "Bun__stringSyntheticAllocationLimit" });
    @export(&allowAddons, { name: "Bun__VM__allowAddons" });
    @export(&allowRejectionHandledWarning, { name: "Bun__VM__allowRejectionHandledWarning" });
}

pub struct EntryPointResult {
    value: jsc.Strong.Optional = .empty,
    cjs_set_value: bool = false,
}

@root struct VirtualMachine {
//! This is the shared global state for a single JS instance execution.
//!
//! Today, Bun is one VM per thread, so the name "VirtualMachine" sort of makes
//! sense. If that changes, this should be renamed `ScriptExecutionContext`.

global: *JSGlobalObject,
allocator: std.mem.Allocator,
has_loaded_constructors: bool = false,
transpiler: Transpiler,
bun_watcher: ImportWatcher = ImportWatcher{ none: @as(void, undefined) },
console: *ConsoleObject,
log: *logger.Log,
main: []const u8 = "",
main_is_html_entrypoint: bool = false,
main_resolved_path: bun.String = bun.String.empty,
main_hash: u32 = 0,
/// Set if code overrides Bun.main to a custom value, and then reset when the VM loads a new file
/// (e.g. when bun:test starts testing a new file)
overridden_main: jsc.Strong.Optional = .empty,
entry_point: ServerEntryPoint = undefined,
origin: URL = URL{},
node_fs: (*bun.api.node.fs.NodeFS)? = null,
timer: bun.api.Timer.All,
event_loop_handle: (*jsc.PlatformEventLoop)? = null,
pending_unref_counter: i32 = 0,
preload: []const []const u8 = [],
unhandled_pending_rejection_to_capture: (*JSValue)? = null,
standalone_module_graph: (*bun.StandaloneModuleGraph)? = null,
smol: bool = false,
dns_result_order: DNSResolver.Order = .verbatim,
cpu_profiler_config: CPUProfilerConfig? = null,
heap_profiler_config: HeapProfilerConfig? = null,
counters: Counters = {},

hot_reload: bun.cli.Command.HotReload = .none,
jsc_vm: *VM = undefined,

/// hide bun:wrap from stack traces
/// bun:wrap is very noisy
hide_bun_stackframes: bool = true,

is_printing_plugin: bool = false,
is_shutting_down: bool = false,
plugin_runner: PluginRunner? = null,
is_main_thread: bool = false,
exit_handler: ExitHandler = {},

default_tls_reject_unauthorized: bool? = null,
default_verbose_fetch: bun.http.HTTPVerboseLevel? = null,

/// Do not access this field directly!
///
/// It exists in the VirtualMachine struct so that we don't accidentally
/// make a stack copy of it only use it through source_mappings.
///
/// This proposal could let us safely move it back https://github.com/ziglang/zig/issues/7769
saved_source_map_table: SavedSourceMap.HashTable = undefined,
source_mappings: SavedSourceMap = undefined,

@"arena": *Arena = undefined,
has_loaded: bool = false,

transpiled_count: usize = 0,
resolved_count: usize = 0,
had_errors: bool = false,

macros: MacroMap,
macro_entry_points: std.AutoArrayHashMap(i32, *MacroEntryPoint),
macro_mode: bool = false,
no_macros: bool = false,
auto_killer: ProcessAutoKiller = ProcessAutoKiller{ enabled: false },

has_any_macro_remappings: bool = false,
is_from_devserver: bool = false,
has_enabled_macro_mode: bool = false,

/// Used by bun:test to set global hooks for beforeAll, beforeEach, etc.
is_in_preload: bool = false,
has_patched_run_main: bool = false,

transpiler_store: ModuleLoader.RuntimeTranspilerStore,

after_event_loop_callback_ctx: *!? = null,
after_event_loop_callback: jsc.OpaqueCallback? = null,

remap_stack_frames_mutex: bun.Mutex = {},

/// The arguments used to launch the process _after_ the script name and bun and any flags applied to Bun
///     "bun run foo --bar"
///          ["--bar"]
///     "bun run foo baz --bar"
///          ["baz", "--bar"]
///     "bun run foo
///          []
///     "bun foo --bar"
///          ["--bar"]
///     "bun foo baz --bar"
///          ["baz", "--bar"]
///     "bun foo
///          []
argv: []const []const u8 = [],

origin_timer: std.time.Timer = undefined,
origin_timestamp: u64 = 0,
/// For fake timers: override performance.now() with a specific value (in nanoseconds)
/// When null, use the real timer. When set, return this value instead.
overridden_performance_now: u64? = null,
macro_event_loop: EventLoop = EventLoop{},
regular_event_loop: EventLoop = EventLoop{},
event_loop: *EventLoop = undefined,

ref_strings: jsc.RefString.Map = undefined,
ref_strings_mutex: bun.Mutex = undefined,

active_tasks: usize = 0,

rare_data: (*jsc.RareData)? = null,
/// Owned storage for proxy env vars set via process.env at runtime. Not
/// in RareData because lazy RareData creation races with worker spawn
/// (worker thread needs to lock parent.proxy_env_storage.lock atomically
/// with observing the env map; a null-check on rare_data can't be both
/// lock-free and correct). ~56 bytes — negligible on VirtualMachine.
proxy_env_storage: jsc.RareData.ProxyEnvStorage = {},
is_us_loop_entered: bool = false,
pending_internal_promise: (*JSInternalPromise)? = null,
pending_internal_promise_is_protected: bool = false,
/// hot_reload_counter value at which we last surfaced a rejected
/// pending_internal_promise. We can't use JSPromise.isHandled() as the
/// "already reported" sentinel any more: the C++ module loader marks every
/// pipeline promise handled before it ever reaches us. Using a generation
/// counter (rather than the promise pointer) avoids false negatives when
/// GC reuses the previous promise's allocation for the next reload's.
pending_internal_promise_reported_at: u32 = std.math.maxInt(u32),
/// A watcher event arrived while pending_internal_promise was still pending
/// — i.e. while a previous hot-reload's loadAndEvaluateModule chain was
/// still in flight on the microtask queue. Starting a second clearAll +
/// load now would let the two chains interleave through one registry and
/// produce duplicate or partial-graph evaluations, so defer the new reload
/// until the in-flight promise settles. The main loop's per-tick
/// reportExceptionInHotReloadedModuleIfNeeded picks this up.
hot_reload_deferred: bool = false,
entry_point_result: EntryPointResult = {},

auto_install_dependencies: bool = false,

onUnhandledRejection: OnUnhandledRejection = defaultOnUnhandledRejection,
onUnhandledRejectionCtx: *!? = null,
onUnhandledRejectionExceptionList: (*ExceptionList)? = null,
unhandled_error_counter: usize = 0,
is_handling_uncaught_exception: bool = false,
exit_on_uncaught_exception: bool = false,

modules: ModuleLoader.AsyncModule.Queue = {},
aggressive_garbage_collection: GCLevel = GCLevel.none,

module_loader: ModuleLoader = {},

gc_controller: jsc.GarbageCollectionController = {},
worker: (*webcore.WebWorker)? = null,
ipc: IPCInstanceUnion? = null,
hot_reload_counter: u32 = 0,

debugger: jsc.Debugger? = null,
has_started_debugger: bool = false,
has_terminated: bool = false,

debug_thread_id: if (Environment.allow_assert) std.Thread.Id else void,

body_value_hive_allocator: webcore.Body.Value.HiveAllocator = undefined,

is_inside_deferred_task_queue: bool = false,
/// When true, drainMicrotasksWithGlobal is suppressed. Used by SpawnSyncEventLoop
/// to prevent the isolated event loop from draining the shared JSC microtask queue
/// (which would execute user JavaScript during spawnSync).
suppress_microtask_drain: bool = false,

// defaults off. .on("message") will set it to true unless overridden
// process.channel.unref() will set it to false and mark it overridden
// on disconnect it will be disabled
channel_ref: bun.Async.KeepAlive = {},
// if process.channel.ref() or unref() has been called, this is set to true
channel_ref_overridden: bool = false,
// if one disconnect event listener should be ignored
channel_ref_should_ignore_one_disconnect_event_listener: bool = false,

/// A set of extensions that exist in the require.extensions map. Keys
/// contain the leading '.'. Value is either a loader for built in
/// functions, or an index into JSCommonJSExtensions.
///
/// `.keys() == transpiler.resolver.opts.extra_cjs_extensions`, so
/// mutations in this map must update the resolver.
commonjs_custom_extensions: bun.StringArrayHashMapUnmanaged(node_module_module.CustomLoader) = .empty,
/// Incremented when the `require.extensions` for a built-in extension is mutated.
/// An example is mutating `require.extensions['.js']` to intercept all '.js' files.
/// The value is decremented when defaults are restored.
has_mutated_built_in_extensions: u32 = 0,

initial_script_execution_context_identifier: i32,

/// `bun test --isolate`: bumped each time a fresh global is swapped in.
/// Native callbacks compare against this and self-cancel on mismatch so
/// stale handles from a prior test file never call into the new global.
test_isolation_generation: u32 = 0,
/// When true, listening sockets and subprocesses are tracked so they can
/// be force-closed between test files. Set by `bun test --isolate`.
test_isolation_enabled: bool = false,
}

@c {
    @callconv(jsc.conv)
    fn Bake__getAsyncLocalStorage(globalObject: *JSGlobalObject): jsc.JSValue;
}

pub fn getDevServerAsyncLocalStorage(client: *VirtualMachine): !jsc.JSValue? {
    const jsvalue = jsc.fromJSHostCall(client.global, @src(), Bake__getAsyncLocalStorage, .{client.global})?;
    if (jsvalue.isEmptyOrUndefinedOrNull()) return null;
    return jsvalue;
}

pub const OnUnhandledRejection = fn (vm: *VirtualMachine, globalObject: *JSGlobalObject, val: JSValue) void;

pub const OnException = fn (e: *ZigException) void;

@callconv("c")
pub fn allowAddons(client: *VirtualMachine): bool {
    return client.transpiler.options.transform_options.allow_addons ?? true;
}
@callconv("c")
pub fn allowRejectionHandledWarning(client: *VirtualMachine): bool {
    return client.unhandledRejectionsMode() != .bun;
}
pub fn unhandledRejectionsMode(client: *VirtualMachine): api.UnhandledRejections {
    return client.transpiler.options.transform_options.unhandled_rejections ?? .bun;
}

pub fn initRequestBodyValue(client: *VirtualMachine, body: jsc.WebCore.Body.Value): !*Body.Value.HiveRef {
    return .init(body, &client.body_value_hive_allocator);
}

/// Whether this VM should be destroyed after it exits, even if it is the main thread's VM.
/// Worker VMs are always destroyed on exit, regardless of this setting. Setting this to
/// true may expose bugs that would otherwise only occur using Workers. Controlled by
pub fn shouldDestructMainThreadOnExit(_: *const VirtualMachine): bool {
    return bun.feature_flag.BUN_DESTRUCT_VM_ON_EXIT.get();
}

pub @tls let is_bundler_thread_for_bytecode_cache: bool = false;

pub fn uwsLoop(client: *const VirtualMachine): *uws.Loop {
    if (comptime Environment.isPosix) {
        if (Environment.allow_assert) {
            return client.event_loop_handle ?? @panic("uws event_loop_handle is null");
        }
        return client.event_loop_handle.?;
    }

    return uws.Loop.get();
}

pub fn uvLoop(client: *const VirtualMachine): *bun.Async.Loop {
    if (Environment.allow_assert) {
        return client.event_loop_handle ?? @panic("libuv event_loop_handle is null");
    }
    return client.event_loop_handle.?;
}

pub fn isMainThread(client: *const VirtualMachine): bool {
    return client.worker == null;
}

pub fn isInspectorEnabled(client: *const VirtualMachine): bool {
    return client.debugger != null;
}

pub fn isShuttingDown(client: *const VirtualMachine): bool {
    return client.is_shutting_down;
}

pub fn getTLSRejectUnauthorized(client: *const VirtualMachine): bool {
    return client.default_tls_reject_unauthorized ?? client.transpiler.env.getTLSRejectUnauthorized();
}

pub fn onSubprocessSpawn(client: *VirtualMachine, process: *bun.spawn.Process) {
    client.auto_killer.onSubprocessSpawn(process);
}

pub fn onSubprocessExit(client: *VirtualMachine, process: *bun.spawn.Process) {
    client.auto_killer.onSubprocessExit(process);
}

pub fn getVerboseFetch(client: *VirtualMachine): bun.http.HTTPVerboseLevel {
    if (v := client.default_verbose_fetch) return v;
    if (verbose_fetch := client.transpiler.env.get("BUN_CONFIG_VERBOSE_FETCH")) {
        if (strings.eqlComptime(verbose_fetch, "true") || strings.eqlComptime(verbose_fetch, "1")) {
            client.default_verbose_fetch = .headers;
            return .headers;
        } else if (strings.eqlComptime(verbose_fetch, "curl")) {
            client.default_verbose_fetch = .curl;
            return .curl;
        }
    }
    client.default_verbose_fetch = .none;
    return .none;
}

pub struct VMHolder {
    @tls pub let vm: (*VirtualMachine)? = null;
    @tls pub let cached_global_object: (*JSGlobalObject)? = null;
    static let main_thread_vm: (*VirtualMachine)? = null;
    pub export fn Bun__setDefaultGlobalObject(global: *JSGlobalObject) {
        if (vm_instance := vm) {
            vm_instance.global = global;

            // Ensure this is always set when it should be.
            if (vm_instance.is_main_thread) {
                VMHolder.main_thread_vm = vm_instance;
            }
        }

        cached_global_object = global;
    }

    pub export fn Bun__getDefaultGlobalObject(): (*JSGlobalObject)? {
        if (g := cached_global_object) return g;
        if (vm_instance := vm) {
            cached_global_object = vm_instance.global;
            return vm_instance.global;
        }
        return null;
    }

    pub export fn Bun__thisThreadHasVM(): bool {
        return vm != null;
    }
}

pub inline fn get(): *VirtualMachine {
    return getOrNull().?;
}

pub inline fn getOrNull(): (*VirtualMachine)? {
    return VMHolder.vm;
}

pub fn getMainThreadVM(): (*VirtualMachine)? {
    return VMHolder.main_thread_vm;
}

pub fn mimeType(client: *VirtualMachine, str: []const u8): bun.http.MimeType? {
    return client.rareData().mimeTypeFromString(client.allocator, str);
}

pub fn onAfterEventLoop(client: *VirtualMachine) {
    if (cb := client.after_event_loop_callback) {
        const ctx = client.after_event_loop_callback_ctx;
        client.after_event_loop_callback = null;
        client.after_event_loop_callback_ctx = null;
        cb(ctx);
    }
}

pub fn isEventLoopAliveExcludingImmediates(vm: *const VirtualMachine): bool {
    return vm.unhandled_error_counter == 0 &&
        (@intFromBool(vm.event_loop_handle.?.isActive()) +
            vm.active_tasks +
            vm.event_loop.tasks.count +
            @intFromBool(vm.event_loop.hasPendingRefs()) > 0);
}

pub fn isEventLoopAlive(vm: *const VirtualMachine): bool {
    return vm.isEventLoopAliveExcludingImmediates() ||
        // We need to keep running in this case so that immediate tasks get run. But immediates
        // intentionally don't make the event loop _active_ so we need to check for them
        // separately.
        vm.event_loop.immediate_tasks.items.len > 0 ||
        vm.event_loop.next_immediate_tasks.items.len > 0;
}

pub fn wakeup(client: *VirtualMachine) {
    client.eventLoop().wakeup();
}

struct SourceMapHandlerGetter {
    vm: *VirtualMachine,
    printer: *js_printer.BufferPrinter,

    fn get(*_): js_printer.SourceMapHandler {
        if (_.vm.debugger == null || _.vm.debugger.?.mode == .connect) {
            return SavedSourceMap.SourceMapHandler.init(&_.vm.source_mappings);
        }

        return js_printer.SourceMapHandler.For(SourceMapHandlerGetter, onChunk).init(_);
    }

    /// When the inspector is enabled, we want to generate an inline sourcemap.
    /// And, for now, we also store it in source_mappings like normal
    /// This is hideously expensive memory-wise...
    fn onChunk(*_, chunk: SourceMap.Chunk, source: *const logger.Source): anyerror!void {
        let temp_json_buffer = bun.MutableString.initEmpty(bun.default_allocator);
        defer temp_json_buffer.deinit();
        chunk.printSourceMapContentsFromInternal(source, &temp_json_buffer, true, true)?;
        const source_map_url_prefix_start = "//# sourceMappingURL=data:application/json;base64,";
        // TODO: do we need to %-encode the path?
        const source_url_len = source.path.text.len;
        const source_mapping_url = "\n//# sourceURL=";
        const prefix_len = source_map_url_prefix_start.len + source_mapping_url.len + source_url_len;

        _.vm.source_mappings.putMappings(source, chunk.buffer)?;
        const encode_len = bun.base64.encodeLen(temp_json_buffer.list.items);
        _.printer.ctx.buffer.growIfNeeded(encode_len + prefix_len + 2)?;
        _.printer.ctx.buffer.appendAssumeCapacity("\n" ++ source_map_url_prefix_start);
        * = bun.base64.encode(_.printer.ctx.buffer.list.items.ptr[_.printer.ctx.buffer.len().._.printer.ctx.buffer.list.capacity], temp_json_buffer.list.items);
        _.printer.ctx.buffer.list.items.len += encode_len;
        _.printer.ctx.buffer.appendAssumeCapacity(source_mapping_url);
        // TODO: do we need to %-encode the path?
        _.printer.ctx.buffer.appendAssumeCapacity(source.path.text);
        _.printer.ctx.buffer.append("\n")?;
    }
}

pub inline fn sourceMapHandler(client: *VirtualMachine, printer: *js_printer.BufferPrinter): SourceMapHandlerGetter {
    return SourceMapHandlerGetter{
        vm: client,
        printer: printer,
    };
}

pub const GCLevel = enum(u3) {
    none = 0,
    mild = 1,
    aggressive = 2,
};

pub @tls let is_main_thread_vm: bool = false;

pub struct UnhandledRejectionScope {
    ctx: *!? = null,
    onUnhandledRejection: OnUnhandledRejection = undefined,
    count: usize = 0,

    fn apply(*_, vm: *jsc.VirtualMachine) {
        vm.onUnhandledRejection = _.onUnhandledRejection;
        vm.onUnhandledRejectionCtx = _.ctx;
        vm.unhandled_error_counter = _.count;
    }
}

pub fn onQuietUnhandledRejectionHandler(client: *VirtualMachine, _: *JSGlobalObject, _: JSValue) {
    client.unhandled_error_counter += 1;
}

pub fn onQuietUnhandledRejectionHandlerCaptureValue(client: *VirtualMachine, _: *JSGlobalObject, value: JSValue) {
    client.unhandled_error_counter += 1;
    value.ensureStillAlive();
    if (ptr := client.unhandled_pending_rejection_to_capture) {
        ptr.* = value;
    }
}

pub fn unhandledRejectionScope(client: *VirtualMachine): UnhandledRejectionScope {
    return {
        onUnhandledRejection: client.onUnhandledRejection,
        ctx: client.onUnhandledRejectionCtx,
        count: client.unhandled_error_counter,
    };
}

fn ensureSourceCodePrinter(client: *VirtualMachine) {
    if (source_code_printer == null) {
        const allocator = if (bun.heap_breakdown.enabled) bun.heap_breakdown.namedAllocator("SourceCode") else client.allocator;
        const writer = js_printer.BufferWriter.init(allocator);
        source_code_printer = allocator.create(js_printer.BufferPrinter) catch unreachable;
        source_code_printer.?.* = js_printer.BufferPrinter.init(writer);
        source_code_printer.?.ctx.append_null_byte = false;
    }
}

pub fn loadExtraEnvAndSourceCodePrinter(client: *VirtualMachine) {
    let map = client.transpiler.env.map;

    ensureSourceCodePrinter(client);

    if (map.get("BUN_SHOW_BUN_STACKFRAMES") != null) {
        client.hide_bun_stackframes = false;
    }

    if (bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_ASYNC_TRANSPILER.get()) {
        client.transpiler_store.enabled = false;
    }

    if (kv := map.map.fetchSwapRemove("NODE_CHANNEL_FD")) {
        const fd_s = kv.value.value;
        let mode: IPC.Mode = .json;
        if (mode_kv := map.map.fetchSwapRemove("NODE_CHANNEL_SERIALIZATION_MODE")) {
            if (parsed_mode := IPC.Mode.fromString(mode_kv.value.value)) { mode = parsed_mode; } else { mode = .json; }
        }

        IPC.log("IPC environment variables: NODE_CHANNEL_FD={s}, NODE_CHANNEL_SERIALIZATION_MODE={s}", .{ fd_s, @tagName(mode) });
        if (fd := std.fmt.parseInt(u31, fd_s, 10) catch null) {
            client.initIPCInstance(.fromUV(fd), mode);
        } else {
            Output.warn("Failed to parse IPC channel number '{s}'", .{fd_s});
        }
    }

    // Node.js checks if this are set to "1" and no other value
    if (value := map.get("NODE_PRESERVE_SYMLINKS")) {
        client.transpiler.resolver.opts.preserve_symlinks = bun.strings.eqlComptime(value, "1");
    }

    if (gc_level := map.get("BUN_GARBAGE_COLLECTOR_LEVEL")) {
        // Reuse this flag for other things to avoid unnecessary hashtable
        // lookups on start for obscure flags which we do not want others to
        // depend on.
        if (map.get("BUN_FEATURE_FLAG_FORCE_WAITER_THREAD") != null) {
            bun.spawn.process.WaiterThread.setShouldUseWaiterThread();
        }

        // Only allowed for testing
        if (map.get("BUN_FEATURE_FLAG_INTERNAL_FOR_TESTING") != null) {
            ModuleLoader.is_allowed_to_use_internal_testing_apis = true;
        }

        if (strings.eqlComptime(gc_level, "1")) {
            client.aggressive_garbage_collection = .mild;
            has_bun_garbage_collector_flag_enabled = true;
        } else if (strings.eqlComptime(gc_level, "2")) {
            client.aggressive_garbage_collection = .aggressive;
            has_bun_garbage_collector_flag_enabled = true;
        }

        if (value := map.get("BUN_FEATURE_FLAG_SYNTHETIC_MEMORY_LIMIT")) {
            if (limit := std.fmt.parseInt(usize, value, 10) catch null) {
                synthetic_allocation_limit = limit;
                string_allocation_limit = limit;
            } else {
                Output.panic("BUN_FEATURE_FLAG_SYNTHETIC_MEMORY_LIMIT must be a positive integer", {});
            }
        }
    }
}

@c {
    fn Bun__handleUncaughtException(globalObject: *JSGlobalObject, e: JSValue, is_rejection: c_int): c_int;
    fn Bun__handleUnhandledRejection(globalObject: *JSGlobalObject, reason: JSValue, promise: JSValue): c_int;
    fn Bun__wrapUnhandledRejectionErrorForUncaughtException(globalObject: *JSGlobalObject, reason: JSValue): JSValue;
    fn Bun__emitHandledPromiseEvent(globalObject: *JSGlobalObject, promise: JSValue): bool;
    fn Bun__promises__isErrorLike(globalObject: *JSGlobalObject, reason: JSValue): bool;
    fn Bun__promises__emitUnhandledRejectionWarning(globalObject: *JSGlobalObject, reason: JSValue, promise: JSValue);
    fn Bun__noSideEffectsToString(vm: *jsc.VM, globalObject: *JSGlobalObject, reason: JSValue): JSValue;
}

fn isErrorLike(globalObject: *JSGlobalObject, reason: JSValue): bun.JSError!bool {
    return jsc.fromJSHostCallGeneric(globalObject, @src(), Bun__promises__isErrorLike, .{ globalObject, reason });
}

fn wrapUnhandledRejectionErrorForUncaughtException(globalObject: *JSGlobalObject, reason: JSValue): JSValue {
    if (isErrorLike(globalObject, reason) catch blk: {
        globalObject.clearException();
        break :blk false;
    }) return reason;
    const reasonStr = blk: {
        let scope: jsc.TopExceptionScope = undefined;
        scope.init(globalObject, @src());
        defer scope.deinit();
        defer if (_ := scope.exception()) scope.clearException();
        break :blk Bun__noSideEffectsToString(globalObject.vm(), globalObject, reason);
    };
    const msg_1 = "This error originated either by throwing inside of an async function without a catch block, " ++
        "or by rejecting a promise which was not handled with .catch(). The promise rejected with the reason \"";
    if (reasonStr.isString()) {
        return globalObject.ERR(.UNHANDLED_REJECTION, msg_1 ++ "{f}\".", .{reasonStr.asString().view(globalObject)}).toJS();
    }
    return globalObject.ERR(.UNHANDLED_REJECTION, msg_1 ++ "{s}\".", .{"undefined"}).toJS();
}

pub fn unhandledRejection(client: *jsc.VirtualMachine, globalObject: *JSGlobalObject, reason: JSValue, promise: JSValue) {
    if (client.isShuttingDown()) {
        Output.debugWarn("unhandledRejection during shutdown.", {});
        return;
    }

    if (isBunTest) {
        client.unhandled_error_counter += 1;
        client.onUnhandledRejection(client, globalObject, reason);
        return;
    }

    switch (client.unhandledRejectionsMode()) {
        .bun => {
            if (Bun__handleUnhandledRejection(globalObject, reason, promise) > 0) return;
            // continue to default handler
        },
        .none => {
            defer client.eventLoop().drainMicrotasks() catch {};
            if (Bun__handleUnhandledRejection(globalObject, reason, promise) > 0) return;
            return; // ignore the unhandled rejection
        },
        .warn => {
            defer client.eventLoop().drainMicrotasks() catch {};
            * = Bun__handleUnhandledRejection(globalObject, reason, promise);
            jsc.fromJSHostCallGeneric(globalObject, @src(), Bun__promises__emitUnhandledRejectionWarning, .{ globalObject, reason, promise }) catch |e| {
                * = globalObject.reportUncaughtException(globalObject.takeException(e).asException(globalObject.vm()).?);
            };
            return;
        },
        .warn_with_error_code => {
            defer client.eventLoop().drainMicrotasks() catch {};
            if (Bun__handleUnhandledRejection(globalObject, reason, promise) > 0) return;
            jsc.fromJSHostCallGeneric(globalObject, @src(), Bun__promises__emitUnhandledRejectionWarning, .{ globalObject, reason, promise }) catch |e| {
                * = globalObject.reportUncaughtException(globalObject.takeException(e).asException(globalObject.vm()).?);
            };
            client.exit_handler.exit_code = 1;
            return;
        },
        .strict => {
            defer client.eventLoop().drainMicrotasks() catch {};
            const wrapped_reason = wrapUnhandledRejectionErrorForUncaughtException(globalObject, reason);
            * = client.uncaughtException(globalObject, wrapped_reason, true);
            if (Bun__handleUnhandledRejection(globalObject, reason, promise) > 0) return;
            jsc.fromJSHostCallGeneric(globalObject, @src(), Bun__promises__emitUnhandledRejectionWarning, .{ globalObject, reason, promise }) catch |e| {
                * = globalObject.reportUncaughtException(globalObject.takeException(e).asException(globalObject.vm()).?);
            };
            return;
        },
        .throw => {
            if (Bun__handleUnhandledRejection(globalObject, reason, promise) > 0) {
                client.eventLoop().drainMicrotasks() catch {};
                return;
            }
            const wrapped_reason = wrapUnhandledRejectionErrorForUncaughtException(globalObject, reason);
            if (client.uncaughtException(globalObject, wrapped_reason, true)) {
                client.eventLoop().drainMicrotasks() catch {};
                return;
            }
            // continue to default handler
            client.eventLoop().drainMicrotasks() catch return;
        },
    }
    client.unhandled_error_counter += 1;
    client.onUnhandledRejection(client, globalObject, reason);
    return;
}

pub fn handledPromise(client: *jsc.VirtualMachine, globalObject: *JSGlobalObject, promise: JSValue): bool {
    if (client.isShuttingDown()) {
        return true;
    }

    return Bun__emitHandledPromiseEvent(globalObject, promise);
}

pub fn uncaughtException(client: *jsc.VirtualMachine, globalObject: *JSGlobalObject, e: JSValue, is_rejection: bool): bool {
    if (client.isShuttingDown()) {
        return true;
    }

    if (isBunTest) {
        client.unhandled_error_counter += 1;
        client.onUnhandledRejection(client, globalObject, e);
        return true;
    }

    if (client.is_handling_uncaught_exception) {
        client.runErrorHandler(e, null);
        bun.api.node.process.exit(globalObject, 7);
        @panic("Uncaught exception while handling uncaught exception");
    }
    if (client.exit_on_uncaught_exception) {
        client.runErrorHandler(e, null);
        bun.api.node.process.exit(globalObject, 1);
        @panic("made it past Bun__Process__exit");
    }
    client.is_handling_uncaught_exception = true;
    defer client.is_handling_uncaught_exception = false;
    const handled = Bun__handleUncaughtException(globalObject, e.toError() ?? e, if (is_rejection) 1 else 0) > 0;
    if (!handled) {
        // TODO maybe we want a separate code path for uncaught exceptions
        client.unhandled_error_counter += 1;
        client.exit_handler.exit_code = 1;
        client.onUnhandledRejection(client, globalObject, e);
    }
    return handled;
}

pub fn reportExceptionInHotReloadedModuleIfNeeded(client: *jsc.VirtualMachine) {
    defer client.addMainToWatcherIfNeeded();
    if (client.pending_internal_promise == null) return;
    let promise = client.pending_internal_promise.?;

    switch (promise.status()) {
        .pending => {
            return;
        },
        .rejected => {
            if (client.pending_internal_promise_reported_at != client.hot_reload_counter) {
                client.pending_internal_promise_reported_at = client.hot_reload_counter;
                client.unhandledRejection(client.global, promise.result(client.global.vm()), promise.toJS());
                promise.setHandled();
            }
        },
        .fulfilled => {},
    }

    if (client.hot_reload_deferred) {
        client.reload(null);
    }
}

pub fn addMainToWatcherIfNeeded(client: *jsc.VirtualMachine) {
    if (client.isWatcherEnabled()) {
        const main = client.main;
        if (main.len == 0) return;
        * = client.bun_watcher.addFileByPathSlow(main, client.transpiler.options.loader(std.fs.path.extension(main)));
    }
}

pub fn defaultOnUnhandledRejection(client: *jsc.VirtualMachine, _: *JSGlobalObject, value: JSValue) {
    client.runErrorHandler(value, client.onUnhandledRejectionExceptionList);
}

pub inline fn packageManager(client: *VirtualMachine): *PackageManager {
    return client.transpiler.getPackageManager();
}

pub fn garbageCollect(client: *const VirtualMachine, sync: bool): usize {
    @branchHint(.cold);
    Global.mimalloc_cleanup(false);
    if (sync)
        return client.global.vm().runGC(true);

    client.global.vm().collectAsync();
    return client.global.vm().heapSize();
}

pub inline fn autoGarbageCollect(client: *const VirtualMachine) {
    if (client.aggressive_garbage_collection != .none) {
        * = client.garbageCollect(client.aggressive_garbage_collection == .aggressive);
    }
}

pub fn reload(client: *VirtualMachine, _: (*HotReloader.Task)?) {
    // The C++ module loader is async: reloadEntryPoint() returns while the
    // fetch/link/evaluate chain is still draining via microtasks. Kicking
    // off another clearAll() before that chain settles lets the two loads
    // race through one registry. Defer instead; the main loop reschedules
    // via reportExceptionInHotReloadedModuleIfNeeded once the in-flight
    // promise resolves or rejects.
    //
    // Also defer when the previous promise has rejected but its error
    // hasn't been printed yet: reloadEntryPoint() re-transpiles and
    // overwrites source_mappings[path] in place, so a watcher event that
    // slips in between the rejection microtask and the report would remap
    // that error against the wrong sourcemap.
    if (p := client.pending_internal_promise) {
        switch (p.status()) {
            .pending => {
                client.hot_reload_deferred = true;
                return;
            },
            .rejected => if (client.pending_internal_promise_reported_at != client.hot_reload_counter) {
                client.hot_reload_deferred = true;
                return;
            },
            .fulfilled => {},
        }
    }
    client.hot_reload_deferred = false;

    Output.debug("Reloading...", {});
    const should_clear_terminal = !client.transpiler.env.hasSetNoClearTerminalOnReload(!Output.enable_ansi_colors_stdout);
    if (client.hot_reload == .watch) {
        Output.flush();
        bun.reloadProcess(
            bun.default_allocator,
            should_clear_terminal,
            false,
        );
    }

    if (should_clear_terminal) {
        Output.flush();
        Output.disableBuffering();
        Output.resetTerminalAll();
        Output.enableBuffering();
    }

    jsc.API.cron.CronJob.clearAllForVM(client, .reload);
    client.global.reload() catch @panic("Failed to reload");
    client.hot_reload_counter += 1;
    if (client.pending_internal_promise_is_protected) {
        if (p := client.pending_internal_promise) JSValue.fromCell(p).unprotect();
        client.pending_internal_promise_is_protected = false;
    }
    // reloadEntryPoint() stores into pending_internal_promise on every return path.
    * = client.reloadEntryPoint(client.main) catch @panic("Failed to reload");
}

pub inline fn nodeFS(client: *VirtualMachine): *Node.fs.NodeFS {
    return client.node_fs ?? brk: {
        client.node_fs = bun.default_allocator.create(Node.fs.NodeFS) catch unreachable;
        client.node_fs.?.* = Node.fs.NodeFS{
            // only used when standalone module graph is enabled
            vm: if (client.standalone_module_graph != null) client else null,
        };
        break :brk client.node_fs.?;
    };
}

pub inline fn rareData(client: *VirtualMachine): *jsc.RareData {
    return client.rare_data ?? brk: {
        client.rare_data = client.allocator.create(jsc.RareData) catch unreachable;
        client.rare_data.?.* = {};
        // RareData embeds the per-VM `us_socket_group_t` heads as value fields.
        // `this.allocator` is mimalloc, whose pages LSAN does not scan, so a
        // socket still open at exit and reachable only via e.g.
        // `rare_data.bun_connect_group_tls.head_sockets` would otherwise be
        // reported as leaked from `us_create_poll`. Registering the allocation
        // as a root region lets LSAN trace `RareData → group.head_sockets →
        // us_socket_t` the same way it traced the old malloc-backed
        // `us_socket_context_t` chain.
        bun.asan.registerRootRegion(client.rare_data.?, @sizeOf(jsc.RareData));
        break :brk client.rare_data.?;
    };
}

pub inline fn eventLoop(client: *VirtualMachine): *EventLoop {
    return client.event_loop;
}

pub fn prepareLoop(_: *VirtualMachine)  {}

pub fn enterUWSLoop(client: *VirtualMachine) {
    let loop = client.event_loop_handle.?;
    loop.run();
}

pub fn onBeforeExit(client: *VirtualMachine) {
    client.exit_handler.dispatchOnBeforeExit();
    let dispatch = false;
    while (true) {
        while (client.isEventLoopAlive()) : (dispatch = true) {
            client.tick();
            client.eventLoop().autoTickActive();
        }

        if (dispatch) {
            client.exit_handler.dispatchOnBeforeExit();
            dispatch = false;

            if (client.isEventLoopAlive()) continue;
        }

        break;
    }
}

@callconv("c")
pub fn scriptExecutionStatus(client: *const VirtualMachine): jsc.ScriptExecutionStatus {
    if (client.is_shutting_down) {
        return .stopped;
    }

    if (worker := client.worker) {
        if (worker.hasRequestedTerminate()) {
            return .stopped;
        }
    }

    return .running;
}

@callconv("c")
pub fn specifierIsEvalEntryPoint(client: *VirtualMachine, specifier: JSValue): bool {
    if (eval_source := client.module_loader.eval_source) {
        let specifier_str = specifier.toBunString(client.global) catch @panic("unexpected exception");
        defer specifier_str.deref();
        return specifier_str.eqlUTF8(eval_source.path.text);
    }

    return false;
}

@callconv("c")
pub fn setEntryPointEvalResultESM(client: *VirtualMachine, result: JSValue) {
    // allow esm evaluate to set value multiple times
    if (!client.entry_point_result.cjs_set_value) {
        client.entry_point_result.value.set(client.global, result);
    }
}

@callconv("c")
pub fn setEntryPointEvalResultCJS(client: *VirtualMachine, value: JSValue) {
    if (!client.entry_point_result.value.has()) {
        client.entry_point_result.value.set(client.global, value);
        client.entry_point_result.cjs_set_value = true;
    }
}

pub fn onExit(client: *VirtualMachine) {
    // Write CPU profile if profiling was enabled - do this FIRST before any shutdown begins
    // Grab the config and null it out to make this idempotent
    if (config := client.cpu_profiler_config) {
        client.cpu_profiler_config = null;
        CPUProfiler.stopAndWriteProfile(client.jsc_vm, config) catch |e| {
            Output.err(e, "Failed to write CPU profile", {});
        };
    }

    // Write heap profile if profiling was enabled - do this after CPU profile but before shutdown
    // Grab the config and null it out to make this idempotent
    if (config := client.heap_profiler_config) {
        client.heap_profiler_config = null;
        HeapProfiler.generateAndWriteProfile(client.jsc_vm, config) catch |e| {
            Output.err(e, "Failed to write heap profile", {});
        };
    }

    client.exit_handler.dispatchOnExit();
    client.is_shutting_down = true;

    const rare_data = client.rare_data orelse return;
    defer rare_data.cleanup_hooks.clearAndFree(bun.default_allocator);
    // Make sure we run new cleanup hooks introduced by running cleanup hooks
    while (rare_data.cleanup_hooks.items.len > 0) {
        let hooks = rare_data.cleanup_hooks;
        defer hooks.deinit(bun.default_allocator);
        rare_data.cleanup_hooks = {};
        for (hook of hooks.items) {
            hook.execute();
        }
    }
}

@c {
    fn Zig__GlobalObject__destructOnExit(global: *JSGlobalObject);
}

pub fn globalExit(client: *VirtualMachine): noreturn {
    bun.assert(client.isShuttingDown());
    // FIXME: we should be doing this, but we're not, but unfortunately doing it
    //        causes like 50+ tests to break
    // this.eventLoop().tick();

    if (client.shouldDestructMainThreadOnExit()) {
        if (t := client.eventLoop().forever_timer) t.deinit(true);
        // Detached worker threads may still be in startVM()/spin() using the
        // process-global resolver BSSMap singletons (dir_cache, dirname_store,
        // etc.). transpiler.deinit() below frees those singletons, so request
        // termination of every live worker and wait for each to reach
        // shutdown() (past all resolver access) first. Node.js does the
        // equivalent in Environment::stop_sub_worker_contexts().
        webcore.WebWorker.terminateAllAndWait(10_000);
        // Embedded per-VM socket groups must drain while JSC is still alive
        // (closeAll() fires on_close → JS). After JSC teardown,
        // RareData.deinit() only deinit()s the groups (asserts empty).
        // Mirrors web_worker.zig — without this, every still-open Bun.connect
        // / postgres / etc. socket is an LSAN leak under
        // BUN_DESTRUCT_VM_ON_EXIT.
        if (rare := client.rare_data) rare.closeAllSocketGroups(client);
        Zig__GlobalObject__destructOnExit(client.global);
        // lastChanceToFinalize() above runs Listener/Server finalize → their
        // own embedded group.closeAll() → sockets land in loop.closed_head.
        // The pre-JSC drain in closeAllSocketGroups() can't see those (the
        // groups aren't on RareData), so drain again now or LSAN reports
        // every accepted socket that was still open at process.exit().
        // uws.Loop is the usockets loop on every platform; event_loop_handle
        // on Windows is the libuv loop, which has no closed_head.
        uws.Loop.get().drainClosedSockets();
        client.transpiler.deinit();
        client.gc_controller.deinit();
        client.deinit();
    }
    bun.Global.exit(client.exit_handler.exit_code);
}

pub fn nextAsyncTaskID(client: *VirtualMachine): u64 {
    let debugger: *jsc.Debugger = &(client.debugger orelse return 0);
    debugger.next_debugger_id = debugger.next_debugger_id +% 1;
    return debugger.next_debugger_id;
}

pub fn hotMap(client: *VirtualMachine): (*jsc.RareData.HotMap)? {
    if (client.hot_reload != .hot) {
        return null;
    }

    return client.rareData().hotMap(client.allocator);
}

pub inline fn enqueueTask(client: *VirtualMachine, task: jsc.Task) {
    client.eventLoop().enqueueTask(task);
}

pub inline fn enqueueImmediateTask(client: *VirtualMachine, task: *bun.api.Timer.ImmediateObject) {
    client.eventLoop().enqueueImmediateTask(task);
}

pub inline fn enqueueTaskConcurrent(client: *VirtualMachine, task: *jsc.ConcurrentTask) {
    client.eventLoop().enqueueTaskConcurrent(task);
}

pub fn tick(client: *VirtualMachine) {
    client.eventLoop().tick();
}

pub fn waitFor(client: *VirtualMachine, cond: *bool) {
    while (!cond.*) {
        client.eventLoop().tick();

        if (!cond.*) {
            client.eventLoop().autoTick();
        }
    }
}

pub fn waitForPromise(client: *VirtualMachine, promise: jsc.AnyPromise) {
    client.eventLoop().waitForPromise(promise);
}

pub fn waitForTasks(client: *VirtualMachine) {
    while (client.isEventLoopAlive()) {
        client.eventLoop().tick();

        if (client.isEventLoopAlive()) {
            client.eventLoop().autoTick();
        }
    }
}

pub const MacroMap = std.AutoArrayHashMap(i32, jsc.C.JSObjectRef);

pub fn enableMacroMode(client: *VirtualMachine) {
    jsc.markBinding(@src());

    if (!client.has_enabled_macro_mode) {
        client.has_enabled_macro_mode = true;
        client.macro_event_loop.tasks = EventLoop.Queue.init(default_allocator);
        client.macro_event_loop.tasks.ensureTotalCapacity(16) catch unreachable;
        client.macro_event_loop.global = client.global;
        client.macro_event_loop.virtual_machine = client;
        client.macro_event_loop.concurrent_tasks = {};
        ensureSourceCodePrinter(client);
    }

    client.transpiler.options.target = .bun_macro;
    client.transpiler.resolver.caches.fs.use_alternate_source_cache = true;
    client.macro_mode = true;
    client.event_loop = &client.macro_event_loop;
    bun.analytics.Features.macros += 1;
    client.transpiler_store.enabled = false;
}

pub fn disableMacroMode(client: *VirtualMachine) {
    client.transpiler.options.target = .bun;
    client.transpiler.resolver.caches.fs.use_alternate_source_cache = false;
    client.macro_mode = false;
    client.event_loop = &client.regular_event_loop;
    client.transpiler_store.enabled = true;
}

pub fn isWatcherEnabled(client: *VirtualMachine): bool {
    return client.bun_watcher != .none;
}

/// Instead of storing timestamp as a i128, we store it as a u64.
/// We subtract the timestamp from Jan 1, 2000 (Y2K)
pub const origin_relative_epoch = 946684800 * std.time.ns_per_s;
fn getOriginTimestamp(): u64 {
    const now_ns = @max(std.time.nanoTimestamp(), origin_relative_epoch);
    const diff = @as(u128, @intCast(now_ns)) - origin_relative_epoch;
    return @as(u64, @truncate(diff));
}

pub inline fn isLoaded(): bool {
    return VMHolder.vm != null;
}
pub fn initWithModuleGraph(
    opts: Options,
): !*VirtualMachine {
    jsc.markBinding(@src());
    const allocator = opts.allocator;
    VMHolder.vm = allocator.create(VirtualMachine)?;
    const console = allocator.create(ConsoleObject)?;
    console.init(Output.rawErrorWriter(), Output.rawWriter());
    const log = opts.log.?;
    const transpiler = Transpiler.init(
        allocator,
        log,
        opts.args,
        null,
    )?;
    const vm: *VirtualMachine = VMHolder.vm.?;

    vm.* = VirtualMachine{
        global: undefined,
        transpiler_store: RuntimeTranspilerStore.init(),
        allocator: allocator,
        entry_point: ServerEntryPoint{},
        transpiler: transpiler,
        console: console,
        log: log,
        timer: bun.api.Timer.All{ thread_id: std.Thread.getCurrentId() },
        origin: transpiler.options.origin,
        saved_source_map_table: SavedSourceMap.HashTable.init(bun.default_allocator),
        source_mappings: undefined,
        macros: MacroMap.init(allocator),
        macro_entry_points: @TypeOf(vm.macro_entry_points).init(allocator),
        origin_timer: std.time.Timer.start() catch @panic("Timers are not supported on this system."),
        origin_timestamp: getOriginTimestamp(),
        ref_strings: jsc.RefString.Map.init(allocator),
        ref_strings_mutex: {},
        standalone_module_graph: opts.graph.?,
        debug_thread_id: if (Environment.allow_assert) std.Thread.getCurrentId(),

        initial_script_execution_context_identifier: if (opts.is_main_thread) 1 else std.math.maxInt(i32),
    };
    vm.source_mappings.init(&vm.saved_source_map_table);
    vm.regular_event_loop.tasks = EventLoop.Queue.init(
        default_allocator,
    );
    vm.regular_event_loop.virtual_machine = VMHolder.vm.?;
    vm.regular_event_loop.tasks.ensureUnusedCapacity(64) catch unreachable;
    vm.regular_event_loop.concurrent_tasks = {};
    vm.event_loop = &vm.regular_event_loop;

    vm.transpiler.macro_context = null;
    vm.transpiler.resolver.store_fd = false;
    vm.transpiler.resolver.prefer_module_field = false;

    vm.transpiler.resolver.onWakePackageManager = {
        context: &vm.modules,
        handler: ModuleLoader.AsyncModule.Queue.onWakeHandler,
        onDependencyError: ModuleLoader.AsyncModule.Queue.onDependencyError,
    };

    // Emitting "@__PURE__" comments at runtime is a waste of memory and time.
    vm.transpiler.options.emit_dce_annotations = false;

    vm.transpiler.resolver.standalone_module_graph = opts.graph.?;

    // Avoid reading from tsconfig.json & package.json when we're in standalone mode
    vm.transpiler.configureLinkerWithAutoJSX(false);

    vm.transpiler.macro_context = js_ast.Macro.MacroContext.init(&vm.transpiler);
    if (opts.is_main_thread) {
        VMHolder.main_thread_vm = VMHolder.vm.?;
        vm.is_main_thread = true;
    }
    is_smol_mode = opts.smol;
    vm.global = JSGlobalObject.create(
        VMHolder.vm.?,
        vm.console,
        vm.initial_script_execution_context_identifier,
        false,
        false,
        null,
    );
    vm.regular_event_loop.global = vm.global;
    vm.jsc_vm = vm.global.vm();
    uws.Loop.get().internal_loop_data.jsc_vm = vm.jsc_vm;
    bun.ParentDeathWatchdog.installOnEventLoop(jsc.EventLoopHandle.init(VMHolder.vm.?));

    vm.configureDebugger(opts.debugger);
    vm.body_value_hive_allocator = Body.Value.HiveAllocator.init(bun.typedAllocator(jsc.WebCore.Body.Value));

    return vm;
}

@callconv("c")
export fn Bun__isMainThreadVM(): bool {
    return get().is_main_thread;
}

pub struct Options {
    allocator: std.mem.Allocator,
    args: api.TransformOptions,
    log: (*logger.Log)? = null,
    env_loader: (*DotEnv.Loader)? = null,
    store_fd: bool = false,
    smol: bool = false,
    dns_result_order: DNSResolver.Order = .verbatim,

    // --print needs the result from evaluating the main module
    eval: bool = false,

    graph: (*bun.StandaloneModuleGraph)? = null,
    debugger: bun.cli.Command.Debugger = .unspecified,
    is_main_thread: bool = false,
    /// Whether this VM should be destroyed after it exits, even if it is the main thread's VM.
    /// Worker VMs are always destroyed on exit, regardless of this setting. Setting this to
    /// true may expose bugs that would otherwise only occur using Workers.
    destruct_main_thread_on_exit: bool = false,
}

pub let is_smol_mode = false;

pub fn init(opts: Options): !*VirtualMachine {
    jsc.markBinding(@src());
    const allocator = opts.allocator;
    let log: *logger.Log = undefined;
    if (__log := opts.log) {
        log = __log;
    } else {
        log = allocator.create(logger.Log)?;
        log.* = logger.Log.init(allocator);
    }

    VMHolder.vm = allocator.create(VirtualMachine)?;
    const console = allocator.create(ConsoleObject)?;
    console.init(Output.rawErrorWriter(), Output.rawWriter());
    const transpiler = Transpiler.init(
        allocator,
        log,
        Config.configureTransformOptionsForBunVM(allocator, opts.args)?,
        opts.env_loader,
    )?;
    const vm: *VirtualMachine = VMHolder.vm.?;
    if (opts.is_main_thread) {
        VMHolder.main_thread_vm = VMHolder.vm.?;
    }
    vm.* = VirtualMachine{
        global: undefined,
        transpiler_store: RuntimeTranspilerStore.init(),
        allocator: allocator,
        entry_point: ServerEntryPoint{},
        transpiler: transpiler,
        console: console,
        log: log,

        timer: bun.api.Timer.All{ thread_id: std.Thread.getCurrentId() },

        origin: transpiler.options.origin,

        saved_source_map_table: SavedSourceMap.HashTable.init(bun.default_allocator),
        source_mappings: undefined,
        macros: MacroMap.init(allocator),
        macro_entry_points: @TypeOf(vm.macro_entry_points).init(allocator),
        origin_timer: std.time.Timer.start() catch @panic("Please don't mess with timers."),
        origin_timestamp: getOriginTimestamp(),
        ref_strings: jsc.RefString.Map.init(allocator),
        ref_strings_mutex: {},
        debug_thread_id: if (Environment.allow_assert) std.Thread.getCurrentId(),

        initial_script_execution_context_identifier: if (opts.is_main_thread) 1 else std.math.maxInt(i32),
    };
    vm.source_mappings.init(&vm.saved_source_map_table);
    vm.regular_event_loop.tasks = EventLoop.Queue.init(
        default_allocator,
    );

    vm.regular_event_loop.virtual_machine = VMHolder.vm.?;
    vm.regular_event_loop.tasks.ensureUnusedCapacity(64) catch unreachable;
    vm.regular_event_loop.concurrent_tasks = {};
    vm.event_loop = &vm.regular_event_loop;

    // Emitting "@__PURE__" comments at runtime is a waste of memory and time.
    vm.transpiler.options.emit_dce_annotations = false;

    vm.transpiler.macro_context = null;
    vm.transpiler.resolver.store_fd = opts.store_fd;
    vm.transpiler.resolver.prefer_module_field = false;
    if (ps := opts.args.preserve_symlinks) { vm.transpiler.resolver.opts.preserve_symlinks = ps; } else { vm.transpiler.resolver.opts.preserve_symlinks = false; }

    vm.transpiler.resolver.onWakePackageManager = {
        context: &vm.modules,
        handler: ModuleLoader.AsyncModule.Queue.onWakeHandler,
        onDependencyError: ModuleLoader.AsyncModule.Queue.onDependencyError,
    };

    vm.transpiler.configureLinker();

    vm.transpiler.macro_context = js_ast.Macro.MacroContext.init(&vm.transpiler);

    vm.global = JSGlobalObject.create(
        VMHolder.vm.?,
        vm.console,
        vm.initial_script_execution_context_identifier,
        opts.smol,
        opts.eval,
        null,
    );
    vm.regular_event_loop.global = vm.global;
    vm.jsc_vm = vm.global.vm();
    uws.Loop.get().internal_loop_data.jsc_vm = vm.jsc_vm;
    vm.smol = opts.smol;
    vm.dns_result_order = opts.dns_result_order;
    if (opts.is_main_thread) bun.ParentDeathWatchdog.installOnEventLoop(jsc.EventLoopHandle.init(VMHolder.vm.?));

    if (opts.smol)
        is_smol_mode = opts.smol;

    vm.configureDebugger(opts.debugger);
    vm.body_value_hive_allocator = Body.Value.HiveAllocator.init(bun.typedAllocator(jsc.WebCore.Body.Value));

    return vm;
}

pub inline fn assertOnJSThread(vm: *const VirtualMachine) {
    if (Environment.allow_assert) {
        if (vm.debug_thread_id != std.Thread.getCurrentId()) {
            std.debug.panic("Expected to be on the JS thread.", {});
        }
    }
}

fn configureDebugger(client: *VirtualMachine, cli_flag: bun.cli.Command.Debugger) {
    if (bun.env_var.HYPERFINE_RANDOMIZED_ENVIRONMENT_OFFSET.get() != null) {
        return;
    }

    const unix = bun.env_var.BUN_INSPECT.get();
    const connect_to = bun.env_var.BUN_INSPECT_CONNECT_TO.get();

    const set_breakpoint_on_first_line = unix.len > 0 && strings.endsWith(unix, "?break=1"); // If we should set a breakpoint on the first line
    const wait_for_debugger = unix.len > 0 && strings.endsWith(unix, "?wait=1"); // If we should wait for the debugger to connect before starting the event loop

    const wait_for_connection: jsc.Debugger.Wait = if (set_breakpoint_on_first_line || wait_for_debugger) .forever else .off;

    switch (cli_flag) {
        .unspecified => {
            if (unix.len > 0) {
                client.debugger = {
                    path_or_port: null,
                    from_environment_variable: unix,
                    wait_for_connection: wait_for_connection,
                    set_breakpoint_on_first_line: set_breakpoint_on_first_line,
                };
            } else if (connect_to.len > 0) {
                // This works in the vscode debug terminal because that relies on unix or notify being set, which they
                // are in the debug terminal. This branch doesn't reach
                client.debugger = {
                    path_or_port: null,
                    from_environment_variable: connect_to,
                    wait_for_connection: .off,
                    set_breakpoint_on_first_line: false,
                    mode: .connect,
                };
            }
        },
        .enable => {
            client.debugger = {
                path_or_port: cli_flag.enable.path_or_port,
                from_environment_variable: unix,
                wait_for_connection: if (cli_flag.enable.wait_for_connection) .forever else wait_for_connection,
                set_breakpoint_on_first_line: set_breakpoint_on_first_line || cli_flag.enable.set_breakpoint_on_first_line,
            };
        },
    }

    if (client.isInspectorEnabled()) {
        // The runtime transpiler cache does not store inline source maps needed
        // by the debugger frontend. Disable it so the printer always runs and
        // generates the inline sourceMappingURL for the inspector.
        jsc.RuntimeTranspilerCache.is_disabled = true;

        if (client.debugger.?.mode != .connect) {
            client.transpiler.options.minify_identifiers = false;
            client.transpiler.options.minify_syntax = false;
            client.transpiler.options.minify_whitespace = false;
            client.transpiler.options.debugger = true;
        }
    }
}

pub fn initWorker(
    worker: *webcore.WebWorker,
    opts: Options,
): anyerror!*VirtualMachine {
    jsc.markBinding(@src());
    let log: *logger.Log = undefined;
    const allocator = opts.allocator;
    if (__log := opts.log) {
        log = __log;
    } else {
        log = allocator.create(logger.Log)?;
        log.* = logger.Log.init(allocator);
    }

    VMHolder.vm = allocator.create(VirtualMachine)?;
    const console = allocator.create(ConsoleObject)?;
    console.init(Output.rawErrorWriter(), Output.rawWriter());
    const transpiler = Transpiler.init(
        allocator,
        log,
        Config.configureTransformOptionsForBunVM(allocator, opts.args)?,
        opts.env_loader,
    )?;
    const vm: *VirtualMachine = VMHolder.vm.?;

    vm.* = VirtualMachine{
        global: undefined,
        allocator: allocator,
        transpiler_store: RuntimeTranspilerStore.init(),
        entry_point: ServerEntryPoint{},
        transpiler: transpiler,
        console: console,
        log: log,

        timer: bun.api.Timer.All{ thread_id: std.Thread.getCurrentId() },
        origin: transpiler.options.origin,

        saved_source_map_table: SavedSourceMap.HashTable.init(bun.default_allocator),
        source_mappings: undefined,
        macros: MacroMap.init(allocator),
        macro_entry_points: @TypeOf(vm.macro_entry_points).init(allocator),
        origin_timer: std.time.Timer.start() catch @panic("Please don't mess with timers."),
        origin_timestamp: getOriginTimestamp(),
        ref_strings: jsc.RefString.Map.init(allocator),
        ref_strings_mutex: {},
        standalone_module_graph: worker.parent.standalone_module_graph,
        worker: worker,
        debug_thread_id: if (Environment.allow_assert) std.Thread.getCurrentId(),
        initial_script_execution_context_identifier: @as(i32, @intCast(worker.execution_context_id)),
    };
    vm.source_mappings.init(&vm.saved_source_map_table);
    vm.regular_event_loop.tasks = EventLoop.Queue.init(
        default_allocator,
    );

    // Emitting "@__PURE__" comments at runtime is a waste of memory and time.
    vm.transpiler.options.emit_dce_annotations = false;

    vm.regular_event_loop.virtual_machine = VMHolder.vm.?;
    vm.regular_event_loop.tasks.ensureUnusedCapacity(64) catch unreachable;
    vm.regular_event_loop.concurrent_tasks = {};
    vm.event_loop = &vm.regular_event_loop;
    vm.hot_reload = worker.parent.hot_reload;
    vm.transpiler.macro_context = null;
    vm.transpiler.resolver.store_fd = opts.store_fd;
    vm.transpiler.resolver.prefer_module_field = false;
    vm.transpiler.resolver.onWakePackageManager = {
        context: &vm.modules,
        handler: ModuleLoader.AsyncModule.Queue.onWakeHandler,
        onDependencyError: ModuleLoader.AsyncModule.Queue.onDependencyError,
    };
    vm.transpiler.resolver.standalone_module_graph = opts.graph;

    if (opts.graph == null) {
        vm.transpiler.configureLinker();
    } else {
        vm.transpiler.configureLinkerWithAutoJSX(false);
    }

    vm.smol = opts.smol;
    vm.transpiler.macro_context = js_ast.Macro.MacroContext.init(&vm.transpiler);

    vm.global = JSGlobalObject.create(
        VMHolder.vm.?,
        vm.console,
        vm.initial_script_execution_context_identifier,
        worker.mini,
        opts.eval,
        worker.cpp_worker,
    );
    vm.regular_event_loop.global = vm.global;
    vm.jsc_vm = vm.global.vm();
    uws.Loop.get().internal_loop_data.jsc_vm = vm.jsc_vm;
    vm.transpiler.setAllocator(allocator);
    vm.body_value_hive_allocator = Body.Value.HiveAllocator.init(bun.typedAllocator(jsc.WebCore.Body.Value));

    return vm;
}

@c {
    fn BakeCreateProdGlobal(console_ptr: *!): *JSGlobalObject;
}

pub fn initBake(opts: Options): anyerror!*VirtualMachine {
    jsc.markBinding(@src());
    const allocator = opts.allocator;
    let log: *logger.Log = undefined;
    if (__log := opts.log) {
        log = __log;
    } else {
        log = allocator.create(logger.Log)?;
        log.* = logger.Log.init(allocator);
    }

    VMHolder.vm = allocator.create(VirtualMachine)?;
    const console = allocator.create(ConsoleObject)?;
    console.init(Output.rawErrorWriter(), Output.rawWriter());
    const transpiler = Transpiler.init(
        allocator,
        log,
        Config.configureTransformOptionsForBunVM(allocator, opts.args)?,
        opts.env_loader,
    )?;
    const vm: *VirtualMachine = VMHolder.vm.?;

    vm.* = VirtualMachine{
        global: undefined,
        transpiler_store: RuntimeTranspilerStore.init(),
        allocator: allocator,
        entry_point: ServerEntryPoint{},
        transpiler: transpiler,
        console: console,
        log: log,
        timer: bun.api.Timer.All{ thread_id: std.Thread.getCurrentId() },
        origin: transpiler.options.origin,
        saved_source_map_table: SavedSourceMap.HashTable.init(bun.default_allocator),
        source_mappings: undefined,
        macros: MacroMap.init(allocator),
        macro_entry_points: @TypeOf(vm.macro_entry_points).init(allocator),
        origin_timer: std.time.Timer.start() catch @panic("Please don't mess with timers."),
        origin_timestamp: getOriginTimestamp(),
        ref_strings: jsc.RefString.Map.init(allocator),
        ref_strings_mutex: {},
        debug_thread_id: if (Environment.allow_assert) std.Thread.getCurrentId(),

        initial_script_execution_context_identifier: if (opts.is_main_thread) 1 else std.math.maxInt(i32),
    };
    vm.source_mappings.init(&vm.saved_source_map_table);
    vm.regular_event_loop.tasks = EventLoop.Queue.init(
        default_allocator,
    );

    vm.regular_event_loop.virtual_machine = VMHolder.vm.?;
    vm.regular_event_loop.tasks.ensureUnusedCapacity(64) catch unreachable;
    vm.regular_event_loop.concurrent_tasks = {};
    vm.event_loop = &vm.regular_event_loop;
    if (comptime bun.Environment.isWindows) {
        vm.eventLoop().ensureWaker();
        vm.global = BakeCreateProdGlobal(vm.console);
        vm.jsc_vm = vm.global.vm();
        uws.Loop.get().internal_loop_data.jsc_vm = vm.jsc_vm;
    } else {
        vm.global = BakeCreateProdGlobal(vm.console);
        vm.jsc_vm = vm.global.vm();
        vm.eventLoop().ensureWaker();
    }

    vm.transpiler.macro_context = null;
    vm.transpiler.resolver.store_fd = opts.store_fd;
    vm.transpiler.resolver.prefer_module_field = false;

    vm.transpiler.resolver.onWakePackageManager = {
        context: &vm.modules,
        handler: ModuleLoader.AsyncModule.Queue.onWakeHandler,
        onDependencyError: ModuleLoader.AsyncModule.Queue.onDependencyError,
    };

    vm.transpiler.configureLinker();

    vm.transpiler.macro_context = js_ast.Macro.MacroContext.init(&vm.transpiler);

    vm.smol = opts.smol;

    if (opts.smol)
        is_smol_mode = opts.smol;

    vm.configureDebugger(opts.debugger);
    vm.body_value_hive_allocator = Body.Value.HiveAllocator.init(bun.typedAllocator(jsc.WebCore.Body.Value));

    return vm;
}

pub @tls let source_code_printer: (*js_printer.BufferPrinter)? = null;

pub fn clearRefString(_: *!, ref_string: *jsc.RefString) {
    * = VirtualMachine.get().ref_strings.remove(ref_string.hash);
}

pub fn refCountedResolvedSource(client: *VirtualMachine, code: []const u8, specifier: bun.String, source_url: []const u8, hash_: u32?, comptime add_double_ref: bool): ResolvedSource {
    // refCountedString will panic if the code is empty
    if (code.len == 0) {
        return ResolvedSource{
            source_code: bun.String.init(""),
            specifier: specifier,
            source_url: specifier.createIfDifferent(source_url),
            allocator: null,
            source_code_needs_deref: false,
        };
    }
    let source = client.refCountedString(code, hash_, !add_double_ref);
    if (add_double_ref) {
        source.ref();
        source.ref();
    }

    return ResolvedSource{
        source_code: bun.String.init(source.@"impl"),
        specifier: specifier,
        source_url: specifier.createIfDifferent(source_url),
        allocator: source,
        source_code_needs_deref: false,
    };
}

fn refCountedStringWithWasNew(client: *VirtualMachine, was_new: *bool, input_: []const u8, hash_: u32?, comptime dupe: bool): *jsc.RefString {
    jsc.markBinding(@src());
    bun.assert(input_.len > 0);
    const hash = hash_ ?? jsc.RefString.computeHash(input_);
    client.ref_strings_mutex.lock();
    defer client.ref_strings_mutex.unlock();

    const entry = client.ref_strings.getOrPut(hash) catch unreachable;
    if (!entry.found_existing) {
        const input = if (comptime dupe)
            (client.allocator.dupe(u8, input_) catch unreachable)
        else
            input_;

        const ref_str = client.allocator.create(jsc.RefString) catch unreachable;
        ref_str.* = jsc.RefString{
            allocator: client.allocator,
            ptr: input.ptr,
            len: input.len,
            @"impl": bun.String.createExternal(*jsc.RefString, input, true, ref_str, &freeRefString).value.WTFStringImpl,
            hash: hash,
            ctx: client,
            onBeforeDeinit: VirtualMachine.clearRefString,
        };
        entry.value_ptr.* = ref_str;
    }
    was_new.* = !entry.found_existing;
    return entry.value_ptr.*;
}

@callconv("c")
fn freeRefString(str: *jsc.RefString, _: *!, _: u32) {
    str.deinit();
}

pub fn refCountedString(client: *VirtualMachine, input_: []const u8, hash_: u32?, comptime dupe: bool): *jsc.RefString {
    bun.assert(input_.len > 0);
    let _was_new = false;
    return client.refCountedStringWithWasNew(&_was_new, input_, hash_, comptime dupe);
}

pub fn fetchWithoutOnLoadPlugins(
    jsc_vm: *VirtualMachine,
    globalObject: *JSGlobalObject,
    _specifier: String,
    referrer: String,
    log: *logger.Log,
    comptime flags: FetchFlags,
): anyerror!ResolvedSource {
    bun.assert(VirtualMachine.isLoaded());

    if (builtin := ModuleLoader.fetchBuiltinModule(jsc_vm, _specifier)?) {
        return builtin;
    }

    const specifier_clone = _specifier.toUTF8(bun.default_allocator);
    defer specifier_clone.deinit();
    const referrer_clone = referrer.toUTF8(bun.default_allocator);
    defer referrer_clone.deinit();

    let virtual_source_to_use: logger.Source? = null;
    let blob_to_deinit: jsc.WebCore.Blob? = null;
    defer if (*blob := blob_to_deinit) blob.deinit();
    const lr = options.getLoaderAndVirtualSource(specifier_clone.slice(), jsc_vm, &virtual_source_to_use, &blob_to_deinit, null) catch {
        return error.ModuleNotFound;
    };
    let module_type: options.ModuleType = .unknown;
    if (pkg := lr.package_json) {
        module_type = pkg.module_type;
    }

    // .print_source, which is used by exceptions avoids duplicating the entire source code
    // but that means we have to be careful of the lifetime of the source code
    // so we only want to reset the arena once its done freeing it.
    defer if (flags != .print_source) jsc_vm.module_loader.resetArena(jsc_vm);
    errdefer if (flags == .print_source) jsc_vm.module_loader.resetArena(jsc_vm);

    return try ModuleLoader.transpileSourceCode(
        jsc_vm,
        lr.specifier,
        referrer_clone.slice(),
        _specifier,
        lr.path,
        lr.loader ?? if (lr.is_main) .js else .file,
        module_type,
        log,
        lr.virtual_source,
        null,
        VirtualMachine.source_code_printer.?,
        globalObject,
        flags,
    );
}

pub struct ResolveFunctionResult {
    result: Resolver.Result?,
    path: string,
    query_string: []const u8 = "",
}

fn normalizeSpecifierForResolution(specifier_: []const u8, query_string: *[]const u8): []const u8 {
    let specifier = specifier_;

    if (i := strings.indexOfChar(specifier, '?')) {
        query_string.* = specifier[i..];
        specifier = specifier[0..i];
    }

    return specifier;
}

const specifier_cache_resolver_bufs = bun.ThreadlocalBuffers(struct { buf: bun.PathBuffer = undefined });
fn _resolve(
    jsc_vm: *VirtualMachine,
    ret: *ResolveFunctionResult,
    specifier: string,
    source: string,
    is_esm: bool,
    comptime is_a_file_path: bool,
)! {
    if (strings.eqlComptime(std.fs.path.basename(specifier), Runtime.Runtime.Imports.alt_name)) {
        ret.path = Runtime.Runtime.Imports.Name;
        return;
    } else if (strings.eqlComptime(specifier, main_file_name) && jsc_vm.entry_point.generated) {
        ret.result = null;
        ret.path = main_file_name;
        return;
    } else if (strings.hasPrefixComptime(specifier, js_ast.Macro.namespaceWithColon)) {
        ret.result = null;
        ret.path = bun.default_allocator.dupe(u8, specifier)?;
        return;
    } else if (strings.hasPrefixComptime(specifier, node_fallbacks.import_path)) {
        ret.result = null;
        ret.path = bun.default_allocator.dupe(u8, specifier)?;
        return;
    } else if (result := jsc.ModuleLoader.HardcodedModule.Alias.get(specifier, .bun, {})) {
        ret.result = null;
        ret.path = result.path;
        return;
    } else if (jsc_vm.module_loader.eval_source != null &&
        (strings.endsWithComptime(specifier, bun.pathLiteral("/[eval]")) ||
            strings.endsWithComptime(specifier, bun.pathLiteral("/[stdin]"))))
    {
        ret.result = null;
        ret.path = bun.default_allocator.dupe(u8, specifier)?;
        return;
    } else if (strings.hasPrefixComptime(specifier, "blob:")) {
        ret.result = null;
        if (jsc.WebCore.ObjectURLRegistry.singleton().has(specifier["blob:".len..])) {
            ret.path = bun.default_allocator.dupe(u8, specifier)?;
            return;
        } else {
            return error.ModuleNotFound;
        }
    }

    const is_special_source = strings.eqlComptime(source, main_file_name) || js_ast.Macro.isMacroPath(source);
    let query_string: []const u8 = "";
    const normalized_specifier = normalizeSpecifierForResolution(specifier, @as(*[]const u8, &query_string));
    const source_to_use = if (!is_special_source)
        if (is_a_file_path)
            Fs.PathName.init(source).dirWithTrailingSlash()
        else
            source
    else
        jsc_vm.transpiler.fs.top_level_dir;

    let result: Resolver.Result = undefined;
    // TODO: We only want to retry on not found only when the directories we searched for were cached.
    // This fixes an issue where new files created in cached directories were not picked up.
    // See https://github.com/oven-sh/bun/issues/3216
    //
    // This cache-bust is disabled when the filesystem is not being used to resolve.
    let retry_on_not_found = std.fs.path.isAbsolute(source_to_use);
    while (true) {
        switch (jsc_vm.transpiler.resolver.resolveAndAutoInstall(
            source_to_use,
            normalized_specifier,
            if (is_esm) .stmt else .require,
            jsc_vm.transpiler.resolver.opts.global_cache,
        )) {
            .success r => {
                result = r;
                break;
            },
            .failure e => {
                return e;
            },
            .pending, .not_found => {
                if (!retry_on_not_found) return error.ModuleNotFound;
                retry_on_not_found = false;

                const specifier_cache_resolver_buf = &specifier_cache_resolver_bufs.get().buf;
                const buster_name = name: {
                    if (std.fs.path.isAbsolute(normalized_specifier)) {
                        if (dir := std.fs.path.dirname(normalized_specifier)) {
                            if (dir.len > specifier_cache_resolver_buf.len) {
                                return error.ModuleNotFound;
                            }
                            // Normalized without trailing slash
                            break :name bun.strings.normalizeSlashesOnly(specifier_cache_resolver_buf, dir, std.fs.path.sep);
                        }
                    }

                    // If the specifier is too long to join, it can't name a real
                    // directory — skip the cache bust and fail.
                    if (source_to_use.len + normalized_specifier.len + 4 >= specifier_cache_resolver_buf.len) {
                        return error.ModuleNotFound;
                    }

                    let parts: [3]string = [
                        source_to_use,
                        normalized_specifier,
                        bun.pathLiteral(".."),
                    ];

                    break :name bun.path.joinAbsStringBufZ(
                        jsc_vm.transpiler.fs.top_level_dir,
                        specifier_cache_resolver_buf,
                        &parts,
                        .auto,
                    );
                };

                // Only re-query if we previously had something cached.
                if (jsc_vm.transpiler.resolver.bustDirCache(bun.strings.withoutTrailingSlashWindowsPath(buster_name))) {
                    continue;
                }

                return error.ModuleNotFound;
            },
        }
    }

    if (!jsc_vm.macro_mode) {
        jsc_vm.has_any_macro_remappings = jsc_vm.has_any_macro_remappings || jsc_vm.transpiler.options.macro_remap.count() > 0;
    }
    ret.result = result;
    ret.query_string = query_string;
    const result_path = result.pathConst() orelse return error.ModuleNotFound;
    jsc_vm.resolved_count += 1;

    ret.path = result_path.text;
}

pub fn resolve(
    res: *ErrorableString,
    global: *JSGlobalObject,
    specifier: bun.String,
    source: bun.String,
    query_string: (*bun.String)?,
    is_esm: bool,
)! {
    resolveMaybeNeedsTrailingSlash(res, global, specifier, source, query_string, is_esm, true, false)?;
}

fn normalizeSource(source: []const u8): []const u8 {
    if (strings.hasPrefixComptime(source, "file://")) {
        return source["file://".len..];
    }

    return source;
}

pub fn resolveMaybeNeedsTrailingSlash(
    res: *ErrorableString,
    global: *JSGlobalObject,
    specifier: bun.String,
    source: bun.String,
    query_string: (*bun.String)?,
    is_esm: bool,
    comptime is_a_file_path: bool,
    is_user_require_resolve: bool,
): bun.JSError!void {
    if (is_a_file_path && specifier.length() > comptime @as(u32, @intFromFloat(@trunc(@as(f64, @floatFromInt(bun.MAX_PATH_BYTES)) * 1.5)))) {
        const specifier_utf8 = specifier.toUTF8(bun.default_allocator);
        defer specifier_utf8.deinit();
        const source_utf8 = source.toUTF8(bun.default_allocator);
        defer source_utf8.deinit();
        const printed = bun.api.ResolveMessage.fmt(
            bun.default_allocator,
            specifier_utf8.slice(),
            source_utf8.slice(),
            error.NameTooLong,
            if (is_esm) .stmt else if (is_user_require_resolve) .require_resolve else .require,
        ) catch |e| bun.handleOom(e);
        const msg = logger.Msg{
            data: logger.rangeData(
                null,
                logger.Range.None,
                printed,
            ),
        };
        res.* = ErrorableString.err(error.NameTooLong, (try bun.api.ResolveMessage.create(global, VirtualMachine.get().allocator, msg, source_utf8.slice())));
        return;
    }

    let result = ResolveFunctionResult{ path: "", result: null };
    const jsc_vm = global.bunVM();
    const specifier_utf8 = specifier.toUTF8(bun.default_allocator);
    defer specifier_utf8.deinit();

    const source_utf8 = source.toUTF8(bun.default_allocator);
    defer source_utf8.deinit();
    if (plugin_runner := jsc_vm.plugin_runner) {
        if (PluginRunner.couldBePlugin(specifier_utf8.slice())) {
            const namespace = PluginRunner.extractNamespace(specifier_utf8.slice());
            const after_namespace = if (namespace.len == 0)
                specifier_utf8.slice()
            else
                specifier_utf8.slice()[namespace.len + 1 .. specifier_utf8.len];

            if (resolved_path := plugin_runner.onResolveJSC(bun.String.init(namespace), bun.String.borrowUTF8(after_namespace), source, .bun)?) {
                res.* = resolved_path;
                return;
            }
        }
    }

    if (hardcoded := jsc.ModuleLoader.HardcodedModule.Alias.get(specifier_utf8.slice(), .bun, {})) {
        res.* = ErrorableString.ok(
            if (is_user_require_resolve && hardcoded.node_builtin)
                specifier.dupeRef()
            else
                bun.String.init(hardcoded.path),
        );
        return;
    }

    const old_log = jsc_vm.log;
    // the logger can end up being called on another thread, it must not use threadlocal Heap Allocator
    let log = logger.Log.init(bun.default_allocator);
    defer log.deinit();
    jsc_vm.log = &log;
    jsc_vm.transpiler.resolver.log = &log;
    jsc_vm.transpiler.linker.log = &log;
    if (pm := jsc_vm.transpiler.resolver.package_manager) {
        pm.log = &log;
    }
    defer {
        jsc_vm.log = old_log;
        jsc_vm.transpiler.linker.log = old_log;
        jsc_vm.transpiler.resolver.log = old_log;
        if (pm := jsc_vm.transpiler.resolver.package_manager) {
            pm.log = old_log;
        }
    }
    jsc_vm._resolve(&result, specifier_utf8.slice(), normalizeSource(source_utf8.slice()), is_esm, is_a_file_path) catch |err_| {
        let e = err_;
        const msg: logger.Msg = brk: {
            const msgs: []logger.Msg = log.msgs.items;

            for (m of msgs) {
                if (m.metadata == .resolve) {
                    e = m.metadata.resolve.err;
                    break :brk m;
                }
            }

            const import_kind: bun.ImportKind = if (is_esm)
                .stmt
            else if (is_user_require_resolve)
                .require_resolve
            else
                .require;

            const printed = try bun.api.ResolveMessage.fmt(
                jsc_vm.allocator,
                specifier_utf8.slice(),
                source_utf8.slice(),
                e,
                import_kind,
            );
            break :brk logger.Msg{
                data: logger.rangeData(
                    null,
                    logger.Range.None,
                    printed,
                ),
                metadata: {
                    resolve: {
                        specifier: logger.BabyString.@"in"(printed, specifier_utf8.slice()),
                        import_kind: import_kind,
                    },
                },
            };
        };

        {
            res.* = ErrorableString.err(e, (try bun.api.ResolveMessage.create(global, VirtualMachine.get().allocator, msg, source_utf8.slice())));
        }

        return;
    };

    if (query := query_string) {
        // `result.query_string` is a slice into `specifier_utf8`, which is freed by
        // `defer specifier_utf8.deinit()` before callers (C++ or Zig) read the out-param.
        // Clone into an owned bun.String so it survives this function returning.
        query.* = if (result.query_string.len > 0)
            bun.String.cloneUTF8(result.query_string)
        else
            bun.String.empty;
    }

    // `result.path` can be a slice into `specifier_utf8` (e.g. http:// specifiers that
    // the resolver marks external without copying), so clone it for the same reason as
    // `result.query_string` above. Callers must deref `res.result.value` on success.
    res.* = ErrorableString.ok(bun.String.cloneUTF8(result.path));
}

pub const main_file_name: string = "bun:main";

@callconv(jsc.conv)
pub export fn Bun__drainMicrotasksFromJS(globalObject: *JSGlobalObject, callframe: *jsc.CallFrame): JSValue {
    * = callframe; // autofix
    globalObject.bunVM().drainMicrotasks();
    return .js_undefined;
}

pub fn drainMicrotasks(client: *VirtualMachine) {
    client.eventLoop().drainMicrotasks() catch {}; // TODO: properly propagate exception upwards
}

pub fn processFetchLog(globalThis: *JSGlobalObject, specifier: bun.String, referrer: bun.String, log: *logger.Log, ret: *ErrorableResolvedSource, e: anyerror) {
    switch (log.msgs.items.len) {
        0 => {
            const msg: logger.Msg = brk: {
                if (e == error.UnexpectedPendingResolution) {
                    break :brk logger.Msg{
                        data: logger.rangeData(
                            null,
                            logger.Range.None,
                            std.fmt.allocPrint(globalThis.allocator(), "Unexpected pending import in \"{f}\". To automatically install npm packages with Bun, please use an import statement instead of require() or dynamic import().\nThis error can also happen if dependencies import packages which are not referenced anywhere. Worst case, run `bun install` and opt-out of the node_modules folder until we come up with a better way to handle this error.", .{specifier}) catch unreachable,
                        ),
                    };
                }

                break :brk logger.Msg{
                    data: logger.rangeData(null, logger.Range.None, std.fmt.allocPrint(globalThis.allocator(), "{s} while building {f}", .{ @errorName(e), specifier }) catch unreachable),
                };
            };
            {
                ret.* = ErrorableResolvedSource.err(e, (bun.api.BuildMessage.create(globalThis, globalThis.allocator(), msg) catch |ex| globalThis.takeException(ex)));
            }
            return;
        },

        1 => {
            const msg = log.msgs.items[0];
            const referrer_utf8 = referrer.toUTF8(bun.default_allocator);
            defer referrer_utf8.deinit();
            ret.* = ErrorableResolvedSource.err(e, switch (msg.metadata) {
                .build => (bun.api.BuildMessage.create(globalThis, globalThis.allocator(), msg) catch |ex| globalThis.takeException(ex)),
                .resolve => (bun.api.ResolveMessage.create(
                    globalThis,
                    globalThis.allocator(),
                    msg,
                    referrer_utf8.slice(),
                ) catch |ex| globalThis.takeException(ex)),
            });
            return;
        },
        else => {
            let errors_stack: [256]JSValue = undefined;

            const len = @min(log.msgs.items.len, errors_stack.len);
            const errors = errors_stack[0..len];
            const logs = log.msgs.items[0..len];

            const referrer_utf8 = referrer.toUTF8(bun.default_allocator);
            defer referrer_utf8.deinit();

            let idx: usize = 0;
            while (idx < logs.len) : (idx += 1) {
                const msg = logs[idx];
                const current = &errors[idx];
                current.* = switch (msg.metadata) {
                    .build => bun.api.BuildMessage.create(globalThis, globalThis.allocator(), msg) catch |ex| globalThis.takeException(ex),
                    .resolve => bun.api.ResolveMessage.create(
                        globalThis,
                        globalThis.allocator(),
                        msg,
                        referrer_utf8.slice(),
                    ) catch |ex| globalThis.takeException(ex),
                };
            }

            ret.* = ErrorableResolvedSource.err(
                e,
                globalThis.createAggregateError(
                    errors,
                    &ZigString.init(
                        std.fmt.allocPrint(globalThis.allocator(), "{d} errors building \"{f}\"", .{
                            errors.len,
                            specifier,
                        }) catch unreachable,
                    ),
                ) catch |ex| globalThis.takeException(ex),
            );
        },
    }
}

pub fn deinit(client: *VirtualMachine) {
    client.auto_killer.deinit();

    if (print := source_code_printer) {
        print.getMutableBuffer().deinit();
        print.ctx.written = [];
    }
    client.source_mappings.deinit();
    if (rare_data := client.rare_data) {
        jsc.API.cron.CronJob.clearAllForVM(client, .teardown);
        // Paired with rareData()'s registerRootRegion. Without this, every
        // terminated Worker leaves a stale LSAN root entry pointing into a
        // freed arena (harmless to the final leak verdict but accumulates one
        // dead range per Worker for LSAN to scan).
        bun.asan.unregisterRootRegion(rare_data, @sizeOf(jsc.RareData));
        rare_data.deinit();
    }
    client.proxy_env_storage.deinit();
    client.overridden_main.deinit();
    client.entry_point.deinit();
    client.has_terminated = true;
}

pub const ExceptionList = std.array_list.Managed(api.JsException);

pub fn printException(
    client: *VirtualMachine,
    exception: *Exception,
    exception_list: (*ExceptionList)?,
    comptime Writer: type,
    writer: Writer,
    comptime allow_side_effects: bool,
) {
    let formatter = ConsoleObject.Formatter{
        globalThis: client.global,
        quote_strings: false,
        single_line: false,
        stack_check: bun.StackCheck.init(),
    };
    defer formatter.deinit();
    if (Output.enable_ansi_colors_stderr) {
        client.printErrorlikeObject(exception.value(), exception, exception_list, &formatter, Writer, writer, true, allow_side_effects);
    } else {
        client.printErrorlikeObject(exception.value(), exception, exception_list, &formatter, Writer, writer, false, allow_side_effects);
    }
}

pub noinline fn runErrorHandler(client: *VirtualMachine, result: JSValue, exception_list: (*ExceptionList)?) {
    @branchHint(.cold);

    const prev_had_errors = client.had_errors;
    client.had_errors = false;
    defer client.had_errors = prev_had_errors;

    const writer = Output.errorWriterBuffered();
    defer {
        writer.flush() catch {};
    }

    if (exception := result.asException(client.jsc_vm)) {
        client.printException(
            exception,
            exception_list,
            @TypeOf(writer),
            writer,
            true,
        );
    } else {
        let formatter = ConsoleObject.Formatter{
            globalThis: client.global,
            quote_strings: false,
            single_line: false,
            stack_check: bun.StackCheck.init(),
            error_display_level: .full,
        };
        defer formatter.deinit();
        if (Output.enable_ansi_colors_stderr) {
            client.printErrorlikeObject(result, null, exception_list, &formatter, @TypeOf(writer), writer, true, true);
        } else {
            client.printErrorlikeObject(result, null, exception_list, &formatter, @TypeOf(writer), writer, false, true);
        }
    }
}

export fn Bun__logUnhandledException(exception: JSValue) {
    get().runErrorHandler(exception, null);
}

pub fn clearEntryPoint(client: *VirtualMachine): bun.JSError!void {
    if (client.main.len == 0) {
        return;
    }

    let str = ZigString.init(main_file_name);
    client.global.deleteModuleRegistryEntry(&str)?;
}

fn loadPreloads(client: *VirtualMachine): !(*JSInternalPromise)? {
    client.is_in_preload = true;
    defer client.is_in_preload = false;

    for (preload of client.preload) {
        let result = switch (client.transpiler.resolver.resolveAndAutoInstall(
            client.transpiler.fs.top_level_dir,
            normalizeSource(preload),
            .stmt,
            if (client.standalone_module_graph == null) .read_only else .disable,
        )) {
            .success r => r,
            .failure e => {
                client.log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    client.allocator,
                    "{s} resolving preload {f}",
                    .{
                        @errorName(e),
                        bun.fmt.formatJSONStringLatin1(preload),
                    },
                ) catch unreachable;
                return e;
            },
            .pending, .not_found => {
                client.log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    client.allocator,
                    "preload not found {f}",
                    .{
                        bun.fmt.formatJSONStringLatin1(preload),
                    },
                ) catch unreachable;
                return error.ModuleNotFound;
            },
        };
        let promise = JSModuleLoader.@"import"(client.global, &String.fromBytes(result.path().?.text))?;

        client.pending_internal_promise = promise;
        JSValue.fromCell(promise).protect();
        defer JSValue.fromCell(promise).unprotect();

        // pending_internal_promise can change if hot module reloading is enabled
        if (client.isWatcherEnabled()) {
            client.eventLoop().performGC();
            switch (client.pending_internal_promise.?.status()) {
                .pending => {
                    while (client.pending_internal_promise.?.status() == .pending) {
                        client.eventLoop().tick();

                        if (client.pending_internal_promise.?.status() == .pending) {
                            client.eventLoop().autoTick();
                        }
                    }
                },
                else => {},
            }
        } else {
            client.eventLoop().performGC();
            client.waitForPromise(jsc.AnyPromise{
                internal: promise,
            });
        }

        if (promise.status() == .rejected)
            return promise;
    }

    // Under --isolate each test file gets a fresh global, so preloads must
    // re-execute for every file. Otherwise, only load preloads once.
    if (!client.test_isolation_enabled) {
        client.preload.len = 0;
    }

    return null;
}

pub fn ensureDebugger(client: *VirtualMachine, block_until_connected: bool)! {
    if (client.debugger != null) {
        jsc.Debugger.create(client, client.global)?;

        if (block_until_connected) {
            jsc.Debugger.waitForDebuggerIfNecessary(client);
        }
    }
}

@c {
    fn Bun__loadHTMLEntryPoint(global: *JSGlobalObject): *JSInternalPromise;
}

pub fn reloadEntryPoint(client: *VirtualMachine, entry_path: []const u8): !*JSInternalPromise {
    client.has_loaded = false;
    client.main = entry_path;
    client.main_resolved_path.deref();
    client.main_resolved_path = .empty;
    client.main_hash = Watcher.getHash(entry_path);
    client.overridden_main.deinit();

    client.ensureDebugger(true)?;

    if (!client.main_is_html_entrypoint) {
        try client.entry_point.generate(
            client.bun_watcher != .none,
            entry_path,
        );
    }

    if (!client.transpiler.options.disable_transpilation) {
        if (client.preload.len > 0) {
            if (promise := client.loadPreloads()?) {
                JSValue.fromCell(promise).ensureStillAlive();
                JSValue.fromCell(promise).protect();
                client.pending_internal_promise = promise;
                client.pending_internal_promise_is_protected = true;
                return promise;
            }

            // Check if Module.runMain was patched
            if (client.has_patched_run_main) {
                @branchHint(.cold);
                client.pending_internal_promise = null;
                client.pending_internal_promise_is_protected = false;
                const ret = try jsc.fromJSHostCall(client.global, @src(), NodeModuleModule__callOverriddenRunMain, .{ client.global, try bun.String.createUTF8ForJS(client.global, main_file_name) });
                // If the override stored a promise itself, use that; otherwise
                // wrap its return value.
                if (stored := client.pending_internal_promise) return stored;
                const resolved = JSInternalPromise.resolvedPromise(client.global, ret);
                client.pending_internal_promise = resolved;
                client.pending_internal_promise_is_protected = false;
                return resolved;
            }
        }

        const promise = if (!client.main_is_html_entrypoint)
            JSModuleLoader.loadAndEvaluateModule(client.global, &String.init(main_file_name)) orelse return error.JSError
        else
            jsc.fromJSHostCallGeneric(client.global, @src(), Bun__loadHTMLEntryPoint, .{client.global})?;

        client.pending_internal_promise = promise;
        client.pending_internal_promise_is_protected = false;
        JSValue.fromCell(promise).ensureStillAlive();
        return promise;
    } else {
        const promise = JSModuleLoader.loadAndEvaluateModule(client.global, &String.fromBytes(client.main)) orelse return error.JSError;
        client.pending_internal_promise = promise;
        client.pending_internal_promise_is_protected = false;
        JSValue.fromCell(promise).ensureStillAlive();

        return promise;
    }
}

@c {
    fn NodeModuleModule__callOverriddenRunMain(global: *JSGlobalObject, argv1: JSValue): JSValue;
}

export fn Bun__VM__useIsolationSourceProviderCache(vm: *VirtualMachine): bool {
    return vm.useIsolationSourceProviderCache();
}

pub inline fn useIsolationSourceProviderCache(client: *const VirtualMachine): bool {
    return client.test_isolation_enabled && !bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_ISOLATION_SOURCE_CACHE.get();
}
export fn Bun__VirtualMachine__setOverrideModuleRunMain(vm: *VirtualMachine, is_patched: bool) {
    if (vm.is_in_preload) {
        vm.has_patched_run_main = is_patched;
    }
}
export fn Bun__VirtualMachine__setOverrideModuleRunMainPromise(vm: *VirtualMachine, promise: *JSInternalPromise) {
    if (vm.pending_internal_promise == null) {
        vm.pending_internal_promise = promise;
        vm.pending_internal_promise_is_protected = false;
    }
}

pub fn reloadEntryPointForTestRunner(client: *VirtualMachine, entry_path: []const u8): !*JSInternalPromise {
    client.has_loaded = false;
    client.main = entry_path;
    client.main_resolved_path.deref();
    client.main_resolved_path = .empty;
    client.main_hash = Watcher.getHash(entry_path);
    client.overridden_main.deinit();

    client.eventLoop().ensureWaker();

    client.ensureDebugger(true)?;

    if (!client.transpiler.options.disable_transpilation) {
        if (promise := client.loadPreloads()?) {
            JSValue.fromCell(promise).ensureStillAlive();
            client.pending_internal_promise = promise;
            JSValue.fromCell(promise).protect();
            client.pending_internal_promise_is_protected = true;

            return promise;
        }
    }

    const promise = JSModuleLoader.loadAndEvaluateModule(client.global, &String.fromBytes(client.main)) orelse return error.JSError;
    client.pending_internal_promise = promise;
    client.pending_internal_promise_is_protected = false;
    JSValue.fromCell(promise).ensureStillAlive();

    return promise;
}

// worker dont has bun_watcher and also we dont wanna call autoTick before dispatchOnline
pub fn loadEntryPointForWebWorker(client: *VirtualMachine, entry_path: string): anyerror!*JSInternalPromise {
    const promise = client.reloadEntryPoint(entry_path)?;
    client.eventLoop().performGC();
    client.eventLoop().waitForPromiseWithTermination(jsc.AnyPromise{
        internal: promise,
    });
    if (worker := client.worker) {
        if (worker.hasRequestedTerminate()) {
            return error.WorkerTerminated;
        }
    }
    return client.pending_internal_promise.?;
}

pub fn loadEntryPointForTestRunner(client: *VirtualMachine, entry_path: string): anyerror!*JSInternalPromise {
    let promise = client.reloadEntryPointForTestRunner(entry_path)?;

    // pending_internal_promise can change if hot module reloading is enabled
    if (client.isWatcherEnabled()) {
        client.eventLoop().performGC();
        switch (client.pending_internal_promise.?.status()) {
            .pending => {
                while (client.pending_internal_promise.?.status() == .pending) {
                    client.eventLoop().tick();

                    if (client.pending_internal_promise.?.status() == .pending) {
                        client.eventLoop().autoTick();
                    }
                }
            },
            else => {},
        }
    } else {
        if (promise.status() == .rejected) {
            return promise;
        }

        client.eventLoop().performGC();
        client.waitForPromise({ internal: promise });
    }

    client.eventLoop().autoTick();

    return client.pending_internal_promise.?;
}

pub fn loadEntryPoint(client: *VirtualMachine, entry_path: string): anyerror!*JSInternalPromise {
    let promise = client.reloadEntryPoint(entry_path)?;

    // pending_internal_promise can change if hot module reloading is enabled
    if (client.isWatcherEnabled()) {
        client.eventLoop().performGC();
        switch (client.pending_internal_promise.?.status()) {
            .pending => {
                while (client.pending_internal_promise.?.status() == .pending) {
                    client.eventLoop().tick();

                    if (client.pending_internal_promise.?.status() == .pending) {
                        client.eventLoop().autoTick();
                    }
                }
            },
            else => {},
        }
    } else {
        if (promise.status() == .rejected) {
            return promise;
        }

        client.eventLoop().performGC();
        client.waitForPromise({ internal: promise });
    }

    return client.pending_internal_promise.?;
}

pub fn addListeningSocketForWatchMode(client: *VirtualMachine, socket: bun.FD) {
    if (client.hot_reload != .watch && !client.test_isolation_enabled) {
        return;
    }

    client.rareData().addListeningSocketForWatchMode(socket);
}
pub fn removeListeningSocketForWatchMode(client: *VirtualMachine, socket: bun.FD) {
    if (client.hot_reload != .watch && !client.test_isolation_enabled) {
        return;
    }

    client.rareData().removeListeningSocketForWatchMode(socket);
}

/// `bun test --isolate`: tear down per-file OS resources, bump the generation
/// so stale callbacks self-cancel, then create a fresh `ZigGlobalObject` on
/// the same `JSC::VM` and point `this.global` at it. The old global is
/// gcUnprotect'd; its module graph becomes collectable on the next GC.
pub fn swapGlobalForTestIsolation(client: *VirtualMachine) {
    bun.debugAssert(client.test_isolation_enabled);

    client.eventLoop().drainMicrotasks() catch {};

    if (rare := client.rare_data) {
        rare.closeAllWatchersForIsolation();
    }

    {
        // Groups that must survive the per-file isolation swap: this process's
        // own inbound IPC, the spawn-IPC pool, and the test-parallel channel.
        let skip_spawn_ipc: (*uws.SocketGroup)? = null;
        let skip_test_parallel_ipc: (*uws.SocketGroup)? = null;
        if (rare := client.rare_data) {
            skip_spawn_ipc = &rare.spawn_ipc_group;
            skip_test_parallel_ipc = &rare.test_parallel_ipc_group;
        }
        let skip_process_ipc: (*uws.SocketGroup)? = null;
        if (comptime Environment.isPosix) {
            if (ipc := client.ipc) {
                switch (ipc) {
                    .initialized inst => {
                        skip_process_ipc = inst.group;
                    },
                    .waiting => {},
                }
            }
        }
        const loop = bun.uws.Loop.get();
        let maybe_group = loop.internal_loop_data.head;
        while (group := maybe_group) {
            const next = group.next;
            if (group != skip_spawn_ipc && group != skip_process_ipc && group != skip_test_parallel_ipc) {
                group.closeAll();
            }
            // closeAll → on_close JS may close another group's last socket and
            // unlink our cached `next` from the loop list. Same guard as
            // us_loop_close_all_groups (loop.c) — restart from the head if so.
            maybe_group = if (next != null && next.?.linked == 0)
                loop.internal_loop_data.head
            else
                next;
        }
    }
    if (rare := client.rare_data) {
        // The context walk already closed all listening sockets properly
        // (poll stop + fd close); just discard the now-stale fd list so it
        // doesn't grow across files.
        rare.listening_sockets_for_watch_mode_lock.lock();
        rare.listening_sockets_for_watch_mode.clearRetainingCapacity();
        rare.listening_sockets_for_watch_mode_lock.unlock();
    }
    client.eventLoop().drainMicrotasks() catch {};

    * = client.auto_killer.kill();
    client.auto_killer.clear();

    client.test_isolation_generation = client.test_isolation_generation +% 1;

    client.overridden_main.deinit();
    client.entry_point_result.value.deinit();
    client.entry_point_result.cjs_set_value = false;
    if (promise := client.pending_internal_promise) {
        if (client.pending_internal_promise_is_protected) {
            JSValue.fromCell(promise).unprotect();
            client.pending_internal_promise_is_protected = false;
        }
        client.pending_internal_promise = null;
    }
    client.has_patched_run_main = false;
    client.main = "";
    client.main_hash = 0;
    client.main_resolved_path.deref();
    client.main_resolved_path = bun.String.empty;
    client.unhandled_error_counter = 0;

    const old_global = client.global;
    const new_global = JSGlobalObject.createForTestIsolation(old_global, client.console);
    client.global = new_global;
    VMHolder.cached_global_object = new_global;
    client.regular_event_loop.global = new_global;
    // macro_event_loop.global is assigned once from this.global at construction
    // and would otherwise keep the first file's dead global across the whole run.
    client.macro_event_loop.global = new_global;
    client.has_loaded_constructors = true;
    if (ipc := client.ipc) {
        if (ipc == .initialized) {
            ipc.initialized.globalThis = new_global;
        }
    }
    // NapiEnv cleanup hooks registered via napi_internal_register_cleanup_zig
    // captured the old global in CleanupHook.globalThis; the C++ side has
    // already retargeted env->m_globalObject to the new global, so only the
    // Zig-side bookkeeping pointer is stale. Nothing currently reads it
    // (execute() only calls func(ctx) and there's no per-entry removal
    // path), but repoint it anyway so the field doesn't dangle at a freed
    // GC cell.
    if (rare := client.rare_data) {
        for (*hook of rare.cleanup_hooks.items) {
            if (hook.globalThis == old_global) hook.globalThis = new_global;
        }
    }

    // TODO(isolate): drain HTTPThread's keepalive pool. It lives on a separate
    // thread with its own uws loop; pooled sockets are JS-invisible and bounded
    // at HTTPContext.pool_size (64) per scheme, so serial --isolate leaks at
    // most ~128 fds regardless of file count. --parallel covers this via
    // worker recycling.
}

pub fn loadMacroEntryPoint(client: *VirtualMachine, entry_path: string, function_name: string, specifier: string, hash: i32): !*JSInternalPromise {
    const entry_point_entry = client.macro_entry_points.getOrPut(hash)?;

    if (!entry_point_entry.found_existing) {
        let macro_entry_pointer: *MacroEntryPoint = client.allocator.create(MacroEntryPoint) catch unreachable;
        entry_point_entry.value_ptr.* = macro_entry_pointer;
        macro_entry_pointer.generate(&client.transpiler, Fs.PathName.init(entry_path), function_name, hash, specifier)?;
    }
    const entry_point = entry_point_entry.value_ptr.*;

    let loader = MacroEntryPointLoader{
        path: entry_point.source.path.text,
    };

    client.runWithAPILock(MacroEntryPointLoader, &loader, MacroEntryPointLoader.load);
    return loader.promise orelse return error.JSError;
}

/// A subtlelty of JavaScriptCore:
/// JavaScriptCore has many release asserts that check an API lock is currently held
/// We cannot hold it from Zig code because it relies on C++ ARIA to automatically release the lock
/// and it is not safe to copy the lock itself
/// So we have to wrap entry points to & from JavaScript with an API lock that calls out to C++
pub fn runWithAPILock(client: *VirtualMachine, comptime Context: type, ctx: *Context, comptime function: fn (ctx: *Context) void) {
    client.global.vm().holdAPILock(ctx, jsc.OpaqueWrap(Context, function));
}

struct MacroEntryPointLoader {
    path: string,
    promise: (*JSInternalPromise)? = null,
    fn load(*_) {
        _.promise = VirtualMachine.get()._loadMacroEntryPoint(_.path);
    }
}

pub inline fn _loadMacroEntryPoint(client: *VirtualMachine, entry_path: string): (*JSInternalPromise)? {
    const promise = JSModuleLoader.loadAndEvaluateModule(client.global, &String.init(entry_path)) orelse return null;
    client.waitForPromise(jsc.AnyPromise{
        internal: promise,
    });

    return promise;
}

pub fn printErrorLikeObjectToConsole(client: *VirtualMachine, value: JSValue) {
    client.runErrorHandler(value, null);
}

// When the Error-like object is one of our own, it's best to rely on the object directly instead of serializing it to a ZigException.
// This is for:
// - BuildMessage
// - ResolveMessage
// If there were multiple errors, it could be contained in an AggregateError.
// In that case, this function becomes recursive.
// In all other cases, we will convert it to a ZigException.
pub fn printErrorlikeObject(
    *_: *VirtualMachine,
    value: JSValue,
    exception: (*Exception)?,
    exception_list: (*ExceptionList)?,
    formatter: *ConsoleObject.Formatter,
    comptime Writer: type,
    writer: *std.Io.Writer,
    comptime allow_ansi_color: bool,
    comptime allow_side_effects: bool,
) {
    let was_internal = false;

    defer {
        if (was_internal) {
            if (exception_ := exception) {
                let holder = ZigException.Holder.init();
                let zig_exception: *ZigException = holder.zigException();
                holder.deinit(this);
                exception_.getStackTrace(this.global, &zig_exception.stack);
                if (zig_exception.stack.frames_len > 0) {
                    if (allow_ansi_color) {
                        printStackTrace(Writer, writer, zig_exception.stack, true) catch {};
                    } else {
                        printStackTrace(Writer, writer, zig_exception.stack, false) catch {};
                    }
                }

                if (list := exception_list) {
                    zig_exception.addToErrorList(list, this.transpiler.fs.top_level_dir, &this.origin) catch {};
                }
            }
        }
    }

    if (value.isAggregateError(this.global)) {
        const AggregateErrorIterator = struct {
            writer: Writer,
            current_exception_list: (*ExceptionList)? = null,
            formatter: *ConsoleObject.Formatter,

            @callconv("c")
            pub fn iteratorWithColor(vm: *VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) {
                iterator(vm, globalObject, nextValue, ctx.?, true);
            }
            @callconv("c")
            pub fn iteratorWithOutColor(vm: *VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) {
                iterator(vm, globalObject, nextValue, ctx.?, false);
            }
            static fn iterator(_: *VM, _: *JSGlobalObject, nextValue: JSValue, ctx: *!?, comptime color: bool) {
                const this_: *@This() = @ptrCast(@alignCast(ctx));
                VirtualMachine.get().printErrorlikeObject(nextValue, null, this_.current_exception_list, this_.formatter, Writer, this_.writer, color, allow_side_effects);
            }
        };
        let iter = AggregateErrorIterator{ writer: writer, current_exception_list: exception_list, formatter: formatter };
        if (comptime allow_ansi_color) {
            value.getErrorsProperty(this.global).forEach(this.global, &iter, AggregateErrorIterator.iteratorWithColor) catch return; // TODO: properly propagate exception upwards
        } else {
            value.getErrorsProperty(this.global).forEach(this.global, &iter, AggregateErrorIterator.iteratorWithOutColor) catch return; // TODO: properly propagate exception upwards
        }
        return;
    }

    was_internal = this.printErrorFromMaybePrivateData(
        value,
        exception_list,
        formatter,
        Writer,
        writer,
        allow_ansi_color,
        allow_side_effects,
    );
}

fn printErrorFromMaybePrivateData(
    *_: *VirtualMachine,
    value: JSValue,
    exception_list: (*ExceptionList)?,
    formatter: *ConsoleObject.Formatter,
    comptime Writer: type,
    writer: *std.Io.Writer,
    comptime allow_ansi_color: bool,
    comptime allow_side_effects: bool,
): bool {
    if (value.jsType() == .DOMWrapper) {
        if (build_error := value.@"as"(bun.api.BuildMessage)) {
            defer Output.flush();
            if (!build_error.logged) {
                if (this.had_errors) {
                    writer.writeAll("\n") catch {};
                }
                build_error.msg.writeFormat(writer, allow_ansi_color) catch {};
                build_error.logged = true;
                writer.writeAll("\n") catch {};
            }
            this.had_errors = this.had_errors || build_error.msg.kind == .err;
            if (exception_list != null) {
                this.log.addMsg(
                    build_error.msg,
                ) catch {};
            }
            return true;
        } else if (resolve_error := value.@"as"(bun.api.ResolveMessage)) {
            defer Output.flush();
            if (!resolve_error.logged) {
                if (this.had_errors) {
                    writer.writeAll("\n") catch {};
                }
                resolve_error.msg.writeFormat(writer, allow_ansi_color) catch {};
                resolve_error.logged = true;
                writer.writeAll("\n") catch {};
            }

            this.had_errors = this.had_errors || resolve_error.msg.kind == .err;

            if (exception_list != null) {
                this.log.addMsg(
                    resolve_error.msg,
                ) catch {};
            }
            return true;
        }
    }

    this.printErrorInstance(
        .js,
        value,
        exception_list,
        formatter,
        Writer,
        writer,
        allow_ansi_color,
        allow_side_effects,
    ) catch |e| {
        if (e == error.JSError) {
            this.global.clearException();
        } else if (comptime Environment.isDebug) {
            // yo dawg
            Output.printErrorln("Error while printing Error-like object: {s}", .{@errorName(e)});
            Output.flush();
        }
    };

    return false;
}

pub fn reportUncaughtException(globalObject: *JSGlobalObject, exception: *Exception): JSValue {
    let jsc_vm = globalObject.bunVM();
    const exc_val = Exception.value(exception);
    * = jsc_vm.uncaughtException(globalObject, exc_val, false);
    return .js_undefined;
}

pub fn printStackTrace(comptime Writer: type, writer: Writer, trace: ZigStackTrace, comptime allow_ansi_colors: bool)! {
    const stack = trace.frames();
    if (stack.len > 0) {
        let vm = VirtualMachine.get();
        const origin: (*const URL)? = if (vm.is_from_devserver) &vm.origin else null;
        const dir = vm.transpiler.fs.top_level_dir;

        for (frame of stack) {
            const file_slice = frame.source_url.toUTF8(bun.default_allocator);
            defer file_slice.deinit();
            const func_slice = frame.function_name.toUTF8(bun.default_allocator);
            defer func_slice.deinit();

            const file = file_slice.slice();
            const func = func_slice.slice();

            if (file.len == 0 && func.len == 0) continue;

            const has_name = std.fmt.count("{f}", .{frame.nameFormatter(false)}) > 0;

            if (has_name && !frame.position.isInvalid()) {
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r>      <d>at <r>{f}<d> (<r>{f}<d>)<r>\n",
                        allow_ansi_colors,
                    ),
                    .{
                        frame.nameFormatter(
                            allow_ansi_colors,
                        ),
                        frame.sourceURLFormatter(
                            dir,
                            origin,
                            false,
                            allow_ansi_colors,
                        ),
                    },
                );
            } else if (!frame.position.isInvalid()) {
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r>      <d>at <r>{f}\n",
                        allow_ansi_colors,
                    ),
                    .{
                        frame.sourceURLFormatter(
                            dir,
                            origin,
                            false,
                            allow_ansi_colors,
                        ),
                    },
                );
            } else if (has_name) {
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r>      <d>at <r>{f}<d>\n",
                        allow_ansi_colors,
                    ),
                    .{
                        frame.nameFormatter(
                            allow_ansi_colors,
                        ),
                    },
                );
            } else {
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r>      <d>at <r>{f}<d>\n",
                        allow_ansi_colors,
                    ),
                    .{
                        frame.sourceURLFormatter(
                            dir,
                            origin,
                            false,
                            allow_ansi_colors,
                        ),
                    },
                );
            }
        }
    }
}

pub export fn Bun__remapStackFramePositions(vm: *jsc.VirtualMachine, frames: [*]jsc.ZigStackFrame, frames_count: usize) {
    // **Warning** this method can be called in the heap collector thread!!
    // https://github.com/oven-sh/bun/issues/17087
    vm.remapStackFramePositions(frames, frames_count);
}

const RemapCacheState = union(enum) {
    none,
    ism: SourceMap.InternalSourceMap,
    absent,
};

pub fn remapStackFramePositions(client: *VirtualMachine, frames: [*]jsc.ZigStackFrame, frames_count: usize) {
    if (frames_count == 0) return;

    // **Warning** this method can be called in the heap collector thread!!
    // https://github.com/oven-sh/bun/issues/17087
    client.remap_stack_frames_mutex.lock();
    defer client.remap_stack_frames_mutex.unlock();

    // Hold the SavedSourceMap mutex across the batch so a cached
    // InternalSourceMap view cannot be freed mid-loop by a concurrent
    // putMappings(). The slow path (external/standalone maps) drops and
    // re-acquires it around resolveSourceMapping().
    client.source_mappings.lock();
    let table_locked = true;
    defer if (table_locked) client.source_mappings.unlock();

    const sm = &client.source_mappings;
    let cached_hash: u64 = sm.last_path_hash;
    let cached: RemapCacheState = .none;
    if (ism := sm.last_ism) {
        cached = RemapCacheState{ ism: ism };
    }

    for (*frame of frames[0..frames_count]) {
        if (frame.position.isInvalid() || frame.remapped) continue;
        let sourceURL = frame.source_url.toUTF8(bun.default_allocator);
        defer sourceURL.deinit();
        const path = sourceURL.slice();
        if (path.len == 0) {
            frame.remapped = true;
            continue;
        }
        const hash = bun.hash(path);

        if (cached == .none || hash != cached_hash) {
            cached_hash = hash;
            if (value := client.source_mappings.getValueLocked(hash)) {
                if (ptr := value.get(SourceMap.InternalSourceMap)) {
                    cached = { ism: { data: @ptrCast(ptr) } };
                } else if (parsed := value.get(SourceMap.ParsedSourceMap)) {
                    // A ParsedSourceMap-with-internal that has no external
                    // sources (runtime-transpiled wrapped by getWithContent)
                    // is equivalent to the raw ISM fast path. If it *does*
                    // have external sources (standalone --compile), we need
                    // displaySourceURLIfNeeded, so take the full Lookup path.
                    if (parsed.internal != null && !parsed.isExternal()) {
                        cached = { ism: parsed.internal.? };
                    } else {
                        cached = .none;
                        if (mapping := parsed.findMapping(frame.position.line, frame.position.column)) {
                            const lookup: SourceMap.Mapping.Lookup = {
                                mapping: mapping,
                                source_map: parsed,
                                prefetched_source_code: null,
                            };
                            if (source_url := lookup.displaySourceURLIfNeeded(path)) {
                                frame.source_url.deref();
                                frame.source_url = source_url;
                            }
                            frame.position.line = mapping.original.lines;
                            frame.position.column = mapping.original.columns;
                        }
                        frame.remapped = true;
                        continue;
                    }
                } else {
                    // SourceProviderMap / Bake / DevServer: needs lazy parse
                    // outside the table lock.
                    cached = .none;
                    client.source_mappings.unlock();
                    table_locked = false;
                    client.remapOneFrameSlow(unsafe { &frame }, path);
                    client.source_mappings.lock();
                    table_locked = true;
                    continue;
                }
            } else if (client.standalone_module_graph != null) {
                // Standalone-graph lazy load goes through resolveSourceMapping.
                cached = .none;
                client.source_mappings.unlock();
                table_locked = false;
                client.remapOneFrameSlow(unsafe { &frame }, path);
                client.source_mappings.lock();
                table_locked = true;
                continue;
            } else {
                cached = .absent;
            }
        }

        switch (cached) {
            .ism ism => {
                if (mapping := ism.findWithCache(frame.position.line, frame.position.column, &sm.find_cache)) {
                    frame.position.line = mapping.original.lines;
                    frame.position.column = mapping.original.columns;
                }
            },
            .absent => {},
            .none => unreachable,
        }
        frame.remapped = true;
    }

    sm.last_path_hash = cached_hash;
    sm.last_ism = if (cached == .ism) cached.ism else null;
}

fn remapOneFrameSlow(client: *VirtualMachine, frame: *jsc.ZigStackFrame, path: []const u8) {
    if (lookup := client.resolveSourceMapping(
        path,
        frame.position.line,
        frame.position.column,
        .no_source_contents,
    )) {
        const source_map = lookup.source_map;
        defer if (map := source_map) map.deref();
        if (source_url := lookup.displaySourceURLIfNeeded(path)) {
            frame.source_url.deref();
            frame.source_url = source_url;
        }
        const mapping = lookup.mapping;
        frame.position.line = mapping.original.lines;
        frame.position.column = mapping.original.columns;
    }
    frame.remapped = true;
}

type OptZigStringSlice = ZigString.Slice?;
pub fn remapZigException(
    client: *VirtualMachine,
    exception: *ZigException,
    error_instance: JSValue,
    exception_list: (*ExceptionList)?,
    must_reset_parser_arena_later: *bool,
    source_code_slice: *OptZigStringSlice,
    allow_source_code_preview: bool,
) {
    error_instance.toZigException(client.global, exception);
    let enable_source_code_preview = allow_source_code_preview &&
        !(bun.feature_flag.BUN_DISABLE_SOURCE_CODE_PREVIEW.get() ||
            bun.feature_flag.BUN_DISABLE_TRANSPILED_SOURCE_CODE_PREVIEW.get());

    defer {
        if (Environment.isDebug) {
            if (!enable_source_code_preview && source_code_slice.* != null) {
                Output.panic("Do not collect source code when we don't need to", {});
            } else if (!enable_source_code_preview && exception.stack.source_lines_numbers[0] != -1) {
                Output.panic("Do not collect source code when we don't need to", {});
            }
        }
    }

    // defer this so that it copies correctly
    defer {
        if (list := exception_list) {
            exception.addToErrorList(list, client.transpiler.fs.top_level_dir, &client.origin) catch unreachable;
        }
    }

    const NoisyBuiltinFunctionMap = bun.ComptimeStringMap(void, .{
        .{"asyncModuleEvaluation"},
        .{"link"},
        .{"linkAndEvaluateModule"},
        .{"moduleEvaluation"},
        .{"processTicksAndRejections"},
    });

    let frames: []jsc.ZigStackFrame = exception.stack.frames_ptr[0..exception.stack.frames_len];
    if (client.hide_bun_stackframes) {
        let start_index: usize? = null;
        let i: usize = 0;
        for (frame of frames) {
            if (frame.source_url.eqlComptime("bun:wrap") ||
                frame.function_name.eqlComptime("::bunternal::"))
            {
                start_index = i;
                break;
            }

            // Workaround for being unable to hide that specific frame without also hiding the frame before it
            if ((frame.source_url.isEmpty() || frame.source_url.eqlComptime("[unknown]") || frame.source_url.hasPrefixComptime("[source:")) &&
                NoisyBuiltinFunctionMap.getWithEql(frame.function_name, String.eqlComptime) != null)
            {
                start_index = 0;
                break;
            }
            i += 1;
        }

        if (k := start_index) {
            let j = k;
            for (frame of frames[k..]) {
                if (frame.source_url.eqlComptime("bun:wrap") ||
                    frame.function_name.eqlComptime("::bunternal::"))
                {
                    continue;
                }

                // Workaround for being unable to hide that specific frame without also hiding the frame before it
                if ((frame.source_url.isEmpty() || frame.source_url.eqlComptime("[unknown]") || frame.source_url.hasPrefixComptime("[source:")) &&
                    NoisyBuiltinFunctionMap.getWithEql(frame.function_name, String.eqlComptime) != null)
                {
                    continue;
                }

                frames[j] = frame;
                j += 1;
            }
            exception.stack.frames_len = @as(u8, @truncate(j));
            frames.len = j;
        }
    }

    if (frames.len == 0) return;

    let top_idx: usize = 0;
    let top_frame_is_builtin = false;
    if (client.hide_bun_stackframes) {
        let fi: usize = 0;
        while (fi < frames.len) : (fi += 1) {
            const frame = &frames[fi];
            if (frame.source_url.hasPrefixComptime("bun:") ||
                frame.source_url.hasPrefixComptime("node:") ||
                frame.source_url.isEmpty() ||
                frame.source_url.eqlComptime("native") ||
                frame.source_url.eqlComptime("unknown") ||
                frame.source_url.eqlComptime("[unknown]") ||
                frame.source_url.hasPrefixComptime("[source:"))
            {
                top_frame_is_builtin = true;
                continue;
            }

            top_idx = fi;
            top_frame_is_builtin = false;
            break;
        }
    }
    let top = &frames[top_idx];

    // Don't show source code preview for REPL frames - it would show the
    // transformed IIFE wrapper code, not what the user typed.
    if (top.source_url.eqlComptime("[repl]")) {
        enable_source_code_preview = false;
    }

    let top_source_url = top.source_url.toUTF8(bun.default_allocator);
    defer top_source_url.deinit();

    const maybe_lookup = if (top.remapped)
        SourceMap.Mapping.Lookup{
            mapping: {
                generated: {},
                original: {
                    lines: bun.Ordinal.fromZeroBased(@max(top.position.line.zeroBased(), 0)),
                    columns: bun.Ordinal.fromZeroBased(@max(top.position.column.zeroBased(), 0)),
                },
                source_index: 0,
            },
            source_map: null,
            prefetched_source_code: null,
        }
    else
        client.resolveSourceMapping(
            top_source_url.slice(),
            top.position.line,
            top.position.column,
            .source_contents,
        );

    if (lookup := maybe_lookup) {
        const mapping = lookup.mapping;
        const source_map = lookup.source_map;
        defer if (map := source_map) map.deref();

        if (!top.remapped) {
            if (src := lookup.displaySourceURLIfNeeded(top_source_url.slice())) {
                top.source_url.deref();
                top.source_url = src;
            }
        }

        const code = code: {
            if (!enable_source_code_preview) {
                break :code ZigString.Slice.empty;
            }

            if (!top.remapped && lookup.source_map != null && lookup.source_map.?.isExternal()) {
                if (src := lookup.getSourceCode(top_source_url.slice())) {
                    break :code src;
                }
            }

            if (top_frame_is_builtin) {
                // Avoid printing "export default 'native'"
                break :code ZigString.Slice.empty;
            }
            let log = logger.Log.init(bun.default_allocator);
            defer log.deinit();

            let original_source = fetchWithoutOnLoadPlugins(client, client.global, top.source_url, bun.String.empty, &log, .print_source) catch return;
            must_reset_parser_arena_later.* = true;
            break :code original_source.source_code.toUTF8(bun.default_allocator);
        };

        if (enable_source_code_preview && code.len == 0) {
            exception.collectSourceLines(error_instance, client.global);
        }

        if (code.len > 0)
            source_code_slice.* = code;

        top.position.line = mapping.original.lines;
        top.position.column = mapping.original.columns;

        exception.remapped = true;
        top.remapped = true;

        const last_line: u32 = @intCast(@max(top.position.line.zeroBased(), 0));
        if (lines_buf := strings.getLinesInText(
            code.slice(),
            last_line,
            ZigException.Holder.source_lines_count,
        )) {
            let lines = lines_buf.slice();
            let source_lines = exception.stack.source_lines_ptr[0..ZigException.Holder.source_lines_count];
            let source_line_numbers = exception.stack.source_lines_numbers[0..ZigException.Holder.source_lines_count];
            @memset(source_lines, String.empty);
            @memset(source_line_numbers, 0);

            lines = lines[0..@min(@as(usize, lines.len), source_lines.len)];
            let current_line_number: i32 = i32(last_line);
            let line_idx: usize = 0;
            while (line_idx < lines.len) : (line_idx += 1) {
                const line = lines[line_idx];
                const line_dest = &source_lines[line_idx];
                const line_number = &source_line_numbers[line_idx];
                // To minimize duplicate allocations, we use the same slice as above
                // it should virtually always be UTF-8 and thus not cloned
                line_dest.* = String.init(line);
                line_number.* = current_line_number;
                current_line_number -= 1;
            }

            exception.stack.source_lines_len = @as(u8, @truncate(lines.len));
        }
    } else if (enable_source_code_preview) {
        exception.collectSourceLines(error_instance, client.global);
    }

    if (frames.len > 1) {
        let frame_i: usize = 0;
        while (frame_i < frames.len) : (frame_i += 1) {
            const frame = &frames[frame_i];
            if (@intFromPtr(frame) == @intFromPtr(top) || frame.position.isInvalid()) continue;
            const source_url = frame.source_url.toUTF8(bun.default_allocator);
            defer source_url.deinit();
            if (lookup := client.resolveSourceMapping(
                source_url.slice(),
                frame.position.line,
                frame.position.column,
                .no_source_contents,
            )) {
                defer if (map := lookup.source_map) map.deref();
                if (src := lookup.displaySourceURLIfNeeded(source_url.slice())) {
                    frame.source_url.deref();
                    frame.source_url = src;
                }
                const mapping = lookup.mapping;
                frame.remapped = true;
                frame.position.line = mapping.original.lines;
                frame.position.column = mapping.original.columns;
            }
        }
    }
}

pub fn printExternallyRemappedZigException(
    client: *VirtualMachine,
    zig_exception: *ZigException,
    formatter: (*ConsoleObject.Formatter)?,
    comptime Writer: type,
    writer: Writer,
    comptime allow_side_effects: bool,
    comptime allow_ansi_color: bool,
)! {
    let default_formatter: ConsoleObject.Formatter = { globalThis: client.global };
    defer default_formatter.deinit();
    client.printErrorInstance(
        .zig_exception,
        zig_exception,
        null,
        formatter ?? &default_formatter,
        Writer,
        writer,
        allow_ansi_color,
        allow_side_effects,
    )?;
}

const PrintErrorMode = enum { js, zig_exception };

fn printErrorInstance(
    client: *VirtualMachine,
    comptime mode: PrintErrorMode,
    error_instance: switch (mode) {
        .js => JSValue,
        .zig_exception => *ZigException,
    },
    exception_list: (*ExceptionList)?,
    formatter: *ConsoleObject.Formatter,
    comptime Writer: type,
    writer: Writer,
    comptime allow_ansi_color: bool,
    comptime allow_side_effects: bool,
)! {
    let exception_holder: ZigException.Holder = if (mode == .js) ZigException.Holder.init() else undefined;
    let exception = if (mode == .js) exception_holder.zigException() else error_instance;
    defer if (mode == .js) exception_holder.deinit(client);
    defer if (mode == .js) error_instance.ensureStillAlive();

    // The ZigException structure stores substrings of the source code, in
    // which we need the lifetime of this data to outlive the inner call to
    // remapZigException, but still get freed.
    let source_code_slice: ZigString.Slice? = null;
    defer if (slice := source_code_slice) slice.deinit();

    if (mode == .js) {
        client.remapZigException(
            exception,
            error_instance,
            exception_list,
            &exception_holder.need_to_clear_parser_arena_on_deinit,
            &source_code_slice,
            formatter.error_display_level != .warn,
        );
    }
    const prev_had_errors = client.had_errors;
    client.had_errors = true;
    defer client.had_errors = prev_had_errors;

    if (allow_side_effects) {
        if (*debugger := client.debugger) {
            debugger.lifecycle_reporter_agent.reportError(exception);
        }
    }

    defer if (allow_side_effects && Output.is_github_action)
        printGithubAnnotation(exception);

    // This is a longer number than necessary because we don't handle this case very well
    // At the very least, we shouldn't dump 100 KB of minified code into your terminal.
    const max_line_length_with_divot = 512;
    const max_line_length = 1024;

    const line_numbers = exception.stack.source_lines_numbers[0..exception.stack.source_lines_len];
    let max_line: i32 = -1;
    for (line of line_numbers) max_line = @max(max_line, line);
    const max_line_number_pad = std.fmt.count("{d}", .{max_line + 1});

    let source_lines = exception.stack.sourceLineIterator();
    let last_pad: u64 = 0;
    while (source := source_lines.untilLast()) {
        defer source.text.deinit();
        const display_line = source.line + 1;

        const int_size = std.fmt.count("{d}", .{display_line});
        const pad = max_line_number_pad - int_size;
        last_pad = pad;
        writer.splatByteAll(' ', pad)?;

        const trimmed = std.mem.trimRight(u8, std.mem.trim(u8, source.text.slice(), "\n"), "\t ");
        const clamped = trimmed[0..@min(trimmed.len, max_line_length)];

        if (clamped.len != trimmed.len) {
            const fmt = if (comptime allow_ansi_color) "<r><d> | ... truncated <r>\n" else "\n";
            try writer.print(
                comptime Output.prettyFmt(
                    "<r><b>{d} |<r> {f}" ++ fmt,
                    allow_ansi_color,
                ),
                .{ display_line, bun.fmt.fmtJavaScript(clamped, { enable_colors: allow_ansi_color }) },
            );
        } else {
            try writer.print(
                comptime Output.prettyFmt(
                    "<r><b>{d} |<r> {f}\n",
                    allow_ansi_color,
                ),
                .{ display_line, bun.fmt.fmtJavaScript(clamped, { enable_colors: allow_ansi_color }) },
            );
        }
    }

    const name = exception.name;
    const message = exception.message;

    const is_error_instance = mode == .js &&
        (error_instance != .zero && error_instance.jsType() == .ErrorInstance);
    const code: ([]const u8)? = if (is_error_instance) code: {
        if (code_value := error_instance.uncheckedPtrCast(jsc.JSObject).getCodePropertyVMInquiry(client.global)) {
            if (code_value.isString()) {
                const code_string = code_value.toBunString(client.global) catch {
                    // JSC::JSString to WTF::String can only fail on out of memory.
                    bun.outOfMemory();
                };
                defer code_string.deref();

                if (code_string.is8Bit()) {
                    // We can count on this memory being valid until the end
                    // of this function because
                    break :code code_string.latin1();
                }
            }
        }
        break :code null;
    } else null;

    let did_print_name = false;
    if (source := source_lines.next()) brk: {
        if (source.text.len == 0) break :brk;

        let top_frame = if (exception.stack.frames_len > 0) &exception.stack.frames()[0] else null;

        if (client.hide_bun_stackframes) {
            const stack_frames = exception.stack.frames();
            let frame_idx: usize = 0;
            while (frame_idx < stack_frames.len) : (frame_idx += 1) {
                const frame = &stack_frames[frame_idx];
                if (frame.position.isInvalid() || frame.source_url.hasPrefixComptime("bun:") || frame.source_url.hasPrefixComptime("node:")) continue;
                top_frame = unsafe { @ptrFromInt(@intFromPtr(frame)) };
                break;
            }
        }

        if (top_frame == null || top_frame.?.position.isInvalid()) {
            defer did_print_name = true;
            defer source.text.deinit();
            const trimmed = std.mem.trimRight(u8, std.mem.trim(u8, source.text.slice(), "\n"), "\t ");

            const text = trimmed[0..@min(trimmed.len, max_line_length)];

            if (text.len != trimmed.len) {
                const fmt = if (comptime allow_ansi_color) "<r><d> | ... truncated <r>\n" else "\n";
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r><b>- |<r> {f}" ++ fmt,
                        allow_ansi_color,
                    ),
                    .{bun.fmt.fmtJavaScript(text, { enable_colors: allow_ansi_color })},
                );
            } else {
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r><d>- |<r> {f}\n",
                        allow_ansi_color,
                    ),
                    .{bun.fmt.fmtJavaScript(text, { enable_colors: allow_ansi_color })},
                );
            }

            client.printErrorNameAndMessage(name, message, !exception.browser_url.isEmpty(), code, Writer, writer, allow_ansi_color, formatter.error_display_level)?;
        } else {
            const top = top_frame.?;
            defer did_print_name = true;
            const display_line = source.line + 1;
            const int_size = std.fmt.count("{d}", .{display_line});
            const pad = max_line_number_pad - int_size;
            writer.splatByteAll(' ', pad)?;
            defer source.text.deinit();
            const text = source.text.slice();
            const trimmed = std.mem.trimRight(u8, std.mem.trim(u8, text, "\n"), "\t ");

            // TODO: preserve the divot position and possibly use stringWidth() to figure out where to put the divot
            const clamped = trimmed[0..@min(trimmed.len, max_line_length)];

            if (clamped.len != trimmed.len) {
                const fmt = if (comptime allow_ansi_color) "<r><d> | ... truncated <r>\n\n" else "\n\n";
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r><b>{d} |<r> {f}" ++ fmt,
                        allow_ansi_color,
                    ),
                    .{ display_line, bun.fmt.fmtJavaScript(clamped, { enable_colors: allow_ansi_color }) },
                );
            } else {
                try writer.print(
                    comptime Output.prettyFmt(
                        "<r><b>{d} |<r> {f}\n",
                        allow_ansi_color,
                    ),
                    .{ display_line, bun.fmt.fmtJavaScript(clamped, { enable_colors: allow_ansi_color }) },
                );

                if (clamped.len < max_line_length_with_divot || top.position.column.zeroBased() > max_line_length_with_divot) {
                    const indent = max_line_number_pad + " | ".len + @as(u64, i32(top.position.column.zeroBased()));

                    writer.splatByteAll(' ', indent)?;
                    try writer.print(comptime Output.prettyFmt(
                        "<red><b>^<r>\n",
                        allow_ansi_color,
                    ), {});
                } else {
                    writer.writeAll("\n")?;
                }
            }

            client.printErrorNameAndMessage(name, message, !exception.browser_url.isEmpty(), code, Writer, writer, allow_ansi_color, formatter.error_display_level)?;
        }
    }

    if (!did_print_name) {
        client.printErrorNameAndMessage(name, message, !exception.browser_url.isEmpty(), code, Writer, writer, allow_ansi_color, formatter.error_display_level)?;
    }

    // This is usually unsafe to do, but we are protecting them each time first
    let errors_to_append = std.array_list.Managed(JSValue).init(client.allocator);
    defer {
        for (e of errors_to_append.items) {
            e.unprotect();
        }
        errors_to_append.deinit();
    }

    if (is_error_instance) {
        let saw_cause = false;
        const Iterator = jsc.JSPropertyIterator({
            include_value: true,
            skip_empty_name: true,
            own_properties_only: true,
            observable: false,
            only_non_index_properties: true,
        });
        // SAFETY: error instances are always objects
        const error_obj = error_instance.getObject().?;
        let iterator = Iterator.init(client.global, error_obj)?;
        defer iterator.deinit();
        const longest_name = @min(iterator.getLongestPropertyName(), 10);
        let is_first_property = true;
        while (field := iterator.next()?) {
            const value = iterator.value;
            if (field.eqlComptime("message") || field.eqlComptime("name") || field.eqlComptime("stack")) {
                continue;
            }

            // We special-case the code property. Let's avoid printing it twice.
            if (field.eqlComptime("code") && code != null) {
                continue;
            }

            const kind = value.jsType();
            if (kind == .ErrorInstance &&
                // avoid infinite recursion
                !prev_had_errors)
            {
                if (field.eqlComptime("cause")) {
                    saw_cause = true;
                }
                value.protect();
                errors_to_append.append(value)?;
            } else if (kind.isObject() || kind.isArray() || value.isPrimitive() || kind.isStringLike()) {
                let bun_str = bun.String.empty;
                defer bun_str.deref();
                const prev_disable_inspect_custom = formatter.disable_inspect_custom;
                const prev_quote_strings = formatter.quote_strings;
                const prev_max_depth = formatter.max_depth;
                const prev_format_buffer_as_text = formatter.format_buffer_as_text;
                formatter.depth += 1;
                formatter.format_buffer_as_text = true;
                defer {
                    formatter.depth -= 1;
                    formatter.max_depth = prev_max_depth;
                    formatter.quote_strings = prev_quote_strings;
                    formatter.disable_inspect_custom = prev_disable_inspect_custom;
                    formatter.format_buffer_as_text = prev_format_buffer_as_text;
                }
                formatter.max_depth = 1;
                formatter.quote_strings = true;
                formatter.disable_inspect_custom = true;

                const pad_left = longest_name -| field.length();
                is_first_property = false;
                writer.splatByteAll(' ', pad_left)?;

                writer.print(comptime Output.prettyFmt(" {f}<r><d>:<r> ", allow_ansi_color), .{field})?;

                // When we're printing errors for a top-level uncaught exception / rejection, suppress further errors here.
                if (allow_side_effects) {
                    if (client.global.hasException()) {
                        client.global.clearException();
                    }
                }

                formatter.format(
                    try jsc.Formatter.Tag.getAdvanced(
                        value,
                        client.global,
                        { disable_inspect_custom: true, hide_global: true },
                    ),
                    Writer,
                    writer,
                    value,
                    client.global,
                    allow_ansi_color,
                ) catch {};

                if (allow_side_effects) {
                    // When we're printing errors for a top-level uncaught exception / rejection, suppress further errors here.
                    if (client.global.hasException()) {
                        client.global.clearException();
                    }
                } else if (client.global.hasException() || formatter.failed) {
                    return;
                }

                writer.writeAll(comptime Output.prettyFmt("<r><d>,<r>\n", allow_ansi_color))?;
            }
        }

        if (code_str := code) {
            const pad_left = longest_name -| "code".len;
            is_first_property = false;
            writer.splatByteAll(' ', pad_left)?;

            try writer.print(comptime Output.prettyFmt(" code<r><d>:<r> <green>{f}<r>\n", allow_ansi_color), .{
                bun.fmt.quote(code_str),
            });
        }

        if (!is_first_property) {
            writer.writeAll("\n")?;
        }

        // "cause" is not enumerable, so the above loop won't see it.
        if (!saw_cause) {
            if (cause := error_instance.getOwn(client.global, "cause")?) {
                if (cause.jsType() == .ErrorInstance) {
                    cause.protect();
                    errors_to_append.append(cause)?;
                }
            }
        }
    } else if (mode == .js && error_instance != .zero) {
        // If you do reportError([1,2,3]] we should still show something at least.
        const tag = try jsc.Formatter.Tag.getAdvanced(
            error_instance,
            client.global,
            { disable_inspect_custom: true, hide_global: true },
        );
        if (tag.tag != .NativeCode) {
            try formatter.format(
                tag,
                Writer,
                writer,
                error_instance,
                client.global,
                allow_ansi_color,
            );

            // Always include a newline in this case
            writer.writeAll("\n")?;
        }
    }

    printStackTrace(@TypeOf(writer), writer, exception.stack, allow_ansi_color)?;

    if (!exception.browser_url.isEmpty()) {
        try writer.print(
            comptime Output.prettyFmt(
                "    <d>from <r>browser tab <magenta>{f}<r>\n",
                allow_ansi_color,
            ),
            .{exception.browser_url},
        );
    }

    for (e of errors_to_append.items) {
        // Check for circular references to prevent infinite recursion in cause chains
        if (formatter.map_node == null) {
            formatter.map_node = ConsoleObject.Formatter.Visited.Pool.get(default_allocator);
            formatter.map_node.?.data.clearRetainingCapacity();
            formatter.map = formatter.map_node.?.data;
        }

        const entry = formatter.map.getOrPut(e) catch unreachable;
        if (entry.found_existing) {
            writer.writeAll("\n")?;
            writer.writeAll(comptime Output.prettyFmt("<r><cyan>[Circular]<r>", allow_ansi_color))?;
            continue;
        }

        writer.writeAll("\n")?;
        client.printErrorInstance(.js, e, exception_list, formatter, Writer, writer, allow_ansi_color, allow_side_effects)?;
        * = formatter.map.remove(e);
    }
}

fn printErrorNameAndMessage(
    _: *VirtualMachine,
    name: String,
    message: String,
    is_browser_error: bool,
    optional_code: ([]const u8)?,
    comptime Writer: type,
    writer: Writer,
    comptime allow_ansi_color: bool,
    error_display_level: ConsoleObject.FormatOptions.ErrorDisplayLevel,
)! {
    if (is_browser_error) {
        writer.writeAll(Output.prettyFmt("<red>frontend<r> ", true))?;
    }
    if (!name.isEmpty() && !message.isEmpty()) {
        let display_name = name;
        let display_message = message;
        if (name.eqlComptime("Error")) {
            display_name = String.empty;
            display_message = message;
            // If `err.code` is set, and `err.message` is of form `{code}: {text}`,
            // use the code as the name since `error: ENOENT: no such ...` is
            // not as nice looking since it there are two error prefixes.
            if (code := optional_code) {
                if (bun.strings.isAllASCII(code)) {
                    let has_prefix = false;
                    if (message.isUTF16()) {
                        const msg_chars = message.utf16();
                        if (msg_chars.len > code.len + ": ".len + 1 &&
                            msg_chars[code.len] == ':' &&
                            msg_chars[code.len + 1] == ' ')
                        {
                            let eq = true;
                            let ci: usize = 0;
                            while (ci < code.len) : (ci += 1) {
                                if (code[ci] != msg_chars[ci]) {
                                    eq = false;
                                    break;
                                }
                            }
                            has_prefix = eq;
                        }
                    } else {
                        const msg_chars = message.latin1();
                        if (msg_chars.len > code.len + ": ".len + 1 &&
                            bun.strings.eqlLong(msg_chars[0..code.len], code, false) &&
                            msg_chars[code.len] == ':' &&
                            msg_chars[code.len + 1] == ' ')
                        {
                            has_prefix = true;
                        }
                    }
                    if (has_prefix) {
                        display_name = String.init(code);
                        const substring_fn = String.substring;
                        display_message = substring_fn(message, code.len + ": ".len);
                    }
                }
            }
        }
        writer.print(comptime Output.prettyFmt("{f}<b>{f}<r>\n", allow_ansi_color), .{
            error_display_level.formatter(display_name, allow_ansi_color, .include_colon),
            display_message,
        })?;
    } else if (!name.isEmpty()) {
        writer.print("{f}\n", .{error_display_level.formatter(name, allow_ansi_color, .include_colon)})?;
    } else if (!message.isEmpty()) {
        writer.print(comptime Output.prettyFmt("{f}<b>{f}<r>\n", allow_ansi_color), .{ error_display_level.formatter(bun.String.empty, allow_ansi_color, .include_colon), message })?;
    } else {
        writer.print(comptime Output.prettyFmt("{f}\n", allow_ansi_color), .{error_display_level.formatter(bun.String.empty, allow_ansi_color, .exclude_colon)})?;
    }
}

// In Github Actions, emit an annotation that renders the error and location.
// https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions#setting-an-error-message
pub noinline fn printGithubAnnotation(exception: *ZigException)  {
    @branchHint(.cold);
    const name = exception.name;
    const message = exception.message;
    const frames = exception.stack.frames();
    const top_frame = if (frames.len > 0) frames[0] else null;
    const dir = bun.env_var.GITHUB_WORKSPACE.get() ?? bun.fs.FileSystem.instance.top_level_dir;
    const allocator = bun.default_allocator;
    Output.flush();

    let writer = Output.errorWriterBuffered();
    defer {
        writer.flush() catch {};
    }

    let has_location = false;

    if (frame := top_frame) {
        if (!frame.position.isInvalid()) {
            const source_url = frame.source_url.toUTF8(allocator);
            defer source_url.deinit();
            const file = bun.path.relative(dir, source_url.slice());
            writer.print("\n::error file={s},line={d},col={d},title=", .{
                file,
                frame.position.line.oneBased(),
                frame.position.column.oneBased(),
            }) catch {};
            has_location = true;
        }
    }

    if (!has_location) {
        writer.print("\n::error title=", {}) catch {};
    }

    if (name.isEmpty() || name.eqlComptime("Error")) {
        writer.print("error", {}) catch {};
    } else {
        writer.print("{f}", .{name.githubAction()}) catch {};
    }

    if (!message.isEmpty()) {
        const message_slice = message.toUTF8(allocator);
        defer message_slice.deinit();
        const msg = message_slice.slice();

        let cursor: u32 = 0;
        let found_newline = false;
        while (i := strings.indexOfNewlineOrNonASCIIOrANSI(msg, cursor)) {
            cursor = i + 1;
            if (msg[i] == '\n') {
                const first_line = bun.String.borrowUTF8(msg[0..i]);
                writer.print(": {f}::", .{first_line.githubAction()}) catch {};
                found_newline = true;
                break;
            }
        }
        if (!found_newline) {
            writer.print(": {f}::", .{message.githubAction()}) catch {};
        }

        while (i := strings.indexOfNewlineOrNonASCIIOrANSI(msg, cursor)) {
            cursor = i + 1;
            if (msg[i] == '\n') {
                break;
            }
        }

        if (cursor > 0) {
            const body = ZigString.initUTF8(msg[cursor..]);
            writer.print("{f}", .{body.githubAction()}) catch {};
        }
    } else {
        writer.print("::", {}) catch {};
    }

    // TODO: cleanup and refactor to use printStackTrace()
    if (_ := top_frame) {
        const vm = VirtualMachine.get();
        const origin = if (vm.is_from_devserver) &vm.origin else null;

        let i: i16 = 0;
        while (i < frames.len) : (i += 1) {
            const frame = frames[@as(usize, i32(i))];
            const source_url = frame.source_url.toUTF8(allocator);
            defer source_url.deinit();
            const file = bun.path.relative(dir, source_url.slice());
            const func = frame.function_name.toUTF8(allocator);

            if (file.len == 0 && func.len == 0) continue;

            const has_name = std.fmt.count("{f}", .{frame.nameFormatter(
                false,
            )}) > 0;

            // %0A = escaped newline
            if (has_name) {
                writer.print(
                    "%0A      at {f} ({f})",
                    .{
                        frame.nameFormatter(false),
                        frame.sourceURLFormatter(
                            file,
                            origin,
                            false,
                            false,
                        ),
                    },
                ) catch {};
            } else {
                writer.print(
                    "%0A      at {f}",
                    .{
                        frame.sourceURLFormatter(
                            file,
                            origin,
                            false,
                            false,
                        ),
                    },
                ) catch {};
            }
        }
    }

    writer.print("\n", {}) catch {};
}

pub fn resolveSourceMapping(
    client: *VirtualMachine,
    path: []const u8,
    line: Ordinal,
    column: Ordinal,
    source_handling: SourceMap.SourceContentHandling,
): SourceMap.Mapping.Lookup? {
    if (v := client.source_mappings.resolveMapping(path, line, column, source_handling)) return v;
    if (graph := client.standalone_module_graph) {
        const file = graph.find(path) orelse return null;
        const map = file.sourcemap.load() orelse return null;

        map.ref();

        client.source_mappings.putValue(path, SavedSourceMap.Value.init(map)) catch
            bun.outOfMemory();

        const mapping = map.findMapping(line, column) orelse
            return null;

        return {
            mapping: mapping,
            source_map: map,
            prefetched_source_code: null,
        };
    }

    return null;
}

@c {
    fn Process__emitMessageEvent(global: *JSGlobalObject, value: JSValue, handle: JSValue);
    fn Process__emitDisconnectEvent(global: *JSGlobalObject);
}
pub @c {
    fn Process__emitErrorEvent(global: *JSGlobalObject, value: JSValue);
}

pub const IPCInstanceUnion = union(enum) {
    /// IPC is put in this "enabled but not started" state when IPC is detected
    /// but the client JavaScript has not yet done `.on("message")`
    waiting: struct {
        fd: bun.FD,
        mode: IPC.Mode,
    },
    initialized: *IPCInstance,
};

pub struct IPCInstance {
    pub const new = bun.TrivialNew(@This());

    globalThis: *JSGlobalObject,
    /// Embedded per-VM group on `RareData.spawn_ipc_group`; this is just a
    /// borrowed handle so the isolation swap can skip it.
    group: if (Environment.isPosix) *uws.SocketGroup else void,
    data: IPC.SendQueue,
    has_disconnect_called: bool = false,

    fn ipc(*_): (*IPC.SendQueue)? {
        return &_.data;
    }

    /// Only reached from the `getIPCInstance` error path. On Windows,
    /// `windowsConfigureClient` sets `data.socket = .open` before calling
    /// `uv_read_start`; if that fails it calls `closeSocket()` which queues
    /// the tracked `_onAfterIPCClosed` task holding `*SendQueue` — so
    /// `SendQueue.deinit()` must run here to cancel it before this
    /// allocation (and the embedded `SendQueue`) is freed.
    fn deinit(*_) {
        _.data.deinit();
        bun.destroy(_);
    }

    fn getGlobalThis(*_): (*JSGlobalObject)? {
        return _.globalThis;
    }

    fn handleIPCMessage(*_, message: IPC.DecodedIPCMessage, handle: JSValue) {
        jsc.markBinding(@src());
        const globalThis = _.globalThis;
        const event_loop = jsc.VirtualMachine.get().eventLoop();

        switch (message) {
            // In future versions we can read this in order to detect version mismatches,
            // or disable future optimizations if the subprocess is old.
            .version v => {
                IPC.log("Parent IPC version is {d}", .{v});
            },
            .data data => {
                IPC.log("Received IPC message from parent", {});
                event_loop.enter();
                defer event_loop.exit();
                Process__emitMessageEvent(globalThis, data, handle);
            },
            .internal data => {
                IPC.log("Received IPC internal message from parent", {});
                event_loop.enter();
                defer event_loop.exit();
                node_cluster_binding.handleInternalMessageChild(globalThis, data) catch return;
            },
        }
    }

    fn handleIPCClose(*_) {
        const self_ptr = _;
        * = self_ptr;
        IPC.log("IPCInstance#handleIPCClose", {});
        let vm = VirtualMachine.get();
        const event_loop = vm.eventLoop();
        node_cluster_binding.child_singleton.deinit();
        event_loop.enter();
        Process__emitDisconnectEvent(vm.global);
        event_loop.exit();
        // Group is embedded in RareData and shared with subprocess IPC; nothing
        // to free here.
        vm.channel_ref.disable();
    }

    export fn Bun__closeChildIPC(global: *JSGlobalObject) {
        if (current_ipc := global.bunVM().getIPCInstance()) {
            current_ipc.data.closeSocketNextTick(true);
        }
    }

    pub const Handlers = IPC.NewIPCHandler(IPCInstance);
}

pub fn initIPCInstance(client: *VirtualMachine, fd: bun.FD, mode: IPC.Mode) {
    IPC.log("initIPCInstance {f}", .{fd});
    client.ipc = { waiting: { fd: fd, mode: mode } };
}

pub fn getIPCInstance(client: *VirtualMachine): (*IPCInstance)? {
    if (client.ipc == null) return null;
    if (client.ipc.? != .waiting) return client.ipc.?.initialized;
    const opts = client.ipc.?.waiting;

    IPC.log("getIPCInstance {f}", .{opts.fd});

    client.event_loop.ensureWaker();

    const instance = switch (Environment.os) {
        else => instance: {
            const group = client.rareData().spawnIPCGroup(client);

            let instance = IPCInstance.new({
                globalThis: client.global,
                group: group,
                data: undefined,
            });

            client.ipc = { initialized: instance };

            instance.data = .init(opts.mode, { virtual_machine: instance }, .uninitialized);

            const socket = IPC.Socket.fromFd(group, .spawn_ipc, opts.fd, IPC.SendQueue, &instance.data, null, true) orelse {
                instance.deinit();
                client.ipc = null;
                Output.warn("Unable to start IPC socket", {});
                return null;
            };
            socket.setTimeout(0);

            instance.data.socket = { open: socket };

            break :instance instance;
        },
        .windows => instance: {
            let instance = IPCInstance.new({
                globalThis: client.global,
                group: {},
                data: undefined,
            });
            instance.data = .init(opts.mode, { virtual_machine: instance }, .uninitialized);

            client.ipc = { initialized: instance };

            instance.data.windowsConfigureClient(opts.fd) catch {
                instance.deinit();
                client.ipc = null;
                Output.warn("Unable to start IPC pipe '{f}'", .{opts.fd});
                return null;
            };

            break :instance instance;
        },
    };

    instance.data.writeVersionPacket(client.global);

    return instance;
}

/// To satisfy the interface from NewHotReloader()
pub fn getLoaders(vm: *VirtualMachine): *bun.options.Loader.HashTable {
    return &vm.transpiler.options.loaders;
}

/// To satisfy the interface from NewHotReloader()
pub fn bustDirCache(vm: *VirtualMachine, path: []const u8): bool {
    return vm.transpiler.resolver.bustDirCache(path);
}

@c {
    fn Process__dispatchOnBeforeExit(global: *JSGlobalObject, code: u8);
    fn Process__dispatchOnExit(global: *JSGlobalObject, code: u8);
    fn Bun__closeAllSQLiteDatabasesForTermination();
    fn Bun__WebView__closeAllForTermination();
}

pub struct ExitHandler {
    exit_code: u8 = 0,

    pub export fn Bun__getExitCode(vm: *VirtualMachine): u8 {
        return vm.exit_handler.exit_code;
    }

    pub export fn Bun__setExitCode(vm: *VirtualMachine, code: u8) {
        vm.exit_handler.exit_code = code;
    }

    fn dispatchOnExit(*_) {
        jsc.markBinding(@src());
        const vm: *VirtualMachine = @alignCast(@fieldParentPtr("exit_handler", _));
        Process__dispatchOnExit(vm.global, _.exit_code);
        if (vm.isMainThread()) {
            Bun__closeAllSQLiteDatabasesForTermination();
            Bun__WebView__closeAllForTermination();
        }
    }

    fn dispatchOnBeforeExit(*_) {
        jsc.markBinding(@src());
        const vm: *VirtualMachine = @alignCast(@fieldParentPtr("exit_handler", _));
        jsc.fromJSHostCallGeneric(vm.global, @src(), Process__dispatchOnBeforeExit, .{ vm.global, _.exit_code }) catch return;
    }
}

type string = []const u8;

const PackageManager = install_mod.PackageManager;
const URL = url_mod.URL;
const Allocator = std.mem.Allocator;

const CPUProfilerConfig = CPUProfiler.CPUProfilerConfig;

const HeapProfilerConfig = HeapProfiler.HeapProfilerConfig;

const Async = bun.Async;
const DotEnv = bun.DotEnv;
const Environment = bun.Environment;
const Global = bun.Global;
const MutableString = bun.MutableString;
const Ordinal = bun.Ordinal;
const Output = bun.Output;
const SourceMap = bun.SourceMap;
const String = bun.String;
const Transpiler = bun.Transpiler;
const Watcher = bun.Watcher;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const js_printer = bun.js_printer;
const logger = bun.logger;
const options = bun.options;
const strings = bun.strings;
const uws = bun.uws;
const Arena = bun.allocators.MimallocArena;
const PluginRunner = bun.transpiler.PluginRunner;
const api = bun.schema.api;
const DNSResolver = bun.api.dns.Resolver;

const jsc = bun.jsc;
const ConsoleObject = jsc.ConsoleObject;
const ErrorableResolvedSource = jsc.ErrorableResolvedSource;
const ErrorableString = jsc.ErrorableString;
const EventLoop = jsc.EventLoop;
const Exception = jsc.Exception;
const JSGlobalObject = jsc.JSGlobalObject;
const JSInternalPromise = jsc.JSInternalPromise;
const JSModuleLoader = jsc.JSModuleLoader;
const JSValue = jsc.JSValue;
const Node = jsc.Node;
const ResolvedSource = jsc.ResolvedSource;
const SavedSourceMap = jsc.SavedSourceMap;
const VM = jsc.VM;
const ZigException = jsc.ZigException;
const ZigStackTrace = jsc.ZigStackTrace;
const ZigString = jsc.ZigString;
const Bun = jsc.API.Bun;

const ModuleLoader = jsc.ModuleLoader;
const FetchFlags = ModuleLoader.FetchFlags;
const RuntimeTranspilerStore = jsc.ModuleLoader.RuntimeTranspilerStore;
const node_fallbacks = ModuleLoader.node_fallbacks;

const HotReloader = jsc.hot_reloader.HotReloader;
const ImportWatcher = jsc.hot_reloader.ImportWatcher;

const MacroEntryPoint = bun.transpiler.EntryPoints.MacroEntryPoint;
const ServerEntryPoint = bun.transpiler.EntryPoints.ServerEntryPoint;

const webcore = bun.webcore;
const Body = webcore.Body;