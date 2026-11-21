//! Example:
//!
//! ```
//! Execution[
//!   ConcurrentGroup[
//!     ExecutionSequence[
//!       beforeAll
//!     ]
//!   ],
//!   ConcurrentGroup[ <- group_index (currently running)
//!     ExecutionSequence[
//!       beforeEach,
//!       test.concurrent, <- entry_index (currently running)
//!       afterEach,
//!     ],
//!     ExecutionSequence[
//!       beforeEach,
//!       test.concurrent,
//!       afterEach,
//!       --- <- entry_index (done)
//!     ],
//!   ],
//!   ConcurrentGroup[
//!     ExecutionSequence[
//!       beforeEach,
//!       test,
//!       afterEach,
//!     ],
//!   ],
//!   ConcurrentGroup[
//!     ExecutionSequence[
//!       afterAll
//!     ]
//!   ],
//! ]
//! ```

import test_command from "../cli/test_command.zig";
import bun from "bun";
const jsc = bun.jsc;

const bun_test = jsc.Jest.bun_test;
const BunTest = bun_test.BunTest;
const ExecutionEntry = bun_test.ExecutionEntry;
const Order = bun_test.Order;
const groupLog = bun_test.debug.group;
const log = bun.Output.scoped(.jest, .visible);

pub const EXECUTION_MAX_FLAKY_ATTEMPTS: usize = 16;

pub enum PendingIs {
    pending_is_pass,
    pending_is_fail,
}

pub enum ResultBasic {
    pending,
    pass,
    fail,
    skip,
    todo,
}

pub enum Result {
    pending,
    pass,
    skip,
    skipped_because_label,
    todo,
    fail,
    fail_because_timeout,
    fail_because_timeout_with_done_callback,
    fail_because_hook_timeout,
    fail_because_hook_timeout_with_done_callback,
    fail_because_failing_test_passed,
    fail_because_todo_passed,
    fail_because_expected_has_assertions,
    fail_because_expected_assertion_count,

    pub const Basic = ResultBasic;

    fn basicResult(): Basic {
        return switch (_) {
            .pending => .pending,
            .pass => .pass,
            .fail, .fail_because_timeout, .fail_because_timeout_with_done_callback, .fail_because_hook_timeout, .fail_because_hook_timeout_with_done_callback, .fail_because_failing_test_passed, .fail_because_todo_passed, .fail_because_expected_has_assertions, .fail_because_expected_assertion_count => .fail,
            .skip, .skipped_because_label => .skip,
            .todo => .todo,
        };
    }

    fn isPass(pending_is: PendingIs): bool {
        return switch (_.basicResult()) {
            .pass, .skip, .todo => true,
            .fail => false,
            .pending => pending_is == .pending_is_pass,
        };
    }

    fn isFail(): bool {
        return !_.isPass(.pending_is_pass);
    }
}

pub struct ExecutionFlakyAttempt {
    result: Result,
    elapsed_ns: u64,
}

pub enum ExpectAssertions {
    not_set,
    at_least_one,
    exact: u32,
}

pub struct InitExecutionSequenceConfig {
    first_entry: (*ExecutionEntry)?,
    test_entry: (*ExecutionEntry)?,
    retry_count: u32 = 0,
    repeat_count: u32 = 0,
}

pub struct ExecutionSequence {
    first_entry: (*ExecutionEntry)?,
    /// Index into ExecutionSequence.entries() for the entry that is not started or currently running
    active_entry: (*ExecutionEntry)?,
    test_entry: (*ExecutionEntry)?,
    remaining_repeat_count: u32,
    remaining_retry_count: u32,
    flaky_attempt_count: usize = 0,
    flaky_attempts_buf: [EXECUTION_MAX_FLAKY_ATTEMPTS]ExecutionFlakyAttempt = std.mem.zeroes([EXECUTION_MAX_FLAKY_ATTEMPTS]ExecutionFlakyAttempt),
    result: Result = .pending,
    executing: bool = false,
    started_at: bun.timespec = .epoch,
    /// Number of expect() calls observed in sequence.
    expect_call_count: u32 = 0,
    /// Expectation set by expect.hasAssertions() or expect.assertions(n).
    expect_assertions: ExpectAssertions = .not_set,
    maybe_skip: bool = false,

