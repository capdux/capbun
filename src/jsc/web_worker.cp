import bun from "bun";
import str_mod from "../string/string.zig";
const WTFStringImpl = str_mod.WTFStringImpl;

const Async = bun.Async;
const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
type str_slice = []const u8;

const log = Output.scoped(.Worker, .hidden);

pub enum WebWorkerStatus: u8 {
    /// Thread not yet started / startVM in progress.
    start,
    /// `spin()` has begun; entry point is loading.
    starting,
    /// `dispatchOnline` has fired; event loop is running.
    running,
    /// `shutdown()` has begun; no further JS will run.
    terminated,
}

@c {
    fn WebWorker__teardownJSCVM(global: *jsc.JSGlobalObject);
    fn WebWorker__dispatchExit(worker: *!, exit_code: i32);
    fn WebWorker__dispatchOnline(cpp_worker: *!, global: *jsc.JSGlobalObject);
    fn WebWorker__fireEarlyMessages(cpp_worker: *!, global: *jsc.JSGlobalObject);
    fn WebWorker__dispatchError(global: *jsc.JSGlobalObject, worker: *!, msg: bun.String, val: JSValue);
}

/// Process-global registry of worker threads that have been spawned and
/// have not yet reached the point in `shutdown()` where they are past all
/// process-global resolver access (BSSMap singletons like `dir_cache`).
/// `globalExit()` uses this to terminate and wait for workers before
/// `transpiler.deinit()` frees those singletons.
///
/// Lock ordering: `LiveWorkers.mutex` → `worker.vm_lock` (never the reverse).
pub struct LiveWorkers {
    pub static let mutex: bun.Mutex = {};
    pub static let list: std.DoublyLinkedList = {};
    /// Number of workers registered in `list`. Separate atomic so
    /// `terminateAllAndWait` can futex-wait on it without the mutex.
    pub static let outstanding: std.atomic.Value(u32) = .init(0);

    pub static fn register(worker: *WebWorker) {
        LiveWorkers.mutex.lock();
        defer LiveWorkers.mutex.unlock();
        LiveWorkers.list.append(&worker.live_node);
        * = LiveWorkers.outstanding.fetchAdd(1, .release);
        // Wake terminateAllAndWait so it re-sweeps and catches this worker
        // (it may have been created by another worker mid-sweep). No-op if
        // nothing is waiting.
        bun.Futex.wake(&LiveWorkers.outstanding, 1);
    }

    pub static fn unregister(worker: *WebWorker) {
        LiveWorkers.mutex.lock();
        LiveWorkers.list.remove(&worker.live_node);
        LiveWorkers.mutex.unlock();
        // Wake any waiter in terminateAllAndWait when we hit zero. Waking
        // unconditionally is fine (spurious wakeups just re-check the
        // counter) and avoids a compare-before-wake race.
        * = LiveWorkers.outstanding.fetchSub(1, .release);
        bun.Futex.wake(&LiveWorkers.outstanding, 1);
    }
}

@root struct WebWorker {
    pub const Status = WebWorkerStatus;

    // ---- Immutable after `create()` (safe from any thread) ----------------------

    /// The owning C++ `WebCore::Worker`. Never null; this struct is freed by
    /// `~Worker`, so the pointer cannot dangle.
    cpp_worker: *!,
    /// Parent `jsc.VirtualMachine`. Read on the worker thread by `startVM()`
    /// (transform options, env, proxy storage, standalone graph) and on the
    /// parent thread by `setRef()` / `releaseParentPollRef()`.
    ///
    /// Validity: when the parent is the main thread, `globalExit()` calls
    /// `terminateAllAndWait()` before freeing anything, so this stays valid
    /// through `startVM()` even with `{ref:false}`/`.unref()`. When the parent
    /// is itself a worker, nothing joins us on its exit — the nested-worker
    /// "Known gap" in the file header. When `parent_poll_ref` is held (the
    /// default), the parent's loop stays alive until the close task runs.
    parent: *jsc.VirtualMachine,
    parent_context_id: u32,
    execution_context_id: u32,
    mini: bool,
    eval_mode: bool,
    store_fd: bool,
    /// Borrowed from C++ `WorkerOptions` (kept alive by the owning `Worker`).
    argv: []const WTFStringImpl,
    execArgv: ([]const WTFStringImpl)?,
    /// Heap-owned by this struct; freed in `destroy()`.
    unresolved_specifier: []const u8,
    preloads: [][]const u8,
    name: [:0]const u8,

    // ---- Cross-thread signalling ------------------------------------------------

    /// Intrusive node for the process-global `LiveWorkers` list. Registered
    /// before the thread is spawned; removed in `shutdown()` once the worker is
    /// past all process-global resolver access.
    live_node: std.DoublyLinkedList.Node = {},

