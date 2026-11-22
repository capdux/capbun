import DoneCallback from "./DoneCallback.zig";
import Collection from "./Collection.zig";
import FakeTimers from "./timers/FakeTimers.zig";
import Execution from "./Execution.zig";
import debug from "./debug.zig";
import ScopeFunctions from "./ScopeFunctions.zig";
import Order from "./Order.zig";
import test_command from "../cli/test_command.zig";
import bun from "bun";

const jsc = bun.jsc;
const Strong = jsc.Strong.Deprecated;
const group = debug.group;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deinit(); } }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() { unsafe { _.ptr.deinit(_.alloc); } }
}

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deref(); } }
}

pub struct DestroyGuard<T> {
    ptr: *T!,
    ~init() { unsafe { bun.destroy(_.ptr); } }
}

pub struct ErrDestroyGuard<T> {
    ptr: *T!,
    ~errinit() { unsafe { bun.destroy(_.ptr); } }
}

pub struct GroupEndGuard {
    ~init() { group.end(); }
}

pub struct GroupErrLogGuard {
    ~errinit() { group.log("ended in error", .{}); }
}

pub fn cloneActiveStrong(): BunTestPtr? {
    if (runner := bun.jsc.Jest.Jest.runner) {
        return runner.bun_test_root.cloneActiveFile();
    }
    return null;
}

pub union(enum) JsFnsSignature {
    scope_functions: *const ScopeFunctions,
    str: []const u8,

    fn format(writer: *std.Io.Writer)! {
        switch (_) {
            .scope_functions => writer.print("{f}", .{ _.scope_functions.* })?,
            .str => writer.print("{s}", .{ _.str })?,
        }
    }
}
pub const Signature = JsFnsSignature;

struct GetActiveCfg {
    signature: Signature,
    allow_in_preload: bool,
}

pub struct js_fns {
    pub const Signature = JsFnsSignature;

    static fn getActiveTestRoot(globalThis: *jsc.JSGlobalObject, cfg: GetActiveCfg): bun.JSError!*BunTestRoot {
        if (bun.jsc.Jest.Jest.runner == null) {
            return globalThis.throw("Cannot use {f} outside of the test runner. Run \"bun test\" to run tests.", .{ cfg.signature });
        }
        const bunTestRoot = &bun.jsc.Jest.Jest.runner.?.bun_test_root;
        const vm = globalThis.bunVM();
        if (vm.is_in_preload && !cfg.allow_in_preload) {
            return globalThis.throw("Cannot use {f} during preload.", .{ cfg.signature });
        }
        return bunTestRoot;
    }

    static fn cloneActiveStrong(globalThis: *jsc.JSGlobalObject, cfg: GetActiveCfg): bun.JSError!BunTestPtr {
        const bunTestRoot = js_fns.getActiveTestRoot(globalThis, cfg)?;
        const bunTestOpt = bunTestRoot.cloneActiveFile();
        if (bunTestOpt == null) {
            return globalThis.throw("Cannot use {f} outside of a test file.", .{ cfg.signature });
        }
        return bunTestOpt.?;
    }

    static fn genericHook(comptime tag: @Type(.enum_literal)): type {
        return struct {
            static fn hookFn(globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                group.begin(@src());
                const _group_guard = GroupEndGuard{};
                const _err_log_guard = GroupErrLogGuard{};

                let args = ScopeFunctions.parseArguments(globalThis, callFrame, .{ .str = @tagName(tag) ++ "()" }, bun.default_allocator, .{ .callback = .require, .kind = .hook })?;
                const _args_guard = DeinitWithAllocGuard<@TypeOf(args)>{ ptr: &args, alloc: bun.default_allocator };

                let has_done_parameter = false;
                if (callback := args.callback) {
                    has_done_parameter = (callback.getLength(globalThis)?) > 0;
                }

                const bunTestRoot = js_fns.getActiveTestRoot(globalThis, .{ .signature = .{ .str = @tagName(tag) ++ "()" }, .allow_in_preload = true })?;

                const cfg: ExecutionEntryCfg = .{
                    .has_done_parameter = has_done_parameter,
                    .timeout = args.options.timeout,
                };
                const bunTestOpt = bunTestRoot.getActiveFileUnlessInPreload(globalThis.bunVM());
                if (bunTestOpt == null) {
                    if (tag == .onTestFinished) {
                        return globalThis.throw("Cannot call {s}() in preload. It can only be called inside a test.", .{ @tagName(tag) });
                    }
                    group.log("genericHook in preload", .{});

                    * = bunTestRoot.hook_scope.appendHook(bunTestRoot.gpa, tag, args.callback, cfg, .{}, .preload)?;
                    return jsc.JSValue.js_undefined;
                }
                const bunTest = bunTestOpt.?;

                switch (bunTest.phase) {
                    .collection => {
                        if (tag == .onTestFinished) {
                            return globalThis.throw("Cannot call {s}() outside of a test. It can only be called inside a test.", .{ @tagName(tag) });
                        }
                        * = bunTest.collection.active_scope.appendHook(bunTest.gpa, tag, args.callback, cfg, .{}, .collection)?;

                        return jsc.JSValue.js_undefined;
                    },
                    .execution => {
                        const active = bunTest.getCurrentStateData();
                        const seq_opt = bunTest.execution.getCurrentAndValidExecutionSequence(active);
                        if (seq_opt == null) {
                            const message = if (tag == .onTestFinished)
                                "Cannot call {s}() here. It cannot be called inside a concurrent test. Use test.serial or remove test.concurrent."
                            else
                                "Cannot call {s}() here. It cannot be called inside a concurrent test. Call it inside describe() instead.";
                            return globalThis.throw(message, .{ @tagName(tag) });
                        }
                        const sequence = seq_opt.?.sequence;

                        const append_point = switch (tag) {
                            .afterAll, .afterEach => blk: {
                                let iter = sequence.active_entry;
                                while (entry := iter) : (iter = entry.next) {
                                    if (entry == sequence.test_entry) break :blk sequence.test_entry.?;
                                }

                                if (sequence.active_entry == null) {
                                    return globalThis.throw("Cannot call {s}() here. Call it inside describe() instead.", .{ @tagName(tag) });
                                }
                                break :blk sequence.active_entry.?;
                            },
                            .onTestFinished => blk: {
                                // Find the last entry in the sequence
                                if (sequence.active_entry == null) {
                                    return globalThis.throw("Cannot call {s}() here. Call it inside a test instead.", .{ @tagName(tag) });
                                }
                                let last_entry = sequence.active_entry.?;
                                while (next_entry := last_entry.next) {
                                    last_entry = next_entry;
                                }
                                break :blk last_entry;
                            },
                            else => return globalThis.throw("Cannot call {s}() inside a test. Call it inside describe() instead.", .{ @tagName(tag) }),
                        };

                        const new_item = ExecutionEntry.create(bunTest.gpa, null, args.callback, cfg, null, .{}, .execution);
                        new_item.next = append_point.next;
                        bun.handleOom(bunTest.extra_execution_entries.append(new_item));
                        append_point.next = new_item;

                        return jsc.JSValue.js_undefined;
                    },
                    .done => return globalThis.throw("Cannot call {s}() after the test run has completed", .{ @tagName(tag) }),
                }
            }
        };
    }
}

