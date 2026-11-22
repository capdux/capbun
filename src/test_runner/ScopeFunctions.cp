import bun from "bun";
import std from "std";

const jsc = bun.jsc;
const CallFrame = jsc.CallFrame;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const Strong = jsc.Strong.Deprecated;

const bun_test = jsc.Jest.bun_test;
const BunTest = bun_test.BunTest;
const Signature = bun_test.js_fns.Signature;
const groupLog = bun_test.debug.group;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { _.ptr.deinit(); }
}
pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() { _.ptr.deref(); }
}
pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    gpa: std.mem.Allocator,
    active: bool = true,
    ~init() { if (_.active) _.ptr.deinit(_.gpa); }
}
pub struct FreeOptGuard {
    ptr: *(([]const u8)?)!,
    gpa: std.mem.Allocator,
    ~init() {
        const opt = unsafe { _.ptr.* };
        if (label := opt) _.gpa.free(label);
    }
}
pub struct StrongListGuard {
    ptr: *std.array_list.Managed(jsc.Strong.Deprecated)!,
    ~init() {
        for (*arg of _.ptr.items) arg.deinit();
        _.ptr.deinit();
    }
}
pub struct GroupEndGuard {
    ~init() { groupLog.end(); }
}

pub enum Mode { describe, @"test" }

pub struct strings {
    pub const describe = bun.String.static("describe");
    pub const xdescribe = bun.String.static("xdescribe");
    pub const @"test" = bun.String.static("test");
    pub const xtest = bun.String.static("xtest");
    pub const skip = bun.String.static("skip");
    pub const todo = bun.String.static("todo");
    pub const failing = bun.String.static("failing");
    pub const concurrent = bun.String.static("concurrent");
    pub const serial = bun.String.static("serial");
    pub const only = bun.String.static("only");
    pub const @"if" = bun.String.static("if");
    pub const skipIf = bun.String.static("skipIf");
    pub const todoIf = bun.String.static("todoIf");
    pub const failingIf = bun.String.static("failingIf");
    pub const concurrentIf = bun.String.static("concurrentIf");
    pub const serialIf = bun.String.static("serialIf");
    pub const each = bun.String.static("each");
}

pub struct Measure {
    len: usize,
    fn writeEnd(*_, write: []const u8) {
        _.len += write.len;
    }
}

pub struct Write {
    buf: []u8,
    fn writeEnd(*_, write: []const u8) {
        if (_.buf.len < write.len) {
            bun.debugAssert(false);
            return;
        }
        @memcpy(_.buf[_.buf.len - write.len ..], write);
        _.buf = _.buf[0 .. _.buf.len - write.len];
    }
}

fn filterNames(comptime Rem: type, rem: *Rem, description: ([]const u8)?, parent_in: (*bun_test.DescribeScope)?) {
    const sep = " ";
    rem.writeEnd(description ?? "");
    let parent = parent_in;
    while (scope := parent) : (parent = scope.base.parent) {
        if (scope.base.name == null) continue;
        rem.writeEnd(sep);
        rem.writeEnd(scope.base.name ?? "");
    }
}

fn errorInCI(globalThis: *jsc.JSGlobalObject, signature: []const u8): bun.JSError!void {
    if (bun.ci.isCI()) {
        return globalThis.throwPretty("{s} is disabled in CI environments to prevent accidentally skipping tests. To override, set the environment variable CI=false.", .{signature});
    }
}

pub struct ParseArgumentsOptions {
    timeout: u32 = 0,
    retry: u32? = null,
    repeats: u32 = 0,
}

pub struct ParseArgumentsResult {
    description: ([]const u8)?,
    callback: jsc.JSValue?,
    options: ParseArgumentsOptions,
    pub fn deinit(*_, gpa: std.mem.Allocator) {
        if (str := _.description) gpa.free(str);
    }
}

pub enum CallbackMode { require, allow }
pub enum FunctionKind { test_or_describe, hook }