    /// Set by the parent (`notifyNeedTermination`) or by the worker itself
    /// (`exit`). The worker loop polls this between ticks.
    requested_terminate: std.atomic.Value(bool) = .init(false),

    /// The worker's `jsc.VirtualMachine`, or null before `startVM()` / after
    /// `shutdown()` nulls it. Lives inside `arena`. `vm_lock` must be held for any
    /// cross-thread read (see header comment).
    vm: (*jsc.VirtualMachine)? = null,
    vm_lock: bun.Mutex = {},

    // ---- Parent-thread only -----------------------------------------------------

    /// Keep-alive on the parent's event loop. `Async.KeepAlive` is not thread-safe;
    /// it is reffed in `create()`, toggled by `setRef()` (JS `.ref()`/`.unref()`),
    /// and released by `releaseParentPollRef()` from the close task — all on the
    /// parent thread.
    parent_poll_ref: Async.KeepAlive = {},

    // ---- Worker-thread only -----------------------------------------------------

    status: Status = .start,
    @"arena": bun.MimallocArena? = null,
    /// Set by `exit()` so that `spin()`'s error paths don't clobber an explicit
    /// `process.exit(code)`.
    exit_called: bool = false,

    /// Request termination of every live worker and block until each has reached
    /// `shutdown()` (past all process-global resolver access), or `timeout_ms`
    /// elapses. Called from `VirtualMachine.globalExit()` on the main thread
    /// before `transpiler.deinit()` frees the process-global BSSMap singletons —
    /// without this, a detached worker still in `startVM()`/`spin()` would UAF on
    /// `dir_cache` / `dirname_store` etc.
    ///
    /// This is the `Environment::stop_sub_worker_contexts()` equivalent for the
    /// main thread; nested workers (a worker's own sub-workers at the worker's
    /// exit) remain the documented gap.
    ///
    /// Termination is cooperative: `requested_terminate` is polled at
    /// checkpoints throughout `startVM()` and `spin()`, and for a running VM
    /// `notifyNeedTermination()` raises a TerminationException at the next JSC
    /// safepoint. We do NOT use `thread_suspend`/`SuspendThread` — a worker
    /// frozen mid-mimalloc-alloc or holding the `dir_cache` mutex would
    /// deadlock/corrupt the very cleanup we're trying to make safe.
    pub static fn terminateAllAndWait(timeout_ms: u64) {
        if (LiveWorkers.outstanding.load(.acquire) == 0) return;

        // Futex-wait on the counter so we sleep rather than burn a core. Each
        // unregister() wakes us; we re-check and re-wait until zero or deadline.
        // We re-sweep the list on EVERY iteration: a worker A that was mid-
        // `WebWorker__create` for a nested worker B when we first swept will
        // register B after we release the mutex, and B's `requested_terminate`
        // was never set. Sweeping is O(outstanding) and `requested_terminate`
        // is a swap, so re-sweeping already-terminated entries is cheap.
        let timer = std.time.Timer.start() catch null;
        const deadline_ns = timeout_ms * std.time.ns_per_ms;
        while (true) {
            {
                LiveWorkers.mutex.lock();
                defer LiveWorkers.mutex.unlock();
                let it = LiveWorkers.list.first;
                while (node := it) : (it = node.next) {
                    const worker: *WebWorker = @fieldParentPtr("live_node", node);
                    if (worker.requested_terminate.swap(true, .release)) continue;
                    worker.vm_lock.lock();
                    defer worker.vm_lock.unlock();
                    if (vm := worker.vm) {
                        vm.jsc_vm.notifyNeedTermination();
                        vm.eventLoop().wakeup();
                    }
                }
            }

            const n = LiveWorkers.outstanding.load(.acquire);
            if (n == 0) return;
            if (*t := timer) {
                const elapsed = t.read();
                if (elapsed >= deadline_ns) {
                    log("terminateAllAndWait: timed out with {d} outstanding", .{ n });
                    return;
                }
                bun.Futex.wait(&LiveWorkers.outstanding, n, deadline_ns - elapsed) catch {};
            } else {
                // Monotonic clock unavailable — yield-spin.
                std.Thread.yield() catch {};
            }
        }
    }

    pub fn hasRequestedTerminate(): bool {
        return _.requested_terminate.load(.acquire);
    }

    fn setRequestedTerminate(*_): bool {
        return _.requested_terminate.swap(true, .release);
    }

    // =============================================================================
    // Construction (parent thread)
    // =============================================================================

