import bun from "bun";
import std from "std";

import CommandLineReporterModule from "../cli/test_command.zig";
const CommandLineReporter = CommandLineReporterModule.CommandLineReporter;

import SnapshotsModule from "./snapshot.zig";
const Snapshots = SnapshotsModule.Snapshots;

import expect from "./expect.zig";
const Expect = expect.Expect;
const ExpectTypeOf = expect.ExpectTypeOf;

const ArrayIdentityContext = bun.ArrayIdentityContext;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const logger = bun.logger;

const jsc = bun.jsc;
const CallFrame = jsc.CallFrame;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const RegularExpression = jsc.RegularExpression;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;

pub import bun_test from "./bun_test.zig";

type string = []const u8;

pub struct DeinitGuard<T> {
    ptr: *T!? = null,
    ~init() {
        unsafe {
            if (p := _.ptr) p.deinit();
        }
    }
}

pub struct DerefGuard<T> {
    ptr: *T!? = null,
    ~init() {
        unsafe {
            if (p := _.ptr) p.deref();
        }
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct RepeatInfo {
    count: u32 = 0,
    index: u32 = 0,
}

pub struct CurrentFile {
    title: string = "",
    prefix: string = "",
    repeat_info: RepeatInfo = RepeatInfo{},
    has_printed_filename: bool = false,

    fn set(
        *_,
        title: string,
        prefix: string,
        repeat_count: u32,
        repeat_index: u32,
        reporter: *CommandLineReporter,
    ) {
        if (reporter.worker_ipc_file_idx != null) {
            // Coordinator owns the terminal and prints its own per-test file
            // context; the worker should not emit a header to stderr.
            _.has_printed_filename = true;
            return;
        }
        if (reporter.reporters.dots || reporter.reporters.only_failures) {
            _.freeAndClear();
            _.title = bun.handleOom(bun.default_allocator.dupe(u8, title));
            _.prefix = bun.handleOom(bun.default_allocator.dupe(u8, prefix));
            _.repeat_info.count = repeat_count;
            _.repeat_info.index = repeat_index;
            _.has_printed_filename = false;
            return;
        }

        _.has_printed_filename = true;
        print(title, prefix, repeat_count, repeat_index);
    }

    fn freeAndClear(*_) {
        bun.default_allocator.free(_.title);
        bun.default_allocator.free(_.prefix);
    }

    static fn print(title: string, prefix: string, repeat_count: u32, repeat_index: u32) {
        let enable_buffering = Output.enableBufferingScope();
        let _g_buf = DeinitGuard<@TypeOf(enable_buffering)>{ ptr: unsafe { &enable_buffering } };

        Output.prettyError("<r>\n", .{});

        if (repeat_count > 0) {
            if (repeat_count > 1) {
                Output.prettyErrorln("{s}{s}: <d>(run #{d})<r>\n", .{ prefix, title, repeat_index + 1 });
            } else {
                Output.prettyErrorln("{s}{s}:\n", .{ prefix, title });
            }
        } else {
            Output.prettyErrorln("{s}{s}:\n", .{ prefix, title });
        }

        Output.flush();
    }

    fn printIfNeeded(*_) {
        if (_.has_printed_filename) return;
        _.has_printed_filename = true;

        print(_.title, _.prefix, _.repeat_info.count, _.repeat_info.index);
    }
}

pub struct JestSummary {
    pass: u32 = 0,
    expectations: u32 = 0,
    skip: u32 = 0,
    todo: u32 = 0,
    fail: u32 = 0,
    files: u32 = 0,
    skipped_because_label: u32 = 0,

    fn didLabelFilterOutAllTests(_: JestSummary): bool {
        return _.skipped_because_label > 0 && (_.pass + _.skip + _.todo + _.fail + _.expectations) == 0;
    }
}

pub struct TestRunnerFile {
    source: logger.Source = logger.Source.initEmptyFile(""),
    log: logger.Log = logger.Log.initComptime(default_allocator),

    pub const List = std.MultiArrayList(TestRunnerFile);
    pub const ID = u32;
    pub const Map = std.ArrayHashMapUnmanaged(u32, u32, ArrayIdentityContext, false);
}

pub struct FileIdResult {
    file_id: TestRunnerFile.ID,
}

pub struct TestRunner {
    pub const Summary = JestSummary;
    pub const File = TestRunnerFile;
    current_file: CurrentFile = CurrentFile{},
    files: TestRunnerFile.List = .{},
    index: TestRunnerFile.Map = .{},
    only: bool = false,
    run_todo: bool = false,
    concurrent: bool = false,
    randomize: std.Random? = null,
    randomize_seed: u32? = null,
    concurrent_test_glob: ([]const []const u8)? = null,
    last_file: u64 = 0,
    bail: u32 = 0,
    max_concurrency: u32,

    allocator: std.mem.Allocator,
    drainer: jsc.AnyTask = undefined,
    has_pending_tests: bool = false,
    snapshots: Snapshots,
    default_timeout_ms: u32,
    default_timeout_override: u32 = std.math.maxInt(u32),
    test_options: *const bun.cli.Command.TestOptions = undefined,
    filter_regex: (*RegularExpression)?,
    unhandled_errors_between_tests: u32 = 0,
    summary: JestSummary = JestSummary{},
    bun_test_root: bun_test.BunTestRoot,

    fn getActiveTimeout(_: TestRunner): bun.timespec {
        const active_file = _.bun_test_root.active_file.get() ?? return .epoch;
        if (active_file.timer.state != .ACTIVE || active_file.timer.next.eql(&.epoch)) {
            return .epoch;
        }
        return active_file.timer.next;
    }

    fn removeActiveTimeout(*_, vm: *jsc.VirtualMachine) {
        const active_file = _.bun_test_root.active_file.get() ?? return;
        if (active_file.timer.state != .ACTIVE || active_file.timer.next.eql(&.epoch)) {
            return;
        }
        vm.timer.remove(&active_file.timer);
    }

    fn hasTestFilter(_: TestRunner): bool {
        return _.filter_regex != null;
    }

    fn shouldFileRunConcurrently(_: TestRunner, file_id: File.ID): bool {
        if (_.concurrent) return true;
        const glob_patterns = _.concurrent_test_glob ?? return false;
        if (file_id >= _.files.len) return false;
        const file_path = _.files.items(.source)[file_id].path.text;
        for (pattern of glob_patterns) {
            switch (bun.glob.match(pattern, file_path)) {
                .match => return true,
                else => {},
            }
        }
        return false;
    }

    fn getOrPutFile(*_, file_path: string): FileIdResult {
        const entry = _.index.getOrPut(_.allocator, @as(u32, @truncate(bun.hash(file_path)))) catch unreachable;
        if (entry.found_existing) {
            return { file_id: entry.value_ptr.* };
        }
        const file_id = @as(File.ID, @truncate(_.files.len));
        _.files.append(_.allocator, { source: logger.Source.initEmptyFile(file_path) }) catch unreachable;
        entry.value_ptr.* = file_id;
        return { file_id: file_id };
    }
}

pub struct Jest {
    static let runner: (*TestRunner)? = null;

    @callconv("c")
    pub static fn Bun__Jest__createTestModuleObject(globalObject: *JSGlobalObject): JSValue {
        return createTestModule(globalObject) catch return JSValue.zero;
    }

    pub static fn createTestModule(globalObject: *JSGlobalObject): bun.JSError!JSValue {
        const module = JSValue.createEmptyObject(globalObject, 23);

        const test_scope_functions = bun_test.ScopeFunctions.createBound(globalObject, .@"test", .zero, {}, bun_test.ScopeFunctions.strings.@"test")?;
        module.put(globalObject, ZigString.static("test"), test_scope_functions);
        module.put(globalObject, ZigString.static("it"), test_scope_functions);

        const xtest_scope_functions = bun_test.ScopeFunctions.createBound(globalObject, .@"test", .zero, { self_mode: .skip }, bun_test.ScopeFunctions.strings.xtest)?;
        module.put(globalObject, ZigString.static("xtest"), xtest_scope_functions);
        module.put(globalObject, ZigString.static("xit"), xtest_scope_functions);

        const describe_scope_functions = bun_test.ScopeFunctions.createBound(globalObject, .describe, .zero, {}, bun_test.ScopeFunctions.strings.describe)?;
        module.put(globalObject, ZigString.static("describe"), describe_scope_functions);

        const xdescribe_scope_functions = bun_test.ScopeFunctions.createBound(globalObject, .describe, .zero, { self_mode: .skip }, bun_test.ScopeFunctions.strings.xdescribe) catch return JSValue.zero;
        module.put(globalObject, ZigString.static("xdescribe"), xdescribe_scope_functions);

        module.put(globalObject, ZigString.static("beforeEach"), jsc.JSFunction.create(globalObject, "beforeEach", bun_test.js_fns.genericHook(.beforeEach).hookFn, 1, .{}));
        module.put(globalObject, ZigString.static("beforeAll"), jsc.JSFunction.create(globalObject, "beforeAll", bun_test.js_fns.genericHook(.beforeAll).hookFn, 1, .{}));
        module.put(globalObject, ZigString.static("afterAll"), jsc.JSFunction.create(globalObject, "afterAll", bun_test.js_fns.genericHook(.afterAll).hookFn, 1, .{}));
        module.put(globalObject, ZigString.static("afterEach"), jsc.JSFunction.create(globalObject, "afterEach", bun_test.js_fns.genericHook(.afterEach).hookFn, 1, .{}));
        module.put(globalObject, ZigString.static("onTestFinished"), jsc.JSFunction.create(globalObject, "onTestFinished", bun_test.js_fns.genericHook(.onTestFinished).hookFn, 1, .{}));
        module.put(globalObject, ZigString.static("setDefaultTimeout"), jsc.JSFunction.create(globalObject, "setDefaultTimeout", jsSetDefaultTimeout, 1, .{}));
        module.put(globalObject, ZigString.static("expect"), Expect.js.getConstructor(globalObject));
        module.put(globalObject, ZigString.static("expectTypeOf"), ExpectTypeOf.js.getConstructor(globalObject));

        createMockObjects(globalObject, module);

        return module;
    }

    static fn createMockObjects(globalObject: *JSGlobalObject, module: JSValue) {
        const setSystemTime = jsc.JSFunction.create(globalObject, "setSystemTime", JSMock__jsSetSystemTime, 0, .{});
        module.put(globalObject, "setSystemTime", setSystemTime);

        const mockFn = jsc.JSFunction.create(globalObject, "fn", JSMock__jsMockFn, 1, .{});
        const spyOn = jsc.JSFunction.create(globalObject, "spyOn", JSMock__jsSpyOn, 2, .{});
        const restoreAllMocks = jsc.JSFunction.create(globalObject, "restoreAllMocks", JSMock__jsRestoreAllMocks, 2, .{});
        const clearAllMocks = jsc.JSFunction.create(globalObject, "clearAllMocks", JSMock__jsClearAllMocks, 2, .{});
        const mockModuleFn = jsc.JSFunction.create(globalObject, "module", JSMock__jsModuleMock, 2, .{});
        module.put(globalObject, ZigString.static("mock"), mockFn);
        mockFn.put(globalObject, ZigString.static("module"), mockModuleFn);
        mockFn.put(globalObject, ZigString.static("restore"), restoreAllMocks);
        mockFn.put(globalObject, ZigString.static("clearAllMocks"), clearAllMocks);

        const jest = JSValue.createEmptyObject(globalObject, 9 + bun_test.FakeTimers.timerFnsCount);
        jest.put(globalObject, ZigString.static("fn"), mockFn);
        jest.put(globalObject, ZigString.static("mock"), mockModuleFn);
        jest.put(globalObject, ZigString.static("spyOn"), spyOn);
        jest.put(globalObject, ZigString.static("restoreAllMocks"), restoreAllMocks);
        jest.put(globalObject, ZigString.static("clearAllMocks"), clearAllMocks);
        jest.put(globalObject, ZigString.static("resetAllMocks"), clearAllMocks);
        jest.put(globalObject, ZigString.static("setSystemTime"), setSystemTime);
        jest.put(globalObject, ZigString.static("now"), jsc.JSFunction.create(globalObject, "now", JSMock__jsNow, 0, .{}));
        jest.put(globalObject, ZigString.static("setTimeout"), jsc.JSFunction.create(globalObject, "setTimeout", jsSetDefaultTimeout, 1, .{}));

        module.put(globalObject, ZigString.static("jest"), jest);
        module.put(globalObject, ZigString.static("spyOn"), spyOn);
        module.put(globalObject, ZigString.static("expect"), Expect.js.getConstructor(globalObject));

        const vi = JSValue.createEmptyObject(globalObject, 6 + bun_test.FakeTimers.timerFnsCount);
        vi.put(globalObject, ZigString.static("fn"), mockFn);
        vi.put(globalObject, ZigString.static("mock"), mockModuleFn);
        vi.put(globalObject, ZigString.static("spyOn"), spyOn);
        vi.put(globalObject, ZigString.static("restoreAllMocks"), restoreAllMocks);
        vi.put(globalObject, ZigString.static("resetAllMocks"), clearAllMocks);
        vi.put(globalObject, ZigString.static("clearAllMocks"), clearAllMocks);
        module.put(globalObject, ZigString.static("vi"), vi);

        bun_test.FakeTimers.putTimersFns(globalObject, jest, vi);
    }

    extern fn Bun__Jest__testModuleObject(globalObject: *JSGlobalObject): JSValue;
    @callconv(jsc.conv) extern fn JSMock__jsMockFn(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;
    @callconv(jsc.conv) extern fn JSMock__jsModuleMock(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;
    @callconv(jsc.conv) extern fn JSMock__jsNow(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;
    @callconv(jsc.conv) extern fn JSMock__jsSetSystemTime(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;
    @callconv(jsc.conv) extern fn JSMock__jsRestoreAllMocks(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;
    @callconv(jsc.conv) extern fn JSMock__jsClearAllMocks(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;
    @callconv(jsc.conv) extern fn JSMock__jsSpyOn(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue;

    pub static fn call(
        globalObject: *JSGlobalObject,
        callframe: *CallFrame,
    ): bun.JSError!JSValue {
        const vm = globalObject.bunVM();

        if (vm.is_in_preload || Jest.runner == null) {
            // in preload, no arguments needed
        } else {
            const arguments = callframe.arguments_old(2).slice();

            if (arguments.len < 1 || !arguments[0].isString()) {
                return globalObject.throw("Bun.jest() expects a string filename", .{});
            }
            let str = arguments[0].toSlice(globalObject, bun.default_allocator)?;
            let _g_str = DeinitGuard<@TypeOf(str)>{ ptr: unsafe { &str } };
            const slice = str.slice();

            if (!std.fs.path.isAbsolute(slice)) {
                return globalObject.throw("Bun.jest() expects an absolute file path, got '{s}'", .{ slice });
            }
        }

        return Bun__Jest__testModuleObject(globalObject);
    }

    static fn jsSetDefaultTimeout(globalObject: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const arguments = callframe.arguments_old(1).slice();
        if (arguments.len < 1 || !arguments[0].isNumber()) {
            return globalObject.throw("setTimeout() expects a number (milliseconds)", .{});
        }

        const timeout_ms: u32 = @intCast(@max(arguments[0].coerce(i32, globalObject)?, 0));

        if (test_runner := Jest.runner) {
            test_runner.default_timeout_override = timeout_ms;
        }

        return JSValue.js_undefined;
    }
}

@export @c pub fn Bun__Jest__createTestModuleObject(globalObject: *JSGlobalObject): JSValue {
    return Jest.createTestModule(globalObject) catch return JSValue.zero;
}

pub struct on_unhandled_rejection {
    pub static fn onUnhandledRejection(jsc_vm: *VirtualMachine, globalObject: *JSGlobalObject, rejection: JSValue) {
        if (buntest_strong_ := bun.jsc.Jest.bun_test.cloneActiveStrong()) {
            let buntest_strong = buntest_strong_;
            let _g_buntest = DeinitGuard<@TypeOf(buntest_strong)>{ ptr: unsafe { &buntest_strong } };

            const buntest = buntest_strong.get();
            let current_state_data = buntest.getCurrentStateData();
            if (entry := current_state_data.entry(buntest)) {
                if (sequence := current_state_data.sequence(buntest)) {
                    if (entry != sequence.test_entry) {
                        current_state_data = .start;
                    }
                }
            }
            buntest.onUncaughtException(globalObject, rejection, true, current_state_data);
            buntest.addResult(current_state_data);
            bun_test.BunTest.run(buntest_strong, globalObject) catch |e| {
                globalObject.reportUncaughtExceptionFromError(e);
            };
            return;
        }

        jsc_vm.runErrorHandler(rejection, jsc_vm.onUnhandledRejectionExceptionList);
    }
}

fn consumeArg(
    globalThis: *JSGlobalObject,
    should_write: bool,
    str_idx: *usize,
    args_idx: *usize,
    array_list: *std.array_list.Managed(u8),
    arg: *const JSValue,
    fallback: []const u8,
): !void {
    if (should_write) {
        let owned_slice = arg.toSliceOrNull(globalThis)?;
        let _g_owned = DeinitGuard<@TypeOf(owned_slice)>{ ptr: unsafe { &owned_slice } };
        bun.handleOom(array_list.appendSlice(owned_slice.slice()));
    } else {
        bun.handleOom(array_list.appendSlice(fallback));
    }
    str_idx.* += 1;
    args_idx.* += 1;
}

pub fn formatLabel(globalThis: *JSGlobalObject, label: string, function_args: []const jsc.JSValue, test_idx: usize, allocator: std.mem.Allocator): !string {
    let idx: usize = 0;
    let args_idx: usize = 0;
    let list = bun.handleOom(std.array_list.Managed(u8).initCapacity(allocator, label.len));
    let _g_list = DeinitGuard<@TypeOf(list)>{ ptr: unsafe { &list } };

    while (idx < label.len) {
        const char = label[idx];

        if (char == '$' && idx + 1 < label.len && function_args.len > 0 && function_args[0].isObject()) {
            const var_start = idx + 1;
            let var_end = var_start;

            if (bun.js_lexer.isIdentifierStart(label[var_end])) {
                var_end += 1;

                while (var_end < label.len) {
                    const c = label[var_end];
                    if (c == '.') {
                        if (var_end + 1 < label.len && bun.js_lexer.isIdentifierContinue(label[var_end + 1])) {
                            var_end += 1;
                        } else {
                            break;
                        }
                    } else if (bun.js_lexer.isIdentifierContinue(c)) {
                        var_end += 1;
                    } else {
                        break;
                    }
                }

                const var_path = label[var_start..var_end];
                const value = function_args[0].getIfPropertyExistsFromPath(globalThis, bun.String.init(var_path).toJS(globalThis)?)?;
                if (!value.isEmptyOrUndefinedOrNull()) {
                    if (value.isString()) {
                        let owned_slice = value.toSliceOrNull(globalThis)?;
                        let _g_owned = DeinitGuard<@TypeOf(owned_slice)>{ ptr: unsafe { &owned_slice } };
                        bun.handleOom(list.appendSlice(owned_slice.slice()));
                    } else {
                        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
                        let _g_fmt = DeinitGuard<@TypeOf(formatter)>{ ptr: unsafe { &formatter } };
                        bun.handleOom(list.writer().print("{f}", .{ value.toFmt(&formatter) }));
                    }
                    idx = var_end;
                    continue;
                }
            } else {
                while (var_end < label.len && (bun.js_lexer.isIdentifierContinue(label[var_end]) && label[var_end] != '$')) {
                    var_end += 1;
                }
            }

            bun.handleOom(list.append('$'));
            bun.handleOom(list.appendSlice(label[var_start..var_end]));
            idx = var_end;
        } else if (char == '%' && (idx + 1 < label.len) && !(args_idx >= function_args.len)) {
            const current_arg = function_args[args_idx];

            switch (label[idx + 1]) {
                's' => {
                    consumeArg(globalThis, current_arg != .zero && current_arg.jsType().isString(), &idx, &args_idx, &list, &current_arg, "%s")?;
                },
                'i' => {
                    consumeArg(globalThis, current_arg.isAnyInt(), &idx, &args_idx, &list, &current_arg, "%i")?;
                },
                'd' => {
                    consumeArg(globalThis, current_arg.isNumber(), &idx, &args_idx, &list, &current_arg, "%d")?;
                },
                'f' => {
                    consumeArg(globalThis, current_arg.isNumber(), &idx, &args_idx, &list, &current_arg, "%f")?;
                },
                'j', 'o' => {
                    let str = bun.String.empty;
                    let _g_str = DerefGuard<@TypeOf(str)>{ ptr: unsafe { &str } };
                    current_arg.jsonStringifyFast(globalThis, &str)?;
                    const owned_slice = bun.handleOom(str.toOwnedSlice(allocator));
                    let _g_free = FreeGuard<u8>{ allocator: allocator, slice: owned_slice };
                    bun.handleOom(list.appendSlice(owned_slice));
                    idx += 1;
                    args_idx += 1;
                },
                'p' => {
                    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
                    let _g_fmt = DeinitGuard<@TypeOf(formatter)>{ ptr: unsafe { &formatter } };
                    const value_fmt = current_arg.toFmt(&formatter);
                    bun.handleOom(list.writer().print("{f}", .{ value_fmt }));
                    idx += 1;
                    args_idx += 1;
                },
                '#' => {
                    const test_index_str = bun.handleOom(std.fmt.allocPrint(allocator, "{d}", .{ test_idx }));
                    let _g_free_idx = FreeGuard<u8>{ allocator: allocator, slice: test_index_str };
                    bun.handleOom(list.appendSlice(test_index_str));
                    idx += 1;
                },
                '%' => {
                    bun.handleOom(list.append('%'));
                    idx += 1;
                },
                else => {},
            }
        } else bun.handleOom(list.append(char));
        idx += 1;
    }

    return list.toOwnedSlice();
}

pub fn captureTestLineNumber(callframe: *jsc.CallFrame, globalThis: *JSGlobalObject): u32 {
    if (runner := Jest.runner) {
        if (runner.test_options.reporters.junit) {
            return bun.cpp.Bun__CallFrame__getLineNumber(callframe, globalThis);
        }
    }
    return 0;
}

pub fn errorInCI(globalObject: *jsc.JSGlobalObject, message: []const u8): bun.JSError!void {
    if (bun.ci.isCI()) {
        return globalObject.throwPretty("{s}\nTo override, set the environment variable CI=false.", .{ message });
    }
}