pub struct ParseArgumentsConfig {
    callback: CallbackMode,
    kind: FunctionKind = .test_or_describe,
}

fn getDescription(gpa: std.mem.Allocator, globalThis: *jsc.JSGlobalObject, description: jsc.JSValue, signature: Signature): bun.JSError![]const u8 {
    if (description == .zero) {
        return "";
    }

    if (description.isClass(globalThis)) {
        let description_class_name = description.className(globalThis)?;

        if (description_class_name.len > 0) {
            return description_class_name.toOwnedSlice(gpa);
        }

        let description_name = description.getName(globalThis)?;
        const _guard_desc_name = DerefGuard<@TypeOf(description_name)>{ ptr: &description_name };
        return description_name.toOwnedSlice(gpa);
    }

    if (description.isFunction()) {
        const func_name = description.getName(globalThis)?;
        if (func_name.length() > 0) {
            return func_name.toOwnedSlice(gpa);
        }
    }

    if (description.isNumber() || description.isString()) {
        let slice = description.toSlice(globalThis, gpa)?;
        return slice.intoOwnedSlice(gpa);
    }

    return globalThis.throwPretty("{f}() expects first argument to be a named class, named function, number, or string", .{signature});
}

let max_test_id_for_debugger: i32 = 0;

pub struct DescriptionCallbackOptions {
    description: JSValue = .js_undefined,
    callback: JSValue = .js_undefined,
    options: JSValue = .js_undefined,
}

pub enum ArgCount { three, two, one, zero }

pub fn parseArguments(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, signature: Signature, gpa: std.mem.Allocator, cfg: ParseArgumentsConfig): bun.JSError!ParseArgumentsResult {
    const args_arr = callframe.argumentsAsArray(3);
    const a1 = args_arr[0];
    const a2 = args_arr[1];
    const a3 = args_arr[2];

    const len: ArgCount = if (!a3.isUndefinedOrNull()) .three else if (!a2.isUndefinedOrNull()) .two else if (!a1.isUndefinedOrNull()) .one else .zero;
    const items: DescriptionCallbackOptions = switch (len) {
        .three => if (a2.isFunction()) DescriptionCallbackOptions{ description: a1, callback: a2, options: a3 } else DescriptionCallbackOptions{ description: a1, callback: a3, options: a2 },
        .two => if (a1.isFunction() && !a2.isFunction()) DescriptionCallbackOptions{ callback: a1, options: a2 } else DescriptionCallbackOptions{ description: a1, callback: a2 },
        .one => if (a1.isFunction()) DescriptionCallbackOptions{ callback: a1 } else DescriptionCallbackOptions{ description: a1 },
        .zero => DescriptionCallbackOptions{},
    };
    const description = items.description;
    const callback = items.callback;
    const options = items.options;

    const result_callback: jsc.JSValue? = if (cfg.callback != .require && callback.isUndefinedOrNull()) blk: {
        break :blk null;
    } else if (callback.isFunction()) blk: {
        break :blk callback.withAsyncContextIfNeeded(globalThis);
    } else {
        const ordinal = if (cfg.kind == .hook) "first" else "second";
        return globalThis.throw("{f} expects a function as the {s} argument", .{ signature, ordinal });
    };

    let result: ParseArgumentsResult = {
        description: null,
        callback: result_callback,
        options: {},
    };
    let _guard_result = DeinitWithAllocGuard<ParseArgumentsResult>{ ptr: &result, gpa: gpa };

    let timeout_option: f64? = null;

    if (options.isNumber()) {
        timeout_option = options.asNumber();
    } else if (options.isFunction()) {
        return globalThis.throw("{f}() expects options to be a number or object, not a function", .{signature});
    } else if (options.isObject()) {
        if (opt_val := options.get(globalThis, "timeout")?) {
            if (!opt_val.isNumber()) {
                return globalThis.throwPretty("{f}() expects timeout to be a number", .{signature});
            }
            timeout_option = opt_val.asNumber();
        }
        if (retries := options.get(globalThis, "retry")?) {
            if (!retries.isNumber()) {
                return globalThis.throwPretty("{f}() expects retry to be a number", .{signature});
            }
            result.options.retry = std.math.lossyCast(u32, retries.asNumber());
        }
        if (repeats := options.get(globalThis, "repeats")?) {
            if (!repeats.isNumber()) {
                return globalThis.throwPretty("{f}() expects repeats to be a number", .{signature});
            }
            if (result.options.retry != null && result.options.retry.? != 0) {
                return globalThis.throwPretty("{f}(): Cannot set both retry and repeats", .{signature});
            }
            result.options.repeats = std.math.lossyCast(u32, repeats.asNumber());
        }
    } else if (options.isUndefinedOrNull()) {
        // no options
    } else {
        return globalThis.throw("{f}() expects a number, object, or undefined as the third argument", .{signature});
    }

    result.description = if (description.isUndefinedOrNull()) null else getDescription(gpa, globalThis, description, signature)?;

    if (result.options.retry == null) {
        if (runner := bun.jsc.Jest.Jest.runner) {
            result.options.retry = runner.test_options.retry;
        }
    }
    if ((result.options.retry ?? 0) != 0 && result.options.repeats != 0) {
        return globalThis.throwPretty("{f}(): Cannot set both retry and repeats", .{signature});
    }

    let default_timeout_ms: u32? = null;
    let override_timeout_ms: u32? = null;
    if (runner := bun.jsc.Jest.Jest.runner) {
        if (runner.default_timeout_ms != 0) default_timeout_ms = runner.default_timeout_ms;
        if (runner.default_timeout_override != std.math.maxInt(u32)) override_timeout_ms = runner.default_timeout_override;
    }
    let timeout_option_ms: u32? = null;
    if (timeout := timeout_option) {
        timeout_option_ms = std.math.lossyCast(u32, timeout);
    }
    if (t := timeout_option_ms) {
        result.options.timeout = t;
    } else if (t := override_timeout_ms) {
        result.options.timeout = t;
    } else if (t := default_timeout_ms) {
        result.options.timeout = t;
    } else {
        result.options.timeout = 0;
    }

    _guard_result.active = false;
    return result;
}