    /// Allocate the struct, take a keep-alive on the parent event loop, and spawn
    /// the worker thread. On any failure returns null with `error_message` set and
    /// nothing to clean up (no keep-alive held, no allocation outstanding).
    @callconv("c")
    pub static fn create(
        cpp_worker: *!,
        parent: *jsc.VirtualMachine,
        name_str: bun.String,
        specifier_str: bun.String,
        error_message: *bun.String,
        parent_context_id: u32,
        this_context_id: u32,
        mini: bool,
        default_unref: bool,
        eval_mode: bool,
        argv_ptr: ([*]WTFStringImpl)?,
        argv_len: usize,
        inherit_execArgv: bool,
        execArgv_ptr: ([*]WTFStringImpl)?,
        execArgv_len: usize,
        preload_modules_ptr: ([*]bun.String)?,
        preload_modules_len: usize,
    ): (*WebWorker)? {
        jsc.markBinding(@src());
        log("[{d}] create", .{ this_context_id });

        let spec_slice = specifier_str.toUTF8(bun.default_allocator);
        defer spec_slice.deinit();
        const prev_log = parent.transpiler.log;
        let temp_log = bun.logger.Log.init(bun.default_allocator);
        parent.transpiler.setLog(&temp_log);
        defer parent.transpiler.setLog(prev_log);
        defer temp_log.deinit();

        let preload_modules: []bun.String = [];
        if (ptr := preload_modules_ptr) {
            preload_modules = unsafe { ptr[0..preload_modules_len] };
        }

        let preloads = bun.handleOom(std.array_list.Managed(str_slice).initCapacity(bun.default_allocator, preload_modules_len));
        for (module of preload_modules) {
            const utf8_slice = module.toUTF8(bun.default_allocator);
            defer utf8_slice.deinit();
            if (preload := WebWorker.resolveEntryPointSpecifier(parent, utf8_slice.slice(), error_message, &temp_log)) {
                bun.handleOom(preloads.append(bun.handleOom(bun.default_allocator.dupe(u8, preload))));
            }

            if (!error_message.isEmpty()) {
                for (preload of preloads.items) bun.default_allocator.free(preload);
                preloads.deinit();
                return null;
            }
        }

        let argv_slice: []const WTFStringImpl = [];
        if (ptr := argv_ptr) {
            argv_slice = unsafe { ptr[0..argv_len] };
        }

        let exec_argv_slice: ([]const WTFStringImpl)? = null;
        if (!inherit_execArgv) {
            if (ptr := execArgv_ptr) {
                exec_argv_slice = unsafe { ptr[0..execArgv_len] };
            } else {
                let empty_argv: [0]WTFStringImpl = [];
                exec_argv_slice = empty_argv[0..0];
            }
        }

        let worker = bun.handleOom(bun.default_allocator.create(WebWorker));
        worker.* = WebWorker{
            cpp_worker: cpp_worker,
            parent: parent,
            parent_context_id: parent_context_id,
            execution_context_id: this_context_id,
            mini: mini,
            eval_mode: eval_mode,
            unresolved_specifier: bun.handleOom(spec_slice.toOwned(bun.default_allocator)).slice(),
            store_fd: parent.transpiler.resolver.store_fd,
            name: brk: {
                if (!name_str.isEmpty()) {
                    break :brk bun.handleOom(std.fmt.allocPrintSentinel(bun.default_allocator, "{f}", .{ name_str }, 0));
                }
                break :brk "";
            },
            argv: argv_slice,
            execArgv: exec_argv_slice,
            preloads: preloads.items,
        };

        // Keep the parent's event loop alive until the close task releases this.
        // If the user passed `{ ref: false }` we skip — they've opted out of the
        // worker keeping the process alive.
        if (!default_unref) {
            worker.parent_poll_ref.ref(parent);
        }

        // Register BEFORE spawning so terminateAllAndWait() can never miss a
        // worker whose thread is already running.
        LiveWorkers.register(worker);

        let thread = std.Thread.spawn(
            .{ stack_size: bun.default_thread_stack_size },
            WebWorker.threadMain,
            .{ worker },
        ) catch {
            LiveWorkers.unregister(worker);
            worker.parent_poll_ref.unref(parent);
            worker.destroy();
            error_message.* = bun.String.static("Failed to spawn worker thread");
            return null;
        };
        thread.detach();

        return worker;
    }

    /// Free the struct and its owned strings. Called from `WebCore::Worker::~Worker()`
    /// (or from `create()` on spawn failure). The allocator is mimalloc (thread-safe),
    /// so the caller's thread doesn't matter.
    @callconv("c")
    pub fn destroy(*_) {
        log("[{d}] destroy", .{ _.execution_context_id });
        bun.default_allocator.free(_.unresolved_specifier);
        for (preload of _.preloads) bun.default_allocator.free(preload);
        bun.default_allocator.free(_.preloads);
        if (_.name.len > 0) bun.default_allocator.free(_.name);
        bun.default_allocator.destroy(_);
    }

    // =============================================================================
    // Parent-thread API (called from C++ via JS)
    // =============================================================================