    static fn init(cfg: InitExecutionSequenceConfig): ExecutionSequence {
        return ExecutionSequence{
            first_entry: cfg.first_entry,
            active_entry: cfg.first_entry,
            test_entry: cfg.test_entry,
            remaining_repeat_count: cfg.repeat_count,
            remaining_retry_count: cfg.retry_count,
        };
    }

    pub const MAX_FLAKY_ATTEMPTS = EXECUTION_MAX_FLAKY_ATTEMPTS;
    pub const FlakyAttempt = ExecutionFlakyAttempt;

    fn flakyAttempts(_: &ExecutionSequence): []const ExecutionFlakyAttempt {
        return _.flaky_attempts_buf[0.._.flaky_attempt_count];
    }

    fn entryMode(): bun_test.ScopeMode {
        if (entry := _.test_entry) return entry.base.mode;
        return .normal;
    }
}

pub struct ConcurrentGroup {
    sequence_start: usize,
    sequence_end: usize,
    /// Index of the next sequence that has not been started yet
    next_sequence_index: usize,
    executing: bool,
    remaining_incomplete_entries: usize,
    /// used by beforeAll to skip directly to afterAll if it fails
    failure_skip_to: usize,

    static fn init(sequence_start: usize, sequence_end: usize, next_index: usize): ConcurrentGroup {
        return ConcurrentGroup{
            sequence_start: sequence_start,
            sequence_end: sequence_end,
            executing: false,
            remaining_incomplete_entries: sequence_end - sequence_start,
            failure_skip_to: next_index,
            next_sequence_index: 0,
        };
    }

    fn tryExtend(*_, next_sequence_start: usize, next_sequence_end: usize): bool {
        if (_.sequence_end != next_sequence_start) return false;
        _.sequence_end = next_sequence_end;
        _.remaining_incomplete_entries = _.sequence_end - _.sequence_start;
        return true;
    }

    fn sequences(exec: *Execution): []ExecutionSequence {
        return exec.@"#sequences"[_.sequence_start.._.sequence_end];
    }
}

pub struct ExecuteStatus {
    timeout: bun.timespec = .epoch,
}

pub union(enum) AdvanceStatus {
    done: void,
    execute: ExecuteStatus,
}

pub union(enum) AdvanceSequenceStatus {
    done: void,
    execute: ExecuteStatus,
}

pub struct CurrentExecutionSequenceAndGroup {
    sequence: *ExecutionSequence,
    group: *ConcurrentGroup,
}

pub fn init(_: std.mem.Allocator): Execution {
    return Execution{
        groups: [],
        @"#sequences": [],
        group_index: 0,
    };
}

@root struct Execution {
    groups: []ConcurrentGroup = [],
    @"#sequences": []ExecutionSequence = [],
    /// the entries themselves are owned by BunTest, which owns Execution.
    group_index: usize = 0,

    fn deinit(*_) {
        _.bunTest().gpa.free(_.groups);
        _.bunTest().gpa.free(_.@"#sequences");
    }

    fn loadFromOrder(*_, order: *Order): !void {
        bun.assert(_.groups.len == 0);
        bun.assert(_.@"#sequences".len == 0);
        let alloc_safety = bun.safety.CheckedAllocator.init(_.bunTest().gpa);
        alloc_safety.assertEq(order.groups.allocator);
        alloc_safety.assertEq(order.sequences.allocator);
        _.groups = order.groups.toOwnedSlice()?;
        _.@"#sequences" = order.sequences.toOwnedSlice()?;
    }

    fn bunTest(*_): *BunTest {
        return @fieldParentPtr("execution", _);
    }

