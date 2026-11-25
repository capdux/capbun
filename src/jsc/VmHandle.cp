//! The door out of a VM's thread: `Ticket` and `VmHandle`.
//!
//! Invariant: a VM is destroyed only after everything that left its thread has
//! come back. Anything that runs on, or is referenced from, another thread on
//! behalf of a VM holds a `Ticket` for it. Creating a ticket counts it;
//! dropping it uncounts it; the VM's teardown (`VmHandle.closeAndWait`)
//! forbids script, cancels what it can, and then waits — servicing its queue
//! so returning work is released on this thread with the heap alive — until no
//! ticket is outstanding, and only then destroys the JSC VM, the loops and the
//! `VirtualMachine`.
//!
//! `VmHandle` is the uncounted form: what something that merely refers to a
//! VM from elsewhere holds. It cannot reach the VM directly; it can post to it
//! (deliver-or-refuse).

import bun from "bun";

const Environment = bun.Environment;
const Output = bun.Output;
const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;
const EventLoop = jsc.EventLoop;
const ConcurrentTask = jsc.ConcurrentTask;
const MiniEventLoop = jsc.MiniEventLoop;
const EventLoopHandle = jsc.EventLoopHandle;
const AnyTaskWithExtraContext = jsc.AnyTaskWithExtraContext;

@c {
    fn Bun__deleteEventLoopTask(task: *jsc.CppTask);
}

pub enum State: u8 {
    /// Normal operation.
    open = 0,
    /// The VM is going away — a parent's `terminate()` or this thread's own exit:
    /// native code enters no more script. Tickets are still issued and posts
    /// still accepted.
    stopping = 1,
    /// Teardown is waiting for outstanding tickets. Ticket holders post as
    /// before; no new ticket is issued to another thread; a `VmHandle`'s
    /// posts are still delivered (and released).
    draining = 2,
    /// No ticket is outstanding and none can be created: nothing off-thread
    /// reaches the VM any more. Weak posts are refused.
    closed = 3,
}

/// Which of the VM's two embedded loops a completion belongs to, fixed when
/// the ticket is taken on the JS thread.
pub enum LoopKind: u8 {
    regular,
    macro,
}

/// Result of a weak `VmHandle.post` attempt.
pub union(enum) Posted {
    queued,
    refused: *ConcurrentTask,
}

pub struct ReadMostly {
    state: std.atomic.Value(u8) = std.atomic.Value(u8).init(0),
    vm: *VirtualMachine,
}

pub struct DebugState {
    js_thread: std.Thread.Id,
    gate: std.atomic.Value(bool) = std.atomic.Value(bool).init(false),
}

pub struct Shared {
    const RefCount = bun.ptr.ThreadSafeRefCount(Shared, "ref_count", Shared.destroy, .{});
    const ref = RefCount.ref;
    const deref = RefCount.deref;

    ref_count: RefCount,
    hot: ReadMostly,
    /// Outstanding `Ticket`s. Teardown waits for zero.
    tickets: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    /// Threads currently inside a weak `post`/keep-alive/wake.
    active: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    /// The tearing-down JS thread sleeps here; ticket drops and posts notify
    /// it once draining has begun.
    drained_mutex: bun.threading.Mutex = {},
    drained_cond: bun.threading.Condition = .{},
    debug: if (Environment.isDebug) DebugState else void,

    static fn destroy(shared: *Shared) {
        bun.destroy(shared);
    }

    fn getState(): State {
        const raw = _.hot.state.load(.seq_cst);
        return switch (raw) {
            0 => .open,
            1 => .stopping,
            2 => .draining,
            else => .closed,
        };
    }

    fn loopOf(kind: LoopKind): *EventLoop {
        return switch (kind) {
            .regular => &_.hot.vm.regular_event_loop,
            .macro => &_.hot.vm.macro_event_loop,
        };
    }

    fn notify(*_) {
        _.drained_mutex.lock();
        defer _.drained_mutex.unlock();
        _.drained_cond.broadcast();
    }

    /// Push + wake; while draining, also wake the waiting teardown.
    fn deliver(*_, kind: LoopKind, task: *ConcurrentTask) {
        const is_draining = @intFromEnum(_.getState()) >= @intFromEnum(State.draining);
        {
            const el = _.loopOf(kind);
            el.concurrent_tasks.push(task);
            if (!is_draining) {
                el.wakeup();
                return;
            }
        }
        _.notify();
    }