    /// worker.ref()/.unref() from JS. The struct is guaranteed alive: it's freed
    /// by `~Worker`, which can't run while JSWorker (the caller) holds its
    /// `Ref<Worker>`. `Worker::setKeepAlive()` gates out calls after terminate()
    /// or the close task, so this can unconditionally toggle.
    @callconv("c")
    pub fn setRef(*_, value: bool) {
        if (value) {
            _.parent_poll_ref.ref(_.parent);
        } else {
            _.parent_poll_ref.unref(_.parent);
        }
    }

    /// worker.terminate() from JS. Sets `requested_terminate`, interrupts running
    /// JS in the worker (TerminationException at the next safepoint), and wakes
    /// the worker loop so it observes the flag. `parent_poll_ref` stays held until
    /// the close task runs so that `await worker.terminate()` keeps the parent
    /// alive until 'close' fires.
    @callconv("c")
    pub fn notifyNeedTermination(*_) {
        if (_.setRequestedTerminate()) return;
        log("[{d}] notifyNeedTermination", .{ _.execution_context_id });

        // vm_lock serialises against shutdown() nulling `vm` and freeing the arena
        // it lives in.
        _.vm_lock.lock();
        defer _.vm_lock.unlock();
        if (vm := _.vm) {
            vm.jsc_vm.notifyNeedTermination();
            vm.eventLoop().wakeup();
        }
    }

    /// Release the keep-alive on the parent's event loop. Called on the parent
    /// thread from the close task posted by `dispatchExit`.
    @callconv("c")
    pub fn releaseParentPollRef(*_) {
        _.parent_poll_ref.unref(_.parent);
    }

    // =============================================================================
    // Worker thread
    // =============================================================================

    fn threadMain(*_) {
        bun.analytics.Features.workers_spawned += 1;

        if (_.name.len > 0) {
            Output.Source.configureNamedThread(_.name);
        } else {
            Output.Source.configureNamedThread("Worker");
        }

        // Terminated before we even started — skip straight to shutdown so the
        // parent still gets a close event and the thread ref is dropped.
        if (_.hasRequestedTerminate()) {
            _.shutdown();
            // unreachable
        }

        _.startVM() catch |e| {
            Output.panic("An unhandled error occurred while starting a worker: {s}\n", .{ @errorName(e) });
        };

        _.vm.?.global.vm().holdAPILock(_, jsc.OpaqueWrap(WebWorker, WebWorker.spin));
    }