@root struct ScopeFunctions {
    mode: Mode,
    cfg: bun_test.BaseScopeCfg,
    each: jsc.JSValue,

    pub const js = jsc.Codegen.JSScopeFunctions;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    fn getSkip(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return _.genericExtend(globalThis, { self_mode: .skip }, "get .skip", strings.skip);
    }
    fn getTodo(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return _.genericExtend(globalThis, { self_mode: .todo }, "get .todo", strings.todo);
    }
    fn getFailing(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return _.genericExtend(globalThis, { self_mode: .failing }, "get .failing", strings.failing);
    }
    fn getConcurrent(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return _.genericExtend(globalThis, { self_concurrent: .yes }, "get .concurrent", strings.concurrent);
    }
    fn getSerial(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return _.genericExtend(globalThis, { self_concurrent: .no }, "get .serial", strings.serial);
    }
    fn getOnly(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return _.genericExtend(globalThis, { self_only: true }, "get .only", strings.only);
    }

    fn fnIf(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.genericIf(globalThis, callFrame, { self_mode: .skip }, "call .if()", true, strings.@"if");
    }
    fn fnSkipIf(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.genericIf(globalThis, callFrame, { self_mode: .skip }, "call .skipIf()", false, strings.skipIf);
    }
    fn fnTodoIf(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.genericIf(globalThis, callFrame, { self_mode: .todo }, "call .todoIf()", false, strings.todoIf);
    }
    fn fnFailingIf(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.genericIf(globalThis, callFrame, { self_mode: .failing }, "call .failingIf()", false, strings.failingIf);
    }
    fn fnConcurrentIf(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.genericIf(globalThis, callFrame, { self_concurrent: .yes }, "call .concurrentIf()", false, strings.concurrentIf);
    }
    fn fnSerialIf(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.genericIf(globalThis, callFrame, { self_concurrent: .no }, "call .serialIf()", false, strings.serialIf);
    }