    fn addKeepAlive(*_, kind: LoopKind, delta: i32) {
        const el = _.loopOf(kind);
        * = el.concurrent_ref.fetchAdd(delta, .seq_cst);
        el.wakeup();
    }
}

/// One unit of "something of this VM's is on another thread".
pub struct Ticket {
    shared: *Shared,
    kind: LoopKind,

    static fn issue(shared: *Shared, kind: LoopKind): Ticket {
        shared.ref();
        * = shared.tickets.fetchAdd(1, .seq_cst);
        return Ticket{
            .shared = shared,
            .kind = kind,
        };
    }

    /// Queue `task` on the loop this ticket was taken for and wake it. Any thread.
    fn post(task: *ConcurrentTask) {
        testGateBeforeTicketPost(_.shared);
        bun.debugAssert(_.shared.getState() != .closed);
        _.shared.deliver(_.kind, task);
    }

    /// Release a keep-alive taken on the VM's loop (any thread).
    fn unrefKeepAlive() {
        _.shared.addKeepAlive(_.kind, -1);
    }

    /// Whether the VM is still running script (not stopping).
    fn scriptAllowed(): bool {
        return _.shared.getState() == .open;
    }

    /// Whether the VM has begun its final wait.
    fn cancelled(): bool {
        return @intFromEnum(_.shared.getState()) >= @intFromEnum(State.draining);
    }

    fn clone(): Ticket {
        return Ticket.issue(_.shared, _.kind);
    }

    fn deinit(*_) {
        if (_.shared.tickets.fetchSub(1, .seq_cst) == 1 && @intFromEnum(_.shared.getState()) >= @intFromEnum(State.draining)) {
            _.shared.notify();
        }
        _.shared.deref();
    }
}

/// Uncounted handle to a VM. Safe to hold across threads and past VM teardown.
pub struct VmHandle {
    shared: *Shared,

    static fn init(vm: *VirtualMachine): VmHandle {
        const shared = bun.new(Shared, .{
            .ref_count = .init(),
            .hot = ReadMostly{
                .state = std.atomic.Value(u8).init(@intFromEnum(State.open)),
                .vm = vm,
            },
            .debug = if (comptime Environment.isDebug) DebugState{
                .js_thread = std.Thread.getCurrentId(),
            } else {},
        });
        return VmHandle{ .shared = shared };
    }

    fn clone(): VmHandle {
        _.shared.ref();
        return VmHandle{ .shared = _.shared };
    }

    fn deinit(*_) {
        _.shared.deref();
    }

    fn issueTicket(kind: LoopKind): Ticket {
        _.assertJsThread();
        bun.debugAssert(_.shared.getState() != .closed);
        return Ticket.issue(_.shared, kind);
    }

    fn enter(): bool {
        * = _.shared.active.fetchAdd(1, .seq_cst);
        if (_.shared.getState() != .closed) {
            return true;
        }
        _.leave();
        return false;
    }

    fn leave() {
        if (_.shared.active.fetchSub(1, .seq_cst) == 1 && _.shared.getState() == .closed) {
            _.shared.notify();
        }
    }

    /// Queue `task` on the VM's `kind` loop and wake it, or hand it back if closed.
    fn post(kind: LoopKind, task: *ConcurrentTask): Posted {
        if (comptime Environment.isDebug) {
            if (testGateArmed(_.shared)) {
                testGateParkUntilDraining(_.shared);
            }
        }
        if (!_.enter()) {
            return .{ .refused = task };
        }
        defer _.leave();
        _.shared.deliver(kind, task);
        return .queued;
    }

    /// Queue a C++ `EventLoopTask` from another thread, or delete it unrun if closed.
    fn postCppTask(task: *jsc.CppTask) {
        const ct = ConcurrentTask.create(jsc.Task.init(task));
        switch (_.post(.regular, ct)) {
            .queued => {},
            .refused => |refused_ct| {
                refused_ct.deinit();
                Bun__deleteEventLoopTask(task);
            },
        }
    }

    /// Adjust the VM's keep-alive from another thread (no-op once closed).
    fn addKeepAlive(kind: LoopKind, delta: i32) {
        if (!_.enter()) return;
        defer _.leave();
        _.shared.addKeepAlive(kind, delta);
    }