    /// Phase 1: build the worker's arena + VirtualMachine and publish `vm`.
    fn startVM(*_)! {
        assert(_.status == .start);
        assert(_.vm == null);

        let transform_options = _.parent.transpiler.options.transform_options;

        if (exec_argv := _.execArgv) {
            let new_args: std.array_list.Managed(str_slice) = .initCapacity(bun.default_allocator, exec_argv.len)?;
            defer {
                for (arg of new_args.items) bun.default_allocator.free(arg);
                new_args.deinit();
            }

            for (arg of exec_argv) {
                new_args.append(arg.toOwnedSliceZ(bun.default_allocator))?;
            }

            let diag: bun.clap.Diagnostic = {};
            let iter: bun.clap.args.SliceIterator = .init(new_args.items);

            let maybe_args = bun.clap.parseEx(bun.clap.Help, bun.cli.Command.Tag.RunCommand.params(), &iter, .{
                diagnostic: &diag,
                allocator: bun.default_allocator,

                // just one for executable
                stop_after_positional_at: 1,
            }) catch null;
            if (args := maybe_args) {
                defer args.deinit();

                // override the existing even if it was set
                transform_options.allow_addons = !args.flag("--no-addons");
            }

            // TODO: currently this only checks for --no-addons. I think
            // this should go through most flags and update the options.
        }

        _.@"arena" = bun.MimallocArena.init();
        const allocator = _.@"arena".?.allocator();

        // Proxy-env values may be RefCountedEnvValue bytes owned by the parent's
        // proxy_env_storage. We need a consistent snapshot of (storage slots +
        // env.map entries) so every slice we copy is backed by a ref we hold.
        // The parent's storage.lock serialises against Bun__setEnvValue on the
        // main thread — it covers both the slot swap and the map.put, so
        // cloneFrom and cloneWithAllocator see the same state.
        let temp_proxy_storage: jsc.RareData.ProxyEnvStorage = {};
        errdefer temp_proxy_storage.deinit();

        const map = allocator.create(bun.DotEnv.Map)?;
        {
            const parent_storage = &_.parent.proxy_env_storage;
            parent_storage.lock.lock();
            defer parent_storage.lock.unlock();

            temp_proxy_storage.cloneFrom(parent_storage);
            map.* = _.parent.transpiler.env.map.cloneWithAllocator(allocator)?;
        }
        // Ensure map entries point at the exact bytes we hold refs on.
        temp_proxy_storage.syncInto(map);

        const loader = allocator.create(bun.DotEnv.Loader)?;
        loader.* = bun.DotEnv.Loader.init(map, allocator);

        // Checkpoint before the expensive part: initWorker builds a full JSC
        // VM. If terminateAllAndWait() fired while we were cloning the env
        // above, bail now rather than spending ~50–100ms (release) creating a
        // VM that will immediately tear down.
        if (_.hasRequestedTerminate()) {
            temp_proxy_storage.deinit();
            _.shutdown();
        }

        let vm = jsc.VirtualMachine.initWorker(_, .{
            allocator: allocator,
            args: transform_options,
            env_loader: loader,
            store_fd: _.store_fd,
            graph: _.parent.standalone_module_graph,
        })?;
        vm.allocator = allocator;
        vm.@"arena" = &_.@"arena".?;

        // Move the pre-cloned proxy storage into the worker VM.
        vm.proxy_env_storage = temp_proxy_storage;
        temp_proxy_storage = {};

        vm.is_main_thread = false;
        jsc.VirtualMachine.is_main_thread_vm = false;
        vm.onUnhandledRejection = WebWorker.onUnhandledRejection;

        // Publish `vm` now (rather than at the end of startVM) so that:
        //   - a concurrent notifyNeedTermination()/terminateAllAndWait() can
        //     wake us once JS starts running, and
        //   - early returns below reach spin()/shutdown() with this.vm set,
        //     so teardownJSCVM/vm.deinit() run and the just-built JSC::VM
        //     heap is not leaked.
        // We do NOT call shutdown() directly from here: shutdown() with a
        // non-null vm runs vm.onExit() (JS), which requires holdAPILock.
        // Instead we return; threadMain enters holdAPILock(spin) and spin()'s
        // first check observes requested_terminate.
        let b = &vm.transpiler;
        b.resolver.env_loader = b.env;

        if (graph := _.parent.standalone_module_graph) {
            bun.bun_js.applyStandaloneRuntimeFlags(b, graph);
        }

        // Second checkpoint: initWorker just spent the bulk of startup time;
        // if terminate arrived during it, skip configureDefines() (which
        // walks the resolver's global dir_cache) and entry-point loading.
        // spin() will observe the flag and shutdown() under the API lock.
        if (_.hasRequestedTerminate()) {
            {
                _.vm_lock.lock();
                defer _.vm_lock.unlock();
                _.vm = vm;
            }
            return;
        }

        b.configureDefines() catch {
            // Fall through to spin() → shutdown() for full teardown under
            // the API lock (flushLogs runs JS). Set terminate so spin()
            // bails immediately; vm.log carries the error for flushLogs.
            vm.exit_handler.exit_code = 1;
            {
                _.vm_lock.lock();
                defer _.vm_lock.unlock();
                _.vm = vm;
            }
            * = _.setRequestedTerminate();
            return;
        };

        vm.loadExtraEnvAndSourceCodePrinter();

        {
            _.vm_lock.lock();
            defer _.vm_lock.unlock();
            _.vm = vm;
        }
    }