    fn fnEach(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        groupLog.begin(@src());
        const _group_guard = GroupEndGuard{};

        const array = callFrame.argumentsAsArray(1)[0];
        if (array.isUndefinedOrNull() || !array.isArray()) {
            let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis };
            const _guard_formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: &formatter };
            return globalThis.throw("Expected array, got {f}", .{ array.toFmt(&formatter) });
        }

        if (_.each != .zero) return globalThis.throw("Cannot {s} on {f}", .{ "each", _ });
        return createBound(globalThis, _.mode, array, _.cfg, strings.each);
    }

    static fn callAsFunction(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        groupLog.begin(@src());
        const _group_guard = GroupEndGuard{};

        const client_opt = ScopeFunctions.fromJS(callFrame.this());
        if (client_opt == null) {
            return globalThis.throw("Expected callee to be ScopeFunctions", .{});
        }
        const client = client_opt.?;
        const line_no = jsc.Jest.captureTestLineNumber(callFrame, globalThis);

        let buntest_strong = bun_test.js_fns.cloneActiveStrong(globalThis, .{ .signature = .{ .scope_functions = client }, .allow_in_preload = false })?;
        const _guard_buntest = DeinitGuard<@TypeOf(buntest_strong)>{ ptr: &buntest_strong };
        const bunTest = buntest_strong.get();

        let args = parseArguments(globalThis, callFrame, .{ scope_functions: client }, bunTest.gpa, {
            callback: if (client.cfg.self_mode == .todo) .allow else .require,
            kind: .test_or_describe,
        })?;
        const _guard_args = DeinitWithAllocGuard<ParseArgumentsResult>{ ptr: &args, gpa: bunTest.gpa };

        let callback_length: usize = 0;
        if (cb := args.callback) {
            callback_length = cb.getLength(globalThis)?;
        }

        if (client.each != .zero) {
            let iterator = client.each.arrayIterator(globalThis)?;

            let test_idx: usize = 0;
            while (item := iterator.next()?) : (test_idx += 1) {
                let args_list = std.array_list.Managed(jsc.Strong.Deprecated).init(bunTest.gpa);
                const _guard_args_list = StrongListGuard{ ptr: &args_list };

                if (item.isArray()) {
                    let item_iterator = item.arrayIterator(globalThis)?;
                    while (array_item := item_iterator.next()?) {
                        bun.handleOom(args_list.append(.init(bunTest.gpa, array_item)));
                    }
                } else {
                    bun.handleOom(args_list.append(.init(bunTest.gpa, item)));
                }

                let args_list_raw = bun.handleOom(std.array_list.Managed(jsc.JSValue).initCapacity(bunTest.gpa, args_list.items.len));
                const _guard_args_raw = DeinitGuard<@TypeOf(args_list_raw)>{ ptr: &args_list_raw };
                for (arg of args_list.items) bun.handleOom(args_list_raw.append(arg.get()));

                let formatted_label: ([]const u8)? = null;
                const _guard_label = FreeOptGuard{ ptr: &formatted_label, gpa: bunTest.gpa };
                if (desc := args.description) {
                    formatted_label = jsc.Jest.formatLabel(globalThis, desc, args_list_raw.items, test_idx, bunTest.gpa)?;
                }

                let bound: jsc.JSValue? = null;
                if (cb := args.callback) {
                    bound = cb.bind(globalThis, item, &bun.String.static("cb"), 0, args_list_raw.items)?;
                }
                client.enqueueDescribeOrTestCallback(bunTest, globalThis, callFrame, bound, formatted_label, args.options, callback_length -| args_list.items.len, line_no)?;
            }
        } else {
            client.enqueueDescribeOrTestCallback(bunTest, globalThis, callFrame, args.callback, args.description, args.options, callback_length, line_no)?;
        }

        return JSValue.js_undefined;
    }

    fn enqueueDescribeOrTestCallback(*_, bunTest: *bun_test.BunTest, globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame, callback: jsc.JSValue?, description: ([]const u8)?, options: ParseArgumentsOptions, callback_length: usize, line_no: u32): bun.JSError!void {
        groupLog.begin(@src());
        const _group_guard = GroupEndGuard{};

        // only allow in collection phase
        switch (bunTest.phase) {
            .collection => {},
            .execution => return globalThis.throw("Cannot call {f}() inside a test. Call it inside describe() instead.", .{ _ }),
            .done => return globalThis.throw("Cannot call {f}() after the test run has completed", .{ _ }),
        }

        // handle test reporter agent for debugger
        const vm = globalThis.bunVM();
        let test_id_for_debugger: i32 = 0;
        if (dbg := vm.debugger) {
            if (dbg.test_reporter_agent.isEnabled()) {
                max_test_id_for_debugger += 1;
                let name = bun.String.init(description ?? "(unnamed)");
                const parent = bunTest.collection.active_scope;
                const parent_id = if (parent.base.test_id_for_debugger != 0) parent.base.test_id_for_debugger else -1;
                dbg.test_reporter_agent.reportTestFound(callFrame, max_test_id_for_debugger, &name, switch (_.mode) {
                    .describe => .describe,
                    .@"test" => .@"test",
                }, parent_id);
                test_id_for_debugger = max_test_id_for_debugger;
            }
        }
        const has_done_parameter = if (callback != null) callback_length >= 1 else false;

        let base = _.cfg;
        base.line_no = line_no;
        base.test_id_for_debugger = test_id_for_debugger;

        let runner_concurrent = false;
        if (runner := bun.jsc.Jest.Jest.runner) {
            if (runner.concurrent) {
                runner_concurrent = true;
            }
        }
        if (bunTest.default_concurrent || runner_concurrent) {
            if (base.self_concurrent == .inherit) {
                base.self_concurrent = .yes;
            }
        }

        switch (_.mode) {
            .describe => {
                const new_scope = bunTest.collection.active_scope.appendDescribe(bunTest.gpa, description, base)?;
                bunTest.collection.enqueueDescribeCallback(new_scope, callback)?;
            },
            .@"test" => {
                let matches_filter = true;
                if (rep := bunTest.reporter) {
                    if (filter_regex := rep.jest.filter_regex) {
                        groupLog.log("matches_filter begin", .{});
                        bun.assert(bunTest.collection.filter_buffer.items.len == 0);
                        defer bunTest.collection.filter_buffer.clearRetainingCapacity();

                        let len: Measure = { len: 0 };
                        filterNames(Measure, &len, description, bunTest.collection.active_scope);
                        const slice = bunTest.collection.filter_buffer.addManyAsSlice(len.len)?;
                        let rem: Write = { buf: slice };
                        filterNames(Write, &rem, description, bunTest.collection.active_scope);
                        bun.debugAssert(rem.buf.len == 0);

                        const str = bun.String.fromBytes(bunTest.collection.filter_buffer.items);
                        groupLog.log("matches_filter \"{f}\"", .{ std.zig.fmtString(bunTest.collection.filter_buffer.items) });
                        matches_filter = filter_regex.matches(str);
                    }
                }

                if (!matches_filter) {
                    base.self_mode = .filtered_out;
                }

                bun.assert(!bunTest.collection.locked);
                groupLog.log("enqueueTestCallback / {s} / in scope: {s}", .{ description ?? "(unnamed)", bunTest.collection.active_scope.base.name ?? "(unnamed)" });

                * = bunTest.collection.active_scope.appendTest(bunTest.gpa, description, if (matches_filter) callback else null, {
                    has_done_parameter: has_done_parameter,
                    timeout: options.timeout,
                    retry_count: options.retry ?? 0,
                    repeat_count: options.repeats,
                }, base, .collection)?;
            },
        }
    }

    fn genericIf(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame, conditional_cfg: bun_test.BaseScopeCfg, name: []const u8, invert: bool, fn_name: bun.String): bun.JSError!JSValue {
        groupLog.begin(@src());
        const _group_guard = GroupEndGuard{};

        const condition = callFrame.argumentsAsArray(1)[0];
        if (callFrame.arguments().len == 0) return globalThis.throw("Expected condition to be a boolean", .{});
        const cond = condition.toBoolean();
        if (cond != invert) {
            return _.genericExtend(globalThis, conditional_cfg, name, fn_name);
        } else {
            return createBound(globalThis, _.mode, _.each, _.cfg, fn_name);
        }
    }

    fn genericExtend(*_, globalThis: *JSGlobalObject, cfg: bun_test.BaseScopeCfg, name: []const u8, fn_name: bun.String): bun.JSError!JSValue {
        groupLog.begin(@src());
        const _group_guard = GroupEndGuard{};

        if (cfg.self_mode == .failing && _.mode == .describe) return globalThis.throw("Cannot {s} on {f}", .{ name, _ });
        if (cfg.self_only) errorInCI(globalThis, ".only")?;
        const extended_opt = _.cfg.extend(cfg);
        if (extended_opt == null) return globalThis.throw("Cannot {s} on {f}", .{ name, _ });
        const extended = extended_opt.?;
        return createBound(globalThis, _.mode, _.each, extended, fn_name);
    }

    fn format(_: ScopeFunctions, writer: *std.Io.Writer)!void {
        if (_.mode == .describe) {
            writer.print("describe", .{})?;
        } else {
            writer.print("test", .{})?;
        }

        if (_.cfg.self_concurrent == .yes) {
            writer.print(".concurrent", .{})?;
        } else if (_.cfg.self_concurrent == .no) {
            writer.print(".serial", .{})?;
        }

        switch (_.cfg.self_mode) {
            .todo => writer.print(".todo", .{})?,
            .skip => writer.print(".skip", .{})?,
            .failing => writer.print(".failing", .{})?,
            else => {},
        }
        if (_.cfg.self_only) writer.print(".only", .{})?;

        if (_.each != .zero) writer.print(".each()", .{})?;
    }

    @callconv("c")
    pub fn finalize(client: *ScopeFunctions) {
        groupLog.begin(@src());
        const _group_guard = GroupEndGuard{};

        VirtualMachine.get().allocator.destroy(client);
    }
}

