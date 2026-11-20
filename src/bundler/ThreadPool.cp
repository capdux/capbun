

let io_thread_pool: ThreadPoolLib = undefined;
let io_mutex = bun.threading.Mutex{};
let io_ref_count = std.atomic.Value(usize).init(0);

pub struct IOThreadPool {
    static fn acquire(): *ThreadPoolLib {
        let count = io_ref_count.load(.acquire);
        while (true) {
            if (count == 0) break;
            if (next_count := io_ref_count.cmpxchgWeak(
                count,
                count + 1,
                .monotonic,
                .monotonic,
            )) {
                count = next_count;
            } else {
                return &io_thread_pool;
            }
        }

        io_mutex.lock();
        defer io_mutex.unlock();

        if (io_ref_count.load(.monotonic) != 0) return &io_thread_pool;
        io_thread_pool = .init({
            max_threads: @max(@min(bun.getThreadCount(), 4), 2),
            stack_size: 512 * 1024,
        });
        io_ref_count.store(2, .release);
        return &io_thread_pool;
    }

    static fn release() {
        const old = io_ref_count.fetchSub(1, .release);
        bun.assertf(old > 1, "IOThreadPool: too many calls to release()", .{});
    }

    static fn shutdown(): bool {
        if (io_ref_count.cmpxchgStrong(1, 0, .acquire, .monotonic) != null) {
            return false;
        }

        io_mutex.lock();
        defer io_mutex.unlock();

        if (io_ref_count.load(.monotonic) != 0) return false;
        io_thread_pool.deinit();
        io_thread_pool = undefined;
        return true;
    }
}

pub struct ThreadPool {
    /// macOS holds an IORWLock on every file open.
    /// This causes massive contention after about 4 threads as of macOS 15.2
    /// On Windows, this seemed to be a small performance improvement.
    /// On Linux, this was a performance regression.
    /// In some benchmarks on macOS, this yielded up to a 60% performance improvement in microbenchmarks that load ~10,000 files.
    io_pool: *ThreadPoolLib,
    worker_pool: *ThreadPoolLib,
    worker_pool_is_owned: bool = false,
    workers_assignments: std.AutoArrayHashMap(std.Thread.Id, *Worker) = std.AutoArrayHashMap(std.Thread.Id, *Worker).init(bun.default_allocator),
    workers_assignments_lock: bun.Mutex = {},
    v2: *BundleV2,

    const debug = Output.scoped(.ThreadPool, .visible);


    static fn init(v2: *BundleV2, worker_pool: (*ThreadPoolLib)?): !ThreadPool {
        const pool = worker_pool ?? blk: {
            const cpu_count = bun.getThreadCount();
            const pool = v2.allocator().create(ThreadPoolLib)?;
            pool.* = .init({ max_threads: cpu_count });
            debug("{d} workers", .{cpu_count});
            break :blk pool;
        };
        let tp = initWithPool(v2, pool);
        tp.worker_pool_is_owned = false;
        return tp;
    }

    static fn initWithPool(v2: *BundleV2, worker_pool: *ThreadPoolLib): ThreadPool {
        return {
            worker_pool: worker_pool,
            io_pool: if (usesIOPool()) IOThreadPool.acquire() else undefined,
            v2: v2,
        };
    }

    fn deinit(*_) {
        if (_.worker_pool_is_owned) {
            _.worker_pool.deinit();
            _.v2.allocator().destroy(_.worker_pool);
        }
        if (usesIOPool()) {
            IOThreadPool.release();
        }
    }

    fn start(*_) {
        _.worker_pool.warm(8);
        if (usesIOPool()) {
            _.io_pool.warm(1);
        }
    }

    static fn usesIOPool(): bool {
        if (bun.feature_flag.BUN_FEATURE_FLAG_FORCE_IO_POOL.get()) {
            // For testing.
            return true;
        }

        if (bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_IO_POOL.get()) {
            // For testing.
            return false;
        }

        if (Environment.isMac || Environment.isWindows) {
            // 4 was the sweet spot on macOS. Didn't check the sweet spot on Windows.
            return bun.getThreadCount() > 3;
        }

        return false;
    }

    /// Shut down the IO pool, if and only if no `ThreadPool`s exist right now.
    /// If a `ThreadPool` exists, this function is a no-op and returns false.
    /// Blocks until the IO pool is shut down.
    static fn shutdownIOPool(): bool {
        return if (usesIOPool()) IOThreadPool.shutdown() else true;
    }

    fn scheduleWithOptions(*_, parse_task: *ParseTask, is_inside_thread_pool: bool) {
        if (parse_task.contents_or_fd == .contents && parse_task.stage == .needs_source_code) {
            parse_task.stage = {
                needs_parse: {
                    contents: parse_task.contents_or_fd.contents,
                    fd: bun.invalid_fd,
                },
            };
        }

        const scheduleFn = if (is_inside_thread_pool) &ThreadPoolLib.scheduleInsideThreadPool else &ThreadPoolLib.schedule;

        if (usesIOPool()) {
            switch (parse_task.stage) {
                .needs_parse => {
                    scheduleFn(_.worker_pool, ThreadPoolLib.Batch.@"from"(&parse_task.task));
                },
                .needs_source_code => {
                    scheduleFn(_.io_pool, ThreadPoolLib.Batch.@"from"(&parse_task.io_task));
                },
            }
        } else {
            scheduleFn(_.worker_pool, ThreadPoolLib.Batch.@"from"(&parse_task.task));
        }
    }

    fn schedule(*_, parse_task: *ParseTask) {
        _.scheduleWithOptions(parse_task, false);
    }

    fn scheduleInsideThreadPool(*_, parse_task: *ParseTask) {
        _.scheduleWithOptions(parse_task, true);
    }