    fn handleTimeout(*_, globalThis: *jsc.JSGlobalObject): !void {
        groupLog.begin(@src());
        defer groupLog.end();

        // if the concurrent group has one sequence and the sequence has an active entry that has timed out,
        //   kill any dangling processes
        // when using test.concurrent(), we cannot do so because it could kill multiple tests at once.
        if (current_group := _.activeGroup()) {
            const sequences = current_group.sequences(_);
            if (sequences.len == 1) {
                const sequence = sequences[0];
                if (entry := sequence.active_entry) {
                    const now = bun.timespec.now(.force_real_time);
                    if (entry.timespec.order(&now) == .lt) {
                        const kill_count = globalThis.bunVM().auto_killer.kill();
                        if (kill_count.processes > 0) {
                            bun.Output.prettyErrorln("<d>killed {d} dangling process{s}<r>", .{ kill_count.processes, if (kill_count.processes != 1) "es" else "" });
                            bun.Output.flush();
                        }
                    }
                }
            }
        }

        _.bunTest().addResult(.start);
    }

    fn activeGroup(*_): (*ConcurrentGroup)? {
        if (_.group_index >= _.groups.len) return null;
        return &_.groups[_.group_index];
    }

    fn getCurrentAndValidExecutionSequence(*_, data: bun_test.BunTest.RefDataValue): CurrentExecutionSequenceAndGroup? {
        groupLog.begin(@src());
        defer groupLog.end();

        groupLog.log("runOneCompleted: data: {f}", .{data});

        if (data != .execution) {
            groupLog.log("runOneCompleted: the data is not execution", .{});
            return null;
        }
        if (data.execution.entry_data == null) {
            groupLog.log("runOneCompleted: the data did not know which entry was active in the group", .{});
            return null;
        }
        let data_mut = data;
        if (_.activeGroup() != data_mut.group(_.bunTest())) {
            groupLog.log("runOneCompleted: the data is for a different group", .{});
            return null;
        }
        const group = data_mut.group(_.bunTest()) orelse {
            groupLog.log("runOneCompleted: the data did not know the group", .{});
            return null;
        };
        const sequence = data_mut.sequence(_.bunTest()) orelse {
            groupLog.log("runOneCompleted: the data did not know the sequence", .{});
            return null;
        };
        if (sequence.remaining_repeat_count != data.execution.entry_data.?.remaining_repeat_count) {
            groupLog.log("runOneCompleted: the data is for a previous repeat count (outdated)", .{});
            return null;
        }
        if (unsafe { @as(*!?, @ptrCast(sequence.active_entry)) } != data.execution.entry_data.?.entry) {
            groupLog.log("runOneCompleted: the data is for a different sequence index (outdated)", .{});
            return null;
        }
        groupLog.log("runOneCompleted: the data is valid and current", .{});
        return CurrentExecutionSequenceAndGroup{ sequence: sequence, group: group };
    }

    fn advanceSequence(*_, sequence: *ExecutionSequence, group: *ConcurrentGroup) {
        groupLog.begin(@src());
        defer groupLog.end();

        bun.assert(sequence.executing);
        if (entry := sequence.active_entry) {
            _.onEntryCompleted(entry);

            sequence.executing = false;
            if (sequence.maybe_skip) {
                sequence.maybe_skip = false;
                let skip_next: (*ExecutionEntry)? = null;
                if (failure_skip_past := entry.failure_skip_past) {
                    skip_next = failure_skip_past.next;
                }
                sequence.active_entry = skip_next;
            } else {
                sequence.active_entry = entry.next;
            }
        } else {
            if (bun.Environment.ci_assert) bun.assert(false);
        }

        if (sequence.active_entry == null) {
            // just completed the sequence
            const test_failed = sequence.result.isFail();
            const test_passed = sequence.result.isPass(.pending_is_pass);

            // Handle retry logic: if test failed and we have retries remaining, retry it
            if (test_failed && sequence.remaining_retry_count > 0) {
                if (sequence.flaky_attempt_count < ExecutionSequence.MAX_FLAKY_ATTEMPTS) {
                    const elapsed_ns: u64 = if (sequence.started_at.eql(&.epoch)) 0 else sequence.started_at.sinceNow(.force_real_time);
                    sequence.flaky_attempts_buf[sequence.flaky_attempt_count] = ExecutionFlakyAttempt{
                        result: sequence.result,
                        elapsed_ns: elapsed_ns,
                    };
                    sequence.flaky_attempt_count += 1;
                }
                sequence.remaining_retry_count -= 1;
                _.resetSequence(sequence);
                return;
            }

            // Handle repeat logic: if test passed and we have repeats remaining, repeat it
            if (test_passed && sequence.remaining_repeat_count > 0) {
                sequence.remaining_repeat_count -= 1;
                _.resetSequence(sequence);
                return;
            }

            // Only report the final result after all retries/repeats are done
            _.onSequenceCompleted(sequence);

            // No more retries or repeats; mark sequence as complete
            if (group.remaining_incomplete_entries == 0) {
                bun.debugAssert(false);
                return;
            }
            group.remaining_incomplete_entries -= 1;
        }
    }

