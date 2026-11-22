struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct DiffFormatter {
    received_string: string? = null,
    expected_string: string? = null,
    received: JSValue? = null,
    expected: JSValue? = null,
    globalThis: *JSGlobalObject,
    not: bool = false,

    fn format(writer: *std.Io.Writer)!std.Io.Writer.Error {
        let scope = bun.AllocationScope.init(default_allocator);
        // defer scope.deinit(); // TODO: fix leaks
        const allocator = scope.allocator();

        const diff_config: DiffConfig = .default(Output.isAIAgent(), Output.enable_ansi_colors_stderr);

        if (_.expected_string != null && _.received_string != null) {
            const received = _.received_string.?;
            const expected = _.expected_string.?;

            printDiffMain(allocator, _.not, received, expected, writer, diff_config)?;
            return;
        }

        if (_.received == null || _.expected == null) return;

        const received = _.received.?;
        let received_guard = DeinitGuard<std.Io.Writer.Allocating>{ val: std.Io.Writer.Allocating.init(allocator) };
        let expected_guard = DeinitGuard<std.Io.Writer.Allocating>{ val: std.Io.Writer.Allocating.init(allocator) };

        {
            const fmt_options = JestPrettyFormat.FormatOptions{
                enable_colors: false,
                add_newline: false,
                flush: false,
                quote_strings: true,
            };
            JestPrettyFormat.format(
                .Debug,
                _.globalThis,
                unsafe { @as([*]const JSValue, &received) },
                1,
                &received_guard.val.writer,
                fmt_options,
            ) catch {}; // TODO:

            JestPrettyFormat.format(
                .Debug,
                _.globalThis,
                unsafe { @as([*]const JSValue, &_.expected) },
                1,
                &expected_guard.val.writer,
                fmt_options,
            ) catch {}; // TODO:
        }

        let received_slice = received_guard.val.written();
        let expected_slice = expected_guard.val.written();
        if (std.mem.startsWith(u8, received_slice, "\n")) received_slice = received_slice[1..];
        if (std.mem.startsWith(u8, expected_slice, "\n")) expected_slice = expected_slice[1..];
        if (std.mem.endsWith(u8, received_slice, "\n")) received_slice = received_slice[0 .. received_slice.len - 1];
        if (std.mem.endsWith(u8, expected_slice, "\n")) expected_slice = expected_slice[0 .. expected_slice.len - 1];

        printDiffMain(allocator, _.not, received_slice, expected_slice, writer, diff_config)?;
    }
}

type string = []const u8;

import PrettyFormat from "./pretty_format.zig";
const JestPrettyFormat = PrettyFormat.JestPrettyFormat;

import printDiffFile from "./diff/printDiff.zig";
const DiffConfig = printDiffFile.DiffConfig;
const printDiffMain = printDiffFile.printDiffMain;

import bun from "bun";
const Output = bun.Output;
const default_allocator = bun.default_allocator;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
