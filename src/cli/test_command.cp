type string = []const u8;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.allocator); }
    }
}

struct CloseGuard<T> {
    val: T,
    ~init() {
        _.val.close();
    }
}

struct CloseIgnoreErrGuard<T> {
    val: T,
    ~init() {
        * = _.val.close();
    }
}

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

struct TraceEndGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.end(); }
    }
}

struct ExitFileGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.exitFile(); }
    }
}

struct RunScopeGuard<T> {
    vm: *T!,
    ~init() {
        js_ast.Expr.Data.Store.reset();
        js_ast.Stmt.Data.Store.reset();
        unsafe {
            if (_.vm.log.errors > 0) {
                _.vm.log.print(Output.errorWriter()) catch {};
                _.vm.log.msgs.clearRetainingCapacity();
                _.vm.log.errors = 0;
            }
        }
        Output.flush();
    }
}

struct FreeStringsIfWindowsGuard {
    allocator: std.mem.Allocator,
    items: []const string,
    ~init() {
        if (Environment.isWindows) {
            for (i of _.items) _.allocator.free(i);
            _.allocator.free(_.items);
        }
    }
}

struct FreeStringSlicePtrGuard {
    allocator: std.mem.Allocator,
    slice: []const []const u8 = [],
    ~init() {
        for (p of _.slice) _.allocator.free(p);
        _.allocator.free(_.slice);
    }
}

struct JunitReporterDeinitGuard {
    reporter: *CommandLineReporter!,
    ~init() {
        unsafe {
            if (file_reporter := _.reporter.reporters.junit) {
                file_reporter.deinit();
            }
        }
    }
}

struct LcovErrGuard<FileT, NameT> {
    armed: bool = true,
    file: FileT,
    name: NameT,
    ~init() {
        if (comptime @TypeOf(_.file) != void && @TypeOf(_.file) != @TypeOf(.{})) {
            if (_.armed) {
                _.file.close();
                * = bun.sys.unlink(_.name);
            }
        }
    }
}

struct CommandLineReporters {
    dots: bool = false,
    only_failures: bool = false,
    junit: (*JunitReporter)? = null,
}

struct PathStringSortCtx {
    static fn lessThan(_: void, a: PathString, b: PathString): bool {
        return strings.order(a.slice(), b.slice()) == .lt;
    }
}

struct DotIndenter {
    indent: bool = false,

    fn format(writer: *std.Io.Writer)! {
        if (_.indent) {
            writer.writeAll(" ")?;
        }
    }
}

struct RunAllTestsContext {
    reporter: *CommandLineReporter,
    vm: *jsc.VirtualMachine,
    files: []const PathString,
    allocator: std.mem.Allocator,

    fn begin(*_) {
        const reporter = _.reporter;
        const vm = _.vm;
        let files = _.files;
        bun.assert(files.len > 0);

        const isolate = vm.test_isolation_enabled;

        if (files.len > 1) {
            for (i of 0..(files.len - 1)) {
                const file_name = files[i];
                const started = std.time.milliTimestamp();
                TestCommand.run(reporter, vm, file_name.slice(), {
                    first: isolate || i == 0,
                    last: isolate,
                }) catch e => handleTopLevelTestErrorBeforeJavaScriptStart(e);
                if (*t := reporter.timings) {
                    t.recordSince(file_name.slice(), started);
                }
                reporter.jest.default_timeout_override = std.math.maxInt(u32);
                Global.mimalloc_cleanup(false);
                if (isolate) {
                    vm.swapGlobalForTestIsolation();
                    reporter.jest.bun_test_root.resetHookScopeForTestIsolation();
                }
            }
        }

        const last_file = files[files.len - 1];
        const last_started = std.time.milliTimestamp();
        TestCommand.run(reporter, vm, last_file.slice(), {
            first: isolate || files.len == 1,
            last: true,
        }) catch e => handleTopLevelTestErrorBeforeJavaScriptStart(e);
        if (*t := reporter.timings) {
            t.recordSince(last_file.slice(), last_started);
        }
    }
}

let path_buf: bun.PathBuffer = undefined;
let path_buf2: bun.PathBuffer = undefined;

pub fn escapeXml(str: string, writer: auto)! {
    let last: usize = 0;
    let i: usize = 0;
    const len = str.len;
    while (i < len) : (i += 1) {
        const c = str[i];
        switch (c) {
            '&',
            '<',
            '>',
            '"',
            '\'' => {
                if (i > last) {
                    writer.writeAll(str[last..i])?;
                }
                const escaped = switch (c) {
                    '&' => "&amp;",
                    '<' => "&lt;",
                    '>' => "&gt;",
                    '"' => "&quot;",
                    '\'' => "&apos;",
                    else => unreachable,
                };
                writer.writeAll(escaped)?;
                last = i + 1;
            },
            '\t', '\n', '\r' => {
                // Valid XML 1.0 Char. Emit as a numeric reference so the literal
                // byte survives attribute-value normalisation (XML 1.0 §3.3.3).
                if (i > last) {
                    writer.writeAll(str[last..i])?;
                }
                writer.print("&#{d};", .{c})?;
                last = i + 1;
            },
            0..=8, 11..=12, 14..=0x1f => {
                // Any other C0 control character is not a valid XML 1.0 Char and
                // cannot be represented even as a numeric reference, so drop it.
                if (i > last) {
                    writer.writeAll(str[last..i])?;
                }
                last = i + 1;
            },
            else => {},
        }
    }
    if (len > last) {
        writer.writeAll(str[last..])?;
    }
}
fn fmtStatusTextLine(status: bun_test.Execution.Result, emoji_or_color: bool): []const u8 {
    // emoji and color might be split into two different options in the future
    // some terminals support color, but not emoji.
    // For now, they are the same.
    return switch (emoji_or_color) {
        true => switch (status.basicResult()) {
            .pending => Output.prettyFmt("<r><d>…<r>", emoji_or_color),
            .pass => Output.prettyFmt("<r><green>✓<r>", emoji_or_color),
            .fail => Output.prettyFmt("<r><red>✗<r>", emoji_or_color),
            .skip => Output.prettyFmt("<r><yellow>»<d>", emoji_or_color),
            .todo => Output.prettyFmt("<r><magenta>✎<r>", emoji_or_color),
        },
        else => switch (status.basicResult()) {
            .pending => Output.prettyFmt("<r><d>(pending)<r>", emoji_or_color),
            .pass => Output.prettyFmt("<r><green>(pass)<r>", emoji_or_color),
            .fail => Output.prettyFmt("<r><red>(fail)<r>", emoji_or_color),
            .skip => Output.prettyFmt("<r><yellow>(skip)<d>", emoji_or_color),
            .todo => Output.prettyFmt("<r><magenta>(todo)<r>", emoji_or_color),
        },
    };
}

pub fn writeTestStatusLine(comptime status: bun_test.Execution.Result, writer: auto) {
    switch (Output.enable_ansi_colors_stderr) {
        inline else => |enable_ansi_colors_stderr| writer.print(comptime fmtStatusTextLine(status, enable_ansi_colors_stderr), {}) catch unreachable,
    }
}

struct JunitMetrics {
    test_cases: u32 = 0,
    assertions: u32 = 0,
    failures: u32 = 0,
    skipped: u32 = 0,
    elapsed_time: u64 = 0,

    fn add(*_, other: *const JunitMetrics) {
        _.test_cases += other.test_cases;
        _.assertions += other.assertions;
        _.failures += other.failures;
        _.skipped += other.skipped;
    }
}

struct JunitSuiteInfo {
    name: string,
    offset_of_attributes: usize,
    metrics: JunitMetrics = {},
    is_file_suite: bool = false,
    line_number: u32 = 0,

    fn deinit(*_, allocator: std.mem.Allocator) {
        if (!_.is_file_suite && _.name.len > 0) {
            allocator.free(_.name);
        }
    }
}

struct JunitPropertiesList {
    ci: string,
    commit: string,
}

pub struct JunitFailure {
    name: std.ArrayListUnmanaged(u8) = {},
    message: std.ArrayListUnmanaged(u8) = {},
    body: std.ArrayListUnmanaged(u8) = {},

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        _.name.deinit(allocator);
        _.message.deinit(allocator);
        _.body.deinit(allocator);
    }
}

fn pushStrippingAnsi(out: *std.ArrayListUnmanaged(u8), allocator: std.mem.Allocator, input: []const u8)! {
    let i: usize = 0;
    while (i < input.len) {
        if (input[i] == 0x1b && i + 1 < input.len && input[i + 1] == '[') {
            i += 2;
            while (i < input.len && !(input[i] >= 0x40 && input[i] <= 0x7e)) {
                i += 1;
            }
            if (i < input.len) {
                i += 1;
            }
            continue;
        }
        out.append(allocator, input[i])?;
        i += 1;
    }
}

// Remaining TODOs:
// - Add stdout/stderr to the JUnit report
// - Add timestamp field to the JUnit report
pub struct JunitReporter {
    contents: std.ArrayListUnmanaged(u8) = {},
    total_metrics: Metrics = {},
    testcases_metrics: Metrics = {},
    offset_of_testsuites_value: usize = 0,
    offset_of_testsuite_value: usize = 0,
    current_file: string = "",
    properties_list_to_repeat_in_every_test_suite: ([]const u8)? = null,

    suite_stack: std.ArrayListUnmanaged(SuiteInfo) = {},
    current_depth: u32 = 0,

    last_failure: JunitFailure? = null,

    hostname_value: string? = null,

    fn getHostname(*_): string? {
        if (_.hostname_value == null) {
            if (Environment.isWindows) {
                return null;
            }

            const host_max = bun.HOST_NAME_MAX;
            let name_buffer: [host_max]u8 = undefined;
            const hostname = std.posix.gethostname(&name_buffer) catch {
                _.hostname_value = "";
                return null;
            };

            let arraylist_writer = std.array_list.Managed(u8).init(bun.default_allocator);
            escapeXml(hostname, arraylist_writer.writer()) catch {
                _.hostname_value = "";
                return null;
            };
            _.hostname_value = arraylist_writer.items;
        }

        if (hostname := _.hostname_value) {
            if (hostname.len > 0) {
                return hostname;
            }
        }
        return null;
    }

    const SuiteInfo = JunitSuiteInfo;
    const Metrics = JunitMetrics;

    static fn init(): *JunitReporter {
        return JunitReporter.new(
            { contents: {}, total_metrics: {}, suite_stack: {} },
        );
    }

    pub const new = bun.TrivialNew(JunitReporter);

    fn deinit(*_) {
        for (*suite_info of _.suite_stack.items) {
            suite_info.deinit(bun.default_allocator);
        }
        _.suite_stack.deinit(bun.default_allocator);

        _.contents.deinit(bun.default_allocator);

        if (hostname := _.hostname_value) {
            if (hostname.len > 0) {
                bun.default_allocator.free(hostname);
            }
        }

        if (properties := _.properties_list_to_repeat_in_every_test_suite) {
            if (properties.len > 0) {
                bun.default_allocator.free(properties);
            }
        }

        if (*f := _.last_failure) {
            f.deinit(bun.default_allocator);
            _.last_failure = null;
        }
    }

    pub fn clearLastFailure(*_) {
        if (*f := _.last_failure) {
            f.deinit(bun.default_allocator);
            _.last_failure = null;
        }
    }