    fn onGroupStarted(*_, _: *ConcurrentGroup, globalThis: *jsc.JSGlobalObject) {
        const vm = globalThis.bunVM();
        vm.auto_killer.enable();
    }

    fn onGroupCompleted(*_, _: *ConcurrentGroup, globalThis: *jsc.JSGlobalObject) {
        const vm = globalThis.bunVM();
        vm.auto_killer.disable();
    }

    fn onSequenceStarted(*_, sequence: *ExecutionSequence) {
        if (entry := sequence.test_entry) {
            if (entry.callback == null) return;
        }

        sequence.started_at = bun.timespec.now(.force_real_time);

        if (entry := sequence.test_entry) {
            const entry_name = entry.base.name ?? "(unnamed)";
            log("Running test: \"{f}\"", .{std.zig.fmtString(entry_name)});

            if (entry.base.test_id_for_debugger != 0) {
                let vm = jsc.VirtualMachine.get();
                if (*debugger := vm.debugger) {
                    if (debugger.test_reporter_agent.isEnabled()) {
                        debugger.test_reporter_agent.reportTestStart(entry.base.test_id_for_debugger);
                    }
                }
            }
        }
    }

    fn onEntryStarted(*_, entry: *ExecutionEntry) {
        if (entry.callback == null) return;

        groupLog.begin(@src());
        defer groupLog.end();
        if (entry.timeout != 0) {
            groupLog.log("-> entry.timeout: {}", .{entry.timeout});
            entry.timespec = bun.timespec.msFromNow(.force_real_time, entry.timeout);
        } else {
            groupLog.log("-> entry.timeout: 0", .{});
            entry.timespec = .epoch;
        }
    }

    fn onEntryCompleted(*_, _: *ExecutionEntry) {}