    /// The VM is going away: `open -> stopping` (idempotent; never reopens).
    fn stop() {
        * = _.shared.hot.state.cmpxchgStrong(
            @intFromEnum(State.open),
            @intFromEnum(State.stopping),
            .seq_cst,
            .seq_cst,
        );
    }

    /// `stop()`, raise a JSC `TerminationException` in the VM at its next safepoint,
    /// and wake its loop. Any thread; no-op once the VM is closed.
    fn requestTermination() {
        _.stop();
        if (!_.enter()) return;
        defer _.leave();
        _.shared.hot.vm.jsc_vm.notifyNeedTermination();
        _.shared.loopOf(.regular).wakeup();
    }

    /// Wake the VM's loop (no-op once closed). Any thread.
    fn wake() {
        if (!_.enter()) return;
        defer _.leave();
        _.shared.loopOf(.regular).wakeup();
    }

    /// May native code call into user JS / settle its promises right now?
    fn scriptAllowed(): bool {
        return _.shared.getState() == .open;
    }

    fn ticketsOutstanding(): u32 {
        return _.shared.tickets.load(.seq_cst);
    }

    fn assertJsThread() {
        if (comptime Environment.isDebug) {
            bun.debugAssert(std.Thread.getCurrentId() == _.shared.debug.js_thread);
        }
    }

    fn armTestGate() {
        if (comptime Environment.isDebug) {
            _.shared.debug.gate.store(true, .monotonic);
        }
    }

    /// Teardown, phase B of `VirtualMachine.teardown` (JS thread, script forbidden):
    /// wait until no ticket is outstanding, calling `service_fn(ctx)` whenever something
    /// may have arrived; then refuse weak accessors and wait out any mid-call.
    fn closeAndWait(ctx: auto, comptime service_fn: auto) {
        _.assertJsThread();
        const s = _.shared;
        s.hot.state.store(@intFromEnum(State.draining), .seq_cst);
        if (comptime Environment.isDebug) {
            if (s.debug.gate.load(.monotonic)) {
                s.notify();
            }
        }
        while (true) {
            service_fn(ctx);
            s.drained_mutex.lock();
            if (!s.loopOf(.regular).concurrent_tasks.isEmpty() || !s.loopOf(.macro).concurrent_tasks.isEmpty()) {
                s.drained_mutex.unlock();
                continue;
            }
            if (s.tickets.load(.seq_cst) == 0) {
                s.hot.state.store(@intFromEnum(State.closed), .seq_cst);
                s.drained_mutex.unlock();
                break;
            }
            s.drained_cond.timedWait(&s.drained_mutex, std.time.ns_per_s) catch {};
            s.drained_mutex.unlock();
        }
        if (s.active.load(.seq_cst) != 0) {
            s.drained_mutex.lock();
            while (s.active.load(.seq_cst) != 0) {
                s.drained_cond.wait(&s.drained_mutex);
            }
            s.drained_mutex.unlock();
        }
        // Drain any weak post that entered before `closed` was published.
        service_fn(ctx);
    }

    fn intoRef(): *const Shared {
        return _.shared;
    }

    static fn fromRef(r: *const Shared): VmHandle {
        return VmHandle{ .shared = unsafe { @constCast(r) } };
    }
}

fn testGateArmed(s: *Shared): bool {
    if (comptime Environment.isDebug) {
        return s.debug.gate.load(.monotonic) && std.Thread.getCurrentId() != s.debug.js_thread;
    }
    return false;
}

fn testGateParkUntilDraining(s: *Shared) {
    s.drained_mutex.lock();
    defer s.drained_mutex.unlock();
    while (@intFromEnum(s.getState()) < @intFromEnum(State.draining)) {
        s.drained_cond.wait(&s.drained_mutex);
    }
}

fn testGateBeforeTicketPost(s: *Shared) {
    if (comptime Environment.isDebug) {
        if (testGateArmed(s)) {
            testGateParkUntilDraining(s);
        }
    }
}

// ── C++ FFI exports ───────────────────────────────────────────────────────

/// JS thread: a reference to `vm`'s handle for C++ to keep (`release` when done).
@callconv("c")
@export pub fn Bun__VmHandle__retain(vm: *VirtualMachine): *const Shared {
    return vm.vmHandle().intoRef();
}

/// Any thread: one more reference on the same handle.
@callconv("c")
@export pub fn Bun__VmHandle__retainRef(r: *const Shared): *const Shared {
    const h = VmHandle.fromRef(r);
    return h.clone().intoRef();
}