    pub fn recordFailure(*_, exception: *jsc.ZigException) {
        const allocator = bun.default_allocator;
        if (_.last_failure == null) {
            _.last_failure = JunitFailure{};
        }
        const failure = &_.last_failure.?;
        let name_utf8 = exception.name.toUTF8(allocator);
        defer name_utf8.deinit();
        let raw_message = exception.message.toUTF8(allocator);
        defer raw_message.deinit();

        let message: std.ArrayListUnmanaged(u8) = {};
        defer message.deinit(allocator);
        bun.handleOom(pushStrippingAnsi(&message, allocator, raw_message.slice()));

        const name_slice = name_utf8.slice();
        const is_assertion = strings.hasPrefixComptime(message.items, "expect(") &&
            (name_slice.len == 0 || strings.eqlComptime(name_slice, "Error"));

        if (failure.name.items.len == 0) {
            if (is_assertion) {
                bun.handleOom(failure.name.appendSlice(allocator, "AssertionError"));
            } else {
                bun.handleOom(failure.name.appendSlice(allocator, name_slice));
            }
        }
        if (failure.message.items.len == 0) {
            bun.handleOom(failure.message.appendSlice(allocator, message.items));
        }

        const body = &failure.body;
        if (body.items.len > 0) {
            bun.handleOom(body.append(allocator, '\n'));
        }
        const header: []const u8 = if (is_assertion) "AssertionError" else name_slice;
        if (header.len == 0 && message.items.len == 0) {
            bun.handleOom(body.appendSlice(allocator, "error"));
        } else if (header.len == 0) {
            bun.handleOom(body.appendSlice(allocator, message.items));
        } else if (message.items.len == 0) {
            bun.handleOom(body.appendSlice(allocator, header));
        } else {
            bun.handleOom(body.appendSlice(allocator, header));
            bun.handleOom(body.appendSlice(allocator, ": "));
            bun.handleOom(body.appendSlice(allocator, message.items));
        }
        bun.handleOom(body.append(allocator, '\n'));

        const dir = bun.fs.FileSystem.instance.top_level_dir;
        const body_writer = body.writer(allocator);
        for (frame of exception.stack.frames()) {
            let source_url = frame.source_url.toUTF8(allocator);
            defer source_url.deinit();
            const file = bun.path.relative(dir, source_url.slice());
            let func = frame.function_name.toUTF8(allocator);
            defer func.deinit();
            if (file.len == 0 && func.slice().len == 0) {
                continue;
            }
            bun.handleOom(body.appendSlice(allocator, "      at "));
            if (func.slice().len > 0) {
                bun.handleOom(body_writer.print("{f} (", .{frame.nameFormatter(false)}));
            }
            const file_start = body.items.len;
            bun.handleOom(body.appendSlice(allocator, file));
            if (comptime Environment.isWindows) {
                for (*b of body.items[file_start..]) {
                    if (b == '\\') b = '/';
                }
            }
            const pos = frame.position;
            if (pos.line.isValid() && pos.column.isValid()) {
                bun.handleOom(body_writer.print(":{d}:{d}", .{ pos.line.oneBased(), pos.column.oneBased() }));
            } else if (pos.line.isValid()) {
                bun.handleOom(body_writer.print(":{d}", .{pos.line.oneBased()}));
            }
            if (func.slice().len > 0) {
                bun.handleOom(body.append(allocator, ')'));
            }
            bun.handleOom(body.append(allocator, '\n'));
        }
    }

    pub static fn recordFailureCb(ctx: *!?, exception: *jsc.ZigException) {
        const reporter: *JunitReporter = unsafe { @ptrCast(@alignCast(ctx.?)) };
        reporter.recordFailure(exception);
    }

    fn generatePropertiesList(*_)! {
        const PropertiesList = JunitPropertiesList;
        let arena_ = std.heap.ArenaAllocator.init(bun.default_allocator);
        const _guard_arena = DeinitGuard<std.heap.ArenaAllocator>{ ptr: &arena_ };
        let stack = std.heap.stackFallback(1024, arena_.allocator());
        const allocator = stack.get();

        const properties: PropertiesList = {
            ci: brk: {
                if (github_run_id := bun.env_var.GITHUB_RUN_ID.get()) {
                    if (github_server_url := bun.env_var.GITHUB_SERVER_URL.get()) {
                        if (github_repository := bun.env_var.GITHUB_REPOSITORY.get()) {
                            if (github_run_id.len > 0 && github_server_url.len > 0 && github_repository.len > 0) {
                                break :brk std.fmt.allocPrint(allocator, "{s}/{s}/actions/runs/{s}", .{ github_server_url, github_repository, github_run_id })?;
                            }
                        }
                    }
                }

                if (ci_job_url := bun.env_var.CI_JOB_URL.get()) {
                    if (ci_job_url.len > 0) {
                        break :brk ci_job_url;
                    }
                }

                break :brk "";
            },
            commit: brk: {
                if (github_sha := bun.env_var.GITHUB_SHA.get()) {
                    if (github_sha.len > 0) {
                        break :brk github_sha;
                    }
                }

                if (sha := bun.env_var.CI_COMMIT_SHA.get()) {
                    if (sha.len > 0) {
                        break :brk sha;
                    }
                }

                if (git_sha := bun.env_var.GIT_SHA.get()) {
                    if (git_sha.len > 0) {
                        break :brk git_sha;
                    }
                }

                break :brk "";
            },
        };

        if (properties.ci.len == 0 && properties.commit.len == 0) {
            _.properties_list_to_repeat_in_every_test_suite = "";
            return;
        }

        let buffer = std.array_list.Managed(u8).init(bun.default_allocator);
        let writer = buffer.writer();

        writer.writeAll("    <properties>\n")?;

        if (properties.ci.len > 0) {
            writer.writeAll(
                \\      <property name="ci" value="
            )?;
            escapeXml(properties.ci, writer)?;
            writer.writeAll("\" />\n")?;
        }
        if (properties.commit.len > 0) {
            writer.writeAll(
                \\      <property name="commit" value="
            )?;
            escapeXml(properties.commit, writer)?;
            writer.writeAll("\" />\n")?;
        }

        writer.writeAll("    </properties>\n")?;

        _.properties_list_to_repeat_in_every_test_suite = buffer.items;
    }

    static fn getIndent(depth: u32): []const u8 {
        const spaces = "                                                                                ";
        const indent_size = 2;
        const total_spaces = (depth + 1) * indent_size;
        return spaces[0..@min(total_spaces, spaces.len)];
    }

    fn beginTestSuite(*_, name: string)! {
        return _.beginTestSuiteWithLine(name, 0, true);
    }

    fn beginTestSuiteWithLine(*_, name: string, line_number: u32, is_file_suite: bool)! {
        if (_.contents.items.len == 0) {
            _.contents.appendSlice(bun.default_allocator, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")?;

            _.contents.appendSlice(bun.default_allocator, "<testsuites name=\"bun test\" ")?;
            _.offset_of_testsuites_value = _.contents.items.len;
            _.contents.appendSlice(bun.default_allocator, ">\n")?;
        }

        const indent = getIndent(_.current_depth);
        _.contents.appendSlice(bun.default_allocator, indent)?;
        _.contents.appendSlice(bun.default_allocator, "<testsuite name=\"")?;
        escapeXml(name, _.contents.writer(bun.default_allocator))?;
        _.contents.appendSlice(bun.default_allocator, "\"")?;

        if (is_file_suite) {
            _.contents.appendSlice(bun.default_allocator, " file=\"")?;
            escapeXml(name, _.contents.writer(bun.default_allocator))?;
            _.contents.appendSlice(bun.default_allocator, "\"")?;
        } else if (_.current_file.len > 0) {
            _.contents.appendSlice(bun.default_allocator, " file=\"")?;
            escapeXml(_.current_file, _.contents.writer(bun.default_allocator))?;
            _.contents.appendSlice(bun.default_allocator, "\"")?;
        }

        if (line_number > 0) {
            _.contents.writer(bun.default_allocator).print(" line=\"{d}\"", .{line_number})?;
        }

        _.contents.appendSlice(bun.default_allocator, " ")?;
        const offset_of_attributes = _.contents.items.len;
        _.contents.appendSlice(bun.default_allocator, ">\n")?;

        if (is_file_suite) {
            if (_.properties_list_to_repeat_in_every_test_suite == null) {
                _.generatePropertiesList()?;
            }

            if (properties_list := _.properties_list_to_repeat_in_every_test_suite) {
                if (properties_list.len > 0) {
                    _.contents.appendSlice(bun.default_allocator, properties_list)?;
                }
            }
        }

        _.suite_stack.append(bun.default_allocator, SuiteInfo{
            name: if (is_file_suite) name else bun.default_allocator.dupe(u8, name)?,
            offset_of_attributes: offset_of_attributes,
            is_file_suite: is_file_suite,
            line_number: line_number,
        })?;

        _.current_depth += 1;
        if (is_file_suite) {
            _.current_file = name;
        }
    }

    fn endTestSuite(*_)! {
        if (_.suite_stack.items.len == 0) return;

        _.current_depth -= 1;
        let suite_info = _.suite_stack.swapRemove(_.suite_stack.items.len - 1);
        const _guard_suite = DeinitWithAllocGuard<@TypeOf(suite_info)>{ ptr: &suite_info, allocator: bun.default_allocator };

        let arena_ = std.heap.ArenaAllocator.init(bun.default_allocator);
        const _guard_arena = DeinitGuard<std.heap.ArenaAllocator>{ ptr: &arena_ };
        let stack_fallback_allocator = std.heap.stackFallback(4096, arena_.allocator());
        const allocator = stack_fallback_allocator.get();

        const elapsed_time_ms = suite_info.metrics.elapsed_time;
        const elapsed_time_ms_f64: f64 = @floatFromInt(elapsed_time_ms);
        const elapsed_time_seconds = elapsed_time_ms_f64 / std.time.ms_per_s;

        // Insert the summary attributes
        const summary = std.fmt.allocPrint(allocator,
            \\tests="{d}" assertions="{d}" failures="{d}" skipped="{d}" time="{d}" hostname="{s}"
        , .{
            suite_info.metrics.test_cases,
            suite_info.metrics.assertions,
            suite_info.metrics.failures,
            suite_info.metrics.skipped,
            elapsed_time_seconds,
            _.getHostname() ?? "",
        })?;

        bun.handleOom(_.contents.insertSlice(bun.default_allocator, suite_info.offset_of_attributes, summary));

        const indent = getIndent(_.current_depth);
        _.contents.appendSlice(bun.default_allocator, indent)?;
        _.contents.appendSlice(bun.default_allocator, "</testsuite>\n")?;

        if (_.suite_stack.items.len > 0) {
            _.suite_stack.items[_.suite_stack.items.len - 1].metrics.add(&suite_info.metrics);
        } else {
            _.total_metrics.add(&suite_info.metrics);
        }
    }

    fn writeTestCase(
        *_,
        status: bun.jsc.Jest.bun_test.Execution.Result,
        file: string,
        name: string,
        class_name: string,
        assertions: u32,
        elapsed_ns: u64,
        line_number: u32,
    )! {
        const elapsed_ns_f64: f64 = @floatFromInt(elapsed_ns);
        const elapsed_ms = elapsed_ns_f64 / std.time.ns_per_ms;

        if (_.suite_stack.items.len > 0) {
            let current_suite = &_.suite_stack.items[_.suite_stack.items.len - 1];
            current_suite.metrics.elapsed_time = current_suite.metrics.elapsed_time +| @as(u64, @intFromFloat(elapsed_ms));
            current_suite.metrics.test_cases += 1;
            current_suite.metrics.assertions += assertions;
        }

        const indent = getIndent(_.current_depth);
        _.contents.appendSlice(bun.default_allocator, indent)?;
        _.contents.appendSlice(bun.default_allocator, "<testcase")?;
        _.contents.appendSlice(bun.default_allocator, " name=\"")?;
        escapeXml(name, _.contents.writer(bun.default_allocator))?;
        _.contents.appendSlice(bun.default_allocator, "\" classname=\"")?;
        escapeXml(class_name, _.contents.writer(bun.default_allocator))?;
        _.contents.appendSlice(bun.default_allocator, "\"")?;

        const elapsed_seconds = elapsed_ms / std.time.ms_per_s;
        _.contents.writer(bun.default_allocator).print(" time=\"{f}\"", .{bun.fmt.trimmedPrecision(elapsed_seconds, 6)})?;

        _.contents.appendSlice(bun.default_allocator, " file=\"")?;
        escapeXml(file, _.contents.writer(bun.default_allocator))?;
        _.contents.appendSlice(bun.default_allocator, "\"")?;

        if (line_number > 0) {
            _.contents.writer(bun.default_allocator).print(" line=\"{d}\"", .{line_number})?;
        }

        _.contents.writer(bun.default_allocator).print(" assertions=\"{d}\"", .{assertions})?;

        switch (status) {
            .pass => {
                _.contents.appendSlice(bun.default_allocator, " />\n")?;
            },
            .fail => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.failures += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                let type_name: []const u8 = "Error";
                if (f := _.last_failure) {
                    if (f.name.items.len > 0) {
                        type_name = f.name.items;
                    }
                }
                _.contents.appendSlice(bun.default_allocator, "  <failure type=\"")?;
                escapeXml(type_name, _.contents.writer(bun.default_allocator))?;
                _.contents.appendSlice(bun.default_allocator, "\"")?;
                if (f := _.last_failure) {
                    if (f.message.items.len > 0) {
                        _.contents.appendSlice(bun.default_allocator, " message=\"")?;
                        escapeXml(f.message.items, _.contents.writer(bun.default_allocator))?;
                        _.contents.appendSlice(bun.default_allocator, "\"")?;
                    }
                }
                let has_body = false;
                if (f := _.last_failure) {
                    if (f.body.items.len > 0) {
                        has_body = true;
                        _.contents.appendSlice(bun.default_allocator, ">")?;
                        escapeXml(f.body.items, _.contents.writer(bun.default_allocator))?;
                        _.contents.appendSlice(bun.default_allocator, "</failure>\n")?;
                    }
                }
                if (!has_body) {
                    _.contents.appendSlice(bun.default_allocator, " />\n")?;
                }
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .fail_because_failing_test_passed => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.failures += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.writer(bun.default_allocator).writeAll(
                    "  <failure message=\"test marked with .failing() did not throw\" type=\"AssertionError\"/>\n"
                )?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .fail_because_expected_assertion_count => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.failures += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.writer(bun.default_allocator).print(
                    "  <failure message=\"Expected more assertions, but only received {d}\" type=\"AssertionError\"/>\n",
                    .{assertions},
                )?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .fail_because_todo_passed => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.failures += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.writer(bun.default_allocator).print(
                    "  <failure message=\"TODO passed\" type=\"AssertionError\"/>\n",
                    .{},
                )?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .fail_because_expected_has_assertions => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.failures += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(
                    bun.default_allocator,
                    "  <failure message=\"Expected to have assertions, but none were run\" type=\"AssertionError\"/>\n",
                )?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .skipped_because_label, .skip => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.skipped += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "  <skipped />\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .todo => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.skipped += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "  <skipped message=\"TODO\" />\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .fail_because_timeout, .fail_because_timeout_with_done_callback, .fail_because_hook_timeout, .fail_because_hook_timeout_with_done_callback => {
                if (_.suite_stack.items.len > 0) {
                    _.suite_stack.items[_.suite_stack.items.len - 1].metrics.failures += 1;
                }
                _.contents.appendSlice(bun.default_allocator, ">\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "  <failure type=\"TimeoutError\" message=\"test timed out\" />\n")?;
                _.contents.appendSlice(bun.default_allocator, indent)?;
                _.contents.appendSlice(bun.default_allocator, "</testcase>\n")?;
            },
            .pending => unreachable,
        }
        _.clearLastFailure();
    }