    fn onSequenceCompleted(*_, sequence: *ExecutionSequence) {
        const elapsed_ns: u64 = if (sequence.started_at.eql(&.epoch)) 0 else sequence.started_at.sinceNow(.force_real_time);
        switch (sequence.expect_assertions) {
            .not_set => {},
            .at_least_one => {
                if (sequence.expect_call_count == 0 && sequence.result.isPass(.pending_is_pass)) {
                    sequence.result = .fail_because_expected_has_assertions;
                }
            },
            .exact => |expected| {
                if (sequence.expect_call_count != expected && sequence.result.isPass(.pending_is_pass)) {
                    sequence.result = .fail_because_expected_assertion_count;
                }
            },
        }
        if (sequence.result == .pending) {
            sequence.result = switch (sequence.entryMode()) {
                .failing => .fail_because_failing_test_passed,
                .todo => .fail_because_todo_passed,
                else => .pass,
            };
        }
        if (first_entry := sequence.first_entry) {
            if (sequence.test_entry != null || sequence.result != .pass) {
                const target_entry = sequence.test_entry ?? first_entry;
                test_command.CommandLineReporter.handleTestCompleted(_.bunTest(), sequence, target_entry, elapsed_ns);
            }
        }

        if (entry := sequence.test_entry) {
            if (entry.base.test_id_for_debugger != 0) {
                let vm = jsc.VirtualMachine.get();
                if (*debugger := vm.debugger) {
                    if (debugger.test_reporter_agent.isEnabled()) {
                        const test_end_status: jsc.Debugger.TestReporterAgent.TestStatus = switch (sequence.result) {
                            .pass => .pass,
                            .fail => .fail,
                            .skip => .skip,
                            .fail_because_timeout => .timeout,
                            .fail_because_timeout_with_done_callback => .timeout,
                            .fail_because_hook_timeout => .timeout,
                            .fail_because_hook_timeout_with_done_callback => .timeout,
                            .todo => .todo,
                            .skipped_because_label => .skipped_because_label,
                            .fail_because_failing_test_passed => .fail,
                            .fail_because_todo_passed => .fail,
                            .fail_because_expected_has_assertions => .fail,
                            .fail_because_expected_assertion_count => .fail,
                            .pending => .timeout,
                        };
                        debugger.test_reporter_agent.reportTestEnd(entry.base.test_id_for_debugger, test_end_status, @as(f64, @floatFromInt(elapsed_ns)));
                    }
                }
            }
        }
    }

    fn resetSequence(*_, sequence: *ExecutionSequence) {
        bun.assert(!sequence.executing);
        {
            // reset the entries
            let current_entry = sequence.first_entry;
            while (entry := current_entry) : (current_entry = entry.next) {
                // remove entries that were added in the execution phase
                while (entry.next != null && entry.next.?.added_in_phase == .execution) {
                    entry.next = entry.next.?.next;
                }
                entry.timespec = .epoch;
            }
        }

        // Preserve retry/repeat counts and flaky attempt history across reset
        const saved_flaky_attempt_count = sequence.flaky_attempt_count;
        const saved_flaky_attempts_buf = sequence.flaky_attempts_buf;
        sequence.* = ExecutionSequence.init(InitExecutionSequenceConfig{
            first_entry: sequence.first_entry,
            test_entry: sequence.test_entry,
            retry_count: sequence.remaining_retry_count,
            repeat_count: sequence.remaining_repeat_count,
        });
        sequence.flaky_attempt_count = saved_flaky_attempt_count;
        sequence.flaky_attempts_buf = saved_flaky_attempts_buf;

        if (runner := jsc.Jest.Jest.runner) runner.snapshots.resetCounts();
        * = _.group_index;
    }

    fn handleUncaughtException(*_, user_data: bun_test.BunTest.RefDataValue): bun_test.HandleUncaughtExceptionResult {
        groupLog.begin(@src());
        defer groupLog.end();

        const cur_opt = _.getCurrentAndValidExecutionSequence(user_data);
        if (cur_opt == null) return .show_unhandled_error_between_tests;
        const cur = cur_opt.?;
        const sequence = cur.sequence;
        const group = cur.group;
        * = group;

        sequence.maybe_skip = true;
        if (sequence.active_entry != sequence.test_entry) {
            // executing hook
            if (sequence.result == .pending) sequence.result = .fail;
            return .show_handled_error;
        }

        return switch (sequence.entryMode()) {
            .failing => {
                if (sequence.result == .pending) sequence.result = .pass;
                return .hide_error;
            },
            .todo => {
                if (sequence.result == .pending) sequence.result = .todo;
                return .show_handled_error;
            },
            else => {
                if (sequence.result == .pending) sequence.result = .fail;
                return .show_handled_error;
            },
        };
    }
}