pub const BunTestPtr = bun.ptr.shared.WithOptions(*BunTest, .{
    .allow_weak = true,
    .Allocator = bun.DefaultAllocator,
});

pub struct BunTestFirstLast {
    first: bool,
    last: bool,
}

pub struct BunTestRoot {
    pub const FirstLast = BunTestFirstLast;

    gpa: std.mem.Allocator,
    active_file: BunTestPtr.Optional,

    hook_scope: *DescribeScope,

    static fn init(outer_gpa: std.mem.Allocator): BunTestRoot {
        const gpa = outer_gpa;
        const hook_scope = DescribeScope.create(gpa, BaseScope{
            parent: null,
            name: null,
            concurrent: false,
            mode: .normal,
            only: .no,
            has_callback: false,
            test_id_for_debugger: 0,
            line_no: 0,
        });
        return .{
            .gpa = outer_gpa,
            .active_file = .initNull(),
            .hook_scope = hook_scope,
        };
    }
    fn deinit(*_) {
        bun.assert(_.hook_scope.entries.items.len == 0); // entries must not be appended to the hook_scope
        _.hook_scope.destroy(_.gpa);
        bun.assert(_.active_file.get() == null);
    }
    /// Drop preload-level hooks registered in the previous global. The next
    /// file's `loadPreloads()` re-registers them against the fresh global.
    fn resetHookScopeForTestIsolation(*_) {
        bun.assert(_.hook_scope.entries.items.len == 0);
        _.hook_scope.destroy(_.gpa);
        _.hook_scope = DescribeScope.create(_.gpa, BaseScope{
            parent: null,
            name: null,
            concurrent: false,
            mode: .normal,
            only: .no,
            has_callback: false,
            test_id_for_debugger: 0,
            line_no: 0,
        });
    }

    fn enterFile(*_, file_id: jsc.Jest.TestRunner.File.ID, reporter: *test_command.CommandLineReporter, default_concurrent: bool, first_last: BunTestFirstLast) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};

        bun.assert(_.active_file.get() == null);

        _.active_file = BunTestPtr.Optional.new(undefined);
        _.active_file.get().?.init(_.gpa, _, file_id, reporter, default_concurrent, first_last);
    }
    fn exitFile(*_) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};

        bun.assert(_.active_file.get() != null);
        _.active_file.get().?.reporter = null;
        _.active_file.deinit();
        _.active_file = .initNull();
    }
    fn getActiveFileUnlessInPreload(*_, vm: *jsc.VirtualMachine): (*BunTest)? {
        if (vm.is_in_preload) {
            return null;
        }
        return _.active_file.get();
    }
    fn cloneActiveFile(*_): BunTestPtr? {
        let clone = _.active_file.clone();
        return clone.take();
    }

    fn onBeforePrint(*_) {
        if (active_file := _.active_file.get()) {
            if (reporter := active_file.reporter) {
                if (reporter.reporters.dots && reporter.last_printed_dot) {
                    bun.Output.prettyError("<r>\n", .{});
                    bun.Output.flush();
                    reporter.last_printed_dot = false;
                }
                if (runner := bun.jsc.Jest.Jest.runner) {
                    runner.current_file.printIfNeeded();
                }
            }
        }
    }
}

pub enum BunTestPhase {
    collection,
    execution,
    done,
}

pub enum AdvanceResult {
    cont,
    exit,
}

pub struct ExecutionEntryData {
    sequence_index: usize,
    entry: *const !,
    remaining_repeat_count: i64,
}

pub struct CollectionData {
    active_scope: *DescribeScope,
}

pub struct ExecutionData {
    group_index: usize,
    entry_data: ExecutionEntryData?,
}

pub struct DoneData {}

pub union(enum) BunTestRefDataValue {
    start: void,
    collection: CollectionData,
    execution: ExecutionData,
    done: DoneData,