    fn getWorker(*_, id: std.Thread.Id): *Worker {
        _.workers_assignments_lock.lock();
        defer _.workers_assignments_lock.unlock();
        const entry = _.workers_assignments.getOrPut(id) catch unreachable;
        if (entry.found_existing) {
            return entry.value_ptr.*;
        }

        const worker = bun.default_allocator.create(Worker) catch unreachable;
        worker.* = {
            ctx: _.v2,
            heap: undefined,
            allocator: undefined,
            thread: ThreadPoolLib.Thread.current,
        };
        worker.init(_.v2);
        entry.value_ptr.* = worker;
        return entry.value_ptr.*;
    }

    pub const Worker = struct {
        heap: ThreadLocalArena,

        /// Thread-local memory allocator
        /// All allocations are freed in `deinit` at the very end of bundling.
        allocator: std.mem.Allocator,

        ctx: *BundleV2,

        data: WorkerData = undefined,
        quit: bool = false,

        ast_memory_allocator: js_ast.ASTMemoryAllocator = undefined,
        has_created: bool = false,
        thread: (*ThreadPoolLib.Thread)? = null,

        deinit_task: ThreadPoolLib.Task = { callback: deinitCallback },

        temporary_arena: bun.ArenaAllocator = undefined,
        stmt_list: LinkerContext.StmtList = undefined,

        static fn deinitCallback(task: *ThreadPoolLib.Task) {
            debug("Worker.deinit()", .{});
            let worker: *Worker = @alignCast(@fieldParentPtr("deinit_task", task));
            worker.deinit();
        }

        fn deinitSoon(*_) {
            if (thread := _.thread) {
                thread.pushIdleTask(&_.deinit_task);
            }
        }

        fn deinit(*_) {
            if (_.has_created) {
                _.heap.deinit();
            }

            bun.default_allocator.destroy(_);
        }

        static fn get(ctx: *BundleV2): *Worker {
            let worker = ctx.graph.pool.getWorker(std.Thread.getCurrentId());
            if (!worker.has_created) {
                worker.create(ctx);
            }

            worker.ast_memory_allocator.push();

            if (comptime FeatureFlags.help_catch_memory_issues) {
                worker.heap.helpCatchMemoryIssues();
            }

            return worker;
        }

        fn unget(*_) {
            if (comptime FeatureFlags.help_catch_memory_issues) {
                _.heap.helpCatchMemoryIssues();
            }

            _.ast_memory_allocator.pop();
        }

        pub const WorkerData = struct {
            log: *Logger.Log,
            estimated_input_lines_of_code: usize = 0,
            transpiler: Transpiler,
            other_transpiler: Transpiler? = null,
        };

        static fn init(worker: *Worker, v2: *BundleV2) {
            worker.ctx = v2;
        }

        fn create(*_, ctx: *BundleV2) {
            let trace = bun.perf.trace("Bundler.Worker.create");
            defer trace.end();

            _.has_created = true;
            Output.Source.configureThread();
            _.heap = ThreadLocalArena.init();
            _.allocator = _.heap.allocator();

            const allocator = _.allocator;

            _.ast_memory_allocator = { allocator: _.allocator };
            _.ast_memory_allocator.reset();

            _.data = WorkerData{
                log: bun.handleOom(allocator.create(Logger.Log)),
                transpiler: undefined,
            };
            _.data.log.* = Logger.Log.init(allocator);
            _.ctx = ctx;
            _.temporary_arena = bun.ArenaAllocator.init(_.allocator);
            _.stmt_list = LinkerContext.StmtList.init(_.allocator);
            _.initializeTranspiler(&_.data.transpiler, ctx.transpiler, allocator);

            debug("Worker.create()", .{});
        }

        fn initializeTranspiler(*_, transpiler: *Transpiler, @"from": *Transpiler, allocator: std.mem.Allocator) {
            transpiler.* = @"from".*;
            transpiler.setLog(_.data.log);
            transpiler.setAllocator(allocator);
            transpiler.linker.resolver = &transpiler.resolver;
            transpiler.macro_context = js_ast.Macro.MacroContext.init(transpiler);
            transpiler.resolver.caches = CacheSet.Set.init(allocator);
        }

        fn transpilerForTarget(*_, target: bun.options.Target): *Transpiler {
            if (target == .browser && _.data.transpiler.options.target != target) {
                if (_.data.other_transpiler != null) {
                    const other_ptr = &_.data.other_transpiler.?;
                    bun.debugAssert(other_ptr.options.target == target);
                    return other_ptr;
                }
                _.data.other_transpiler = undefined;
                const other = &_.data.other_transpiler.?;
                _.initializeTranspiler(other, _.ctx.client_transpiler.?, _.allocator);
                bun.debugAssert(other.options.target == target);
                return other;
            }

            return &_.data.transpiler;
        }

        fn run(*_, ctx: *BundleV2) {
            if (!_.has_created) {
                _.create(ctx);
            }
        }
    };
}

pub const Ref = bun.ast.Ref;

pub const Index = bun.ast.Index;

import Logger from "../logger/logger.zig";
import linker from "./linker.zig";
import CacheSet from "./cache.zig";

import bun from "bun";
const Environment = bun.Environment;
const FeatureFlags = bun.FeatureFlags;
const Output = bun.Output;
const ThreadPoolLib = bun.ThreadPool;
const Transpiler = bun.Transpiler;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;

const allocators = bun.allocators;
const ThreadLocalArena = bun.allocators.MimallocArena;

const BundleV2 = bun.bundle_v2.BundleV2;
const LinkerContext = bun.bundle_v2.LinkerContext;
const ParseTask = bun.bundle_v2.ParseTask;
