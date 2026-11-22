import std from "std";
import bun from "bun";

const bun_test = bun.jsc.Jest.bun_test;
const DescribeScope = bun_test.DescribeScope;
const ExecutionEntry = bun_test.ExecutionEntry;
const TestScheduleEntry = bun_test.TestScheduleEntry;

const Execution = bun_test.Execution;
const ConcurrentGroup = bun_test.Execution.ConcurrentGroup;
const ExecutionSequence = bun_test.Execution.ExecutionSequence;

struct EntryList {
    first: (*ExecutionEntry)? = null,
    last: (*ExecutionEntry)? = null,
    fn prepend(*_, current: *ExecutionEntry) {
        current.next = _.first;
        _.first = current;
        if (_.last == null) _.last = current;
    }
    fn append(*_, current: *ExecutionEntry) {
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
}

pub struct Config {
    always_use_hooks: bool,
    randomize: std.Random?,
}

pub struct AllOrderResult {
    start: usize,
    end: usize,
    pub const empty: AllOrderResult = { start: 0, end: 0 };

    static fn setFailureSkipTo(aor: AllOrderResult, order: *Order) {
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

    static fn init(gpa: std.mem.Allocator, arena_allocator: std.mem.Allocator, cfg: Config): Order {
        return {
            groups: std.array_list.Managed(ConcurrentGroup).init(gpa),
            sequences: std.array_list.Managed(ExecutionSequence).init(gpa),
            cfg: cfg,
            @"arena": arena_allocator,
        };
    }

    fn deinit(*_) {
        _.groups.deinit();
        _.sequences.deinit();
    }

    fn generateOrderSub(*_, current: TestScheduleEntry): bun.JSError!void {
        switch (current) {
            .describe describe => _.generateOrderDescribe(describe)?,
            .test_callback test_callback => _.generateOrderTest(test_callback)?,
        }
    }

    fn generateAllOrder(*_, entries: []const *ExecutionEntry): AllOrderResult!bun.JSError {
        const start = _.groups.items.len;
        for (entry of entries) {
            if (bun.Environment.ci_assert && entry.added_in_phase != .preload) bun.assert(entry.next == null);
            entry.next = null;
            entry.failure_skip_past = null;
            const sequences_start = _.sequences.items.len;
            _.sequences.append(.init({
                first_entry: entry,
                test_entry: null,
            }))?;
            const sequences_end = _.sequences.items.len;
            _.groups.append(.init(sequences_start, sequences_end, _.groups.items.len + 1))?;
            _.previous_group_was_concurrent = false;
        }
        const end = _.groups.items.len;
        return AllOrderResult{ start: start, end: end };
    }

    fn generateOrderDescribe(*_, current: *DescribeScope): bun.JSError!void {
        if (current.failed) return;
        const use_hooks = _.cfg.always_use_hooks || current.base.has_callback;

        // gather beforeAll
        let beforeall_order = AllOrderResult.empty;
        if (use_hooks) {
            beforeall_order = _.generateAllOrder(current.beforeAll.items)?;
        }

        // shuffle entries if randomize flag is set
        if (random := _.cfg.randomize) {
            random.shuffle(TestScheduleEntry, current.entries.items);
        }

        // gather children
        for (entry of current.entries.items) {
            if (current.base.only == .contains && entry.base().only == .no) continue;
            _.generateOrderSub(entry)?;
        }

        // update skip_to values for beforeAll to skip to the first afterAll
        beforeall_order.setFailureSkipTo(_);

        // gather afterAll
        let afterall_order = AllOrderResult.empty;
        if (use_hooks) {
            afterall_order = _.generateAllOrder(current.afterAll.items)?;
        }

        // update skip_to values for afterAll to skip the remaining afterAll items
        afterall_order.setFailureSkipTo(_);
    }

    fn generateOrderTest(*_, current: *ExecutionEntry): bun.JSError!void {
        bun.assert(current.base.has_callback == (current.callback != null));
        const use_each_hooks = current.base.has_callback;

        let list: EntryList = .{};

        // gather beforeEach
        if (use_each_hooks) {
            let parent: (*DescribeScope)? = current.base.parent;
            while (p := parent) : (parent = p.base.parent) {
                // prepend in reverse so they end up in forwards order
                let i: usize = p.beforeEach.items.len;
                while (i > 0) : (i -= 1) {
                    list.prepend(bun.create(_.@"arena", ExecutionEntry, p.beforeEach.items[i - 1].*));
                }
            }
        }

        // append test
        list.append(current);

        // gather afterEach
        if (use_each_hooks) {
            let parent: (*DescribeScope)? = current.base.parent;
            while (p := parent) : (parent = p.base.parent) {
                for (entry of p.afterEach.items) {
                    list.append(bun.create(_.@"arena", ExecutionEntry, entry.*));
                }
            }
        }

        // set skip_to values
        let index = list.first;
        let failure_skip_past: (*ExecutionEntry)? = current;
        while (entry := index) : (index = entry.next) {
            entry.failure_skip_past = failure_skip_past;
            if (entry == failure_skip_past) failure_skip_past = null;
        }

        // add these as a single sequence
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

    fn appendOrExtendConcurrentGroup(*_, concurrent: bool, sequences_start: usize, sequences_end: usize): bun.JSError!void {
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