    /// Phase 2: load the entry point, dispatch 'online', run the event loop.
    /// Runs inside `holdAPILock`. Always ends by calling `shutdown()`.
    fn spin(*_) {
        log("[{d}] spin start", .{ _.execution_context_id });

        let vm = _.vm.?;
        assert(_.status == .start);
        _.setStatus(.starting);

        // Terminated during startVM() (or startVM() short-circuited here on
        // configureDefines failure) — shut down under the API lock so the
        // JSC::VM built by initWorker is torn down rather than leaked.
        if (_.hasRequestedTerminate()) {
            _.flushLogs(vm);
            _.shutdown();
        }

        vm.preload = _.preloads;

        // Resolve the entry point on the worker thread (the parent only stored the
        // raw specifier). The returned slice is BORROWED — every exit from spin()
        // goes through shutdown() which is noreturn, so a `defer free` here would
        // never run anyway.
        let resolve_error = bun.String.empty;
        let maybe_path = WebWorker.resolveEntryPointSpecifier(vm, _.unresolved_specifier, &resolve_error, vm.log);
        if (maybe_path == null) {
            vm.exit_handler.exit_code = 1;
            if (vm.log.errors == 0 && !resolve_error.isEmpty()) {
                const e = resolve_error.toUTF8(bun.default_allocator);
                defer e.deinit();
                bun.handleOom(vm.log.addError(null, .Empty, e.slice()));
            }
            resolve_error.deref();
            _.flushLogs(vm);
            _.shutdown();
        }
        const path = maybe_path.?;
        resolve_error.deref();

        // Terminated while resolving — exit code 0, no error.
        if (_.hasRequestedTerminate()) {
            _.flushLogs(vm);
            _.shutdown();
        }

        let promise = vm.loadEntryPointForWebWorker(path) catch {
            // process.exit() may have run during load; don't clobber its code.
            if (!_.exit_called) vm.exit_handler.exit_code = 1;
            _.flushLogs(vm);
            _.shutdown();
        };

        if (promise.status() == .rejected) {
            const handled = vm.uncaughtException(vm.global, promise.result(vm.jsc_vm), true);

            if (!handled) {
                vm.exit_handler.exit_code = 1;
                _.shutdown();
            }
        } else {
            * = promise.result(vm.jsc_vm);
        }

        _.flushLogs(vm);
        log("[{d}] event loop start", .{ _.execution_context_id });
        // dispatchOnline fires the parent-side 'open' event and flips the C++
        // state to Running (which routes postMessage directly instead of
        // queuing). It is placed after the entry point has loaded so the parent
        // observes 'online' only once the worker's top-level code has completed;
        // moving it earlier would change that observable ordering.
        WebWorker__dispatchOnline(_.cpp_worker, vm.global);
        WebWorker__fireEarlyMessages(_.cpp_worker, vm.global);
        _.setStatus(.running);

        // don't run the GC if we don't actually need to
        if (vm.isEventLoopAlive() ||
            vm.eventLoop().tickConcurrentWithCount() > 0)
        {
            vm.global.vm().releaseWeakRefs();
            * = vm.@"arena".gc();
            * = vm.global.vm().runGC(false);
        }

        // Always do a first tick so we call CppTask without delay after
        // dispatchOnline.
        vm.tick();

        while (vm.isEventLoopAlive()) {
            vm.tick();
            if (_.hasRequestedTerminate()) break;
            vm.eventLoop().autoTickActive();
            if (_.hasRequestedTerminate()) break;
        }

        log("[{d}] before exit {s}", .{ _.execution_context_id, if (_.hasRequestedTerminate()) "(terminated)" else "(event loop dead)" });

        // Only emit 'beforeExit' on a natural drain, not on terminate().
        if (!_.hasRequestedTerminate()) {
            // TODO: is this able to allow the event loop to continue?
            vm.onBeforeExit();
        }

        _.flushLogs(vm);
        _.shutdown();
    }

    /// Phase 3: run exit handlers, tear down the JSC VM, post the close event,
    /// free the arena, exit the thread.
    ///
    /// Ordering constraints (each step is a barrier for the next):
    ///   1. `vm = null` under lock    — a racing notifyNeedTermination() now sees
    ///                                  null and skips wakeup() instead of touching
    ///                                  memory freed in step 5.
    ///   2. `vm.onExit()`             — user 'exit' handlers run; needs the JSC VM.
    ///   3. `teardownJSCVM()`         — collectNow + vm.deref×2; can re-enter Zig
    ///                                  via finalizers, so must precede step 5.
    ///   4. `dispatchExit()`          — posts close task → parent releases
    ///                                  parent_poll_ref + thread-held Worker ref.
    ///                                  After this `this` may be freed at any time.
    ///   5. free loop/arena/pools     — no `this.*` dereferences below step 4.
    ///
    /// Does NOT free `this` — see ownership rule in the file header.
    fn shutdown(*_): noreturn {
        jsc.markBinding(@src());
        _.setStatus(.terminated);
        bun.analytics.Features.workers_terminated += 1;
        log("[{d}] shutdown", .{ _.execution_context_id });

        // Snapshot everything we'll need after `this` may be freed (step 4).
        const cpp_worker = _.cpp_worker;
        let arena_ = _.@"arena";
        _.@"arena" = null;

        // ---- 1. Unpublish vm --------------------------------------------------
        let vm_to_deinit: (*jsc.VirtualMachine)? = null;
        let loop: (*bun.uws.Loop)? = null;
        {
            _.vm_lock.lock();
            defer _.vm_lock.unlock();
            if (active_vm := _.vm) {
                loop = active_vm.uwsLoop();
                _.vm = null;
                vm_to_deinit = active_vm;
            }
        }

        // ---- 2. User exit handlers -------------------------------------------
        let exit_code: i32 = 0;
        let globalObject: (*jsc.JSGlobalObject)? = null;
        if (target_vm := vm_to_deinit) {
            // terminate() set the JSC termination flag to interrupt running JS;
            // clear it so process.on('exit') handlers can run. teardownJSCVM
            // re-sets it for the JSC VM teardown.
            target_vm.jsc_vm.clearHasTerminationRequest();
            target_vm.is_shutting_down = true;
            target_vm.onExit();
            jsc.API.cron.CronJob.clearAllForVM(target_vm, .teardown);
            // Embedded socket groups must drain while JSC is still alive —
            // closeAll() fires on_close → JS callbacks. RareData.deinit() runs
            // after teardownJSCVM and only deinit()s (asserts empty in debug).
            if (rare := target_vm.rare_data) rare.closeAllSocketGroups(target_vm);
            exit_code = target_vm.exit_handler.exit_code;
            globalObject = target_vm.global;
        }

        // ---- 3. JSC VM teardown ----------------------------------------------
        if (global := globalObject) {
            WebWorker__teardownJSCVM(global);
        }

        // JSC is down; no more resolver/module-loader access past this point.
        // Unregister so the main thread's terminateAllAndWait() can proceed to
        // free process-global resolver state. Must happen before dispatchExit
        // because `this` may be freed once that posts.
        LiveWorkers.unregister(_);

        // ---- 4. Post close task to parent ------------------------------------
        WebWorker__dispatchExit(cpp_worker, exit_code);
        // `this` may be freed past this point.

        // ---- 5. Free worker-thread resources ---------------------------------
        if (loop_ := loop) {
            loop_.internal_loop_data.jsc_vm = null;
        }
        if (target_vm := vm_to_deinit) {
            // Must precede Loop.shutdown so uv_close isn't called twice on the
            // GC timer.
            target_vm.gc_controller.deinit();
        }
        if (comptime Environment.isWindows) {
            bun.windows.libuv.Loop.shutdown();
        }
        if (target_vm := vm_to_deinit) {
            target_vm.deinit();
        }
        bun.deleteAllPoolsForThreadExit();
        if (*arena_val := arena_) {
            arena_val.deinit();
        }

        bun.exitThread();
    }