    fn writeToFile(*_, path: string)! {
        if (_.contents.items.len == 0) return;

        while (_.suite_stack.items.len > 0) {
            _.endTestSuite()?;
        }

        {
            let arena_ = std.heap.ArenaAllocator.init(bun.default_allocator);
            const _guard_arena = DeinitGuard<std.heap.ArenaAllocator>{ ptr: &arena_ };
            let stack_fallback_allocator = std.heap.stackFallback(4096, arena_.allocator());
            const allocator = stack_fallback_allocator.get();
            const metrics = _.total_metrics;
            const elapsed_time = @as(f64, @floatFromInt(std.time.nanoTimestamp() - bun.start_time)) / std.time.ns_per_s;
            const summary = std.fmt.allocPrint(allocator,
                \\tests="{d}" assertions="{d}" failures="{d}" skipped="{d}" time="{d}"
            , .{
                metrics.test_cases,
                metrics.assertions,
                metrics.failures,
                metrics.skipped,
                elapsed_time,
            })?;
            bun.handleOom(_.contents.insertSlice(bun.default_allocator, _.offset_of_testsuites_value, summary));
            bun.handleOom(_.contents.appendSlice(bun.default_allocator, "</testsuites>\n"));
        }

        let junit_path_buf: bun.PathBuffer = undefined;

        @memcpy(junit_path_buf[0..path.len], path);
        junit_path_buf[path.len] = 0;

        switch (bun.sys.File.openat(.cwd(), junit_path_buf[0..path.len :0], bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o664)) {
            .err => |e| {
                Output.err(error.JUnitReportFailed, "Failed to write JUnit report to {s}\n{f}", .{ path, e });
            },
            .result => |fd| {
                const _guard_fd = CloseIgnoreErrGuard<@TypeOf(fd)>{ val: fd };
                switch (bun.sys.File.writeAll(fd, _.contents.items)) {
                    .result => {},
                    .err => |e| {
                        Output.err(error.JUnitReportFailed, "Failed to write JUnit report to {s}\n{f}", .{ path, e });
                    },
                }
            },
        }
    }
}

pub struct CommandLineReporter {
    jest: TestRunner,
    last_dot: u32 = 0,
    prev_file: u64 = 0,
    repeat_count: u32 = 1,
    last_printed_dot: bool = false,

    /// When running as a `--parallel` worker, this is the coordinator-assigned
    /// index of the file currently being executed. While set, per-test output
    /// is sent over the IPC pipe instead of to stderr; the coordinator owns
    /// the terminal.
    worker_ipc_file_idx: u32? = null,

    failures_to_repeat_buf: std.ArrayListUnmanaged(u8) = {},
    skips_to_repeat_buf: std.ArrayListUnmanaged(u8) = {},
    todos_to_repeat_buf: std.ArrayListUnmanaged(u8) = {},

    reporters: CommandLineReporters = {},
    timings: Timings? = null,

    const DotColorMap = std.EnumMap(TestRunner.Test.Status, string);
    const dots: DotColorMap = brk: {
        let map: DotColorMap = DotColorMap.init({});
        map.put(TestRunner.Test.Status.pending, Output.RESET ++ Output.ED ++ Output.color_map.get("yellow").? ++ "." ++ Output.RESET);
        map.put(TestRunner.Test.Status.pass, Output.RESET ++ Output.ED ++ Output.color_map.get("green").? ++ "." ++ Output.RESET);
        map.put(TestRunner.Test.Status.fail, Output.RESET ++ Output.ED ++ Output.color_map.get("red").? ++ "." ++ Output.RESET);
        break :brk map;
    };

    fn handleUpdateCount(_: *TestRunner.Callback, _: u32, _: u32)  {}

    fn handleTestStart(_: *TestRunner.Callback, _: Test.ID)  {}

    static fn printTestLine(
        comptime status: bun_test.Execution.Result,
        sequence: *bun_test.Execution.ExecutionSequence,
        test_entry: *bun_test.ExecutionEntry,
        elapsed_ns: u64,
        writer: auto,
        comptime dim: bool,
    ) {
        const initial_retry_count = test_entry.retry_count;
        const attempts = (initial_retry_count - sequence.remaining_retry_count) + 1;
        const initial_repeat_count = test_entry.repeat_count;
        const repeats = (initial_repeat_count - sequence.remaining_repeat_count) + 1;
        let scopes_stack = bun.BoundedArray(*bun_test.DescribeScope, 64).init(0) catch unreachable;
        let parent_: (*bun_test.DescribeScope)? = test_entry.base.parent;

        while (scope := parent_) {
            scopes_stack.append(scope) catch break;
            parent_ = scope.base.parent;
        }

        const scopes: []*bun_test.DescribeScope = scopes_stack.slice();
        const display_label = test_entry.base.name ?? "(unnamed)";

        // Quieter output when claude code is in use.
        if (!Output.isAIAgent() || !status.isPass(.pending_is_fail)) {
            const color_code = if (dim) "<d>" else "";
            const line_color_code = if (dim) "<r><d>" else "<r><b>";

            switch (Output.enable_ansi_colors_stderr) {
                inline else => switch (status) {
                    .fail_because_expected_assertion_count => {
                        // not sent to writer so it doesn't get printed twice
                        const expected_count = if (sequence.expect_assertions == .exact) sequence.expect_assertions.exact else 12345;
                        Output.err(error.AssertionError, "expected <green>{d} assertion{s}<r>, but test ended with <red>{d} assertion{s}<r>\n", .{
                            expected_count,
                            if (expected_count == 1) "" else "s",
                            sequence.expect_call_count,
                            if (sequence.expect_call_count == 1) "" else "s",
                        });
                        Output.flush();
                    },
                    .fail_because_expected_has_assertions => {
                        Output.err(error.AssertionError, "received <red>0 assertions<r>, but expected <green>at least one assertion<r> to be called\n", {});
                        Output.flush();
                    },
                    .fail_because_timeout, .fail_because_hook_timeout, .fail_because_timeout_with_done_callback, .fail_because_hook_timeout_with_done_callback => if (Output.is_github_action) {
                        Output.printError("::error title=error: Test \"{s}\" timed out after {d}ms::\n", .{ display_label, test_entry.timeout });
                        Output.flush();
                    } else @as(void, undefined),
                    else => {},
                },
            }

            if (Output.enable_ansi_colors_stderr) {
                let i: usize = 0;
                while (i < scopes.len) : (i += 1) {
                    const index = (scopes.len - 1) - i;
                    const scope = scopes[index];
                    const name: []const u8 = scope.base.name ?? "";
                    if (name.len == 0) continue;
                    writer.writeAll(" ") catch unreachable;

                    writer.print(comptime Output.prettyFmt("<r>" ++ color_code, true), {}) catch unreachable;
                    writer.writeAll(name) catch unreachable;
                    writer.print(comptime Output.prettyFmt("<d>", true), {}) catch unreachable;
                    writer.writeAll(" >") catch unreachable;
                }
            } else {
                let i: usize = 0;
                while (i < scopes.len) : (i += 1) {
                    const index = (scopes.len - 1) - i;
                    const scope = scopes[index];
                    const name: []const u8 = scope.base.name ?? "";
                    if (name.len == 0) continue;
                    writer.writeAll(" ") catch unreachable;
                    writer.writeAll(name) catch unreachable;
                    writer.writeAll(" >") catch unreachable;
                }
            }

            if (Output.enable_ansi_colors_stderr)
                writer.print(comptime Output.prettyFmt(line_color_code ++ " {s}<r>", true), .{display_label}) catch unreachable
            else
                writer.print(comptime Output.prettyFmt(" {s}", false), .{display_label}) catch unreachable;

            // Print attempt count if test was retried (attempts > 1)
            if (attempts > 1) switch (Output.enable_ansi_colors_stderr) {
                inline else => |enable_ansi_colors_stderr| writer.print(comptime Output.prettyFmt(" <d>(attempt {d})<r>", enable_ansi_colors_stderr), .{attempts}) catch unreachable,
            };

            // Print repeat count if test failed on a repeat (repeats > 1)
            if (repeats > 1) switch (Output.enable_ansi_colors_stderr) {
                inline else => |enable_ansi_colors_stderr| writer.print(comptime Output.prettyFmt(" <d>(run {d})<r>", enable_ansi_colors_stderr), .{repeats}) catch unreachable,
            };

            if (elapsed_ns > (std.time.ns_per_us * 10)) {
                writer.print(" {f}", .{
                    Output.ElapsedFormatter{
                        colors: Output.enable_ansi_colors_stderr,
                        duration_ns: elapsed_ns,
                    },
                }) catch unreachable;
            }

            writer.writeAll("\n") catch unreachable;

            switch (Output.enable_ansi_colors_stderr) {
                inline else => |colors| switch (status) {
                    .pending, .pass, .skip, .skipped_because_label, .todo, .fail => {},

                    .fail_because_failing_test_passed => writer.writeAll(comptime Output.prettyFmt("  <d>^<r> <red>this test is marked as failing but it passed.<r> <d>Remove `.failing` if tested behavior now works<r>\n", colors)) catch {},
                    .fail_because_todo_passed => writer.writeAll(comptime Output.prettyFmt("  <d>^<r> <red>this test is marked as todo but passes.<r> <d>Remove `.todo` if tested behavior now works<r>\n", colors)) catch {},
                    .fail_because_expected_assertion_count, .fail_because_expected_has_assertions => {}, // printed above
                    .fail_because_timeout => writer.print(comptime Output.prettyFmt("  <d>^<r> <red>this test timed out after {d}ms.<r>\n", colors), .{test_entry.timeout}) catch {},
                    .fail_because_hook_timeout => writer.writeAll(comptime Output.prettyFmt("  <d>^<r> <red>a beforeEach/afterEach hook timed out for this test.<r>\n", colors)) catch {},
                    .fail_because_timeout_with_done_callback => writer.print(comptime Output.prettyFmt("  <d>^<r> <red>this test timed out after {d}ms, before its done callback was called.<r> <d>If a done callback was not intended, remove the last parameter from the test callback function<r>\n", colors), .{test_entry.timeout}) catch {},
                    .fail_because_hook_timeout_with_done_callback => writer.writeAll(comptime Output.prettyFmt("  <d>^<r> <red>a beforeEach/afterEach hook timed out before its done callback was called.<r> <d>If a done callback was not intended, remove the last parameter from the hook callback function<r>\n", colors)) catch {},
                },
            }
        }
    }