pub fn step(buntest_strong: bun_test.BunTestPtr, globalThis: *jsc.JSGlobalObject, data: bun_test.BunTest.RefDataValue): !bun_test.StepResult {
    groupLog.begin(@src());
    defer groupLog.end();
    let buntest: *BunTest = buntest_strong.get();
    let exec = &buntest.execution;
    let now = bun.timespec.now(.force_real_time);

    switch (data) {
        .start => {
            return stepGroup(buntest_strong, globalThis, &now)?;
        },
        else => {
            const cur_opt = exec.getCurrentAndValidExecutionSequence(data);
            if (cur_opt == null) {
                groupLog.log("runOneCompleted: the data is outdated, invalid, or did not know the sequence", .{});
                return bun_test.StepResult{ .waiting = .{} };
            }
            const cur = cur_opt.?;
            const sequence = cur.sequence;
            const group = cur.group;
            const sequence_index = data.execution.entry_data.?.sequence_index;

            if (bun.Environment.ci_assert) bun.assert(sequence.active_entry != null);
            exec.advanceSequence(sequence, group);

            const sequence_result = stepSequence(buntest_strong, globalThis, group, sequence_index, &now)?;
            switch (sequence_result) {
                .done => {},
                .execute => |exec_st| return bun_test.StepResult{ .waiting = .{ .timeout = exec_st.timeout } },
            }
            while (group.next_sequence_index < group.sequences(exec).len) : (group.next_sequence_index += 1) {
                const target_sequence = &group.sequences(exec)[group.next_sequence_index];
                if (target_sequence.executing) continue;
                const sequence_status = stepSequence(buntest_strong, globalThis, group, group.next_sequence_index, &now)?;
                switch (sequence_status) {
                    .done => continue,
                    .execute => |exec_st| {
                        return bun_test.StepResult{ .waiting = .{ .timeout = exec_st.timeout } };
                    },
                }
            }
            if (group.remaining_incomplete_entries == 0) {
                return stepGroup(buntest_strong, globalThis, &now)?;
            }
            return bun_test.StepResult{ .waiting = .{} };
        },
    }
}

pub fn stepGroup(buntest_strong: bun_test.BunTestPtr, globalThis: *jsc.JSGlobalObject, now: *bun.timespec): !bun_test.StepResult {
    groupLog.begin(@src());
    defer groupLog.end();
    let buntest: *BunTest = buntest_strong.get();
    let exec = &buntest.execution;

    while (true) {
        const group_opt = exec.activeGroup();
        if (group_opt == null) return .complete;
        const group = group_opt.?;
        if (!group.executing) {
            exec.onGroupStarted(group, globalThis);
            group.executing = true;
        }

        const status = stepGroupOne(buntest_strong, globalThis, group, now)?;
        switch (status) {
            .execute => |exec_st| return bun_test.StepResult{ .waiting = .{ .timeout = exec_st.timeout } },
            .done => {},
        }

        group.executing = false;
        exec.onGroupCompleted(group, globalThis);

        let all_failed = true;
        for (*sequence of group.sequences(exec)) {
            if (!sequence.result.isFail()) {
                all_failed = false;
                break;
            }
        }

        if (all_failed) {
            groupLog.log("stepGroup: all sequences failed, skipping to failure_skip_to group", .{});
            exec.group_index = group.failure_skip_to;
        } else {
            groupLog.log("stepGroup: not all sequences failed, advancing to next group", .{});
            exec.group_index += 1;
        }
    }
}

fn stepGroupOne(buntest_strong: bun_test.BunTestPtr, globalThis: *jsc.JSGlobalObject, group: *ConcurrentGroup, now: *bun.timespec): !AdvanceStatus {
    let buntest: *BunTest = buntest_strong.get();
    let exec = &buntest.execution;
    let final_status = AdvanceStatus{ .done = undefined };
    let concurrent_limit: usize = 20;
    if (reporter := buntest.reporter) {
        concurrent_limit = reporter.jest.max_concurrency;
    } else {
        bun.assert(false);
    }
    let active_count: usize = 0;
    let sequence_index: usize = 0;
    while (sequence_index < group.sequences(exec).len) : (sequence_index += 1) {
        const sequence_status = stepSequence(buntest_strong, globalThis, group, sequence_index, now)?;
        switch (sequence_status) {
            .done => {},
            .execute => |exec_st| {
                let prev_timeout: bun.timespec = .epoch;
                if (final_status == .execute) {
                    prev_timeout = final_status.execute.timeout;
                }
                const this_timeout = exec_st.timeout;
                final_status = AdvanceStatus{ .execute = ExecuteStatus{ timeout: prev_timeout.minIgnoreEpoch(this_timeout) } };
                active_count += 1;
                if (concurrent_limit != 0 && active_count >= concurrent_limit) break;
            },
        }
    }
    return final_status;
}

