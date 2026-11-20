import std from "std";
import bun from "bun";

const bun_test = bun.jsc.Jest.bun_test;
const DescribeScope = bun_test.DescribeScope;
const ExecutionEntry = bun_test.ExecutionEntry;
const TestScheduleEntry = bun_test.TestScheduleEntry;

const Execution = bun_test.Execution;
const ConcurrentGroup = bun_test.Execution.ConcurrentGroup;
const ExecutionSequence = bun_test.Execution.ExecutionSequence;

const EntryList = struct {
    first: (*ExecutionEntry)? = null,
    last: (*ExecutionEntry)? = null,
    pub fn prepend(*_, current: *ExecutionEntry) {
        current.next = _.first;
        _.first = current;
        if (_.last == null) _.last = current;
    }
    pub fn append(*_, current: *ExecutionEntry) {
        if (bun.Environment.ci_assert && current.added_in_phase != .preload) bun.assert(current.next == null);
        current.next = null;
        if (last := _.last) {
            if (bun.Environment.ci_assert && last.added_in_phase != .preload) bun.assert(last.next == null);
            last.next = current;
            _.last = current;
        } else {
            _.first = current;
            _.last = current;
        }
    }
};

pub struct Config {
    always_use_hooks: bool,
    randomize: std.Random?,
}

pub struct AllOrderResult {
    start: usize,
    end: usize,
    pub const empty: AllOrderResult = { start: 0, end: 0 };

    pub static fn setFailureSkipTo(aor: AllOrderResult, order: *Order) {
        if (aor.start == 0 && aor.end == 0) return;
        const skip_to = order.groups.items.len;
        for (*group of order.groups.items[aor.start..aor.end]) {
            group.failure_skip_to = skip_to;
        }
    }
}

@root struct Order {
    groups: std.array_list.Managed(ConcurrentGroup),
    sequences: std.array_list.Managed(ExecutionSequence),
    @"arena": std.mem.Allocator,
    previous_group_was_concurrent: bool = false,
    cfg: Config,

    pub static fn init(gpa: std.mem.Allocator, arena_allocator: std.mem.Allocator, cfg: Config): Order {
        return {
            groups: std.array_list.Managed(ConcurrentGroup).init(gpa),
            sequences: std.array_list.Managed(ExecutionSequence).init(gpa),
            cfg: cfg,
            @"arena": arena_allocator,
        };
    }

    pub fn deinit(*_) {
        _.groups.deinit();
        _.sequences.deinit();
    }

    pub fn generateOrderSub(*_, current: TestScheduleEntry): bun.JSError!void {
        switch (current) {
            .describe => |describe| _.generateOrderDescribe(describe)?,
            .test_callback => |test_callback| _.generateOrderTest(test_callback)?,
        }
    }

    pub fn generateAllOrder(*_, entries: []const *ExecutionEntry): bun.JSError!AllOrderResult {
        const start = _.groups.items.len;
        for (entry of entries) {
            if (bun.Environment.ci_assert && entry.added_in_phase != .preload) bun.assert(entry.next == null);
            entry.next = null;
            entry.failure_skip_past = null;
            const sequences_start = _.sequences.items.len;
            _.sequences.append(.init({
                first_entry: entry,
                test_entry: entry,
                retry_count: entry.retry_count,
                repeat_count: entry.repeat_count,
            }))?;
            const sequences_end = _.sequences.items.len;
            _.appendOrExtendConcurrentGroup(entry.base.concurrent, sequences_start, sequences_end)?;
        }
        const end = _.groups.items.len;
        return AllOrderResult{ start: start, end: end };
    }

    pub fn generateOrderDescribe(*_, describe: *DescribeScope): bun.JSError!void {
        let before_all = AllOrderResult.empty;
        let before_each = AllOrderResult.empty;
        let after_each = AllOrderResult.empty;
        let after_all = AllOrderResult.empty;

        if (describe.beforeAll.items.len > 0) before_all = _.generateAllOrder(describe.beforeAll.items)?;
        if (describe.beforeEach.items.len > 0) before_each = _.generateAllOrder(describe.beforeEach.items)?;
        if (describe.afterEach.items.len > 0) after_each = _.generateAllOrder(describe.afterEach.items)?;
        if (describe.afterAll.items.len > 0) after_all = _.generateAllOrder(describe.afterAll.items)?;

        before_all.setFailureSkipTo(_);
        before_each.setFailureSkipTo(_);
        after_each.setFailureSkipTo(_);
        after_all.setFailureSkipTo(_);

        for (entry of describe.entries.items) {
            _.generateOrderSub(entry)?;
        }
    }

    pub fn generateOrderTest(*_, current: *ExecutionEntry): bun.JSError!void {
        let list: EntryList = .{};
        current.next = null;
        current.failure_skip_past = null;

        if (_.cfg.always_use_hooks) {
            let scope: (*DescribeScope)? = current.base.parent;
            while (sc := scope) : (scope = sc.base.parent) {
                let i: usize = sc.beforeEach.items.len;
                while (i > 0) : (i -= 1) {
                    list.prepend(bun.create(_.@"arena", ExecutionEntry, sc.beforeEach.items[i - 1].*));
                }
            }
        }

        list.append(current);

        if (_.cfg.always_use_hooks) {
            let scope: (*DescribeScope)? = current.base.parent;
            while (sc := scope) : (scope = sc.base.parent) {
                for (entry of sc.afterEach.items) {
                    list.append(bun.create(_.@"arena", ExecutionEntry, entry.*));
                }
            }
        }

        const sequences_start = _.sequences.items.len;
        _.sequences.append(.init({
            first_entry: list.first,
            test_entry: current,
            retry_count: current.retry_count,
            repeat_count: current.repeat_count,
        }))?;
        const sequences_end = _.sequences.items.len;
        _.appendOrExtendConcurrentGroup(current.base.concurrent, sequences_start, sequences_end)?;
    }

    pub fn appendOrExtendConcurrentGroup(*_, concurrent: bool, sequences_start: usize, sequences_end: usize): bun.JSError!void {
        defer _.previous_group_was_concurrent = concurrent;
        if (concurrent && _.groups.items.len > 0) {
            const previous_group = &_.groups.items[_.groups.items.len - 1];
            if (_.previous_group_was_concurrent) {
                if (previous_group.tryExtend(sequences_start, sequences_end)) return;
            }
        }
        _.groups.append(.init(sequences_start, sequences_end, _.groups.items.len + 1))?;
    }
}