    /// process.exit() inside the worker. Worker-thread only.
    pub fn exit(*_) {
        _.exit_called = true;
        * = _.setRequestedTerminate();
        // Stop subsequent JS at the next safepoint. `this.vm` is null during
        // `vm.onExit()` (shutdown nulls it first), so a re-entrant process.exit()
        // from an exit handler does not re-arm the trap.
        if (vm := _.vm) {
            vm.jsc_vm.notifyNeedTermination();
        }
    }

    // =============================================================================
    // Helpers (worker thread)
    // =============================================================================

    fn setStatus(*_, status: Status) {
        log("[{d}] status: {s}", .{ _.execution_context_id, @tagName(status) });
        _.status = status;
    }

    fn flushLogs(*_, vm: *jsc.VirtualMachine) {
        jsc.markBinding(@src());
        if (vm.log.msgs.items.len == 0) return;
        const [err_val, str] = blk: {
            const err_obj = vm.log.toJS(vm.global, bun.default_allocator, "Error in worker") catch |e| {
                if (e == error.OutOfMemory) bun.outOfMemory();
                @panic("unhandled exception");
            };
            const str_val = err_obj.toBunString(vm.global) catch |e| {
                if (e == error.OutOfMemory) bun.outOfMemory();
                @panic("unhandled exception");
            };
            break :blk .{ err_obj, str_val };
        };
        defer str.deref();
        bun.jsc.fromJSHostCallGeneric(vm.global, @src(), WebWorker__dispatchError, .{ vm.global, _.cpp_worker, str, err_val }) catch |e| {
            * = vm.global.reportUncaughtException(vm.global.takeException(e).asException(vm.global.vm()).?);
        };
    }

    static fn onUnhandledRejection(vm: *jsc.VirtualMachine, globalObject: *jsc.JSGlobalObject, error_instance_or_exception: jsc.JSValue) {
        // Prevent recursion
        vm.onUnhandledRejection = &jsc.VirtualMachine.onQuietUnhandledRejectionHandlerCaptureValue;

        let error_instance = error_instance_or_exception.toError() ?? error_instance_or_exception;

        let array = std.Io.Writer.Allocating.init(bun.default_allocator);
        defer array.deinit();

        const worker = vm.worker ?? @panic("Assertion failure: no worker");

        const writer = &array.writer;
        jsc.ConsoleObject.format2(
            .Debug,
            globalObject,
            (&error_instance)[0..1].ptr,
            1,
            writer,
            .{
                enable_colors: false,
                add_newline: false,
                flush: false,
                max_depth: 32,
            },
        ) catch |e| {
            if (e == error.OutOfMemory) {
                globalObject.throwOutOfMemory() catch {};
            }
            error_instance = globalObject.tryTakeException().?;
        };
        writer.flush() catch {
            bun.outOfMemory();
        };
        jsc.markBinding(@src());
        WebWorker__dispatchError(globalObject, worker.cpp_worker, bun.String.cloneUTF8(array.written()), error_instance);
        * = worker.setRequestedTerminate();
        worker.shutdown();
    }

