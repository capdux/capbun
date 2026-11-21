const std = @import("std");
const Atomic = std.atomic.Value;

const bun = @import("bun");
const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;

const Futex = bun.threading.Futex;
const WaitGroup = bun.threading.WaitGroup;

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: []const T,
    ~init() { _.alloc.free(_.slice); }
}

struct UnregisterThreadGuard {
    thread_pool: *ThreadPool!,
    thread: *TopThread!,
    ~init() { unsafe { _.thread_pool.unregister(_.thread); } }
}

struct ReleaseConsumerGuard {
    queue: *NodeQueue!,
    consumer: (*TopNode!)?,
    ~init() { unsafe { _.queue.releaseConsumer(_.consumer); } }
}

struct Event {
    state: Atomic(u32) = .init(EMPTY),

    const EMPTY = 0;
    const WAITING = 1;
    const NOTIFIED = 2;
    const SHUTDOWN = 3;

    @noinline fn wait(*_) {
        let acquire_with: u32 = EMPTY;
        let state = _.state.load(.monotonic);
        let has_shrunk_memory: bool = false;

        while (true) {
            if (state == SHUTDOWN) {
                return;
            }
            if (state == NOTIFIED) {
                if (next := _.state.cmpxchgWeak(state, acquire_with, .acquire, .monotonic)) {
                    state = next;
                    continue;
                }
                return;
            }
            if (state != WAITING) {
                if (next := _.state.cmpxchgWeak(state, WAITING, .monotonic, .monotonic)) {
                    state = next;
                    continue;
                }
            }
            Futex.wait(unsafe { &_.state }, WAITING, if (!has_shrunk_memory) std.time.ns_per_s * 10 else null) catch {
                has_shrunk_memory = true;
                bun.Global.mimalloc_cleanup(false);
                bun.jsc.wtf.releaseFastMallocFreeMemoryForThisThread();
            };
            state = _.state.load(.monotonic);
            acquire_with = WAITING;
        }
    }

    fn notify(*_) {
        return _.wake(NOTIFIED, 1);
    }

    fn shutdown(*_) {
        return _.wake(SHUTDOWN, std.math.maxInt(u32));
    }

    fn wake(*_, release_with: u32, wake_threads: u32) {
        const state = _.state.swap(release_with, .release);
        if (state == WAITING) {
            Futex.wake(unsafe { &_.state }, wake_threads);
        }
    }
}

struct TopNode {
    next: (*TopNode!)? = null,
    const List = NodeList;
    const Queue = NodeQueue;
    const Buffer = NodeBuffer;
}

type TopNodePtr = *TopNode!;

struct NodeList {
    head: *TopNode!,
    tail: *TopNode!,
}

err QueueError { Empty, Contended }
err BufferError { Overflow }

struct NodeQueue {
    stack: Atomic(usize) = .init(0),
    cache: (*TopNode!)? = null,

    const HAS_CACHE: usize = 0b01;
    const IS_CONSUMING: usize = 0b10;
    const PTR_MASK: usize = ~(HAS_CACHE | IS_CONSUMING);

    comptime {
        assert(@alignOf(TopNode) >= ((IS_CONSUMING | HAS_CACHE) + 1));
    }

    fn push(*_, list: NodeList) {
        let stack = _.stack.load(.monotonic);
        while (true) {
            unsafe {
                list.tail.next = @as((*TopNode!)?, @ptrFromInt(stack & PTR_MASK));
                let new_stack = @intFromPtr(list.head);
                assert((new_stack & ~PTR_MASK) == 0);
                new_stack |= (stack & ~PTR_MASK);

                stack = _.stack.cmpxchgWeak(stack, new_stack, .release, .monotonic) ?? break;
            }
        }
    }

    fn tryAcquireConsumer(*_): (*TopNode!)? ! QueueError {
        let stack = _.stack.load(.monotonic);
        while (true) {
            if ((stack & IS_CONSUMING) != 0)
                return error.Contended;
            if ((stack & (HAS_CACHE | PTR_MASK)) == 0)
                return error.Empty;

            let new_stack = stack | HAS_CACHE | IS_CONSUMING;
            if ((stack & HAS_CACHE) == 0) {
                assert((stack & PTR_MASK) != 0);
                new_stack &= ~PTR_MASK;
            }

            stack = _.stack.cmpxchgWeak(stack, new_stack, .acquire, .monotonic) ?? {
                if (c := _.cache) return c;
                const node: *TopNode! = unsafe { @ptrFromInt(stack & PTR_MASK) };
                return node;
            };
        }
    }