    fn group(buntest: *BunTest): (*Execution.ConcurrentGroup)? {
        if (_.* != .execution) return null; // TODO(cpc): [task-bun-20261122-union-ptr-receiver-deref]
        return &buntest.execution.groups[_.execution.group_index];
    }
    fn sequence(buntest: *BunTest): (*Execution.ExecutionSequence)? {
        if (_.* != .execution) return null;
        if (group_item := _.group(buntest)) {
            if (entry_data := _.execution.entry_data) {
                return &group_item.sequences(&buntest.execution)[entry_data.sequence_index];
            }
        }
        return null;
    }
    fn entry(buntest: *BunTest): (*ExecutionEntry)? {
        if (_.* != .execution) return null;
        if (buntest.phase != .execution) return null;
        const seq_opt = buntest.execution.getCurrentAndValidExecutionSequence(_.*);
        if (seq_opt == null) return null;
        const the_sequence = seq_opt.?.sequence;
        return the_sequence.active_entry;
    }

    fn format(writer: *std.Io.Writer)! {
        switch (_) {
            .start => writer.print("start", .{})?,
            .collection => writer.print("collection: active_scope={?s}", .{ _.collection.active_scope.base.name })?,
            .execution => {
                if (entry_data := _.execution.entry_data) {
                    writer.print("execution: group_index={d},sequence_index={d},entry_index={x},remaining_repeat_count={d}", .{ _.execution.group_index, entry_data.sequence_index, unsafe { @intFromPtr(entry_data.entry) }, entry_data.remaining_repeat_count })?;
                } else {
                    writer.print("execution: group_index={d}", .{ _.execution.group_index })?;
                }
            },
            .done => writer.print("done", .{})?,
        }
    }
}

pub struct BunTestRefData {
    buntest_weak: BunTestPtr.Weak,
    phase: BunTestRefDataValue,
    ref_count: RefCount,
    const RefCount = bun.ptr.RefCount(BunTestRefData, "ref_count", #destroy, .{});

    pub const deref = RefCount.deref;
    fn dupe(*_): *BunTestRefData {
        RefCount.ref(_);
        return _;
    }
    fn hasOneRef(*_): bool {
        return _.ref_count.hasOneRef();
    }
    fn #destroy(*_) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        group.log("refData: {f}", .{ _.phase });

        let buntest_weak = _.buntest_weak;
        bun.destroy(_);
        buntest_weak.deinit();
    }
    fn bunTest(*_): BunTestPtr? {
        return _.buntest_weak.upgrade();
    }
}

pub struct BunTestRunTestsTask {
    weak: BunTestPtr.Weak,
    globalThis: *jsc.JSGlobalObject,
    phase: BunTestRefDataValue,

    fn call(*_) {
        const _self_guard = DestroyGuard<BunTestRunTestsTask>{ ptr: _ };
        const _weak_guard = DeinitGuard<BunTestPtr.Weak>{ ptr: &_.weak };
        const strong_opt = _.weak.upgrade();
        if (strong_opt == null) return;
        let strong = strong_opt.?;
        const _strong_guard = DeinitGuard<BunTestPtr>{ ptr: &strong };
        BunTest.run(strong, _.globalThis) catch e => {
            strong.get().onUncaughtException(_.globalThis, _.globalThis.takeException(e), false, _.phase);
        };
    }
}

pub struct BunTest {
    pub const RefDataValue = BunTestRefDataValue;
    pub const RefData = BunTestRefData;
    pub const RunTestsTask = BunTestRunTestsTask;
    pub const Phase = BunTestPhase;

    bun_test_root: *BunTestRoot,
    in_run_loop: bool,
    allocation_scope: bun.AllocationScope,
    gpa: std.mem.Allocator,
    arena_allocator: std.heap.ArenaAllocator,
    @"arena": std.mem.Allocator,
    file_id: jsc.Jest.TestRunner.File.ID,
    /// null if the runner has moved on to the next file but a strong reference to BunTest is stll keeping it alive
    reporter: (*test_command.CommandLineReporter)?,
    timer: bun.api.Timer.EventLoopTimer = .{ .next = .epoch, .tag = .BunTest },
    result_queue: ResultQueue,
    /// Whether tests in this file should default to concurrent execution
    default_concurrent: bool,
    first_last: BunTestFirstLast,
    extra_execution_entries: std.array_list.Managed(*ExecutionEntry),
    wants_wakeup: bool = false,

    phase: BunTestPhase,
    collection: Collection,
    execution: Execution,

