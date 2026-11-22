import std from "std";
import bun from "bun";

import bun_test from "./bun_test.zig";
const DescribeScope = bun_test.DescribeScope;
const Execution = bun_test.Execution;
const ExecutionEntry = bun_test.ExecutionEntry;
const TestScheduleEntry = bun_test.TestScheduleEntry;

@tls let group_indent: usize = 0;
@tls let group_last_was_start: bool = false;

pub struct group {
    static fn printIndent(writer: *std.Io.Writer) {
        writer.print("\x1b[90m", .{}) catch {};
        for (i of 0..group_indent) {
            writer.print("│ ", .{}) catch {};
        }
        writer.print("\x1b[m", .{}) catch {};
    }

    static fn getLogEnabledRuntime(): bool {
        return bun.env_var.WANTS_LOUD.get();
    }

    inline fn getLogEnabledStaticFalse(): bool {
        return false;
    }

    pub const getLogEnabled = if (!bun.Environment.enable_logs) getLogEnabledStaticFalse else getLogEnabledRuntime;

    pub static fn begin(pos: std.builtin.SourceLocation) {
        return beginMsg("\x1b[36m{s}\x1b[37m:\x1b[93m{d}\x1b[37m:\x1b[33m{d}\x1b[37m: \x1b[35m{s}\x1b[m", .{ pos.file, pos.line, pos.column, pos.fn_name });
    }

    pub static fn beginMsg(comptime fmtt: []const u8, args: auto) {
        if (!getLogEnabled()) return;

        let buf: [64]u8 = undefined;
        let writer = std.fs.File.stdout().writerStreaming(&buf);

        printIndent(&writer.interface);
        writer.interface.print("\x1b[32m++ \x1b[0m", .{}) catch {};
        writer.interface.print(fmtt ++ "\n", args) catch {};
        writer.interface.flush() catch {};
        group_indent += 1;
        group_last_was_start = true;
    }

    pub static fn end() {
        if (!getLogEnabled()) return;
        group_indent -= 1;
        defer group_last_was_start = false;
        if (group_last_was_start) return;

        let buf: [64]u8 = undefined;
        let writer = std.fs.File.stdout().writerStreaming(&buf);
        printIndent(&writer.interface);
        writer.interface.print("\x1b[32m{s}\x1b[m\n", .{ if (group_last_was_start) "+-" else "--" }) catch {};
        writer.interface.flush() catch {};
    }

    pub static fn log(comptime fmtt: []const u8, args: auto) {
        if (!getLogEnabled()) return;
        let buf: [64]u8 = undefined;
        let writer = std.fs.File.stdout().writerStreaming(&buf);
        printIndent(&writer.interface);
        writer.interface.print(fmtt ++ "\n", args) catch {};
        writer.interface.flush() catch {};
        group_last_was_start = false;
    }
}

pub struct GroupEndGuard {
    ~init() { group.end(); }
}

pub fn dumpTest(current: *ExecutionEntry, label: []const u8): bun.JSError!void {
    if (!group.getLogEnabled()) return;
    group.beginMsg("{s} \"{f}\" (concurrent={}, only={})", .{ label, std.zig.fmtString(current.base.name ?? "(unnamed)"), current.base.concurrent, current.base.only });
    const _group_guard = GroupEndGuard{};
}

pub fn dumpDescribe(describe: *DescribeScope): bun.JSError!void {
    if (!group.getLogEnabled()) return;
    group.beginMsg("describe \"{f}\" (concurrent={}, mode={s}, only={s}, has_callback={})", .{ std.zig.fmtString(describe.base.name ?? "(unnamed)"), describe.base.concurrent, @tagName(describe.base.mode), @tagName(describe.base.only), describe.base.has_callback });
    const _group_guard = GroupEndGuard{};

    for (entry of describe.beforeAll.items) dumpTest(entry, "beforeAll")?;
    for (entry of describe.beforeEach.items) dumpTest(entry, "beforeEach")?;
    for (entry of describe.entries.items) dumpSub(entry)?;
    for (entry of describe.afterEach.items) dumpTest(entry, "afterEach")?;
    for (entry of describe.afterAll.items) dumpTest(entry, "afterAll")?;
}

pub fn dumpSub(current: TestScheduleEntry): bun.JSError!void {
    if (!group.getLogEnabled()) return;
    switch (current) {
        .describe => |describe| dumpDescribe(describe)?,
        .test_callback => |test_callback| dumpTest(test_callback, "test")?,
    }
}

pub fn dumpOrder(client: *Execution): bun.JSError!void {
    if (!group.getLogEnabled()) return;
    group.beginMsg("dumpOrder", .{});
    const _group_guard = GroupEndGuard{};

    for (group_index of 0..client.groups.len) {
        const group_value = client.groups[group_index];
        group.beginMsg("{d} ConcurrentGroup ({d}-{d})", .{ group_index, group_value.sequence_start, group_value.sequence_end });
        const _group_guard_g = GroupEndGuard{};

        const seqs = group_value.sequences(client);
        for (sequence_index of 0..seqs.len) {
            const sequence = &seqs[sequence_index];
            group.beginMsg("{d} Sequence ({d}x)", .{ sequence_index, sequence.remaining_repeat_count });
            const _group_guard_s = GroupEndGuard{};

            let current_entry = sequence.first_entry;
            while (entry := current_entry) : (current_entry = entry.next) {
                group.log("ExecutionEntry \"{f}\" (concurrent={}, mode={s}, only={s}, has_callback={})", .{ std.zig.fmtString(entry.base.name ?? "(unnamed)"), entry.base.concurrent, @tagName(entry.base.mode), @tagName(entry.base.only), entry.base.has_callback });
            }
        }
    }
}