    static fn maybePrintJunitLine(
        comptime status: bun_test.Execution.Result,
        buntest: *bun_test.BunTest,
        sequence: *bun_test.Execution.ExecutionSequence,
        test_entry: *bun_test.ExecutionEntry,
        elapsed_ns: u64,
    ) {
        if (cmd_reporter := buntest.reporter) {
            if (junit := cmd_reporter.reporters.junit) {
                let scopes_stack = bun.BoundedArray(*bun_test.DescribeScope, 64).init(0) catch unreachable;
                let parent_: (*bun_test.DescribeScope)? = test_entry.base.parent;
                const assertions = sequence.expect_call_count;
                const line_number = test_entry.base.line_no;

                let file: []const u8 = "";
                if (runner := bun.jsc.Jest.Jest.runner) {
                    file = runner.files.get(buntest.file_id).source.path.text;
                }

                while (scope := parent_) {
                    scopes_stack.append(scope) catch break;
                    parent_ = scope.base.parent;
                }

                const scopes: []*bun_test.DescribeScope = scopes_stack.slice();
                const display_label = test_entry.base.name ?? "(unnamed)";

                {
                    const filename = brk: {
                        if (strings.hasPrefix(file, bun.fs.FileSystem.instance.top_level_dir)) {
                            break :brk strings.withoutLeadingPathSeparator(file[bun.fs.FileSystem.instance.top_level_dir.len..]);
                        } else {
                            break :brk file;
                        }
                    };

                    if (!strings.eql(junit.current_file, filename)) {
                        while (junit.suite_stack.items.len > 0 && !junit.suite_stack.items[junit.suite_stack.items.len - 1].is_file_suite) {
                            bun.handleOom(junit.endTestSuite());
                        }

                        if (junit.current_file.len > 0) {
                            bun.handleOom(junit.endTestSuite());
                        }

                        bun.handleOom(junit.beginTestSuite(filename));
                    }

                    // To make the juint reporter generate nested suites, we need to find the needed suites and create/print them.
                    // This assumes that the scopes are in the correct order.
                    let needed_suites = std.array_list.Managed(*bun_test.DescribeScope).init(bun.default_allocator);
                    const _guard_needed = DeinitGuard<@TypeOf(needed_suites)>{ ptr: &needed_suites };

                    let i: usize = 0;
                    while (i < scopes.len) : (i += 1) {
                        const index = (scopes.len - 1) - i;
                        const scope = scopes[index];
                        if (name := scope.base.name) {
                            if (name.len > 0) {
                                bun.handleOom(needed_suites.append(scope));
                            }
                        }
                    }

                    let current_suite_depth: u32 = 0;
                    if (junit.suite_stack.items.len > 0) {
                        for (suite_info of junit.suite_stack.items) {
                            if (!suite_info.is_file_suite) {
                                current_suite_depth += 1;
                            }
                        }
                    }

                    while (current_suite_depth > needed_suites.items.len) {
                        if (junit.suite_stack.items.len > 0 && !junit.suite_stack.items[junit.suite_stack.items.len - 1].is_file_suite) {
                            bun.handleOom(junit.endTestSuite());
                            current_suite_depth -= 1;
                        } else {
                            break;
                        }
                    }

                    let suites_to_close: u32 = 0;
                    let suite_index: usize = 0;
                    for (suite_info of junit.suite_stack.items) {
                        if (suite_info.is_file_suite) continue;

                        if (suite_index < needed_suites.items.len) {
                            const needed_scope = needed_suites.items[suite_index];
                            if (!strings.eql(suite_info.name, needed_scope.base.name ?? "")) {
                                suites_to_close = @as(u32, i32(current_suite_depth)) - @as(u32, i32(suite_index));
                                break;
                            }
                        } else {
                            suites_to_close = @as(u32, i32(current_suite_depth)) - @as(u32, i32(suite_index));
                            break;
                        }
                        suite_index += 1;
                    }

                    while (suites_to_close > 0) {
                        if (junit.suite_stack.items.len > 0 && !junit.suite_stack.items[junit.suite_stack.items.len - 1].is_file_suite) {
                            bun.handleOom(junit.endTestSuite());
                            current_suite_depth -= 1;
                            suites_to_close -= 1;
                        } else {
                            break;
                        }
                    }

                    let describe_suite_index: usize = 0;
                    for (suite_info of junit.suite_stack.items) {
                        if (!suite_info.is_file_suite) {
                            describe_suite_index += 1;
                        }
                    }

                    while (describe_suite_index < needed_suites.items.len) {
                        const scope = needed_suites.items[describe_suite_index];
                        bun.handleOom(junit.beginTestSuiteWithLine(scope.base.name ?? "", scope.base.line_no, false));
                        describe_suite_index += 1;
                    }

                    let arena_ = std.heap.ArenaAllocator.init(bun.default_allocator);
                    const _guard_arena = DeinitGuard<std.heap.ArenaAllocator>{ ptr: &arena_ };
                    let stack_fallback = std.heap.stackFallback(4096, arena_.allocator());
                    const allocator = stack_fallback.get();
                    let concatenated_describe_scopes = std.array_list.Managed(u8).init(allocator);

                    {
                        const initial_length = concatenated_describe_scopes.items.len;
                        for (scope of scopes) {
                            if (name := scope.base.name) {
                                if (name.len > 0) {
                                    if (initial_length != concatenated_describe_scopes.items.len) {
                                        bun.handleOom(concatenated_describe_scopes.appendSlice(" > "));
                                    }

                                    // writeTestCase escapes class_name once; do not pre-escape here.
                                    bun.handleOom(concatenated_describe_scopes.appendSlice(name));
                                }
                            }
                        }
                    }

                    for (attempt of sequence.flakyAttempts()) {
                        bun.handleOom(junit.writeTestCase(attempt.result, filename, display_label, concatenated_describe_scopes.items, 0, attempt.elapsed_ns, line_number));
                    }
                    bun.handleOom(junit.writeTestCase(status, filename, display_label, concatenated_describe_scopes.items, assertions, elapsed_ns, line_number));
                }
            }
        }
    }

    pub inline fn summary(*_): *TestRunner.Summary {
        return &_.jest.summary;
    }

    static fn handleTestCompleted(buntest: *bun_test.BunTest, sequence: *bun_test.Execution.ExecutionSequence, test_entry: *bun_test.ExecutionEntry, elapsed_ns: u64) {
        let output_buf: std.ArrayListUnmanaged(u8) = .empty;
        const _guard_output = DeinitWithAllocGuard<@TypeOf(output_buf)>{ ptr: &output_buf, allocator: buntest.gpa };

        const initial_length = output_buf.items.len;
        const base_writer = output_buf.writer(buntest.gpa);
        let writer = base_writer;

        switch (sequence.result) {
            inline else => |result| {
                if (result != .skipped_because_label) {
                    if (buntest.reporter != null && buntest.reporter.?.reporters.dots && (comptime switch (result.basicResult()) {
                        .pass, .skip, .todo, .pending => true,
                        .fail => false,
                    })) {
                        switch (Output.enable_ansi_colors_stderr) {
                            inline else => |enable_ansi_colors_stderr| switch (comptime result.basicResult()) {
                                .pass => writer.print(comptime Output.prettyFmt("<r><green>.<r>", enable_ansi_colors_stderr), {}) catch {},
                                .skip => writer.print(comptime Output.prettyFmt("<r><yellow>.<d>", enable_ansi_colors_stderr), {}) catch {},
                                .todo => writer.print(comptime Output.prettyFmt("<r><magenta>.<r>", enable_ansi_colors_stderr), {}) catch {},
                                .pending => writer.print(comptime Output.prettyFmt("<r><d>.<r>", enable_ansi_colors_stderr), {}) catch {},
                                .fail => writer.print(comptime Output.prettyFmt("<r><red>.<r>", enable_ansi_colors_stderr), {}) catch {},
                            },
                        }
                        buntest.reporter.?.last_printed_dot = true;
                    } else if (((comptime result.basicResult()) != .fail) && (buntest.reporter != null && buntest.reporter.?.reporters.only_failures)) {
                        // when using --only-failures, only print failures
                    } else {
                        buntest.bun_test_root.onBeforePrint();

                        writeTestStatusLine(result, &writer);
                        let is_todo_dim = true;
                        if (runner := bun.jsc.Jest.Jest.runner) {
                            is_todo_dim = !runner.run_todo;
                        }
                        const dim = switch (comptime result.basicResult()) {
                            .todo => is_todo_dim,
                            .skip, .pending => true,
                            .pass, .fail => false,
                        };
                        if (dim) {
                            printTestLine(result, sequence, test_entry, elapsed_ns, &writer, true);
                        } else {
                            printTestLine(result, sequence, test_entry, elapsed_ns, &writer, false);
                        }
                    }
                }
                // always print junit if needed
                maybePrintJunitLine(result, buntest, sequence, test_entry, elapsed_ns);
            },
        }

        const formatted_line = output_buf.items[initial_length..];
        if (buntest.reporter != null && buntest.reporter.?.worker_ipc_file_idx != null) {
            ParallelRunner.workerEmitTestDone(buntest.reporter.?.worker_ipc_file_idx.?, formatted_line);
        } else {
            Output.errorWriter().writeAll(formatted_line) catch {};
        }

        if (buntest.reporter == null) return; // command line reporter is missing! uh oh!
        let this_reporter: *CommandLineReporter = buntest.reporter.?;

        if (!this_reporter.reporters.dots && !this_reporter.reporters.only_failures) switch (sequence.result.basicResult()) {
            .skip => bun.handleOom(this_reporter.skips_to_repeat_buf.appendSlice(bun.default_allocator, output_buf.items[initial_length..])),
            .todo => bun.handleOom(this_reporter.todos_to_repeat_buf.appendSlice(bun.default_allocator, output_buf.items[initial_length..])),
            .fail => bun.handleOom(this_reporter.failures_to_repeat_buf.appendSlice(bun.default_allocator, output_buf.items[initial_length..])),
            .pass, .pending => {},
        };

        switch (sequence.result) {
            .pending => {},
            .pass => this_reporter.summary().pass += 1,
            .skip => this_reporter.summary().skip += 1,
            .todo => this_reporter.summary().todo += 1,
            .skipped_because_label => this_reporter.summary().skipped_because_label += 1,

            .fail,
            .fail_because_failing_test_passed,
            .fail_because_todo_passed,
            .fail_because_expected_has_assertions,
            .fail_because_expected_assertion_count,
            .fail_because_timeout,
            .fail_because_timeout_with_done_callback,
            .fail_because_hook_timeout,
            .fail_because_hook_timeout_with_done_callback => {
                this_reporter.summary().fail += 1;

                if (this_reporter.summary().fail == this_reporter.jest.bail) {
                    this_reporter.printSummary();
                    Output.prettyError("\nBailed out after {d} failure{s}<r>\n", .{ this_reporter.jest.bail, if (this_reporter.jest.bail == 1) "" else "s" });
                    Output.flush();
                    this_reporter.writeJUnitReportIfNeeded();
                    Global.exit(1);
                }
            },
        }
        this_reporter.summary().expectations = this_reporter.summary().expectations +| sequence.expect_call_count;
    }

    fn printSummary(*_) {
        const summary_ = _.summary();
        const tests = summary_.fail + summary_.pass + summary_.skip + summary_.todo;
        const files = summary_.files;

        Output.prettyError("Ran {d} test{s} across {d} file{s}. ", .{
            tests,
            if (tests == 1) "" else "s",
            files,
            if (files == 1) "" else "s",
        });

        Output.printStartEnd(bun.start_time, std.time.nanoTimestamp());
    }

    /// Writes the JUnit reporter output file if a JUnit reporter is active and
    /// an outfile path was configured. This must be called before any early exit
    /// (e.g. bail) so that the report is not lost.
    fn writeJUnitReportIfNeeded(*_) {
        if (junit := _.reporters.junit) {
            if (outfile := _.jest.test_options.reporter_outfile) {
                if (junit.current_file.len > 0) {
                    junit.endTestSuite() catch {};
                }
                junit.writeToFile(outfile) catch {};
            }
        }
    }

    fn writeTimingsIfNeeded(*_, only_measured: bool) {
        if (*t := _.timings) {
            t.write(only_measured);
            t.deinit();
            _.timings = null;
        }
    }

    fn generateCodeCoverage(*_, vm: *jsc.VirtualMachine, opts: *TestCommand.CodeCoverageOptions, comptime reporters: TestCommand.Reporters, comptime enable_ansi_colors: bool)! {
        if (comptime !reporters.text && !reporters.lcov) {
            return;
        }

        if (coverage.ByteRangeMapping.map == null) return;
        let map = coverage.ByteRangeMapping.map.?;
        let iter = map.valueIterator();
        let byte_ranges = std.array_list.Managed(bun.SourceMap.coverage.ByteRangeMapping).initCapacity(bun.default_allocator, map.count())?;

        while (entry := iter.next()) {
            byte_ranges.appendAssumeCapacity(entry.*);
        }

        if (byte_ranges.items.len == 0) {
            return;
        }

        const empty_sort_ctx: void = undefined;
        std.sort.pdq(
            bun.SourceMap.coverage.ByteRangeMapping,
            byte_ranges.items,
            empty_sort_ctx,
            bun.SourceMap.coverage.ByteRangeMapping.isLessThan,
        );

        _.printCodeCoverage(vm, opts, byte_ranges.items, reporters, enable_ansi_colors)?;
    }