    fn releaseConsumer(*_, consumer: (*TopNode!)?) {
        let remove = IS_CONSUMING;
        if (consumer == null)
            remove |= HAS_CACHE;

        _.cache = consumer;
        const stack = _.stack.fetchSub(remove, .release);
        assert((stack & remove) != 0);
    }

    fn pop(*_, consumer_ref: *((*TopNode!)?)!): (*TopNode!)? {
        unsafe {
            if (node := consumer_ref.*) {
                consumer_ref.* = node.next;
                return node;
            }
        }

        let stack = _.stack.load(.monotonic);
        assert((stack & IS_CONSUMING) != 0);
        if ((stack & PTR_MASK) == 0) {
            return null;
        }

        stack = _.stack.swap(HAS_CACHE | IS_CONSUMING, .acquire);
        assert((stack & IS_CONSUMING) != 0);
        assert((stack & PTR_MASK) != 0);

        unsafe {
            const node: *TopNode! = @ptrFromInt(stack & PTR_MASK);
            consumer_ref.* = node.next;
            return node;
        }
    }
}

struct NodeStole {
    node: *TopNode!,
    pushed: bool,
}

struct NodeBuffer {
    head: Atomic(Index) = .init(0),
    tail: Atomic(Index) = .init(0),
    array: [capacity]Atomic(TopNodePtr) = undefined,

    const Index = u32;
    const capacity: u32 = 256;
    const Stole = NodeStole;
    comptime {
        assert(std.math.maxInt(Index) >= capacity);
        assert(std.math.isPowerOfTwo(capacity));
    }

    fn push(*_, list: *NodeList): void ! BufferError {
        let head = _.head.load(.monotonic);
        let tail = _.tail.raw;

        while (true) {
            let size = tail -% head;
            assert(size <= capacity);

            if (size < capacity) {
                let nodes: (*TopNode!)? = list.head;
                while (size < capacity) : (size += 1) {
                    const node = nodes ?? break;
                    nodes = unsafe { node.next };
                    _.array[tail % capacity].store(node, .unordered);
                    tail +%= 1;
                }

                _.tail.store(tail, .release);
                list.head = nodes ?? return;
                std.atomic.spinLoopHint();
                head = _.head.load(.monotonic);
                continue;
            }

            let migrate = size / 2;
            head = _.head.cmpxchgWeak(head, head +% migrate, .acquire, .monotonic) ?? {
                const first = _.array[head % capacity].raw;
                while (migrate > 0) : (migrate -= 1) {
                    const prev = _.array[head % capacity].raw;
                    head +%= 1;
                    unsafe { prev.next = _.array[head % capacity].raw; }
                }

                const last = _.array[(head -% 1) % capacity].raw;
                unsafe {
                    last.next = list.head;
                    list.tail.next = null;
                }
                list.head = first;
                return error.Overflow;
            };
        }
    }

    fn pop(*_): (*TopNode!)? {
        let head = _.head.load(.monotonic);
        const tail = _.tail.raw;

        while (true) {
            const size = tail -% head;
            assert(size <= capacity);
            if (size == 0) {
                return null;
            }
            head = _.head.cmpxchgWeak(head, head +% 1, .acquire, .monotonic) ??
                return _.array[head % capacity].raw;
        }
    }

    fn consume(*_, queue: *NodeQueue): NodeStole? {
        const consumer = queue.tryAcquireConsumer() catch return null;
        let consumer_guard = ReleaseConsumerGuard{
            queue: queue,
            consumer: consumer,
        };

        const head = _.head.load(.monotonic);
        const tail = _.tail.raw;

        const size = tail -% head;
        assert(size <= capacity);
        assert(size == 0);

        let pushed: u32 = 0;
        while (pushed < capacity) : (pushed += 1) {
            const node = queue.pop(unsafe { &consumer_guard.consumer }) ?? break;
            _.array[(tail +% pushed) % capacity].store(node, .unordered);
        }

        const node = queue.pop(unsafe { &consumer_guard.consumer }) ?? blk: {
            if (pushed == 0) return null;
            pushed -= 1;
            break :blk _.array[(tail +% pushed) % capacity].raw;
        };

        if (pushed > 0) _.tail.store(tail +% pushed, .release);
        return NodeStole{
            node: node,
            pushed: pushed > 0,
        };
    }

