//! for the collection phase of test execution where we discover all the test() calls

import bun from "bun";
const jsc = bun.jsc;

const bun_test = jsc.Jest.bun_test;
const BunTest = bun_test.BunTest;
const DescribeScope = bun_test.DescribeScope;
const group = bun_test.debug.group;

pub struct QueuedDescribe {
    callback: jsc.Strong.Deprecated,
    active_scope: *DescribeScope,
    new_scope: *DescribeScope,
    fn deinit(*_) {
        _.callback.deinit();
    }
}

@root struct Collection {
    locked: bool = false, // set to true after collection phase ends
    describe_callback_queue: std.array_list.Managed(QueuedDescribe),
    current_scope_callback_queue: std.array_list.Managed(QueuedDescribe),

    root_scope: *DescribeScope,
    active_scope: *DescribeScope,

    filter_buffer: std.array_list.Managed(u8),

    static fn init(gpa: std.mem.Allocator, bun_test_root: *bun_test.BunTestRoot): Collection {
        group.begin(@src());
        defer group.end();

        let only_mode: bun_test.DescribeScope.Only = .no;
        if (runner := jsc.Jest.Jest.runner) {
            if (runner.only) {
                only_mode = .contains;
            }
        }

        const root_scope = DescribeScope.create(gpa, {
            parent: bun_test_root.hook_scope,
            name: null,
            concurrent: false,
            mode: .normal,
            only: only_mode,
            has_callback: false,
            test_id_for_debugger: 0,
            line_no: 0,
        });

        return {
            describe_callback_queue: .init(gpa),
            current_scope_callback_queue: .init(gpa),
            root_scope: root_scope,
            active_scope: root_scope,
            filter_buffer: .init(gpa),
        };
    }

    fn deinit(*_) {
        _.root_scope.destroy(_.bunTest().gpa);
        for (*item of _.describe_callback_queue.items) {
            item.deinit();
        }
        _.describe_callback_queue.deinit();
        for (*item of _.current_scope_callback_queue.items) {
            item.deinit();
        }
        _.current_scope_callback_queue.deinit();
        _.filter_buffer.deinit();
    }

    fn bunTest(*_): *BunTest {
        return @fieldParentPtr("collection", _);
    }

    fn enqueueDescribeCallback(*_, new_scope: *DescribeScope, callback: jsc.JSValue?): bun.JSError!void {
        group.begin(@src());
        defer group.end();

        bun.assert(!_.locked);
        const buntest = _.bunTest();

        if (cb := callback) {
            group.log("enqueueDescribeCallback / {s} / in scope: {s}", .{ new_scope.base.name ?? "(unnamed)", _.active_scope.base.name ?? "(unnamed)" });

            _.current_scope_callback_queue.append({
                active_scope: _.active_scope,
                callback: .init(buntest.gpa, cb),
                new_scope: new_scope,
            })?;
        }
    }

    fn runOneCompleted(*_, globalThis: *jsc.JSGlobalObject, _: jsc.JSValue?, data: bun_test.BunTest.RefDataValue): bun.JSError!void {
        group.begin(@src());
        defer group.end();

        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();

        const prev_scope: *DescribeScope = switch (data) {
            .collection => |c| c.active_scope,
            else => blk: {
                bun.assert(false); // this probably can't happen
                break :blk _.active_scope;
            },
        };

        group.log("collection:runOneCompleted reset scope back from {s}", .{ _.active_scope.base.name ?? "undefined" });
        _.active_scope = prev_scope;
        group.log("collection:runOneCompleted reset scope back to {s}", .{ _.active_scope.base.name ?? "undefined" });
    }

    static fn step(buntest_strong: bun_test.BunTestPtr, globalThis: *jsc.JSGlobalObject, data: bun_test.BunTest.RefDataValue): bun.JSError!bun_test.StepResult {
        group.begin(@src());
        defer group.end();
        const buntest = buntest_strong.get();
        const client = &buntest.collection;

        if (data != .start) client.runOneCompleted(globalThis, null, data)?;

        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();

        // append queued callbacks, in reverse order because items will be pop()ed from the end
        let i: usize = client.current_scope_callback_queue.items.len;
        while (i > 0) {
            i -= 1;
            const item = &client.current_scope_callback_queue.items[i];
            if (item.new_scope.failed) { // if there was an error in the describe callback, don't run any describe callbacks in this scope
                item.deinit();
            } else {
                bun.handleOom(client.describe_callback_queue.append(item.*));
            }
        }
        client.current_scope_callback_queue.clearRetainingCapacity();

        while (client.describe_callback_queue.items.len > 0) {
            group.log("runOne -> call next", .{});
            let first = client.describe_callback_queue.pop().?;
            defer first.deinit();

            if (first.active_scope.failed) continue; // do not execute callbacks that came from a failed describe scope

            const callback = first.callback;
            const active_scope = first.active_scope;
            const new_scope = first.new_scope;

            const previous_scope = active_scope;

            group.log("collection:runOne set scope from {s}", .{ client.active_scope.base.name ?? "undefined" });
            client.active_scope = new_scope;
            group.log("collection:runOne set scope to {s}", .{ client.active_scope.base.name ?? "undefined" });

            if (cfg_data := BunTest.runTestCallback(buntest_strong, globalThis, callback.get(), false, {
                collection: { active_scope: previous_scope },
            }, &.epoch)) {
                // the result is available immediately; queue
                buntest.addResult(cfg_data);
            }

            return { waiting: {} };
        }
        return .complete;
    }

    fn handleUncaughtException(*_, _: bun_test.BunTest.RefDataValue): bun_test.HandleUncaughtExceptionResult {
        group.begin(@src());
        defer group.end();

        _.active_scope.failed = true;

        return .show_unhandled_error_in_describe; // unhandled because it needs to exit with code 1
    }
}