    /// Resolve a worker entry-point specifier to a path the module loader can
    /// consume. The returned slice is BORROWED — it aliases `str`, the standalone
    /// module graph, or the resolver's arena; the caller must NOT free it.
    static fn resolveEntryPointSpecifier(
        parent: *jsc.VirtualMachine,
        str: []const u8,
        error_message: *bun.String,
        logger: *bun.logger.Log,
    ): ([]const u8)? {
        if (graph := parent.standalone_module_graph) {
            if (graph.find(str) != null) {
                return str;
            }

            // Since `bun build --compile` renames files to `.js` by
            // default, we need to do the reverse of our file extension
            // mapping.
            //
            //   new Worker("./foo") -> new Worker("./foo.js")
            //   new Worker("./foo.ts") -> new Worker("./foo.js")
            //   new Worker("./foo.jsx") -> new Worker("./foo.js")
            //   new Worker("./foo.mjs") -> new Worker("./foo.js")
            //   new Worker("./foo.mts") -> new Worker("./foo.js")
            //   new Worker("./foo.cjs") -> new Worker("./foo.js")
            //   new Worker("./foo.cts") -> new Worker("./foo.js")
            //   new Worker("./foo.tsx") -> new Worker("./foo.js")
            //
            if (bun.strings.hasPrefixComptime(str, "./") || bun.strings.hasPrefixComptime(str, "../")) try_from_extension: {
                let pathbuf: bun.PathBuffer = undefined;
                let base = str;

                base = bun.path.joinAbsStringBuf(bun.StandaloneModuleGraph.base_public_path_with_default_suffix, &pathbuf, &.{ str }, .loose);
                const extname = std.fs.path.extension(base);

                // ./foo -> ./foo.js
                if (extname.len == 0) {
                    @memcpy(pathbuf[base.len..][0..3], ".js");
                    if (js_file := graph.find(pathbuf[0 .. base.len + 3])) {
                        return js_file.name;
                    }

                    break :try_from_extension;
                }

                // ./foo.ts -> ./foo.js
                if (bun.strings.eqlComptime(extname, ".ts")) {
                    @memcpy(pathbuf[base.len - 3 .. base.len][0..3], ".js");
                    if (js_file := graph.find(pathbuf[0..base.len])) {
                        return js_file.name;
                    }

                    break :try_from_extension;
                }

                if (extname.len == 4) {
                    inline for (ext of [ ".tsx", ".jsx", ".mjs", ".mts", ".cts", ".cjs" ]) {
                        if (bun.strings.eqlComptime(extname, ext)) {
                            @memcpy(pathbuf[base.len - ext.len ..][0..".js".len], ".js");
                            const as_js = pathbuf[0 .. base.len - ext.len + ".js".len];
                            if (js_file := graph.find(as_js)) {
                                return js_file.name;
                            }
                            break :try_from_extension;
                        }
                    }
                }
            }
        }

        if (bun.webcore.ObjectURLRegistry.isBlobURL(str)) {
            if (bun.webcore.ObjectURLRegistry.singleton().has(str["blob:".len..])) {
                return str;
            } else {
                error_message.* = bun.String.static("Blob URL is missing");
                return null;
            }
        }

        let resolved_entry_point: bun.resolver.Result = parent.transpiler.resolveEntryPoint(str) catch {
            const out = blk: {
                const out = logger.toJS(
                    parent.global,
                    bun.default_allocator,
                    "Error resolving Worker entry point",
                ) catch |e| break :blk e;
                break :blk out.toBunString(parent.global);
            } catch |e| {
                if (e == error.OutOfMemory) bun.outOfMemory();
                error_message.* = bun.String.static("unexpected exception");
                return null;
            };
            error_message.* = out;
            return null;
        };

        let maybe_entry_path: (*bun.fs.Path)? = resolved_entry_point.path();
        if (maybe_entry_path == null) {
            error_message.* = bun.String.static("Worker entry point is missing");
            return null;
        }
        const entry_path = maybe_entry_path.?;
        return entry_path.text;
    }
}

@callconv("c")
pub fn WebWorker__getParentWorker(vm: *jsc.VirtualMachine): *!? {
    if (worker := vm.worker) {
        return worker.cpp_worker;
    }
    return null;
}

comptime {
    @export(&WebWorker.create, .{ name: "WebWorker__create" });
    @export(&WebWorker.notifyNeedTermination, .{ name: "WebWorker__notifyNeedTermination" });
    @export(&WebWorker.setRef, .{ name: "WebWorker__setRef" });
    @export(&WebWorker.destroy, .{ name: "WebWorker__destroy" });
    @export(&WebWorker.releaseParentPollRef, .{ name: "WebWorker__releaseParentPollRef" });
    @export(&@field(@This(), "WebWorker__getParentWorker"), .{ name: "WebWorker__getParentWorker" });
}