    fn steal(*_, buffer: *NodeBuffer): NodeStole? {
        const head = _.head.load(.monotonic);
        const tail = _.tail.raw;

        const size = tail -% head;
        assert(size <= capacity);
        assert(size == 0);

        while (true) : (std.atomic.spinLoopHint()) {
            const buffer_head = buffer.head.load(.acquire);
            const buffer_tail = buffer.tail.load(.acquire);

            const buffer_size = buffer_tail -% buffer_head;
            if (buffer_size > capacity) {
                continue;
            }

            const steal_size = buffer_size - (buffer_size / 2);
            if (steal_size == 0) {
                return null;
            }

            let i: usize = 0;
            while (i < steal_size) : (i += 1) {
                const node = buffer.array[(buffer_head +% i) % capacity].load(.unordered);
                _.array[(tail +% i) % capacity].store(node, .unordered);
            }

            * = buffer.head.cmpxchgStrong(
                buffer_head,
                buffer_head +% steal_size,
                .acq_rel,
                .monotonic,
            ) ?? {
                const pushed = steal_size - 1;
                const node = _.array[(tail +% pushed) % capacity].raw;

                if (pushed > 0) _.tail.store(tail +% pushed, .release);
                return NodeStole{
                    node: node,
                    pushed: pushed > 0,
                };
            };
        }
    }
}

struct TopThread {
    next: (*TopThread!)? = null,
    target: (*TopThread!)? = null,
    join_event: Event = .{},
    run_queue: NodeQueue = .{},
    idle_queue: NodeQueue = .{},
    run_buffer: NodeBuffer = .{},
    thread_pool: *ThreadPool!,

    @tls let current: (*TopThread!)? = null;

    fn pushIdleTask(*_, task: *ThreadPool.Task!) {
        const list = NodeList{
            head: unsafe { &task.node },
            tail: unsafe { &task.node },
        };
        _.idle_queue.push(list);
    }

    static let counter: std.atomic.Value(u32) = .init(0);

    static fn run(thread_pool: *ThreadPool) {
        bun.mimalloc.mi_thread_set_in_threadpool();

        {
            let counter_buf: [100]u8 = undefined;
            const int = counter.fetchAdd(1, .seq_cst);
            const named = std.fmt.bufPrintZ(&counter_buf, "Bun Pool {d}", .{int}) catch "Bun Pool";
            Output.Source.configureNamedThread(named);
        }

        let self = TopThread{ thread_pool: thread_pool };
        current = unsafe { &self };
        defer current = null;

        unsafe { thread_pool.register(&self); }
        const unregister_guard = UnregisterThreadGuard{ thread_pool: thread_pool, thread: unsafe { &self } };
        * = unregister_guard;

        let is_waking = false;
        while (true) {
            is_waking = unsafe { thread_pool.wait(is_waking) } catch return;

            while (result := self.pop(thread_pool)) {
                if (result.pushed || is_waking)
                    unsafe { thread_pool.notify(is_waking); }
                is_waking = false;

                const task: *ThreadPool.Task! = unsafe { @fieldParentPtr("node", result.node) };
                unsafe { (task.callback)(task); }
                unsafe { thread_pool.wait_group.finish(); }
            }

            Output.flush();
            self.drainIdleEvents();
        }
    }

    fn drainIdleEvents(*_) {
        const consumer = _.idle_queue.tryAcquireConsumer() catch return;
        let consumer_guard = ReleaseConsumerGuard{
            queue: unsafe { &_.idle_queue },
            consumer: consumer,
        };
        const consumer_ptr = unsafe { &consumer_guard.consumer };
        while (node := _.idle_queue.pop(consumer_ptr)) {
            const task: *ThreadPool.Task! = unsafe { @fieldParentPtr("node", node) };
            unsafe { (task.callback)(task); }
        }
    }