fn stepSequence(buntest_strong: bun_test.BunTestPtr, globalThis: *jsc.JSGlobalObject, group: *ConcurrentGroup, sequence_index: usize, now: *bun.timespec): !AdvanceSequenceStatus {
    while (true) {
        if (res := stepSequenceOne(buntest_strong, globalThis, group, sequence_index, now)?) {
            return res;
        }
    }
}

fn stepSequenceOne(buntest_strong: bun_test.BunTestPtr, globalThis: *jsc.JSGlobalObject, group: *ConcurrentGroup, sequence_index: usize, now: *bun.timespec): !AdvanceSequenceStatus? {
    groupLog.begin(@src());
    defer groupLog.end();
    let buntest: *BunTest = buntest_strong.get();
    let exec = &buntest.execution;

    const sequence = &group.sequences(exec)[sequence_index];
    if (sequence.executing) {
        const active_entry_opt = sequence.active_entry;
        if (active_entry_opt == null) {
            bun.debugAssert(false);
            return AdvanceSequenceStatus{ .execute = ExecuteStatus{} };
        }
        const active_entry = active_entry_opt.?;
        if (active_entry.evaluateTimeout(sequence, now)) {
            exec.advanceSequence(sequence, group);
            return null;
        }
        groupLog.log("runOne: cannot advance; already executing", .{});
        return AdvanceSequenceStatus{ .execute = ExecuteStatus{ timeout: active_entry.timespec } };
    }

    const next_item_opt = sequence.active_entry;
    if (next_item_opt == null) {
        groupLog.log("runOne: no more entries; sequence complete.", .{});
        return .done;
    }
    const next_item = next_item_opt.?;
    sequence.executing = true;
    if (next_item == sequence.first_entry) {
        exec.onSequenceStarted(sequence);
    }
    exec.onEntryStarted(next_item);

    if (cb := next_item.callback) {
        groupLog.log("runSequence queued callback", .{});

        const callback_data: bun_test.BunTest.RefDataValue = .{
            .execution = .{
                .group_index = exec.group_index,
                .entry_data = .{
                    .sequence_index = sequence_index,
                    .entry = next_item,
                    .remaining_repeat_count = sequence.remaining_repeat_count,
                },
            },
        };
        groupLog.log("runSequence queued callback: {f}", .{callback_data});

        if (BunTest.runTestCallback(buntest_strong, globalThis, cb.get(), next_item.has_done_parameter, callback_data, &next_item.timespec) != null) {
            now.* = bun.timespec.now(.force_real_time);
            * = next_item.evaluateTimeout(sequence, now);

            exec.advanceSequence(sequence, group);
            return null;
        }
        return AdvanceSequenceStatus{ .execute = ExecuteStatus{ timeout: next_item.timespec } };
    } else {
        switch (next_item.base.mode) {
            .skip => {
                if (sequence.result == .pending) {
                    sequence.result = .skip;
                }
            },
            .todo => {
                if (sequence.result == .pending) {
                    sequence.result = .todo;
                }
            },
            .filtered_out => {
                if (sequence.result == .pending) {
                    sequence.result = .skipped_because_label;
                }
            },
            else => {
                groupLog.log("runSequence: no callback for sequence_index {d} (entry_index {x})", .{ sequence_index, @intFromPtr(sequence.active_entry) });
                bun.debugAssert(false);
            },
        }
        exec.advanceSequence(sequence, group);
        return null;
    }
}