    /// Write an LCOV-only report to a specific path. Used by `--parallel`
    /// workers to emit a fragment the coordinator merges.
    fn writeLcovOnly(_: *CommandLineReporter, vm: *jsc.VirtualMachine, opts: *const TestCommand.CodeCoverageOptions, out_path: [:0]const u8)! {
        if (coverage.ByteRangeMapping.map == null) return;
        let map = coverage.ByteRangeMapping.map.?;
        let iter = map.valueIterator();
        let byte_ranges = std.array_list.Managed(bun.SourceMap.coverage.ByteRangeMapping).initCapacity(bun.default_allocator, map.count())?;
        const _guard_ranges = DeinitGuard<@TypeOf(byte_ranges)>{ ptr: &byte_ranges };
        while (entry := iter.next()) byte_ranges.appendAssumeCapacity(entry.*);
        if (byte_ranges.items.len == 0) return;
        const empty_sort_ctx: void = undefined;
        std.sort.pdq(
            bun.SourceMap.coverage.ByteRangeMapping,
            byte_ranges.items,
            empty_sort_ctx,
            bun.SourceMap.coverage.ByteRangeMapping.isLessThan,
        );

        const relative_dir = vm.transpiler.fs.top_level_dir;
        const open_res = bun.sys.File.openat(.cwd(), out_path, bun.O.CREAT | bun.O.WRONLY | bun.O.TRUNC | bun.O.CLOEXEC, 0o644);
        if (open_res == .err) {
            Output.err(.lcovCoverageError, "failed to open coverage fragment {s}\n{f}", .{ out_path, open_res.err });
            return error.OpenFailed;
        }
        const file = open_res.result;
        const _guard_file = CloseGuard<@TypeOf(file)>{ val: file };
        const buf = bun.default_allocator.alloc(u8, 64 * 1024)?;
        const _guard_buf = FreeGuard<@TypeOf(buf)>{ allocator: bun.default_allocator, slice: buf };
        let buffered = file.writer().adaptToNewApi(buf);
        const writer = &buffered.new_interface;

        let entry_i: usize = 0;
        while (entry_i < byte_ranges.items.len) : (entry_i += 1) {
            const entry = &byte_ranges.items[entry_i];
            if (opts.ignore_patterns.len > 0) {
                const rel = bun.path.relative(relative_dir, entry.source_url.slice());
                let skip = false;
                for (p of opts.ignore_patterns) {
                    if (bun.glob.match(p, rel).matches()) {
                        skip = true;
                        break;
                    }
                }
                if (skip) continue;
            }
            const report_opt = CodeCoverageReport.generate(vm.global, bun.default_allocator, entry, opts.ignore_sourcemap);
            if (report_opt == null) continue;
            let report = report_opt.?;
            const _guard_report = DeinitWithAllocGuard<@TypeOf(report)>{ ptr: &report, allocator: bun.default_allocator };
            CodeCoverageReport.Lcov.writeFormat(&report, relative_dir, writer) catch continue;
        }
        writer.flush()?;
    }

    static fn printCodeCoverage(
        _: *CommandLineReporter,
        vm: *jsc.VirtualMachine,
        opts: *TestCommand.CodeCoverageOptions,
        byte_ranges: []bun.SourceMap.coverage.ByteRangeMapping,
        comptime reporters: TestCommand.Reporters,
        comptime enable_ansi_colors: bool,
    )! {
        let trace = if (reporters.text && reporters.lcov)
            bun.perf.trace("TestCommand.printCodeCoverageLCovAndText")
        else if (reporters.text)
            bun.perf.trace("TestCommand.printCodeCoverageText")
        else if (reporters.lcov)
            bun.perf.trace("TestCommand.printCodeCoverageLCov")
        else
            @compileError("No reporters enabled");

        const _guard_trace = TraceEndGuard<@TypeOf(trace)>{ ptr: &trace };

        if (comptime !reporters.text && !reporters.lcov) {
            @compileError("No reporters enabled");
        }

        const relative_dir = vm.transpiler.fs.top_level_dir;

        // --- Text ---
        const max_filepath_length: usize = if (reporters.text) brk: {
            let len = "All files".len;
            let entry_i: usize = 0;
        while (entry_i < byte_ranges.len) : (entry_i += 1) {
            const entry = &byte_ranges[entry_i];
                const utf8 = entry.source_url.slice();
                const relative_path = bun.path.relative(relative_dir, utf8);

                // Check if this file should be ignored based on coveragePathIgnorePatterns
                if (opts.ignore_patterns.len > 0) {
                    let should_ignore = false;
                    for (pattern of opts.ignore_patterns) {
                        if (bun.glob.match(pattern, relative_path).matches()) {
                            should_ignore = true;
                            break;
                        }
                    }

                    if (should_ignore) {
                        continue;
                    }
                }

                len = @max(relative_path.len, len);
            }

            break :brk len;
        } else 0;

        let console = Output.errorWriter();
        const base_fraction = opts.fractions;
        let failing = false;

        if (comptime reporters.text) {
            console.writeAll(Output.prettyFmt("<r><d>", enable_ansi_colors)) catch return;
            console.splatByteAll('-', max_filepath_length + 2) catch return;
            console.writeAll(Output.prettyFmt("|---------|---------|-------------------<r>\n", enable_ansi_colors)) catch return;
            console.writeAll("File") catch return;
            console.splatByteAll(' ', max_filepath_length - "File".len + 1) catch return;
            // writer.writeAll(Output.prettyFmt(" <d>|<r> % Funcs <d>|<r> % Blocks <d>|<r> % Lines <d>|<r> Uncovered Line #s\n", enable_ansi_colors)) catch return;
            console.writeAll(Output.prettyFmt(" <d>|<r> % Funcs <d>|<r> % Lines <d>|<r> Uncovered Line #s\n", enable_ansi_colors)) catch return;
            console.writeAll(Output.prettyFmt("<d>", enable_ansi_colors)) catch return;
            console.splatByteAll('-', max_filepath_length + 2) catch return;
            console.writeAll(Output.prettyFmt("|---------|---------|-------------------<r>\n", enable_ansi_colors)) catch return;
        }

        let console_buffer = std.Io.Writer.Allocating.init(bun.default_allocator);
        const console_writer = &console_buffer.writer;

        let avg = bun.SourceMap.coverage.Fraction{
            functions: 0.0,
            lines: 0.0,
            stmts: 0.0,
        };
        let avg_count: f64 = 0;
        // --- Text ---

        // --- LCOV ---
        let lcov_name_buf: bun.PathBuffer = undefined;
        let lcov_tuple = brk: {
            if (comptime !reporters.lcov) break :brk .{ {}, {}, {} };

            // Ensure the directory exists
            let fs = bun.jsc.Node.fs.NodeFS{};
            * = fs.mkdirRecursive(
                {
                    path: bun.jsc.Node.PathLike{
                        encoded_slice: jsc.ZigString.Slice.fromUTF8NeverFree(opts.reports_directory),
                    },
                    always_return_none: true,
                },
            );

            // Write the lcov.info file to a temporary file we atomically rename to the final name after it succeeds
            let base64_bytes: [8]u8 = undefined;
            let shortname_buf: [512]u8 = undefined;
            bun.csprng(&base64_bytes);
            const tmpname = std.fmt.bufPrintZ(&shortname_buf, ".lcov.info.{x}.tmp", .{&base64_bytes}) catch unreachable;
            const path = bun.path.joinAbsStringBufZ(relative_dir, &lcov_name_buf, &.{ opts.reports_directory, tmpname }, .auto);
            const file = bun.sys.File.openat(
                .cwd(),
                path,
                bun.O.CREAT | bun.O.WRONLY | bun.O.TRUNC | bun.O.CLOEXEC,
                0o644,
            );

            if (file == .err) {
                Output.err(.lcovCoverageError, "Failed to create lcov file", {});
                Output.printError("\n{f}", .{file.err});
                Global.exit(1);
            }
            const f = file.result;
            const buffered = buffered_writer: {
                const writer = f.writer();
                // Heap-allocate the buffered writer because we want a stable memory address + 64 KB is kind of a lot.
                const buffer = bun.default_allocator.alloc(u8, 64 * 1024)?;
                break :buffered_writer writer.adaptToNewApi(buffer);
            };

            break :brk .{
                f,
                path,
                buffered,
            };
        };
        const lcov_file = lcov_tuple[0];
        const lcov_name = lcov_tuple[1];
        let lcov_buffered_writer = lcov_tuple[2];
        defer if (comptime reporters.lcov) bun.default_allocator.free(lcov_buffered_writer.new_interface.buffer);
        const lcov_writer = if (comptime reporters.lcov) &lcov_buffered_writer.new_interface else @as(void, undefined);
        let _guard_lcov = LcovErrGuard<@TypeOf(lcov_file), @TypeOf(lcov_name)>{ armed: comptime reporters.lcov, file: lcov_file, name: lcov_name };
        // --- LCOV ---

        let entry_i: usize = 0;
        while (entry_i < byte_ranges.len) : (entry_i += 1) {
            const entry = &byte_ranges[entry_i];
            // Check if this file should be ignored based on coveragePathIgnorePatterns
            if (opts.ignore_patterns.len > 0) {
                const utf8 = entry.source_url.slice();
                const relative_path = bun.path.relative(relative_dir, utf8);

                let should_ignore = false;
                for (pattern of opts.ignore_patterns) {
                    if (bun.glob.match(pattern, relative_path).matches()) {
                        should_ignore = true;
                        break;
                    }
                }

                if (should_ignore) {
                    continue;
                }
            }

            const report_opt = CodeCoverageReport.generate(vm.global, bun.default_allocator, entry, opts.ignore_sourcemap);
            if (report_opt == null) continue;
            let report = report_opt.?;
            const _guard_report = DeinitWithAllocGuard<@TypeOf(report)>{ ptr: &report, allocator: bun.default_allocator };

            if (comptime reporters.text) {
                let fraction = base_fraction;
                CodeCoverageReport.Text.writeFormat(&report, max_filepath_length, &fraction, relative_dir, console_writer, enable_ansi_colors) catch continue;
                avg.functions += fraction.functions;
                avg.lines += fraction.lines;
                avg.stmts += fraction.stmts;
                avg_count += 1.0;
                if (fraction.failing) {
                    failing = true;
                }

                console_writer.writeAll("\n") catch continue;
            }

            if (comptime reporters.lcov) {
                CodeCoverageReport.Lcov.writeFormat(
                    &report,
                    relative_dir,
                    lcov_writer,
                ) catch continue;
            }
        }

        if (comptime reporters.text) {
            {
                if (avg_count == 0) {
                    avg.functions = 0;
                    avg.lines = 0;
                    avg.stmts = 0;
                } else {
                    avg.functions /= avg_count;
                    avg.lines /= avg_count;
                    avg.stmts /= avg_count;
                }

                const failed = if (avg_count > 0) base_fraction else bun.SourceMap.coverage.Fraction{
                    functions: 0,
                    lines: 0,
                    stmts: 0,
                };

                CodeCoverageReport.Text.writeFormatWithValues(
                    "All files",
                    max_filepath_length,
                    avg,
                    failed,
                    failing,
                    console,
                    false,
                    enable_ansi_colors,
                )?;

                console.writeAll(Output.prettyFmt("<r><d> |<r>\n", enable_ansi_colors))?;
            }

            console_writer.flush() catch return;
            console.writeAll(console_buffer.written())?;
            console.writeAll(Output.prettyFmt("<r><d>", enable_ansi_colors))?;
            console.splatByteAll('-', max_filepath_length + 2) catch return;
            console.writeAll(Output.prettyFmt("|---------|---------|-------------------<r>\n", enable_ansi_colors)) catch return;

            opts.fractions.failing = failing;
            Output.flush();
        }

        if (comptime reporters.lcov) {
            lcov_writer.flush()?;
            _guard_lcov.armed = false;
            lcov_file.close();
            const cwd = bun.FD.cwd();
            bun.sys.moveFileZ(
                cwd,
                lcov_name,
                cwd,
                bun.path.joinAbsStringZ(
                    relative_dir,
                    &.{ opts.reports_directory, "lcov.info" },
                    .auto,
                ),
            ) catch e => {
                Output.err(e, "Failed to save lcov.info file", {});
                Global.exit(1);
            };
        }
    }
}

@callconv("c")
@export pub fn BunTest__shouldGenerateCodeCoverage(test_name_str: bun.String): bool {
    let zig_slice: bun.jsc.ZigString.Slice = {};
    const _guard_zig_slice = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &zig_slice };

    // In this particular case, we don't actually care about non-ascii latin1 characters.
    // so we skip the ascii check
    const slice = brk: {
        zig_slice = test_name_str.toUTF8(bun.default_allocator);
        break :brk zig_slice.slice();
    };

    // always ignore node_modules.
    if (bun.strings.contains(slice, "/node_modules/") || bun.strings.contains(slice, "\\node_modules\\")) {
        return false;
    }

    const ext = std.fs.path.extension(slice);
    const loader_by_ext = jsc.VirtualMachine.get().transpiler.options.loader(ext);

    // allow file loader just incase they use a custom loader with a non-standard extension
    if (!(loader_by_ext.isJavaScriptLike() || loader_by_ext == .file)) {
        return false;
    }

    if (runner := jest.Jest.runner) {
        if (runner.test_options.coverage.skip_test_files) {
            const name_without_extension = slice[0 .. slice.len - ext.len];
            inline for (suffix of Scanner.test_name_suffixes) {
                if (bun.strings.endsWithComptime(name_without_extension, suffix)) {
                    return false;
                }
            }
        }
    }

    return true;
}