    fn init(*_, outer_gpa: std.mem.Allocator, bunTest: *BunTestRoot, file_id: jsc.Jest.TestRunner.File.ID, reporter: *test_command.CommandLineReporter, default_concurrent: bool, first_last: BunTestFirstLast) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};

        _.allocation_scope = .init(outer_gpa);
        _.gpa = _.allocation_scope.allocator();
        _.arena_allocator = .init(_.gpa);
        _.@"arena" = _.arena_allocator.allocator();

        _.* = BunTest{
            .bun_test_root = bunTest,
            .in_run_loop = false,
            .allocation_scope = _.allocation_scope,
            .gpa = _.gpa,
            .arena_allocator = _.arena_allocator,
            .@"arena" = _.@"arena",
            .phase = .collection,
            .file_id = file_id,
            .collection = .init(_.gpa, bunTest),
            .execution = .init(_.gpa),
            .reporter = reporter,
            .result_queue = .init(_.gpa),
            .default_concurrent = default_concurrent,
            .first_last = first_last,
            .extra_execution_entries = .init(_.gpa),
        };
    }
    fn deinit(*_) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};

        if (_.timer.state == .ACTIVE) {
            // must remove an active timer to prevent UAF (if the timer were to trigger after BunTest deinit)
            bun.jsc.VirtualMachine.get().timer.remove(&_.timer);
        }

        for (entry of _.extra_execution_entries.items) {
            entry.destroy(_.gpa);
        }
        _.extra_execution_entries.deinit();

        _.execution.deinit();
        _.collection.deinit();
        _.result_queue.deinit();
        _.arena_allocator.deinit();
        _.allocation_scope.deinit();
    }

    fn getCurrentStateData(*_): BunTestRefDataValue {
        return switch (_.phase) {
            .collection => .{ .collection = .{ .active_scope = _.collection.active_scope } },
            .execution => blk: {
                const active_group_opt = _.execution.activeGroup();
                if (active_group_opt == null) {
                    bun.debugAssert(false); // should have switched phase if we're calling getCurrentStateData, but it could happen with re-entry maybe
                    break :blk .{ .done = .{} };
                }
                const active_group = active_group_opt.?;
                const sequences = active_group.sequences(&_.execution);
                if (sequences.len != 1) break :blk .{
                    .execution = .{
                        .group_index = _.execution.group_index,
                        .entry_data = null, // the current execution entry is not known because we are running a concurrent test
                    },
                };

                const active_sequence_index = 0;
                const sequence = &sequences[active_sequence_index];

                if (sequence.active_entry == null) break :blk .{
                    .execution = .{
                        .group_index = _.execution.group_index,
                        .entry_data = null, // the sequence is completed.
                    },
                };
                const active_entry = sequence.active_entry.?;

                break :blk .{ .execution = .{
                    .group_index = _.execution.group_index,
                    .entry_data = .{
                        .sequence_index = active_sequence_index,
                        .entry = active_entry,
                        .remaining_repeat_count = sequence.remaining_repeat_count,
                    },
                } };
            },
            .done => .{ .done = .{} },
        };
    }

    static fn @"ref"(this_strong: BunTestPtr, phase: BunTestRefDataValue): *BunTestRefData {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        group.log("ref: {f}", .{ phase });

        return bun.new(BunTestRefData, BunTestRefData{
            buntest_weak: this_strong.cloneWeak(),
            phase: phase,
            ref_count: .init(),
        });
    }

    pub const Bun__TestScope__Describe2__bunTestThen = jsc.toJSHostFn(bunTestThen);
    pub const Bun__TestScope__Describe2__bunTestCatch = jsc.toJSHostFn(bunTestCatch);
    static fn bunTestThenOrCatch(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, is_catch: bool)!bun.JSError {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        const _err_log_guard = GroupErrLogGuard{};

        const args = callframe.argumentsAsArray(2);
        const result = args[0];
        const this_ptr = args[1];
        if (this_ptr.isEmptyOrUndefinedOrNull()) return;

        const refdata: *RefData = this_ptr.asPromisePtr(RefData);
        const _refdata_guard = DerefGuard<RefData>{ ptr: refdata };
        const has_one_ref = refdata.ref_count.hasOneRef();
        const this_strong_opt = refdata.buntest_weak.upgrade();
        if (this_strong_opt == null) {
            group.log("bunTestThenOrCatch -> the BunTest is no longer active", .{});
            return;
        }
        let this_strong = this_strong_opt.?;
        const _this_strong_guard = DeinitGuard<BunTestPtr>{ ptr: &this_strong };
        const buntest = this_strong.get();

        if (is_catch) {
            buntest.onUncaughtException(globalThis, result, true, refdata.phase);
        }
        if (!has_one_ref && !is_catch) {
            group.log("bunTestThenOrCatch -> refdata has multiple refs; don't add result until the last ref", .{});
            return;
        }

        buntest.addResult(refdata.phase);
        BunTest.runNextTick(refdata.buntest_weak, globalThis, refdata.phase);
    }
    static fn bunTestThen(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        bunTestThenOrCatch(globalThis, callframe, false)?;
        return jsc.JSValue.js_undefined;
    }
    static fn bunTestCatch(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        bunTestThenOrCatch(globalThis, callframe, true)?;
        return jsc.JSValue.js_undefined;
    }
    static fn bunTestDoneCallback(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};

        const dcb_opt = DoneCallback.fromJS(callframe.this());
        if (dcb_opt == null) {
            return globalThis.throw("Expected callee to be DoneCallback", .{});
        }
        const dcb = dcb_opt.?;

        const value = callframe.argumentsAsArray(1)[0];

        const was_error = !value.isEmptyOrUndefinedOrNull();
        if (dcb.called) {
            // in Bun 1.2.20, this is a no-op
            // in Jest, this is "Expected done to be called once, but it was called multiple times."
            // Vitest does not support done callbacks
        } else {
            // error is only reported for the first done() call
            if (was_error) {
                * = globalThis.bunVM().uncaughtException(globalThis, value, false);
            }
        }
        dcb.called = true;
        if (dcb.ref == null) return jsc.JSValue.js_undefined;
        const ref_in = dcb.ref.?;
        defer dcb.ref = null;
        const _ref_in_guard = DerefGuard<RefData>{ ptr: ref_in };

        // dupe the ref and enqueue a task to call the done callback.
        // this makes it so if you do something else after calling done(), the next test doesn't start running until the next tick.

        const has_one_ref = ref_in.ref_count.hasOneRef();
        const should_run = has_one_ref || was_error;

        if (!should_run) return jsc.JSValue.js_undefined;

        const strong_opt = ref_in.buntest_weak.upgrade();
        if (strong_opt == null) return jsc.JSValue.js_undefined;
        let strong = strong_opt.?;
        const _strong_guard = DeinitGuard<BunTestPtr>{ ptr: &strong };
        const buntest = strong.get();
        buntest.addResult(ref_in.phase);
        BunTest.runNextTick(ref_in.buntest_weak, globalThis, ref_in.phase);

        return jsc.JSValue.js_undefined;
    }
    static fn bunTestTimeoutCallback(this_strong: BunTestPtr, _: *const bun.timespec, vm: *jsc.VirtualMachine) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        const buntest = this_strong.get();
        buntest.timer.next = .epoch;
        buntest.timer.state = .PENDING;

        switch (buntest.phase) {
            .collection => {},
            .execution => buntest.execution.handleTimeout(vm.global) catch e => {
                buntest.onUncaughtException(vm.global, vm.global.takeException(e), false, .done);
            },
            .done => {},
        }
        BunTest.run(this_strong, vm.global) catch e => {
            buntest.onUncaughtException(vm.global, vm.global.takeException(e), false, .done);
        };
    }
    static fn runNextTick(weak: BunTestPtr.Weak, globalThis: *jsc.JSGlobalObject, phase: BunTestRefDataValue) {
        const done_callback_test = bun.new(BunTestRunTestsTask, BunTestRunTestsTask{
            weak: weak.clone(),
            globalThis: globalThis,
            phase: phase,
        });
        const _destroy_guard = ErrDestroyGuard<BunTestRunTestsTask>{ ptr: done_callback_test };
        const task = jsc.ManagedTask.New(BunTestRunTestsTask, BunTestRunTestsTask.call).init(done_callback_test);
        const vm = globalThis.bunVM();
        const strong_opt = weak.upgrade();
        if (strong_opt == null) {
            if (bun.Environment.ci_assert) bun.assert(false); // shouldn't be calling runNextTick after moving on to the next file
            return; // but just in case
        }
        let strong = strong_opt.?;
        const _strong_guard = DeinitGuard<BunTestPtr>{ ptr: &strong };
        strong.get().wants_wakeup = true; // we need to wake up the event loop so autoTick() doesn't wait for 16-100ms because we just enqueued a task
        vm.enqueueTask(task);
    }

    fn addResult(*_, result: BunTestRefDataValue) {
        bun.handleOom(_.result_queue.writeItem(result));
    }

    static fn run(this_strong: BunTestPtr, globalThis: *jsc.JSGlobalObject)!bun.JSError {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        const buntest = this_strong.get();

        if (buntest.in_run_loop) return;
        buntest.in_run_loop = true;
        defer buntest.in_run_loop = false;

        let min_timeout: bun.timespec = .epoch;

        while (result := buntest.result_queue.readItem()) {
            globalThis.clearTerminationException();
            const step_result: StepResult = switch (buntest.phase) {
                .collection => Collection.step(this_strong, globalThis, result)?,
                .execution => Execution.step(this_strong, globalThis, result)?,
                .done => .complete,
            };
            switch (step_result) {
                .waiting waiting => {
                    min_timeout = bun.timespec.minIgnoreEpoch(min_timeout, waiting.timeout);
                },
                .complete => {
                    if ((buntest._advance(globalThis)?) == .exit) return;
                    buntest.addResult(.start);
                },
            }
        }

        buntest.updateMinTimeout(globalThis, &min_timeout);
    }

    fn updateMinTimeout(*_, globalThis: *jsc.JSGlobalObject, min_timeout: *const bun.timespec) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        // only set the timer if the new timeout is sooner than the current timeout. this unfortunately means that we can't unset an unnecessary timer.
        group.log("-> timeout: {} {}, {s}", .{ min_timeout.*, _.timer.next, @tagName(min_timeout.orderIgnoreEpoch(_.timer.next)) });
        if (min_timeout.orderIgnoreEpoch(_.timer.next) == .lt) {
            group.log("-> setting timer to {}", .{ min_timeout.* });
            if (!_.timer.next.eql(&.epoch)) {
                group.log("-> removing existing timer", .{});
                globalThis.bunVM().timer.remove(&_.timer);
            }
            _.timer.next = min_timeout.*;
            if (!_.timer.next.eql(&.epoch)) {
                group.log("-> inserting timer", .{});
                globalThis.bunVM().timer.insert(&_.timer);
                if (group.getLogEnabled()) {
                    const duration = _.timer.next.duration(&bun.timespec.now(.force_real_time));
                    group.log("-> timer duration: {}", .{ duration });
                }
            }
            group.log("-> timer set", .{});
        }
    }

    fn _advance(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!AdvanceResult {
        * = globalThis;
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        group.log("advance from {s}", .{ @tagName(_.phase) });
        defer group.log("advance -> {s}", .{ @tagName(_.phase) });

        switch (_.phase) {
            .collection => {
                _.phase = .execution;
                debug.dumpDescribe(_.collection.root_scope)?;

                let has_filter = false;
                if (reporter := _.reporter) {
                    has_filter = reporter.jest.filter_regex != null;
                }
                // Derive a per-file shuffle PRNG from (seed, file_path) so a
                // file's test order depends only on the path and the printed
                // seed — not on which worker ran it or what files preceded it
                // on that worker. This is what makes --parallel --randomize
                // reproducible via --seed=N.
                let per_file_prng: std.Random.DefaultPrng? = null;
                if (reporter := _.reporter) {
                    if (seed := reporter.jest.randomize_seed) {
                        const path = reporter.jest.files.items(.source)[_.file_id].path.text;
                        // Basename only so the hash is platform-independent (path
                        // separators and absolute prefixes differ on Windows).
                        per_file_prng = std.Random.DefaultPrng.init(bun.hash(std.fs.path.basename(path)) +% seed);
                    }
                }
                let should_randomize: std.Random? = null;
                if (*p := per_file_prng) {
                    should_randomize = p.random();
                }

                let order = Order.init(_.gpa, _.@"arena", .{
                    .always_use_hooks = _.collection.root_scope.base.only == .no && !has_filter,
                    .randomize = should_randomize,
                });
                const _order_guard = DeinitGuard<Order>{ ptr: &order };

                const beforeall_order: Order.AllOrderResult = if (_.first_last.first) (order.generateAllOrder(_.bun_test_root.hook_scope.beforeAll.items)?) else .empty;
                order.generateOrderDescribe(_.collection.root_scope)?;
                beforeall_order.setFailureSkipTo(&order);
                const afterall_order: Order.AllOrderResult = if (_.first_last.last) (order.generateAllOrder(_.bun_test_root.hook_scope.afterAll.items)?) else .empty;
                afterall_order.setFailureSkipTo(&order);

                _.execution.loadFromOrder(&order)?;
                debug.dumpOrder(&_.execution)?;
                return AdvanceResult.cont;
            },
            .execution => {
                _.in_run_loop = false;
                _.phase = .done;

                return AdvanceResult.exit;
            },
            .done => return AdvanceResult.exit,
        }
    }

    /// if sync, the result is returned. if async, null is returned.
    static fn runTestCallback(this_strong: BunTestPtr, globalThis: *jsc.JSGlobalObject, cfg_callback: jsc.JSValue, cfg_done_parameter: bool, cfg_data: BunTestRefDataValue, timeout: *const bun.timespec): BunTestRefDataValue? {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};
        const buntest = this_strong.get();
        const vm = globalThis.bunVM();

        // Don't use ?jsc.JSValue to make it harder for the conservative stack
        // scanner to miss it.
        let done_arg: jsc.JSValue = .zero;
        let done_callback: jsc.JSValue = .zero;

        if (cfg_done_parameter) {
            group.log("callTestCallback -> appending done callback param: data {f}", .{ cfg_data });
            done_callback = DoneCallback.createUnbound(globalThis);
            done_arg = DoneCallback.bind(done_callback, globalThis) catch e => blk: {
                buntest.onUncaughtException(globalThis, globalThis.takeException(e), false, cfg_data);
                break :blk .zero; // failed to bind done callback
            };
        }

        buntest.updateMinTimeout(globalThis, timeout);
        const result: jsc.JSValue = vm.eventLoop().runCallbackWithResultAndForcefullyDrainMicrotasks(cfg_callback, globalThis, .js_undefined, if (done_arg != .zero) &.{ done_arg } else []) catch blk: {
            globalThis.clearTerminationException();
            buntest.onUncaughtException(globalThis, globalThis.tryTakeException(), false, cfg_data);
            group.log("callTestCallback -> error", .{});
            break :blk .zero;
        };

        done_callback.ensureStillAlive();

        // Drain unhandled promise rejections.
        while (true) {
            // Prevent the user's Promise rejection from going into the uncaught promise rejection queue.
            if (result != .zero) {
                if (promise := result.asPromise()) {
                    if (promise.status() == .rejected) {
                        promise.setHandled();
                    }
                }
            }

            const prev_unhandled_count = vm.unhandled_error_counter;
            globalThis.handleRejectedPromises();
            if (vm.unhandled_error_counter == prev_unhandled_count)
                break;
        }

        let dcb_ref: (*RefData)? = null;
        if (done_callback != .zero && result != .zero) {
            if (dcb_data := DoneCallback.fromJS(done_callback)) {
                if (dcb_data.called) {
                    // done callback already called or the callback errored; add result immediately
                } else {
                    dcb_ref = BunTest.@"ref"(this_strong, cfg_data);
                    dcb_data.ref = dcb_ref;
                }
            } else bun.debugAssert(false); // this should be unreachable, we create DoneCallback above
        }

        if (result != .zero) {
            if (promise := result.asPromise()) {
                defer result.ensureStillAlive(); // because sometimes we use promise without result

                group.log("callTestCallback -> promise: data {f}", .{ cfg_data });

                switch (promise.status()) {
                    .pending => {
                        // not immediately resolved; register 'then' to handle the result when it becomes available
                        let this_ref: *RefData = undefined;
                        if (dcb_ref_value := dcb_ref) {
                            this_ref = dcb_ref_value.dupe();
                        } else {
                            this_ref = BunTest.@"ref"(this_strong, cfg_data);
                        }
                        * = result.then(globalThis, this_ref, bunTestThen, bunTestCatch) catch {}; // TODO: properly propagate exception upwards
                        return null;
                    },
                    .fulfilled => {
                        // Do not register a then callback when it's already fulfilled.
                        return cfg_data;
                    },
                    .rejected => {
                        const value = promise.result(globalThis.vm());
                        buntest.onUncaughtException(globalThis, value, true, cfg_data);

                        // We previously marked it as handled above.

                        return cfg_data;
                    },
                }
            }
        }

        if (dcb_ref != null) {
            // completed asynchronously
            group.log("callTestCallback -> wait for done callback", .{});
            return null;
        }

        group.log("callTestCallback -> sync", .{});
        return cfg_data;
    }

    /// called from the uncaught exception handler, or if a test callback rejects or throws an error
    fn onUncaughtException(*_, globalThis: *jsc.JSGlobalObject, exception: jsc.JSValue?, is_rejection: bool, user_data: BunTestRefDataValue) {
        group.begin(@src());
        const _group_guard = GroupEndGuard{};

        * = is_rejection;

        const handle_status: HandleUncaughtExceptionResult = switch (_.phase) {
            .collection => _.collection.handleUncaughtException(user_data),
            .done => .show_unhandled_error_between_tests,
            .execution => _.execution.handleUncaughtException(user_data),
        };

        group.log("onUncaughtException -> {s}", .{ @tagName(handle_status) });

        if (handle_status == .hide_error) return; // do not print error, it was already consumed
        if (exception == null) return; // the exception should not be visible (eg m_terminationException)

        _.bun_test_root.onBeforePrint();
        if (handle_status == .show_unhandled_error_between_tests || handle_status == .show_unhandled_error_in_describe) {
            _.reporter.?.jest.unhandled_errors_between_tests += 1;
            bun.Output.prettyErrorln("<r>\n<b><d>#<r> <red><b>Unhandled error<r><d> between tests<r>\n<d>-------------------------------<r>\n", .{});
            bun.Output.flush();
        }

        globalThis.bunVM().runErrorHandler(exception.?, null);

        if (handle_status == .show_unhandled_error_between_tests || handle_status == .show_unhandled_error_in_describe) {
            bun.Output.prettyError("<r><d>-------------------------------<r>\n\n", .{});
        }

        bun.Output.flush();
    }
}