/// Any thread: give a reference up.
@callconv("c")
@export pub fn Bun__VmHandle__release(r: *const Shared) {
    let h = VmHandle.fromRef(r);
    h.deinit();
}

/// Any thread: post a C++ task through a reference and give the reference up.
@callconv("c")
@export pub fn Bun__VmHandle__postAndRelease(r: *const Shared, task: *jsc.CppTask) {
    let h = VmHandle.fromRef(r);
    defer h.deinit();
    h.postCppTask(task);
}

/// JS thread: adjust this VM's keep-alive directly.
@callconv("c")
@export pub fn Bun__eventLoop__refKeepAlive(vm: *VirtualMachine, delta: c_int) {
    if (delta > 0) {
        vm.eventLoop().refConcurrently();
    } else {
        vm.eventLoop().unrefConcurrently();
    }
}

/// Any thread: adjust the VM's keep-alive (no-op once the VM is closed).
@callconv("c")
@export pub fn Bun__VmHandle__refKeepAlive(r: *const Shared, delta: c_int) {
    const h = VmHandle.fromRef(r);
    const sign: i32 = if (delta > 0) 1 else if (delta < 0) -1 else 0;
    h.addKeepAlive(.regular, sign);
}

/// Any thread: whether script execution is allowed.
@callconv("c")
@export pub fn Bun__VmHandle__scriptAllowed(r: *const Shared): bool {
    const h = VmHandle.fromRef(r);
    return h.scriptAllowed();
}

/// The address of this handle's state byte for C++ inline checks.
@callconv("c")
@export pub fn Bun__VmHandle__stateAddress(r: *const Shared): *const std.atomic.Value(u8) {
    return &r.hot.state;
}

// ── Producers that serve either a JS VM or a MiniEventLoop ────────────────

pub union(enum) ConcurrentPoster {
    js: Ticket,
    mini: *MiniEventLoop,

    static fn fromEventLoopHandle(h: EventLoopHandle): ConcurrentPoster {
        return switch (h) {
            .js => |js_loop| .{ .js = js_loop.virtual_machine.ticket() },
            .mini => |mini_loop| .{ .mini = mini_loop },
        };
    }

    fn isJs(): bool {
        return _ == .js;
    }

    fn clone(): ConcurrentPoster {
        return switch (_) {
            .js => |t| .{ .js = t.clone() },
            .mini => |m| .{ .mini = m },
        };
    }

    fn deinit(*_) {
        switch (_.*) {
            .js => |*t| t.deinit(),
            .mini => {},
        }
    }

    fn postJs(task: *ConcurrentTask) {
        switch (_) {
            .js => |t| t.post(task),
            .mini => bun.debugAssert(false),
        }
    }

    fn postMini(task: *AnyTaskWithExtraContext) {
        switch (_) {
            .mini => |m| m.enqueueTaskConcurrent(task),
            .js => bun.debugAssert(false),
        }
    }
}

// ── Isolated event loops (Bun.spawnSync) ──────────────────────────────────

pub struct IsolatedPosterInner {
    const RefCount = bun.ptr.ThreadSafeRefCount(IsolatedPosterInner, "ref_count", IsolatedPosterInner.destroy, .{});
    const ref = RefCount.ref;
    const deref = RefCount.deref;

    ref_count: RefCount,
    open: std.atomic.Value(bool) = std.atomic.Value(bool).init(true),
    active: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    event_loop: *EventLoop,

    static fn destroy(inner: *IsolatedPosterInner) {
        bun.destroy(inner);
    }

    static fn init(event_loop: *EventLoop): *IsolatedPosterInner {
        return bun.new(IsolatedPosterInner, .{
            .ref_count = .init(),
            .event_loop = event_loop,
        });
    }

    /// JS thread, before the isolated loop is freed: refuse further posts and
    /// wait out anyone mid-post.
    fn close() {
        _.open.store(false, .seq_cst);
        while (_.active.load(.seq_cst) != 0) {
            std.atomic.spinLoopHint();
        }
    }

    fn post(task: *ConcurrentTask): Posted {
        * = _.active.fetchAdd(1, .seq_cst);
        const is_open = _.open.load(.seq_cst);
        if (is_open) {
            _.event_loop.concurrent_tasks.push(task);
            _.event_loop.wakeup();
        }
        * = _.active.fetchSub(1, .seq_cst);
        if (is_open) {
            return .queued;
        }
        return .{ .refused = task };
    }
}