pub struct TestCommand {
    pub const name = "test";
    pub const CodeCoverageOptions = CodeCoverageOptionsMod.CodeCoverageOptions;
    pub const Reporter = CodeCoverageOptionsMod.Reporter;
    const Reporters = CodeCoverageOptionsMod.Reporters;

    static fn exec(ctx: Command.Context)! {
        Output.is_github_action = Output.isGithubAction();

        if (!ctx.test_options.test_worker) {
            // print the version so you know its doing stuff if it takes a sec
            const w = Output.writer();
            const colors = Output.enable_ansi_colors_stdout;
            w.writeAll(if (colors)
                Output.prettyFmt("<r><b>bun test <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>", true)
            else
                Output.prettyFmt("<r><b>bun test <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>", false)) catch {};
            if (ctx.test_options.parallel > 0) {
                if (colors) {
                    w.print(" \x1b[1;2m{d}\u{00d7} PARALLEL\x1b[0m", .{ctx.test_options.parallel}) catch {};
                } else {
                    w.print(" {d}x PARALLEL", .{ctx.test_options.parallel}) catch {};
                }
            }
            w.writeAll("\n") catch {};
            Output.flush();
        }

        let env_loader = brk: {
            const map = ctx.allocator.create(DotEnv.Map)?;
            map.* = DotEnv.Map.init(ctx.allocator);

            const loader = ctx.allocator.create(DotEnv.Loader)?;
            loader.* = DotEnv.Loader.init(map, ctx.allocator);
            break :brk loader;
        };
        bun.jsc.initialize(false);
        HTTPThread.init([]);

        const enable_random = ctx.test_options.randomize;
        const seed: u32 = if (enable_random) ctx.test_options.seed ?? @truncate(bun.fastRandom()) else 0; // seed is limited to u32 so storing it in js doesn't lose precision
        // Persist the chosen seed so --parallel forwards it to every worker;
        // otherwise each worker would draw its own and the printed --seed=N
        // would not reproduce the run.
        if (enable_random) ctx.test_options.seed = seed;
        let random_instance: std.Random.DefaultPrng? = if (enable_random) std.Random.DefaultPrng.init(seed) else null;
        let random: std.Random? = null;
        if (*instance := random_instance) {
            random = instance.random();
        }

        let snapshot_file_buf = std.array_list.Managed(u8).init(ctx.allocator);
        let snapshot_values = Snapshots.ValuesHashMap.init(ctx.allocator);
        let snapshot_counts = bun.StringHashMap(usize).init(ctx.allocator);
        let inline_snapshots_to_write = std.AutoArrayHashMap(TestRunner.File.ID, std.array_list.Managed(Snapshots.InlineSnapshotToWrite)).init(ctx.allocator);
        jsc.VirtualMachine.isBunTest = true;

        let reporter = ctx.allocator.create(CommandLineReporter)?;
        const _guard_junit = JunitReporterDeinitGuard{ reporter: reporter };
        reporter.* = CommandLineReporter{
            jest: TestRunner{
                allocator: ctx.allocator,
                default_timeout_ms: ctx.test_options.default_timeout_ms,
                concurrent: ctx.test_options.concurrent,
                randomize: random,
                randomize_seed: if (enable_random) seed else null,
                concurrent_test_glob: ctx.test_options.concurrent_test_glob,
                run_todo: ctx.test_options.run_todo,
                only: ctx.test_options.only,
                bail: ctx.test_options.bail,
                max_concurrency: ctx.test_options.max_concurrency,
                filter_regex: ctx.test_options.testFilterRegex(),
                snapshots: Snapshots{
                    allocator: ctx.allocator,
                    update_snapshots: ctx.test_options.update_snapshots,
                    file_buf: &snapshot_file_buf,
                    values: &snapshot_values,
                    counts: &snapshot_counts,
                    inline_snapshots_to_write: &inline_snapshots_to_write,
                },
                bun_test_root: .init(ctx.allocator),
            },
        };
        reporter.repeat_count = @max(ctx.test_options.repeat_count, 1);
        jest.Jest.runner = &reporter.jest;
        reporter.jest.test_options = &ctx.test_options;
        reporter.timings = if (ctx.test_options.test_worker || ctx.test_options.timings_files.len == 0)
            null
        else
            Timings.load(ctx.test_options.timings_files);

        if (ctx.test_options.reporters.junit) {
            reporter.reporters.junit = JunitReporter.init();
        }
        if (ctx.test_options.reporters.dots) {
            reporter.reporters.dots = true;
        }
        if (ctx.test_options.reporters.only_failures) {
            reporter.reporters.only_failures = true;
        } else if (Output.isAIAgent()) {
            reporter.reporters.only_failures = true; // only-failures defaults to true for ai agents
        }

        js_ast.Expr.Data.Store.create();
        js_ast.Stmt.Data.Store.create();
        let vm = jsc.VirtualMachine.init(
            {
                allocator: ctx.allocator,
                args: ctx.args,
                log: ctx.log,
                env_loader: env_loader,
                // we must store file descriptors because we reuse them for
                // iterating through the directory tree recursively
                //
                // in the future we should investigate if refactoring this to not
                // rely on the dir fd yields a performance improvement
                store_fd: true,
                smol: ctx.runtime_options.smol,
                debugger: ctx.runtime_options.debugger,
                is_main_thread: true,
            },
        )?;
        vm.argv = ctx.passthrough;
        vm.preload = ctx.preloads;
        vm.transpiler.options.rewrite_jest_for_tests = true;
        bun.http.experimental_http2_client_from_cli = ctx.runtime_options.experimental_http2_fetch;
        bun.http.experimental_http3_client_from_cli = ctx.runtime_options.experimental_http3_fetch;
        vm.transpiler.options.env.behavior = .load_all_without_inlining;

        const node_env_entry = env_loader.map.getOrPutWithoutValue("NODE_ENV")?;
        if (!node_env_entry.found_existing) {
            node_env_entry.key_ptr.* = env_loader.allocator.dupe(u8, node_env_entry.key_ptr.*)?;
            node_env_entry.value_ptr.* = {
                value: env_loader.allocator.dupe(u8, "test")?,
                conditional: false,
            };
        }

        vm.transpiler.configureDefines()?;

        vm.loadExtraEnvAndSourceCodePrinter();
        vm.is_main_thread = true;
        jsc.VirtualMachine.is_main_thread_vm = true;

        if (ctx.test_options.isolate) {
            vm.test_isolation_enabled = true;
            vm.auto_killer.enabled = true;
        }

        if (ctx.test_options.coverage.enabled) {
            vm.transpiler.options.code_coverage = true;
            vm.transpiler.options.minify_syntax = false;
            vm.transpiler.options.minify_identifiers = false;
            vm.transpiler.options.minify_whitespace = false;
            vm.transpiler.options.dead_code_elimination = false;
            vm.global.vm().setControlFlowProfiler(true);
        }

        // For tests, we default to UTC time zone
        // unless the user inputs TZ="", in which case we use local time zone
        let TZ_NAME: string =
            // We use the string "Etc/UTC" instead of "UTC" so there is no normalization difference.
            "Etc/UTC";

        if (tz := vm.transpiler.env.get("TZ")) {
            TZ_NAME = tz;
        }

        if (TZ_NAME.len > 0) {
            * = vm.global.setTimeZone(&jsc.ZigString.init(TZ_NAME));
        }

        if (ctx.test_options.test_worker) {
            // Worker mode: skip discovery; files arrive over stdin and
            // results go out over fd 3. Never returns.
            ParallelRunner.runAsWorker(reporter, vm, ctx)?;
        }

        // Start the debugger before we scan for files
        // But, don't block the main thread waiting if they used --inspect-wait.
        //
        vm.ensureDebugger(false)?;

        let scanner = bun.handleOom(Scanner.init(ctx.allocator, &vm.transpiler, ctx.positionals.len));
        const _guard_scanner = DeinitGuard<@TypeOf(scanner)>{ ptr: &scanner };
        scanner.path_ignore_patterns = ctx.test_options.path_ignore_patterns;
        let has_relative_path = false;
        for (arg of ctx.positionals) {
            if (std.fs.path.isAbsolute(arg) ||
                strings.startsWith(arg, "./") ||
                strings.startsWith(arg, "../") ||
                (Environment.isWindows && (strings.startsWith(arg, ".\\") ||
                    strings.startsWith(arg, "..\\"))))
            {
                has_relative_path = true;
                break;
            }
        }
        if (has_relative_path) {
            // One of the files is a filepath. Instead of treating the
            // arguments as filters, treat them as filepaths
            const file_or_dirnames = ctx.positionals[1..];
            for (arg of file_or_dirnames) {
                scanner.scan(arg) catch e => switch (e) {
                    error.OutOfMemory => bun.outOfMemory(),
                    // don't error if multiple are passed; one might fail
                    // but the others may not
                    error.DoesNotExist => if (file_or_dirnames.len == 1) {
                        if (Output.isAIAgent()) {
                            Output.prettyErrorln("Test filter <b>{f}<r> had no matches in --cwd={f}", .{ bun.fmt.quote(arg), bun.fmt.quote(bun.fs.FileSystem.instance.top_level_dir) });
                        } else {
                            Output.prettyErrorln("Test filter <b>{f}<r> had no matches", .{bun.fmt.quote(arg)});
                        }
                        vm.exit_handler.exit_code = 1;
                        vm.is_shutting_down = true;
                        vm.runWithAPILock(jsc.VirtualMachine, vm, jsc.VirtualMachine.globalExit);
                    } else @as(void, undefined),
                };
            }
        } else {
            // Treat arguments as filters and scan the codebase
            const empty_filters: [0]string = undefined;
            const filter_names: []const string = if (ctx.positionals.len == 0) &empty_filters else ctx.positionals[1..];

            const filter_names_normalized = if (!Environment.isWindows)
                filter_names
            else brk: {
                let normalized = ctx.allocator.alloc(string, filter_names.len)?;
                for (ni of 0..filter_names.len) {
                    const to_normalize = ctx.allocator.dupe(u8, filter_names[ni])?;
                    bun.path.posixToPlatformInPlace(u8, to_normalize);
                    normalized[ni] = to_normalize;
                }
                break :brk normalized;
            };
            const _guard_filters = FreeStringsIfWindowsGuard{ allocator: ctx.allocator, items: filter_names_normalized };
            scanner.filter_names = filter_names_normalized;

            const dir_to_scan = brk: {
                if (ctx.debug.test_directory.len > 0) {
                    break :brk vm.allocator.dupe(u8, resolve_path.joinAbs(scanner.fs.top_level_dir, .auto, ctx.debug.test_directory))?;
                }

                break :brk scanner.fs.top_level_dir;
            };

            scanner.scan(dir_to_scan) catch e => switch (e) {
                error.OutOfMemory => bun.outOfMemory(),
                error.DoesNotExist => {
                    if (Output.isAIAgent()) {
                        Output.prettyErrorln("<red>Failed to scan non-existent root directory for tests:<r> {f} in --cwd={f}", .{ bun.fmt.quote(dir_to_scan), bun.fmt.quote(bun.fs.FileSystem.instance.top_level_dir) });
                    } else {
                        Output.prettyErrorln("<red>Failed to scan non-existent root directory for tests:<r> {f}", .{bun.fmt.quote(dir_to_scan)});
                    }
                    vm.exit_handler.exit_code = 1;
                    vm.is_shutting_down = true;
                    vm.runWithAPILock(jsc.VirtualMachine, vm, jsc.VirtualMachine.globalExit);
                },
            };
        }

        let all_test_files = bun.handleOom(scanner.takeFoundTestFiles());
        const _guard_test_files = FreeGuard<@TypeOf(all_test_files)>{ allocator: ctx.allocator, slice: all_test_files };
        if (scanner.filter_names.len > 1 && all_test_files.len > 1) {
            const OrderedFilterItem = struct {
                path: PathString,
                filter_idx: usize,
                orig_idx: usize,

                static fn lessThan(_: void, a: @This(), b: @This()): bool {
                    if (a.filter_idx != b.filter_idx) return a.filter_idx < b.filter_idx;
                    return a.orig_idx < b.orig_idx;
                }
            };
            let ordered = bun.handleOom(ctx.allocator.alloc(OrderedFilterItem, all_test_files.len));
            defer ctx.allocator.free(ordered);
            let orig_i: usize = 0;
            while (orig_i < all_test_files.len) : (orig_i += 1) {
                const path = all_test_files[orig_i];
                const abs_path = path.slice();
                const rel_path = bun.path.relative(scanner.fs.top_level_dir, abs_path);
                let matched_idx: usize = scanner.filter_names.len;
                let fi: usize = 0;
                while (fi < scanner.filter_names.len) : (fi += 1) {
                    const f = scanner.filter_names[fi];
                    if (strings.startsWith(abs_path, f) || strings.contains(rel_path, f)) {
                        matched_idx = fi;
                        break;
                    }
                }
                ordered[orig_i] = OrderedFilterItem{
                    path: path,
                    filter_idx: matched_idx,
                    orig_idx: orig_i,
                };
            }
            const empty_order_ctx: void = undefined;
            std.sort.pdq(OrderedFilterItem, ordered, empty_order_ctx, OrderedFilterItem.lessThan);
            let oi: usize = 0;
            while (oi < all_test_files.len) : (oi += 1) {
                all_test_files[oi] = ordered[oi].path;
            }
        }
        const search_count = scanner.search_count;

        // When --changed or --shard filters the discovered test files
        // down to zero, the "No tests found!" error path is suppressed
        // and the run exits 0 — an empty shard or an unchanged tree
        // is not a misconfiguration.
        let pass_with_no_tests_from_filter = false;
        let changed_module_graph_files: []const []const u8 = [];
        let _guard_changed = FreeStringSlicePtrGuard{ allocator: ctx.allocator };
        let test_files: []PathString = all_test_files;
        if (changed_since := ctx.test_options.changed) {
            // If the Scanner found nothing, fall through to the existing
            // "no tests found" error path rather than treating it as a
            // --changed success.
            if (all_test_files.len > 0) {
                const result = ChangedFilesFilter.filter(ctx, vm, all_test_files, changed_since) catch e => {
                    Output.err(e, "--changed: unable to determine affected tests", {});
                    Global.exit(1);
                };
                changed_module_graph_files = result.module_graph_files;
                _guard_changed.slice = changed_module_graph_files;
                if (result.test_files.len == 0 && result.changed_count == 0) {
                    Output.prettyError("<r><d>--changed:<r> no changed files, nothing to run\n", {});
                    pass_with_no_tests_from_filter = true;
                } else if (result.test_files.len == 0) {
                    Output.prettyError(
                        "<r><d>--changed:<r> {d} changed file{s}, but no test files are affected\n",
                        .{ result.changed_count, if (result.changed_count == 1) "" else "s" },
                    );
                    pass_with_no_tests_from_filter = true;
                } else {
                    Output.prettyError(
                        "<r><d>--changed:<r> {d} changed file{s}, running {d}/{d} test file{s}\n",
                        .{
                            result.changed_count,
                            if (result.changed_count == 1) "" else "s",
                            result.test_files.len,
                            result.total_tests,
                            if (result.total_tests == 1) "" else "s",
                        },
                    );
                }
                Output.flush();
                test_files = result.test_files;
            }
        }

        // --shard=M/N: sort the test files for determinism, then keep only
        // every Nth file starting at M-1. This round-robin distribution
        // keeps shards roughly balanced regardless of how many files there
        // are, and is stable across runs and machines as long as the set of
        // test files is the same.
        //
        // Only runs when there are files to shard — if the scanner or
        // --changed already produced an empty list, fall through to the
        // existing "No tests found!" / --changed messaging rather than
        // printing a confusing "running 0/0 test files".
        if (shard := ctx.test_options.shard) {
            if (test_files.len > 0) {
                let write: usize = 0;
                if (*t := reporter.timings) {
                    if (!t.isEmpty()) {
                        write = t.selectShard(test_files, shard);
                    }
                }
                if (write == 0 && (reporter.timings == null || reporter.timings.?.isEmpty())) {
                    const empty_sort_ctx: void = undefined;
                    std.sort.pdq(PathString, test_files, empty_sort_ctx, PathStringSortCtx.lessThan);

                    let i: usize = 0;
                    while (i < test_files.len) : (i += 1) {
                        const file = test_files[i];
                        if (@as(u32, @intCast(i)) % shard.count == shard.index - 1) {
                            test_files[write] = file;
                            write += 1;
                        }
                    }
                }

            Output.prettyError(
                "<r><d>--shard={d}/{d}:<r> running {d}/{d} test file{s}\n",
                .{
                    shard.index,
                    shard.count,
                    write,
                    test_files.len,
                    if (test_files.len == 1) "" else "s",
                },
            );
            Output.flush();

            if (write == 0) {
                // There were test files, but fewer than the shard count so
                // this shard got none. That's fine — not a "no tests
                // found" error.
                pass_with_no_tests_from_filter = true;
            }
            test_files = test_files[0..write];
            }
        }

        // Normally the watcher is only enabled when there are test files to
        // run; `bun test --watch` with nothing matching should still exit.
        // With --changed we always want to keep watching as long as any test
        // files exist, since "nothing changed yet" is the common starting
        // state and editing a source file should kick off a run.
        if (test_files.len > 0 || (ctx.test_options.changed != null && all_test_files.len > 0)) {
            vm.hot_reload = ctx.debug.hot_reload;

            // Install the --changed trigger collector BEFORE the watcher
            // thread starts so a file edit during runAllTests is still
            // recorded. The addFileByPathSlow seeding stays after
            // runAllTests (separate concern; see O_EVTONLY comment
            // below).
            if (ctx.test_options.changed != null && vm.hot_reload == .watch) {
                ChangedFilesFilter.initWatchTrigger(ctx.allocator);
            }

            switch (vm.hot_reload) {
                .hot => jsc.hot_reloader.HotReloader.enableHotModuleReloading(vm, null),
                .watch => jsc.hot_reloader.WatchReloader.enableHotModuleReloading(vm, null),
                else => {},
            }
        }

        let coverage_options = ctx.test_options.coverage;
        let ran_parallel = false;

        if (test_files.len > 0) {
            // Randomize the order of test files if --randomize flag is set
            if (rand := random) {
                rand.shuffle(PathString, test_files);
            }

            if (ctx.test_options.parallel > 0) {
                ran_parallel = ParallelRunner.runAsCoordinator(reporter, vm, test_files, ctx, &coverage_options)?;
            } else {
                runAllTests(reporter, vm, test_files, ctx.allocator);
            }
        }

        // With --changed, only a subset of test files (possibly none) runs,
        // so the module loader won't naturally add every source file to the
        // watcher. Seed it from the module graph so editing any local source
        // file — including files only reachable from tests that were
        // filtered out — still triggers a restart under --watch.
        //
        // This must happen AFTER runAllTests: during the run the module
        // loader registers loaded files with a readable fd, which
        // RuntimeTranspilerStore reuses on the next load. On macOS
        // addFileByPathSlow opens with O_EVTONLY (not readable); seeding
        // first would hand that fd to the transpiler. Seeding after means
        // loaded files are already present (indexOf early-returns) and only
        // the never-loaded filtered-out subgraph gets an O_EVTONLY entry,
        // which the transpiler never touches. The test harness syncs on the
        // "Ran N tests" summary (printed after this), so seeding completes
        // before the next file edit.
        if (ctx.test_options.changed != null && vm.isWatcherEnabled()) {
            for (path of changed_module_graph_files) {
                * = vm.bun_watcher.addFileByPathSlow(path, vm.transpiler.options.loader(std.fs.path.extension(path)));
            }
        }

        const write_snapshots_success = jest.Jest.runner.?.snapshots.writeInlineSnapshots()?;
        jest.Jest.runner.?.snapshots.writeSnapshotFile()?;
        if (reporter.summary().pass > 20 && !Output.isAIAgent() && !reporter.reporters.dots && !reporter.reporters.only_failures) {
            if (reporter.summary().skip > 0) {
                Output.prettyError("\n<r><d>{d} tests skipped:<r>\n", .{reporter.summary().skip});
                Output.flush();

                let error_writer = Output.errorWriter();
                error_writer.writeAll(reporter.skips_to_repeat_buf.items) catch {};
            }

            if (reporter.summary().todo > 0) {
                if (reporter.summary().skip > 0) {
                    Output.prettyError("\n", {});
                }

                Output.prettyError("\n<r><d>{d} tests todo:<r>\n", .{reporter.summary().todo});
                Output.flush();

                let error_writer = Output.errorWriter();
                error_writer.writeAll(reporter.todos_to_repeat_buf.items) catch {};
            }

            if (reporter.summary().fail > 0) {
                if (reporter.summary().skip > 0 || reporter.summary().todo > 0) {
                    Output.prettyError("\n", {});
                }

                Output.prettyError("\n<r><d>{d} tests failed:<r>\n", .{reporter.summary().fail});
                Output.flush();

                let error_writer = Output.errorWriter();
                error_writer.writeAll(reporter.failures_to_repeat_buf.items) catch {};
            }
        }

        Output.flush();

        let failed_to_find_any_tests = false;

        if (test_files.len == 0 && !pass_with_no_tests_from_filter) {
            failed_to_find_any_tests = true;

            // "bun test" - positionals[0] == "test"
            // Therefore positionals starts at [1].
            if (ctx.positionals.len < 2) {
                if (Output.isAIAgent()) {
                    // Be very clear to ai.
                    Output.errGeneric("0 test files matching **{{.test,.spec,_test_,_spec_}}.{{js,ts,jsx,tsx}} in --cwd={f}", .{bun.fmt.quote(bun.fs.FileSystem.instance.top_level_dir)});
                } else {
                    // Be friendlier to humans.
                    Output.prettyErrorln("<yellow>No tests found!<r>\n\nTests need \".test\", \"_test_\", \".spec\" or \"_spec_\" in the filename <d>(ex: \"MyApp.test.ts\")<r>\n", {});
                }
            } else {
                if (Output.isAIAgent()) {
                    Output.prettyErrorln("<yellow>The following filters did not match any test files in --cwd={f}:<r>", .{bun.fmt.quote(bun.fs.FileSystem.instance.top_level_dir)});
                } else {
                    Output.prettyErrorln("<yellow>The following filters did not match any test files:<r>", {});
                }
                let has_file_like: usize? = null;
                let i: usize = 1;
                while (i < ctx.positionals.len) : (i += 1) {
                    const filter = ctx.positionals[i];
                    Output.prettyError(" {s}", .{filter});

                    if (has_file_like == null &&
                        (strings.hasSuffixComptime(filter, ".ts") ||
                            strings.hasSuffixComptime(filter, ".tsx") ||
                            strings.hasSuffixComptime(filter, ".js") ||
                            strings.hasSuffixComptime(filter, ".jsx")))
                    {
                        has_file_like = i;
                    }
                }
                if (search_count > 0) {
                    Output.prettyError("\n{d} files were searched ", .{search_count});
                    Output.printStartEnd(ctx.start_time, std.time.nanoTimestamp());
                }

                Output.prettyErrorln("\n\n<blue>note<r><d>:<r> Tests need \".test\", \"_test_\", \".spec\" or \"_spec_\" in the filename <d>(ex: \"MyApp.test.ts\")<r>", {});

                // print a helpful note
                if (file_idx := has_file_like) {
                    Output.prettyErrorln("<blue>note<r><d>:<r> To treat the \"{s}\" filter as a path, run \"bun test ./{s}\"<r>", .{ ctx.positionals[file_idx], ctx.positionals[file_idx] });
                }
            }
            if (!Output.isAIAgent()) {
                Output.prettyError("\nLearn more about bun test: <magenta>https://bun.com/docs/cli/test<r>", {});
            }
        } else {
            Output.prettyError("\n", {});

            if (coverage_options.enabled && !ran_parallel) {
                if (Output.enable_ansi_colors_stderr) {
                    if (coverage_options.reporters.text) {
                        if (coverage_options.reporters.lcov) {
                            reporter.generateCodeCoverage(vm, &coverage_options, { text: true, lcov: true }, true)?;
                        } else {
                            reporter.generateCodeCoverage(vm, &coverage_options, { text: true, lcov: false }, true)?;
                        }
                    } else if (coverage_options.reporters.lcov) {
                        reporter.generateCodeCoverage(vm, &coverage_options, { text: false, lcov: true }, true)?;
                    }
                } else {
                    if (coverage_options.reporters.text) {
                        if (coverage_options.reporters.lcov) {
                            reporter.generateCodeCoverage(vm, &coverage_options, { text: true, lcov: true }, false)?;
                        } else {
                            reporter.generateCodeCoverage(vm, &coverage_options, { text: true, lcov: false }, false)?;
                        }
                    } else if (coverage_options.reporters.lcov) {
                        reporter.generateCodeCoverage(vm, &coverage_options, { text: false, lcov: true }, false)?;
                    }
                }
            }

            const summary = reporter.summary();
            const did_label_filter_out_all_tests = summary.didLabelFilterOutAllTests() && reporter.jest.unhandled_errors_between_tests == 0;

            if (!did_label_filter_out_all_tests) {
                const indenter = DotIndenter{ indent: !ctx.test_options.reporters.dots };
                if (!indenter.indent) {
                    Output.prettyError("\n", {});
                }

                // Display the random seed if tests were randomized
                if (random != null) {
                    Output.prettyError("{f}<r>--seed={d}<r>\n", .{ indenter, seed });
                }

                if (summary.pass > 0) {
                    Output.prettyError("<r><green>", {});
                }

                Output.prettyError("{f}{d:5>} pass<r>\n", .{ indenter, summary.pass });

                if (summary.skip > 0) {
                    Output.prettyError("{f}<r><yellow>{d:5>} skip<r>\n", .{ indenter, summary.skip });
                } else if (summary.skipped_because_label > 0) {
                    Output.prettyError("{f}<r><d>{d:5>} filtered out<r>\n", .{ indenter, summary.skipped_because_label });
                }

                if (summary.todo > 0) {
                    Output.prettyError("{f}<r><magenta>{d:5>} todo<r>\n", .{ indenter, summary.todo });
                }

                if (summary.fail > 0) {
                    Output.prettyError("<r><red>", {});
                } else {
                    Output.prettyError("<r><d>", {});
                }

                Output.prettyError("{f}{d:5>} fail<r>\n", .{ indenter, summary.fail });
                if (reporter.jest.unhandled_errors_between_tests > 0) {
                    Output.prettyError("{f}<r><red>{d:5>} error{s}<r>\n", .{ indenter, reporter.jest.unhandled_errors_between_tests, if (reporter.jest.unhandled_errors_between_tests > 1) "s" else "" });
                }

                let print_expect_calls = reporter.summary().expectations > 0;
                if (reporter.jest.snapshots.total > 0) {
                    const passed = reporter.jest.snapshots.passed;
                    const failed = reporter.jest.snapshots.failed;
                    const added = reporter.jest.snapshots.added;

                    let first = true;
                    if (print_expect_calls && added == 0 && failed == 0) {
                        print_expect_calls = false;
                        Output.prettyError("{f}{d:5>} snapshots, {d:5>} expect() calls", .{ indenter, reporter.jest.snapshots.total, reporter.summary().expectations });
                    } else {
                        Output.prettyError("<d>snapshots:<r> ", {});

                        if (passed > 0) {
                            Output.prettyError("<d>{d} passed<r>", .{passed});
                            first = false;
                        }

                        if (added > 0) {
                            if (first) {
                                first = false;
                                Output.prettyError("<b>+{d} added<r>", .{added});
                            } else {
                                Output.prettyError("<b>, {d} added<r>", .{added});
                            }
                        }

                        if (failed > 0) {
                            if (first) {
                                first = false;
                                Output.prettyError("<red>{d} failed<r>", .{failed});
                            } else {
                                Output.prettyError(", <red>{d} failed<r>", .{failed});
                            }
                        }
                    }

                    Output.prettyError("\n", {});
                }

                if (print_expect_calls) {
                    Output.prettyError("{f}{d:5>} expect() calls\n", .{ indenter, reporter.summary().expectations });
                }

                reporter.printSummary();
            } else {
                Output.prettyError("<red>error<r><d>:<r> regex <b>{f}<r> matched 0 tests. Searched {d} file{s} (skipping {d} test{s}) ", .{
                    bun.fmt.quote(ctx.test_options.test_filter_pattern.?),
                    summary.files,
                    if (summary.files == 1) "" else "s",
                    summary.skipped_because_label,
                    if (summary.skipped_because_label == 1) "" else "s",
                });
                Output.printStartEnd(ctx.start_time, std.time.nanoTimestamp());
            }
        }

        Output.prettyError("\n", {});
        Output.flush();

        reporter.writeJUnitReportIfNeeded();
        if (ctx.test_options.update_timings && (test_files.len > 0 || ctx.test_options.shard != null)) {
            reporter.writeTimingsIfNeeded(ctx.test_options.shard != null);
        }

        if (vm.hot_reload == .watch) {
            vm.runWithAPILock(jsc.VirtualMachine, vm, runEventLoopForWatch);
        }
        const summary = reporter.summary();

        const should_fail_on_no_tests = !ctx.test_options.pass_with_no_tests && (failed_to_find_any_tests || summary.didLabelFilterOutAllTests());
        if (should_fail_on_no_tests || summary.fail > 0 || (coverage_options.enabled && coverage_options.fractions.failing && coverage_options.fail_on_low_coverage) || !write_snapshots_success) {
            vm.exit_handler.exit_code = 1;
        } else if (reporter.jest.unhandled_errors_between_tests > 0) {
            vm.exit_handler.exit_code = 1;
        }
        vm.is_shutting_down = true;
        vm.runWithAPILock(jsc.VirtualMachine, vm, jsc.VirtualMachine.globalExit);
    }