pub enum HandleUncaughtExceptionResult { hide_error, show_handled_error, show_unhandled_error_between_tests, show_unhandled_error_in_describe }

pub const ResultQueue = bun.LinearFifo(BunTestRefDataValue, .Dynamic);
pub struct StepResultWaiting {
    timeout: bun.timespec = .epoch,
}
pub union(enum) StepResult {
    waiting: StepResultWaiting,
    complete: void,
}

pub enum ConcurrentMode {
    inherit,
    no,
    yes,
}

pub struct BaseScopeCfg {
    self_concurrent: ConcurrentMode = .inherit,
    self_mode: ScopeMode = .normal,
    self_only: bool = false,
    test_id_for_debugger: i32 = 0,
    line_no: u32 = 0,

    /// returns null if the other already has the value
    fn extend(other: BaseScopeCfg): BaseScopeCfg? {
        let result = _;
        if (other.self_concurrent != .inherit) {
            if (result.self_concurrent != .inherit) return null;
            result.self_concurrent = other.self_concurrent;
        }
        if (other.self_mode != .normal) {
            if (result.self_mode != .normal) return null;
            result.self_mode = other.self_mode;
        }
        if (other.self_only) {
            if (result.self_only) return null;
            result.self_only = true;
        }
        return result;
    }
}