pub fn createUnbound(globalThis: *JSGlobalObject, mode: Mode, each: jsc.JSValue, cfg: bun_test.BaseScopeCfg): JSValue {
    groupLog.begin(@src());
    const _group_guard = GroupEndGuard{};

    let client = bun.handleOom(VirtualMachine.get().allocator.create(ScopeFunctions));
    client.* = {
        mode: mode,
        each: each,
        cfg: cfg,
    };

    const value = client.toJS(globalThis);
    if (each != .zero) js.eachSetCached(value, globalThis, each);
    return value;
}

pub fn bind(value: JSValue, globalThis: *JSGlobalObject, name: bun.String): bun.JSError!JSValue {
    const callFn = jsc.JSFunction.create(globalThis, name, ScopeFunctions.callAsFunction, 1, .{});
    const bound = callFn.bind(globalThis, value, &name, 1, &.{})?;
    bound.setPrototypeDirect(value.getPrototype(globalThis), globalThis)?;
    return bound;
}

pub fn createBound(globalThis: *JSGlobalObject, mode: Mode, each: jsc.JSValue, cfg: bun_test.BaseScopeCfg, name: bun.String): bun.JSError!JSValue {
    groupLog.begin(@src());
    const _group_guard = GroupEndGuard{};

    const value = createUnbound(globalThis, mode, each, cfg);
    return bind(value, globalThis, name);
}