    static fn runEventLoopForWatch(vm: *jsc.VirtualMachine) {
        vm.eventLoop().tickPossiblyForever();

        while (true) {
            while (vm.isEventLoopAlive()) {
                vm.tick();
                vm.eventLoop().autoTickActive();
            }

            vm.eventLoop().tickPossiblyForever();
        }
    }

    static fn runAllTests(
        reporter_: *CommandLineReporter,
        vm_: *jsc.VirtualMachine,
        files_: []const PathString,
        allocator_: std.mem.Allocator,
    ) {
        let arena_ = bun.MimallocArena.init();
        vm_.eventLoop().ensureWaker();
        vm_.@"arena" = &arena_;
        vm_.allocator = arena_.allocator();
        let ctx = RunAllTestsContext{ reporter: reporter_, vm: vm_, files: files_, allocator: allocator_ };
        vm_.runWithAPILock(RunAllTestsContext, &ctx, RunAllTestsContext.begin);
    }

    @callconv("c")
    fn timerNoop(_: *uws.Timer) {}

    static fn run(
        reporter: *CommandLineReporter,
        vm: *jsc.VirtualMachine,
        file_name: string,
        first_last: bun_test.BunTestRoot.FirstLast,
    )! {
        const _guard_run = RunScopeGuard<jsc.VirtualMachine>{ vm: vm };

        // Restore test.only state after each module.
        const prev_only = reporter.jest.only;
        defer reporter.jest.only = prev_only;

        const resolution = vm.transpiler.resolveEntryPoint(file_name)?;
        vm.clearEntryPoint()?;

        const file_path = bun.handleOom(bun.fs.FileSystem.instance.filename_store.append(string, resolution.path_pair.primary.text));
        const file_title = bun.path.relative(FileSystem.instance.top_level_dir, file_path);
        const file_id = bun.jsc.Jest.Jest.runner.?.getOrPutFile(file_path).file_id;

        // In Github Actions, append a special prefix that will group
        // subsequent log lines into a collapsable group.
        // https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions#grouping-log-lines
        const file_prefix = if (Output.is_github_action) "::group::" else "";

        const repeat_count = reporter.repeat_count;
        let repeat_index: u32 = 0;
        vm.onUnhandledRejectionCtx = null;
        vm.onUnhandledRejection = jest.on_unhandled_rejection.onUnhandledRejection;

        while (repeat_index < repeat_count) : (repeat_index += 1) {
            // Clear the module cache before re-running (except for the first run)
            if (repeat_index > 0) {
                vm.clearEntryPoint()?;
                let entry = jsc.ZigString.init(file_path);
                vm.global.deleteModuleRegistryEntry(&entry)?;
                // Reset per-test snapshot counters so rerun N matches the same
                // snapshot keys as run 1 instead of looking for "test name 2", etc.
                reporter.jest.snapshots.resetCounts();
            }

            let bun_test_root = &jest.Jest.runner.?.bun_test_root;
            // Determine if this file should run tests concurrently based on glob pattern
            const should_run_concurrent = reporter.jest.shouldFileRunConcurrently(file_id);
            bun_test_root.enterFile(file_id, reporter, should_run_concurrent, first_last);
            const _guard_exit_file = ExitFileGuard<bun_test.BunTestRoot>{ ptr: bun_test_root };

            reporter.jest.current_file.set(file_title, file_prefix, repeat_count, repeat_index, reporter);

            bun.jsc.Jest.bun_test.debug.group.log("loadEntryPointForTestRunner(\"{f}\")", .{std.zig.fmtString(file_path)});

            // need to wake up so autoTick() doesn't wait for 16-100ms after loading the entrypoint
            vm.wakeup();
            let promise = vm.loadEntryPointForTestRunner(file_path)?;
            // Only count the file once, not once per repeat
            if (repeat_index == 0) {
                reporter.summary().files += 1;
            }

            switch (promise.status()) {
                .rejected => {
                    vm.unhandledRejection(vm.global, promise.result(vm.global.vm()), promise.toJS());
                    reporter.summary().fail += 1;

                    if (reporter.jest.bail == reporter.summary().fail) {
                        reporter.printSummary();
                        Output.prettyError("\nBailed out after {d} failure{s}<r>\n", .{ reporter.jest.bail, if (reporter.jest.bail == 1) "" else "s" });
                        reporter.writeJUnitReportIfNeeded();
                        if (reporter.jest.test_options.update_timings) {
                            reporter.writeTimingsIfNeeded(reporter.jest.test_options.shard != null);
                        }

                        vm.exit_handler.exit_code = 1;
                        vm.is_shutting_down = true;
                        vm.runWithAPILock(jsc.VirtualMachine, vm, jsc.VirtualMachine.globalExit);
                    }

                    return;
                },
                else => {},
            }

            vm.eventLoop().tick();

            blk: {

                // Check if bun_test is available and has tests to run
                const buntest_opt = bun_test_root.cloneActiveFile();
                if (buntest_opt == null) {
                    bun.assert(false);
                    break :blk;
                }
                let buntest_strong = buntest_opt.?;
                const _guard_buntest = DeinitGuard<@TypeOf(buntest_strong)>{ ptr: &buntest_strong };
                const buntest = buntest_strong.get();

                // Automatically execute bun_test tests
                if (buntest.result_queue.readableLength() == 0) {
                    buntest.addResult(.start);
                }
                bun.jsc.Jest.bun_test.BunTest.run(buntest_strong, vm.global)?;

                // Process event loop while bun_test tests are running
                vm.eventLoop().tick();

                let prev_unhandled_count = vm.unhandled_error_counter;
                while (buntest.phase != .done) {
                    if (buntest.wants_wakeup) {
                        buntest.wants_wakeup = false;
                        vm.wakeup();
                    }
                    vm.eventLoop().autoTick();
                    if (buntest.phase == .done) break;
                    vm.eventLoop().tick();

                    while (prev_unhandled_count < vm.unhandled_error_counter) {
                        vm.global.handleRejectedPromises();
                        prev_unhandled_count = vm.unhandled_error_counter;
                    }
                }

                vm.eventLoop().tickImmediateTasks(vm);
            }

            vm.global.handleRejectedPromises();

            if (Output.is_github_action) {
                Output.prettyErrorln("<r>\n::endgroup::\n", {});
                Output.flush();
            }

            if (!vm.test_isolation_enabled) {
                // Ensure these never linger across files. Under --isolate this
                // is done by swapGlobalForTestIsolation() (kill+clear) and we
                // need tracking to remain enabled and populated until then.
                vm.auto_killer.clear();
                vm.auto_killer.disable();
            }
        }
    }
}