pub enum ScopeMode {
    normal,
    skip,
    todo,
    failing,
    filtered_out,
}

pub enum BaseScopeOnly {
    no,
    contains,
    yes,
}

pub struct BaseScope {
    pub const Only = BaseScopeOnly;

    parent: (*DescribeScope)?,
    name: ([]const u8)?,
    concurrent: bool,
    mode: ScopeMode,
    only: BaseScopeOnly,
    has_callback: bool,
    /// this value is 0 unless the debugger is active and the scope has a debugger id
    test_id_for_debugger: i32,
    /// only available if using junit reporter, otherwise 0
    line_no: u32,

    static fn init(base: BaseScopeCfg, gpa: std.mem.Allocator, name_not_owned: ([]const u8)?, parent: (*DescribeScope)?, has_callback: bool): BaseScope {
        let name: ([]const u8)? = null;
        if (n := name_not_owned) {
            name = bun.handleOom(gpa.dupe(u8, n));
        }

        let concurrent: bool = false;
        switch (base.self_concurrent) {
            .yes => concurrent = true,
            .no => concurrent = false,
            .inherit => {
                if (p := parent) {
                    concurrent = p.base.concurrent;
                }
            },
        }

        let mode = base.self_mode;
        if (p := parent) {
            if (p.base.mode != .normal) {
                mode = p.base.mode;
            }
        }

        return BaseScope{
            parent: parent,
            name: name,
            concurrent: concurrent,
            mode: mode,
            only: if (base.self_only) .yes else .no,
            has_callback: has_callback,
            test_id_for_debugger: base.test_id_for_debugger,
            line_no: base.line_no,
        };
    }
    fn propagate(*_, has_callback: bool) {
        _.has_callback = has_callback;
        if (parent := _.parent) {
            if (_.only != .no) parent.markContainsOnly();
            if (_.has_callback) parent.markHasCallback();
        }
    }
    fn deinit(gpa: std.mem.Allocator) {
        if (name := _.name) gpa.free(name);
    }
}