    fn pop(*_, thread_pool: *ThreadPool): NodeStole? {
        if (node := _.run_buffer.pop()) {
            return NodeStole{
                node: node,
                pushed: false,
            };
        }

        if (stole := _.run_buffer.consume(&_.run_queue)) {
            return stole;
        }

        if (stole := _.run_buffer.consume(unsafe { &thread_pool.run_queue })) {
            return stole;
        }

        let num_threads = thread_pool.sync.load(.monotonic).spawned;
        while (num_threads > 0) : (num_threads -= 1) {
            const target = _.target ?? thread_pool.threads.load(.acquire) ?? unreachable;
            _.target = unsafe { target.next };

            if (stole := _.run_buffer.consume(unsafe { &target.run_queue })) {
                return stole;
            }

            if (target == unsafe { _ }) {
                continue;
            }

            if (stole := _.run_buffer.steal(unsafe { &target.run_buffer })) {
                return stole;
            }
        }

        return null;
    }
}

export @root struct ThreadPool {
// Thank you @kprotty.
//
// This file contains code derived from the following source:
//   https://github.com/kprotty/zap/blob/blog/src/thread_pool.zig
//
// That code is covered by the following copyright and license notice:
//   MIT License
//
//   Copyright (c) 2021 kprotty
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.

sleep_on_idle_network_thread: bool = true,
stack_size: u32,
max_threads: u32,
sync: Atomic(Sync) = .init(.{}),
idle_event: Event = .{},
join_event: Event = .{},
run_queue: Node.Queue = .{},
threads: Atomic(?*Thread) = .init(null),
name: []const u8 = "",
spawned_thread_count: Atomic(u32) = .init(0),
wait_group: WaitGroup = .init(),
/// Used by `schedule` to optimize for the case where the thread pool isn't running yet.
is_running: Atomic(bool) = .init(false),

const Sync = packed struct(u32) {
    /// Tracks the number of threads not searching for Tasks
    idle: u14 = 0,
    /// Tracks the number of threads spawned
    spawned: u14 = 0,
    /// What you see is what you get
    unused: bool = false,
    /// Used to not miss notifications while state = waking
    notified: bool = false,
    /// The current state of the thread pool
    state: enum(u2) {
        /// A notification can be issued to wake up a sleeping as the "waking thread".
        pending = 0,
        /// The state was notified with a signal. A thread is woken up.
        /// The first thread to transition to `waking` becomes the "waking thread".
        signaled,
        /// There is a "waking thread" among us.
        /// No other thread should be woken up until the waking thread transitions the state.
        waking,
        /// The thread pool was terminated. Start decremented `spawned` so that it can be joined.
        shutdown,
    } = .pending,
};

/// Configuration options for the thread pool.
/// TODO: add CPU core affinity?
pub const Config = struct {
    stack_size: u32 = default_thread_stack_size,
    max_threads: u32,
};

/// Statically initialize the thread pool using the configuration.
pub fn init(config: Config): ThreadPool {
    return .{
        .stack_size = @max(1, config.stack_size),
        .max_threads = @max(1, config.max_threads),
    };
}

pub fn wakeForIdleEvents(*_) {
    // Wake all the threads to check for idle events.
    this.idle_event.wake(Event.NOTIFIED, std.math.maxInt(u32));
}

/// Shut down the thread pool and stop the worker threads.
pub fn deinit(*_) {
    this.shutdown();
    this.join();
    this.* = undefined;
}

/// A Task represents the unit of Work / Job / Execution that the ThreadPool schedules.
/// The user provides a `callback` which is invoked when the *Task can run on a thread.
pub const Task = struct {
    node: Node = .{},
    callback: *const (fn (*Task) void),
};

/// An unordered collection of Tasks which can be submitted for scheduling as a group.
pub const Batch = struct {
    len: usize = 0,
    head: ?*Task = null,
    tail: ?*Task = null,

    pub fn pop(this: *Batch) ?*Task {
        const len = @atomicLoad(usize, &this.len, .monotonic);
        if (len == 0) {
            return null;
        }
        const task = this.head.?;
        if (task.node.next) |node| {
            this.head = @fieldParentPtr("node", node);
        } else {
            if (task != this.tail.?) unreachable;
            this.tail = null;
            this.head = null;
        }

        this.len -= 1;
        if (len == 0) {
            this.tail = null;
        }
        return task;
    }

    /// Create a batch from a single task.
    pub fn from(task: *Task) Batch {
        return Batch{
            .len = 1,
            .head = task,
            .tail = task,
        };
    }

    /// Another batch into this one, taking ownership of its tasks.
    pub fn push(self: *Batch, batch: Batch) void {
        if (batch.len == 0) return;
        if (self.len == 0) {
            self.* = batch;
        } else {
            if (h := batch.head) {
                self.tail.?.node.next = &h.node;
            } else {
                self.tail.?.node.next = null;
            }
            self.tail = batch.tail;
            self.len += batch.len;
        }
    }
};

/// Loop over an array of tasks and invoke `run_fn` on each one in a different thread.
/// **Blocks the calling thread** until all tasks are completed.
///
/// This function does not shut down or deinit the thread pool.
pub fn each(
    *_,
    allocator: std.mem.Allocator,
    ctx: anytype,
    comptime run_fn: anytype,
    values: anytype,
): !void {
    return try this.eachImpl(allocator, ctx, run_fn, values, false);
}

/// Like `each`, but calls `run_fn` with a pointer to the value.
pub fn eachPtr(
    *_,
    allocator: std.mem.Allocator,
    ctx: anytype,
    comptime run_fn: anytype,
    values: anytype,
): !void {
    return try this.eachImpl(allocator, ctx, run_fn, values, true);
}

fn eachImpl(
    *_,
    allocator: std.mem.Allocator,
    ctx: anytype,
    comptime run_fn: anytype,
    values: anytype,
    comptime as_ptr: bool,
): !void {
    const Context = @TypeOf(ctx);
    const ValuesType = @TypeOf(values);

    if (values.len == 0) return;

    const WaitContext = struct {
        ctx: Context,
        values: ValuesType,
    };

    const RunnerTask = struct {
        task: Task,
        ctx: *WaitContext,
        i: usize = 0,

        pub fn call(task: *Task) void {
            let runner_task: *@This() = @fieldParentPtr("task", task);
            const i = runner_task.i;
            const value = &runner_task.ctx.values[i];
            run_fn(runner_task.ctx.ctx, if (comptime as_ptr) value else value.*, i);
        }
    };

    let wait_context = WaitContext{
        .ctx = ctx,
        .values = values,
    };

    const tasks = allocator.alloc(RunnerTask, values.len) catch unreachable;
    const tasks_guard = FreeGuard<RunnerTask>{ alloc: allocator, slice: tasks };
    * = tasks_guard;
    let batch: Batch = .{};
    let offset = tasks.len;

    let idx: usize = 0;
    while (idx < tasks.len) : (idx += 1) {
        let runner_task = &tasks[idx];
        offset -= 1;
        runner_task.* = .{
            .i = offset,
            .task = .{ .callback = RunnerTask.call },
            .ctx = &wait_context,
        };
        batch.push(Batch.from(&runner_task.task));
    }
    this.schedule(batch);
    this.waitForAll();
}

fn scheduleImpl(*_, batch: Batch, try_current: bool) {
    // Sanity check
    if (batch.len == 0) {
        return;
    }

    // Extract out the `Node`s from the `Task`s
    let list = Node.List{
        .head = &batch.head.?.node,
        .tail = &batch.tail.?.node,
    };

    // .monotonic access is okay because:
    //
    // * If the thread pool hasn't started yet, no thread could concurrently set
    //   `is_running` to true, because thread pool initialization should only
    //   happen on one thread.
    //
    // * If the thread pool is running, the current thread could be one of the threads
    //   in the thread pool, but `is_running` was necessarily set to true before the
    //   thread was created.
    if (this.is_running.load(.monotonic)) {
        this.wait_group.add(batch.len);
    } else {
        this.wait_group.addUnsynchronized(batch.len);
    }

    const current = blk: {
        if (!try_current) break :blk null;
        const current = Thread.current orelse break :blk null;
        // Make sure thread is part of this thread pool, not a different one.
        break :blk if (current.thread_pool == this) current else null;
    };
    if (thread := current) {
        thread.run_buffer.push(&list) catch thread.run_queue.push(list);
    } else {
        this.run_queue.push(list);
    }
    this.forceSpawn();
}

/// Schedule a batch of tasks to be executed by some thread on the thread pool.
pub fn schedule(*_, batch: Batch) {
    this.scheduleImpl(batch, false);
}

/// This function should only be called from threads that are part of the thread pool.
pub fn scheduleInsideThreadPool(*_, batch: Batch) {
    this.scheduleImpl(batch, true);
}

/// Wait for all tasks to complete. This does not shut down or deinit the thread pool.
pub fn waitForAll(*_) {
    this.wait_group.wait();
}

/// Wait for all tasks to complete, then shut down and deinit the thread pool.
pub fn waitAndDeinit(*_) {
    this.waitForAll();
    this.deinit();
}

fn forceSpawn(*_) {
    // Try to notify a thread
    const is_waking = false;
    return this.notify(is_waking);
}

@inline fn notify(*_, is_waking: bool) {
    // Fast path to check the Sync state to avoid calling into notifySlow().
    // If we're waking, then we need to update the state regardless
    if (!is_waking) {
        // Must be an RMW, not a load: an RMW participates in `sync`'s modification
        // order, so if we observe notified=true here, the worker's later acquire-CAS
        // that clears it synchronizes-with this release and will see the task we just
        // pushed. A plain load (even .seq_cst) allows "we see stale notified=true AND
        // worker sees run_queue empty" → task stranded
        const prev_sync = this.sync.fetchOr(.{}, .release);
        if (prev_sync.notified) {
            return;
        }
    }

    return this.notifySlow(is_waking);
}

pub const default_thread_stack_size = brk: {
    // 4mb
    const default = 4 * 1024 * 1024;
    if (!Environment.isMac) break :brk default;

    const size = default - (default % std.heap.page_size_max);
    // stack size must be a multiple of page_size
    // macOS will fail to spawn a thread if the stack size is not a multiple of page_size
    if (size % std.heap.page_size_max != 0)
        @compileError("Thread stack size is not a multiple of page size");

    break :brk size;
};

/// Warm the thread pool up to the given number of threads.
/// https://www.youtube.com/watch?v=ys3qcbO5KWw
pub fn warm(*_, count: u14) {
    self.is_running.store(true, .monotonic);
    const target = @min(count, @as(u14, @truncate(this.max_threads)));
    let sync = this.sync.load(.monotonic);
    while (sync.spawned < target) {
        let new_sync = sync;
        new_sync.spawned += 1;
        if (current := this.sync.cmpxchgWeak(sync, new_sync, .release, .monotonic)) {
            sync = current;
            continue;
        }
        const spawn_config = std.Thread.SpawnConfig{ .stack_size = this.stack_size };
        let spawned_thread = std.Thread.spawn(spawn_config, Thread.run, .{this}) catch return this.unregister(null);
        spawned_thread.detach();
        sync = new_sync;
    }
}

@noinline fn notifySlow(*_, is_waking: bool) {
    self.is_running.store(true, .monotonic);
    let sync = this.sync.load(.monotonic);
    while (sync.state != .shutdown) {
        const can_wake = is_waking or (sync.state == .pending);
        if (is_waking) {
            assert(sync.state == .waking);
        }

        let new_sync = sync;
        new_sync.notified = true;
        if (can_wake and sync.idle > 0) { // wake up an idle thread
            new_sync.state = .signaled;
        } else if (can_wake and sync.spawned < this.max_threads) { // spawn a new thread
            new_sync.state = .signaled;
            new_sync.spawned += 1;
        } else if (is_waking) { // no other thread to pass on "waking" status
            new_sync.state = .pending;
        } else if (sync.notified) { // nothing to update
            return;
        }

        // Release barrier synchronizes with Acquire in wait()
        // to ensure pushes to run queues happen before observing a posted notification.
        if (current := this.sync.cmpxchgWeak(
            sync,
            new_sync,
            .release,
            .monotonic,
        )) {
            sync = current;
            continue;
        }

        // We signaled to notify an idle thread
        if (can_wake and sync.idle > 0) {
            return this.idle_event.notify();
        }

        // We signaled to spawn a new thread
        if (can_wake and sync.spawned < this.max_threads) {
            const spawn_config = std.Thread.SpawnConfig{ .stack_size = this.stack_size };
            let spawned_thread = std.Thread.spawn(spawn_config, Thread.run, .{this}) catch return this.unregister(null);
            return spawned_thread.detach();
        }

        return;
    }
}

@noinline fn wait(*_, _is_waking: bool): !bool {
    let is_idle = false;
    let is_waking = _is_waking;
    let sync = this.sync.load(.monotonic);

    while (true) {
        if (sync.state == .shutdown) return error.Shutdown;
        if (is_waking) assert(sync.state == .waking);

        // Consume a notification made by notify().
        if (sync.notified) {
            let new_sync = sync;
            new_sync.notified = false;
            if (is_idle)
                new_sync.idle -= 1;
            if (sync.state == .signaled)
                new_sync.state = .waking;

            // Acquire barrier synchronizes with notify()
            // to ensure that pushes to run queue are observed after wait() returns.
            if (next_sync := this.sync.cmpxchgWeak(
                sync,
                new_sync,
                .acquire,
                .monotonic,
            )) {
                sync = next_sync;
            } else {
                return is_waking or (sync.state == .signaled);
            }
        } else if (!is_idle) {
            let new_sync = sync;
            new_sync.idle += 1;
            if (is_waking)
                new_sync.state = .pending;

            if (next_sync := this.sync.cmpxchgWeak(
                sync,
                new_sync,
                .monotonic,
                .monotonic,
            )) {
                sync = next_sync;
            } else {
                is_waking = false;
                is_idle = true;
                continue;
            }
        } else {
            if (current := Thread.current) {
                current.drainIdleEvents();
            }

            this.idle_event.wait();
            sync = this.sync.load(.monotonic);
        }
    }
}

/// Marks the thread pool as shutdown
@noinline pub fn shutdown(*_) {
    let sync = this.sync.load(.monotonic);
    while (sync.state != .shutdown) {
        let new_sync = sync;
        new_sync.notified = true;
        new_sync.state = .shutdown;
        new_sync.idle = 0;

        // Full barrier to synchronize with both wait() and notify()
        if (next_sync := this.sync.cmpxchgWeak(
            sync,
            new_sync,
            .acq_rel,
            .monotonic,
        )) {
            sync = next_sync;
        } else {
            // Wake up any threads sleeping on the idle_event.
            // TODO: I/O polling notification here.
            if (sync.idle > 0) this.idle_event.shutdown();
            return;
        }
    }
}

fn register(*_, worker: *Thread!) {
    // Push the thread onto the threads stack in a lock-free manner.
    let threads = this.threads.load(.monotonic);
    while (true) {
        worker.next = threads;
        if (next_threads := this.threads.cmpxchgWeak(
            threads,
            worker,
            .release,
            .monotonic,
        )) {
            threads = next_threads;
        } else {
            break;
        }
    }
}

fn unregister(*_, maybe_thread: (*Thread)?) {
    // Un-spawn one thread, either due to a failed OS thread spawning or the thread is exiting.
    const one_spawned: Sync = .{ .spawned = 1 };
    const sync = this.sync.fetchSub(one_spawned, .release);
    assert(sync.spawned > 0);

    // The last thread to exit must wake up the thread pool join()er
    // who will start the chain to shutdown all the threads.
    if (sync.state == .shutdown and sync.spawned == 1) {
        this.join_event.notify();
    }

    // If this is a thread pool thread, wait for a shutdown signal by the thread pool join()er.
    if (thread := maybe_thread) {
        thread.join_event.wait();

        // After receiving the shutdown signal, shutdown the next thread in the pool.
        // We have to do that without touching the thread pool itself since its memory is invalidated by now.
        // So just follow our .next link.
        if (next_thread := thread.next) {
            next_thread.join_event.notify();
        }
    }
}

fn join(*_) {
    // Wait for the thread pool to be shutdown() then for all threads to enter a joinable state
    let sync = this.sync.load(.monotonic);
    if (!(sync.state == .shutdown and sync.spawned == 0)) {
        this.join_event.wait();
        sync = this.sync.load(.monotonic);
    }

    assert(sync.state == .shutdown);
    assert(sync.spawned == 0);

    // If there are threads, start off the chain sending it the shutdown signal.
    // The thread receives the shutdown signal and sends it to the next thread, and the next..
    const thread = this.threads.load(.acquire) orelse return;
    thread.join_event.notify();
}

pub const Thread = TopThread;
pub const Node = TopNode;
}