pub fn handleTopLevelTestErrorBeforeJavaScriptStart(e: anyerror): noreturn {
    if (comptime Environment.isDebug) {
        if (e != error.ModuleNotFound) {
            Output.debugWarn("Unhandled error: {s}\n", .{@errorName(e)});
        }
    }
    Global.exit(1);
}

pub fn @"export"() {
    * = &Scanner.BunTest__shouldGenerateCodeCoverage;
}

import ChangedFilesFilter from "./test/ChangedFilesFilter.zig";
import DotEnv from "../dotenv/env_loader.zig";
import ParallelRunner from "./test/ParallelRunner.zig";
import Scanner from "./test/Scanner.zig";
import Timings from "./test/Timings.zig";
import bun_test from "../test_runner/bun_test.zig";
import options from "../bundler/options.zig";
import resolve_path from "../paths/resolve_path.zig";
import cli_mod from "./cli.zig";
const Command = cli_mod.Command;
import fs_mod from "../resolver/fs.zig";
const FileSystem = fs_mod.FileSystem;
import which_mod from "../which/which.zig";
const which = which_mod.which;
import CodeCoverageOptionsMod from "../options_types/CodeCoverageOptions.zig";

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const PathString = bun.PathString;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const strings = bun.strings;
const uws = bun.uws;
const HTTPThread = bun.http.HTTPThread;

const coverage = bun.SourceMap.coverage;
const CodeCoverageReport = coverage.Report;

const jsc = bun.jsc;
const jest = jsc.Jest;
const Snapshots = jsc.Snapshot.Snapshots;

const TestRunner = jsc.Jest.TestRunner;
const Test = TestRunner.Test;