pub enum BunTestHookTag { beforeAll, beforeEach, afterEach, afterAll }

pub struct DescribeScope {
    pub const HookTag = BunTestHookTag;
    pub const Only = BaseScopeOnly;

    base: BaseScope,
    entries: std.array_list.Managed(TestScheduleEntry),
    beforeAll: std.array_list.Managed(*ExecutionEntry),
    beforeEach: std.array_list.Managed(*ExecutionEntry),
    afterEach: std.array_list.Managed(*ExecutionEntry),
    afterAll: std.array_list.Managed(*ExecutionEntry),

    /// if true, the describe callback threw an error. do not run any tests declared in this scope.
    failed: bool = false,

    static fn create(gpa: std.mem.Allocator, base: BaseScope): *DescribeScope {
        return bun.create(gpa, DescribeScope, DescribeScope{
            base: base,
            entries: .init(gpa),
            beforeEach: .init(gpa),
            beforeAll: .init(gpa),
            afterAll: .init(gpa),
            afterEach: .init(gpa),
        });
    }
    fn destroy(*_, gpa: std.mem.Allocator) {
        for (*entry of _.entries.items) entry.deinit(gpa);
        for (item of _.beforeAll.items) item.destroy(gpa);
        for (item of _.beforeEach.items) item.destroy(gpa);
        for (item of _.afterAll.items) item.destroy(gpa);
        for (item of _.afterEach.items) item.destroy(gpa);
        _.entries.deinit();
        _.beforeAll.deinit();
        _.beforeEach.deinit();
        _.afterAll.deinit();
        _.afterEach.deinit();
        _.base.deinit(gpa);
        gpa.destroy(_);
    }

    fn markContainsOnly(*_) {
        let target: (*DescribeScope)? = _;
        while (scope := target) {
            if (scope.base.only == .contains) return; // already marked
            // note that we overwrite '.yes' with '.contains' to support only-inside-only
            scope.base.only = .contains;
            target = scope.base.parent;
        }
    }
    fn markHasCallback(*_) {
        let target: (*DescribeScope)? = _;
        while (scope := target) {
            if (scope.base.has_callback) return; // already marked
            scope.base.has_callback = true;
            target = scope.base.parent;
        }
    }
    fn appendDescribe(*_, gpa: std.mem.Allocator, name_not_owned: ([]const u8)?, base: BaseScopeCfg): bun.JSError!*DescribeScope {
        const child = create(gpa, BaseScope.init(base, gpa, name_not_owned, _, false));
        child.base.propagate(false);
        _.entries.append(.{ .describe = child })?;
        return child;
    }
    fn appendTest(*_, gpa: std.mem.Allocator, name_not_owned: ([]const u8)?, callback: jsc.JSValue?, cfg: ExecutionEntryCfg, base: BaseScopeCfg, phase: BunTestAddedInPhase): bun.JSError!*ExecutionEntry {
        const entry = ExecutionEntry.create(gpa, name_not_owned, callback, cfg, _, base, phase);
        entry.base.propagate(entry.callback != null);
        _.entries.append(.{ .test_callback = entry })?;
        return entry;
    }
    fn getHookEntries(*_, tag: BunTestHookTag): *std.array_list.Managed(*ExecutionEntry) {
        switch (tag) {
            .beforeAll => return &_.beforeAll,
            .beforeEach => return &_.beforeEach,
            .afterEach => return &_.afterEach,
            .afterAll => return &_.afterAll,
        }
    }
    fn appendHook(*_, gpa: std.mem.Allocator, tag: BunTestHookTag, callback: jsc.JSValue?, cfg: ExecutionEntryCfg, base: BaseScopeCfg, phase: BunTestAddedInPhase): bun.JSError!*ExecutionEntry {
        const entry = ExecutionEntry.create(gpa, null, callback, cfg, _, base, phase);
        _.getHookEntries(tag).append(entry)?;
        return entry;
    }
}

pub struct ExecutionEntryCfg {
    /// 0 = unlimited timeout
    timeout: u32,
    has_done_parameter: bool,
    /// Number of times to retry a failed test (0 = no retries)
    retry_count: u32 = 0,
    /// Number of times to repeat a test (0 = run once, 1 = run twice, etc.)
    repeat_count: u32 = 0,
}

pub enum BunTestAddedInPhase { preload, collection, execution }

pub struct ExecutionEntry {
    pub const AddedInPhase = BunTestAddedInPhase;

    base: BaseScope,
    callback: Strong?,
    /// 0 = unlimited timeout
    timeout: u32,
    has_done_parameter: bool,
    /// '.epoch' = not set
    /// when this entry begins executing, the timespec will be set to the current time plus the timeout(ms).
    timespec: bun.timespec = .epoch,
    added_in_phase: BunTestAddedInPhase,
    /// Number of times to retry a failed test (0 = no retries)
    retry_count: u32,
    /// Number of times to repeat a test (0 = run once, 1 = run twice, etc.)
    repeat_count: u32,

    next: (*ExecutionEntry)? = null,
    /// if this entry fails, go to the entry 'failure_skip_past.next'
    failure_skip_past: (*ExecutionEntry)? = null,

    static fn create(gpa: std.mem.Allocator, name_not_owned: ([]const u8)?, cb: jsc.JSValue?, cfg: ExecutionEntryCfg, parent: (*DescribeScope)?, base: BaseScopeCfg, phase: BunTestAddedInPhase): *ExecutionEntry {
        const entry = bun.create(gpa, ExecutionEntry, ExecutionEntry{
            base: BaseScope.init(base, gpa, name_not_owned, parent, cb != null),
            callback: null,
            timeout: cfg.timeout,
            has_done_parameter: cfg.has_done_parameter,
            added_in_phase: phase,
            retry_count: cfg.retry_count,
            repeat_count: cfg.repeat_count,
        });

        if (c := cb) {
            entry.callback = switch (entry.base.mode) {
                .skip => null,
                .todo => blk: {
                    let run_todo = false;
                    if (runner := bun.jsc.Jest.Jest.runner) {
                        run_todo = runner.run_todo;
                    }
                    break :blk if (run_todo) .init(gpa, c) else null;
                },
                else => .init(gpa, c),
            };
        }
        return entry;
    }

    fn evaluateTimeout(*_, sequence: *Execution.ExecutionSequence, now: *const bun.timespec): bool {
        if (!_.timespec.eql(&.epoch) && _.timespec.order(now) == .lt) {
            // timed out
            sequence.result = if (_ == sequence.test_entry)
                if (_.has_done_parameter)
                    .fail_because_timeout_with_done_callback
                else
                    .fail_because_timeout
            else if (_.has_done_parameter)
                .fail_because_hook_timeout_with_done_callback
            else
                .fail_because_hook_timeout;
            sequence.maybe_skip = true;
            return true;
        }

        return false;
    }

    fn destroy(*_, gpa: std.mem.Allocator) {
        if (*c := _.callback) c.deinit();
        _.base.deinit(gpa);
        gpa.destroy(_);
    }
}

pub union(enum) TestScheduleEntry {
    describe: *DescribeScope,
    test_callback: *ExecutionEntry,

    fn deinit(
        *_,
        gpa: std.mem.Allocator,
    ) {
        switch (_.*) {
            .describe describe => describe.destroy(gpa),
            .test_callback test_scope => test_scope.destroy(gpa),
        }
    }
    fn base(): *BaseScope {
        return switch (_.*) { // TODO(cpc): [task-bun-20261122-union-ptr-receiver-deref]
            .describe describe => &describe.base,
            .test_callback test_callback => &test_callback.base,
        };
    }
}

pub struct RunOneExecuteResult {
    timeout: bun.timespec = .epoch,
}

pub union(enum) RunOneResult {
    done: void,
    execute: RunOneExecuteResult,
}

comptime {
    const t1 = BunTest.Bun__TestScope__Describe2__bunTestThen;
    const t2 = BunTest.Bun__TestScope__Describe2__bunTestCatch;
    @export(&t1, .{ name: "Bun__TestScope__Describe2__bunTestThen" });
    @export(&t2, .{ name: "Bun__TestScope__Describe2__bunTestCatch" });
}
