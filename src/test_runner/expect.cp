import std from "std";
import diff_format_mod from "./diff_format.zig";
const DiffFormatter = diff_format_mod.DiffFormatter;
import toBeMod from "./expect/toBe.zig";
import toBeArrayMod from "./expect/toBeArray.zig";
import toBeArrayOfSizeMod from "./expect/toBeArrayOfSize.zig";
import toBeBooleanMod from "./expect/toBeBoolean.zig";
import toBeCloseToMod from "./expect/toBeCloseTo.zig";
import toBeDateMod from "./expect/toBeDate.zig";
import toBeDefinedMod from "./expect/toBeDefined.zig";
import toBeEmptyMod from "./expect/toBeEmpty.zig";
import toBeEmptyObjectMod from "./expect/toBeEmptyObject.zig";
import toBeEvenMod from "./expect/toBeEven.zig";
import toBeFalseMod from "./expect/toBeFalse.zig";
import toBeFalsyMod from "./expect/toBeFalsy.zig";
import toBeFiniteMod from "./expect/toBeFinite.zig";
import toBeFunctionMod from "./expect/toBeFunction.zig";
import toBeGreaterThanMod from "./expect/toBeGreaterThan.zig";
import toBeGreaterThanOrEqualMod from "./expect/toBeGreaterThanOrEqual.zig";
import toBeInstanceOfMod from "./expect/toBeInstanceOf.zig";
import toBeIntegerMod from "./expect/toBeInteger.zig";
import toBeLessThanMod from "./expect/toBeLessThan.zig";
import toBeLessThanOrEqualMod from "./expect/toBeLessThanOrEqual.zig";
import toBeNaNMod from "./expect/toBeNaN.zig";
import toBeNegativeMod from "./expect/toBeNegative.zig";
import toBeNilMod from "./expect/toBeNil.zig";
import toBeNullMod from "./expect/toBeNull.zig";
import toBeNumberMod from "./expect/toBeNumber.zig";
import toBeObjectMod from "./expect/toBeObject.zig";
import toBeOddMod from "./expect/toBeOdd.zig";
import toBeOneOfMod from "./expect/toBeOneOf.zig";
import toBePositiveMod from "./expect/toBePositive.zig";
import toBeStringMod from "./expect/toBeString.zig";
import toBeSymbolMod from "./expect/toBeSymbol.zig";
import toBeTrueMod from "./expect/toBeTrue.zig";
import toBeTruthyMod from "./expect/toBeTruthy.zig";
import toBeTypeOfMod from "./expect/toBeTypeOf.zig";
import toBeUndefinedMod from "./expect/toBeUndefined.zig";
import toBeValidDateMod from "./expect/toBeValidDate.zig";
import toBeWithinMod from "./expect/toBeWithin.zig";
import toContainMod from "./expect/toContain.zig";
import toContainAllKeysMod from "./expect/toContainAllKeys.zig";
import toContainAllValuesMod from "./expect/toContainAllValues.zig";
import toContainAnyKeysMod from "./expect/toContainAnyKeys.zig";
import toContainAnyValuesMod from "./expect/toContainAnyValues.zig";
import toContainEqualMod from "./expect/toContainEqual.zig";
import toContainKeyMod from "./expect/toContainKey.zig";
import toContainKeysMod from "./expect/toContainKeys.zig";
import toContainValueMod from "./expect/toContainValue.zig";
import toContainValuesMod from "./expect/toContainValues.zig";
import toEndWithMod from "./expect/toEndWith.zig";
import toEqualMod from "./expect/toEqual.zig";
import toEqualIgnoringWhitespaceMod from "./expect/toEqualIgnoringWhitespace.zig";
import toHaveBeenCalledMod from "./expect/toHaveBeenCalled.zig";
import toHaveBeenCalledOnceMod from "./expect/toHaveBeenCalledOnce.zig";
import toHaveBeenCalledTimesMod from "./expect/toHaveBeenCalledTimes.zig";
import toHaveBeenCalledWithMod from "./expect/toHaveBeenCalledWith.zig";
import toHaveBeenLastCalledWithMod from "./expect/toHaveBeenLastCalledWith.zig";
import toHaveBeenNthCalledWithMod from "./expect/toHaveBeenNthCalledWith.zig";
import toHaveLastReturnedWithMod from "./expect/toHaveLastReturnedWith.zig";
import toHaveLengthMod from "./expect/toHaveLength.zig";
import toHaveNthReturnedWithMod from "./expect/toHaveNthReturnedWith.zig";
import toHavePropertyMod from "./expect/toHaveProperty.zig";
import toHaveReturnedMod from "./expect/toHaveReturned.zig";
import toHaveReturnedTimesMod from "./expect/toHaveReturnedTimes.zig";
import toHaveReturnedWithMod from "./expect/toHaveReturnedWith.zig";
import toIncludeMod from "./expect/toInclude.zig";
import toIncludeRepeatedMod from "./expect/toIncludeRepeated.zig";
import toMatchMod from "./expect/toMatch.zig";
import toMatchInlineSnapshotMod from "./expect/toMatchInlineSnapshot.zig";
import toMatchObjectMod from "./expect/toMatchObject.zig";
import toMatchSnapshotMod from "./expect/toMatchSnapshot.zig";
import toSatisfyMod from "./expect/toSatisfy.zig";
import toStartWithMod from "./expect/toStartWith.zig";
import toStrictEqualMod from "./expect/toStrictEqual.zig";
import toThrowMod from "./expect/toThrow.zig";
import toThrowErrorMatchingInlineSnapshotMod from "./expect/toThrowErrorMatchingInlineSnapshot.zig";
import toThrowErrorMatchingSnapshotMod from "./expect/toThrowErrorMatchingSnapshot.zig";

import bun from "bun";
const Environment = bun.Environment;
const MutableString = bun.MutableString;
const Output = bun.Output;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const strings = bun.strings;

const jsc = bun.jsc;
const CallFrame = jsc.CallFrame;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;

const jest = bun.jsc.Jest;
const DescribeScope = jest.DescribeScope;
const Jest = jest.Jest;
const TestRunner = jest.TestRunner;

extern fn AsymmetricMatcherConstructorType__fromJS(globalObject: *JSGlobalObject, value: JSValue): i8;

pub enum AsymmetricMatcherType: u5 {
    none = 0,
    Symbol = 1,
    String = 2,
    Object = 3,
    Array = 4,
    BigInt = 5,
    Boolean = 6,
    Number = 7,
    Promise = 8,
    InstanceOf = 9,

    pub static fn fromJS(globalObject: *JSGlobalObject, value: JSValue): bun.JSError!AsymmetricMatcherType {
        const result = AsymmetricMatcherConstructorType__fromJS(globalObject, value);
        if (result == -1) return error.JSError;
        return @enumFromInt(result);
    }
}

pub struct Counter {
    expected: u32 = 0,
    actual: u32 = 0,
}

type ExpectRefDataPtr = *bun.jsc.Jest.bun_test.BunTest.RefData;

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct OptDerefGuard<T> {
    val: T? = null,
    fn take(*_): T? {
        const v = _.val;
        _.val = null;
        return v;
    }
    ~init() {
        if (v := _.val) v.deref();
    }
}

pub struct OptDeinitGuard<T> {
    val: T? = null,
    fn take(*_): T? {
        const v = _.val;
        _.val = null;
        return v;
    }
    ~init() {
        if (v := _.val) v.deinit();
    }
}

type OptStringSlice = ([]const u8)?;

pub struct OptSliceFreeGuard {
    allocator: std.mem.Allocator,
    slot: *OptStringSlice!,
    ~init() {
        unsafe {
            if (name := _.slot.*) {
                _.allocator.free(name);
                _.slot.* = null;
            }
        }
    }
}

/// Helper to retrieve matcher flags from a jsvalue of a class like ExpectAny, ExpectStringMatching, etc.
pub fn getMatcherFlags(comptime T: type, value: JSValue): Expect.Flags {
    if (flagsValue := T.flagsGetCached(value)) {
        if (!flagsValue.isEmpty()) {
            return Expect.Flags.fromBitset(flagsValue.toInt32());
        }
    }
    return {};
}

/// https://jestjs.io/docs/expect
// To support async tests, we need to track the test ID
pub struct _TestScope {
    test_id: TestRunner.Test.ID,
    describe: *DescribeScope,
}

pub enum _ExpectPromiseFlag: u2 {
    none = 0,
    resolves = 1,
    rejects = 2,
}

@packed(u8)
pub struct _ExpectFlags {
    // note: keep this struct in sync with C++ implementation (at bindings.cpp)
    promise: _ExpectPromiseFlag = .none,
    not: bool = false,
    // This was originally padding.
    // We don't use all the bits in the u5, so if you need to reuse this elsewhere, you could.
    asymmetric_matcher_constructor_type: AsymmetricMatcherType = .none,

    pub const AsymmetricMatcherConstructorType = AsymmetricMatcherType;
    pub const FlagsCppType = u8;

    comptime {
        if (@bitSizeOf(_ExpectFlags) != @bitSizeOf(FlagsCppType)) @compileError("Flags size is invalid, should match FlagsCppType");
    }

    inline fn encode(): FlagsCppType {
        return @bitCast(_);
    }

    static inline fn decode(bitset: FlagsCppType): _ExpectFlags {
        return @bitCast(bitset);
    }

    static inline fn fromBitset(bitset: auto): _ExpectFlags {
        return @bitCast(@as(u8, @truncate(@as(u32, @bitCast(bitset)))));
    }
}

struct _TrimResult {
    trimmed: []const u8,
    start_indent: ([]const u8)?,
    end_indent: ([]const u8)?,
}

struct _CustomMatcherParamsFormatter {
    colors: bool,
    globalThis: *JSGlobalObject,
    matcher_fn: JSValue,

    fn format(writer: *std.Io.Writer)! {
        // try to detect param names from matcher_fn (user function) source code
        if (source_str := jsc.JSFunction.getSourceCode(_.matcher_fn)) {
            const source_slice = DeinitGuard<ZigString.Slice>{ val: source_str.toUTF8(_.globalThis.allocator()) };

            let source: string = source_slice.val.slice();
            if (lparen := std.mem.indexOfScalar(u8, source, '(')) {
                if (rparen := std.mem.indexOfScalarPos(u8, source, lparen, ')')) {
                    const params_str = source[(lparen + 1)..rparen];
                    let param_index: usize = 0;
                    let iter = std.mem.splitScalar(u8, params_str, ',');
                    while (param_name := iter.next()) : (param_index += 1) {
                        if (param_index > 0) { // skip the first param from the matcher_fn, which is the received value
                            if (param_index > 1) {
                                writer.writeAll(if (_.colors) Output.prettyFmt("<r><d>, <r><green>", true) else ", ")?;
                            } else if (_.colors) {
                                writer.writeAll("<green>")?;
                            }
                            const param_name_trimmed = std.mem.trim(u8, param_name, " ");
                            if (param_name_trimmed.len > 0) {
                                writer.writeAll(param_name_trimmed)?;
                            } else {
                                writer.print("arg{d}", .{ param_index - 1 })?;
                            }
                        }
                    }
                    if (param_index > 1 && _.colors) {
                        writer.writeAll("<r>")?;
                    }
                    return; // don't do fallback
                }
            }
        }

        // fallback
        if (_.colors) {
            writer.print(Output.prettyFmt("<green>...args<r>", true), .{})?;
        } else {
            writer.print(Output.prettyFmt("<green>...args<r>", false), .{})?;
        }
    }
}

pub struct Expect {
    pub const js = jsc.Codegen.JSExpect;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Flags = {},
    parent: (*bun.jsc.Jest.bun_test.BunTest.RefData)?,
    custom_label: bun.String = bun.String.empty,

    pub const TestScope = _TestScope;

    fn incrementExpectCallCounter(*_) {
        if (_.parent == null) return; // not in bun:test
        const parent = _.parent.?;
        const buntest_opt = parent.bunTest();
        if (buntest_opt == null) return; // the test file this expect() call was for is no longer
        let buntest_strong = DeinitGuard<bun.jsc.Jest.bun_test.BunTestPtr>{ val: buntest_opt.? };
        const buntest = buntest_strong.val.get();
        if (sequence := parent.phase.sequence(buntest)) {
            // found active sequence
            sequence.expect_call_count = std.math.add(u32, sequence.expect_call_count, 1) catch std.math.maxInt(u32);
        } else {
            // in concurrent group or otherwise failed to get the sequence; increment the expect call count in the reporter directly
            if (reporter := buntest.reporter) {
                reporter.summary().expectations = std.math.add(u32, reporter.summary().expectations, 1) catch std.math.maxInt(u32);
            }
        }
    }

    fn bunTest(*_): bun.jsc.Jest.bun_test.BunTestPtr? {
        const parent_opt = _.parent;
        if (parent_opt == null) return null;
        const parent = parent_opt.?;
        return parent.bunTest();
    }

    pub const Flags = _ExpectFlags;

    static fn getSignature(comptime matcher_name: string, comptime args: string, comptime not: bool): [:0]const u8 {
        const received = "<d>expect(<r><red>received<r><d>).<r>";
        comptime if (not) {
            return received ++ "not<d>.<r>" ++ matcher_name ++ "<d>(<r>" ++ args ++ "<d>)<r>";
        };
        return received ++ matcher_name ++ "<d>(<r>" ++ args ++ "<d>)<r>";
    }

    static fn throwPrettyMatcherError(globalThis: *JSGlobalObject, custom_label: bun.String, matcher_name: auto, matcher_params: auto, flags: Flags, comptime message_fmt: string, message_args: auto): bun.JSError {
        switch (Output.enable_ansi_colors_stderr) {
            inline else => |colors| {
                const chain = switch (flags.promise) {
                    .resolves => if (flags.not) Output.prettyFmt("resolves<d>.<r>not<d>.<r>", colors) else Output.prettyFmt("resolves<d>.<r>", colors),
                    .rejects => if (flags.not) Output.prettyFmt("rejects<d>.<r>not<d>.<r>", colors) else Output.prettyFmt("rejects<d>.<r>", colors),
                    .none => if (flags.not) Output.prettyFmt("not<d>.<r>", colors) else "",
                };
                switch (!custom_label.isEmpty()) {
                    inline else => |use_default_label| {
                        if (use_default_label) {
                            const fmt = comptime Output.prettyFmt("<d>expect(<r><red>received<r><d>).<r>" ++ bun.deprecated.autoFormatLabel(@TypeOf(chain)) ++ bun.deprecated.autoFormatLabel(@TypeOf(matcher_name)) ++ "<d>(<r>" ++ bun.deprecated.autoFormatLabel(@TypeOf(matcher_params)) ++ "<d>)<r>\n\n" ++ message_fmt, colors);
                            return globalThis.throwPretty(fmt, .{ chain, matcher_name, matcher_params } ++ message_args);
                        } else {
                            const fmt = comptime Output.prettyFmt("{f}\n\n" ++ message_fmt, colors);
                            return globalThis.throwPretty(fmt, .{ custom_label } ++ message_args);
                        }
                    },
                }
            },
        }
    }

    fn getNot(*_, thisValue: JSValue, _: *JSGlobalObject): JSValue {
        _.flags.not = !_.flags.not;
        return thisValue;
    }

    fn getResolves(*_, thisValue: JSValue, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        _.flags.promise = switch (_.flags.promise) {
            .resolves, .none => .resolves,
            .rejects => {
                return globalThis.throw("Cannot chain .resolves() after .rejects()", .{});
            },
        };

        return thisValue;
    }

    fn getRejects(*_, thisValue: JSValue, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        _.flags.promise = switch (_.flags.promise) {
            .none, .rejects => .rejects,
            .resolves => {
                return globalThis.throw("Cannot chain .rejects() after .resolves()", .{});
            },
        };

        return thisValue;
    }

    fn getValue(*_, globalThis: *JSGlobalObject, thisValue: JSValue, matcher_name: string, comptime matcher_params_fmt: string): bun.JSError!JSValue {
        let value: JSValue = undefined;
        if (v := js.capturedValueGetCached(thisValue)) {
            value = v;
        } else return globalThis.throw("Internal error: the expect(value) was garbage collected but it should not have been!", .{});
        value.ensureStillAlive();

        const matcher_params = switch (Output.enable_ansi_colors_stderr) {
            inline else => |colors| comptime Output.prettyFmt(matcher_params_fmt, colors),
        };
        return processPromise(_.custom_label, _.flags, globalThis, value, matcher_name, matcher_params, false);
    }

    /// Processes the async flags (resolves/rejects), waiting for the async value if needed.
    /// If no flags, returns the original value
    /// If either flag is set, waits for the result, and returns either it as a JSValue, or null if the expectation failed (in which case if silent is false, also throws a js exception)
    static fn processPromise(custom_label: bun.String, flags: Expect.Flags, globalThis: *JSGlobalObject, value: JSValue, matcher_name: auto, matcher_params: auto, comptime silent: bool): bun.JSError!JSValue {
        switch (flags.promise) {
            inline .resolves, .rejects => |resolution| {
                if (p := value.asAnyPromise()) {
                    let promise = p;
                    const vm = globalThis.vm();
                    promise.setHandled(vm);

                    globalThis.bunVM().waitForPromise(promise);

                    const newValue = promise.result(vm);
                    switch (promise.status()) {
                        .fulfilled => switch (resolution) {
                            .resolves => {},
                            .rejects => {
                                if (!silent) {
                                    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
                                    const message = "Expected promise that rejects<r>\nReceived promise that resolved: <red>{f}<r>\n";
                                    return throwPrettyMatcherError(globalThis, custom_label, matcher_name, matcher_params, flags, message, .{ value.toFmt(&formatter.val) });
                                }
                                return error.JSError;
                            },
                            .none => unreachable,
                        },
                        .rejected => switch (resolution) {
                            .rejects => {},
                            .resolves => {
                                if (!silent) {
                                    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
                                    const message = "Expected promise that resolves<r>\nReceived promise that rejected: <red>{f}<r>\n";
                                    return throwPrettyMatcherError(globalThis, custom_label, matcher_name, matcher_params, flags, message, .{ value.toFmt(&formatter.val) });
                                }
                                return error.JSError;
                            },
                            .none => unreachable,
                        },
                        .pending => unreachable,
                    }

                    newValue.ensureStillAlive();
                    return newValue;
                } else {
                    if (!silent) {
                        let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
                        const message = "Expected promise<r>\nReceived: <red>{f}<r>\n";
                        return throwPrettyMatcherError(globalThis, custom_label, matcher_name, matcher_params, flags, message, .{ value.toFmt(&formatter.val) });
                    }
                    return error.JSError;
                }
            },
            else => {},
        }

        return value;
    }

    static fn isAsymmetricMatcher(value: JSValue): bool {
        if (ExpectCustomAsymmetricMatcher.fromJS(value) != null) return true;
        if (ExpectAny.fromJS(value) != null) return true;
        if (ExpectAnything.fromJS(value) != null) return true;
        if (ExpectStringMatching.fromJS(value) != null) return true;
        if (ExpectCloseTo.fromJS(value) != null) return true;
        if (ExpectObjectContaining.fromJS(value) != null) return true;
        if (ExpectStringContaining.fromJS(value) != null) return true;
        if (ExpectArrayContaining.fromJS(value) != null) return true;
        return false;
    }

    /// Called by C++ when matching with asymmetric matchers
    @callconv("c")
    static fn readFlagsAndProcessPromise(instanceValue: JSValue, globalThis: *JSGlobalObject, outFlags: *Expect.Flags.FlagsCppType, value: *JSValue, any_constructor_type: *u8): bool {
        const flags: Expect.Flags = flags: {
            if (instance := ExpectCustomAsymmetricMatcher.fromJS(instanceValue)) {
                break :flags instance.flags;
            } else if (instance := ExpectAny.fromJS(instanceValue)) {
                any_constructor_type.* = @intFromEnum(instance.flags.asymmetric_matcher_constructor_type);
                break :flags instance.flags;
            } else if (instance := ExpectAnything.fromJS(instanceValue)) {
                break :flags instance.flags;
            } else if (instance := ExpectStringMatching.fromJS(instanceValue)) {
                break :flags instance.flags;
            } else if (instance := ExpectCloseTo.fromJS(instanceValue)) {
                break :flags instance.flags;
            } else if (instance := ExpectObjectContaining.fromJS(instanceValue)) {
                break :flags instance.flags;
            } else if (instance := ExpectStringContaining.fromJS(instanceValue)) {
                break :flags instance.flags;
            } else if (instance := ExpectArrayContaining.fromJS(instanceValue)) {
                break :flags instance.flags;
            } else {
                const default_flags: Expect.Flags = {}; break :flags default_flags;
            }
        };

        outFlags.* = flags.encode();

        // (note that matcher_name/matcher_args are not used because silent=true)
        value.* = processPromise(bun.String.empty, flags, globalThis, value.*, "", "", true) catch return false;
        return true;
    }

    fn getSnapshotName(*_, allocator: std.mem.Allocator, hint: string): ![]const u8 {
        const parent_opt = _.parent;
        if (parent_opt == null) return error.NoTest;
        const parent = parent_opt.?;
        const buntest_opt = parent.bunTest();
        if (buntest_opt == null) return error.TestNotActive;
        let buntest_strong = DeinitGuard<bun.jsc.Jest.bun_test.BunTestPtr>{ val: buntest_opt.? };
        const buntest = buntest_strong.val.get();
        const execution_entry_opt = parent.phase.entry(buntest);
        if (execution_entry_opt == null) return error.SnapshotInConcurrentGroup;
        const execution_entry = execution_entry_opt.?;

        const test_name = execution_entry.base.name ?? "(unnamed)";

        let length: usize = 0;
        let curr_scope = execution_entry.base.parent;
        while (scope := curr_scope) {
            if (scope.base.name != null && scope.base.name.?.len > 0) {
                length += scope.base.name.?.len + 1;
            }
            curr_scope = scope.base.parent;
        }
        length += test_name.len;
        if (hint.len > 0) {
            length += hint.len + 2;
        }

        let buf = allocator.alloc(u8, length)?;

        let index = buf.len;
        if (hint.len > 0) {
            index -= hint.len;
            bun.copy(u8, buf[index..], hint);
            index -= test_name.len + 2;
            bun.copy(u8, buf[index..], test_name);
            bun.copy(u8, buf[index + test_name.len ..], ": ");
        } else {
            index -= test_name.len;
            bun.copy(u8, buf[index..], test_name);
        }
        // copy describe scopes in reverse order
        curr_scope = execution_entry.base.parent;
        while (scope := curr_scope) {
            if (scope.base.name != null && scope.base.name.?.len > 0) {
                index -= scope.base.name.?.len + 1;
                bun.copy(u8, buf[index..], scope.base.name.?);
                buf[index + scope.base.name.?.len] = ' ';
            }
            curr_scope = scope.base.parent;
        }

        return buf;
    }

    @callconv("c")
    fn finalize(*_) {
        _.custom_label.deref();
        if (parent := _.parent) parent.deref();
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        const value = if (arguments.len < 1) .js_undefined else arguments[0];

        let custom_label = bun.String.empty;
        if (arguments.len > 1) {
            if (arguments[1].isString() || arguments[1].implementsToString(globalThis)?) {
                const label = arguments[1].toBunString(globalThis)?;
                if (globalThis.hasException()) return JSValue.zero;
                custom_label = label;
            }
        }

        let expect = globalThis.bunVM().allocator.create(Expect) catch {
            custom_label.deref();
            return globalThis.throwOutOfMemory();
        };

        let entry_ref_guard = OptDerefGuard<ExpectRefDataPtr>{};
        if (buntest_strong_ := bun.jsc.Jest.bun_test.cloneActiveStrong()) {
            let buntest_strong = DeinitGuard<bun.jsc.Jest.bun_test.BunTestPtr>{ val: buntest_strong_ };
            entry_ref_guard.val = bun.jsc.Jest.bun_test.BunTest.ref(buntest_strong.val, buntest_strong.val.get().getCurrentStateData());
        }

        expect.* = {
            custom_label: custom_label,
            parent: entry_ref_guard.take(),
        };
        const expect_js_value = expect.toJS(globalThis);
        expect_js_value.ensureStillAlive();
        js.capturedValueSetCached(expect_js_value, globalThis, value);
        expect_js_value.ensureStillAlive();

        expect.postMatch(globalThis);
        return expect_js_value;
    }

    static fn ArgsTupleType(comptime T: type): type {
        const ti = @typeInfo(T);
        if (ti == .@"struct" && !ti.@"struct".is_tuple) {
            const fields = ti.@"struct".fields;
            const n = fields.len;
            const TypeT = @TypeOf(u8);
            comptime let types: [n]TypeT = undefined;
            comptime let i: usize = 0;
            inline while (i < n) : (i += 1) {
                types[i] = fields[i].@"type";
            }
            return std.meta.Tuple(&types);
        }
        return T;
    }

    static fn argsToTuple(args: auto): ArgsTupleType(@TypeOf(args)) {
        const T = @TypeOf(args);
        const ti = @typeInfo(T);
        if (comptime ti == .@"struct" && !ti.@"struct".is_tuple) {
            const fields = ti.@"struct".fields;
            let tup: ArgsTupleType(T) = undefined;
            comptime let i: usize = 0;
            inline while (i < fields.len) : (i += 1) {
                @field(tup, std.fmt.comptimePrint("{d}", .{i})) = @field(args, fields[i].name);
            }
            return tup;
        }
        return args;
    }

    fn throw(*_, globalThis: *JSGlobalObject, comptime signature: [:0]const u8, comptime fmt: [:0]const u8, args: auto): bun.JSError {
        const tup_args = Expect.argsToTuple(args);
        if (_.custom_label.isEmpty()) {
            return globalThis.throwPretty(signature ++ fmt, tup_args);
        } else {
            return globalThis.throwPretty("{f}" ++ fmt, .{ _.custom_label } ++ tup_args);
        }
    }

    static fn constructor(globalThis: *JSGlobalObject, _: *CallFrame): bun.JSError!*Expect {
        return globalThis.throw("expect() cannot be called with new", .{});
    }

    // pass here has a leading underscore to avoid name collision with the pass variable in other functions
    fn _pass(
        *_,
        globalThis: *JSGlobalObject,
        callFrame: *CallFrame,
    ): bun.JSError!JSValue {
        defer _.postMatch(globalThis);

        const arguments_ = callFrame.arguments_old(1);
        const arguments = arguments_.slice();

        let _msg: ZigString = ZigString.Empty;

        if (arguments.len > 0) {
            const value = arguments[0];
            value.ensureStillAlive();

            if (!value.isString()) {
                return globalThis.throwInvalidArgumentType("pass", "message", "string");
            }

            value.toZigString(&_msg, globalThis)?;
        } else {
            _msg = ZigString.fromBytes("passes by .pass() assertion");
        }

        _.incrementExpectCallCounter();

        const not = _.flags.not;
        let pass = true;

        if (not) pass = !pass;
        if (pass) return JSValue.js_undefined;

        let msg = DeinitGuard<ZigString.Slice>{ val: _msg.toSlice(default_allocator) };

        if (not) {
            const signature = comptime getSignature("pass", "", true);
            return _.throw(globalThis, signature, "\n\n{s}\n", .{ msg.val.slice() });
        }

        // should never reach here
        return JSValue.zero;
    }

    fn fail(
        *_,
        globalThis: *JSGlobalObject,
        callFrame: *CallFrame,
    ): bun.JSError!JSValue {
        defer _.postMatch(globalThis);

        const arguments_ = callFrame.arguments_old(1);
        const arguments = arguments_.slice();

        let _msg: ZigString = ZigString.Empty;

        if (arguments.len > 0) {
            const value = arguments[0];
            value.ensureStillAlive();

            if (!value.isString()) {
                return globalThis.throwInvalidArgumentType("fail", "message", "string");
            }

            value.toZigString(&_msg, globalThis)?;
        } else {
            _msg = ZigString.fromBytes("fails by .fail() assertion");
        }

        _.incrementExpectCallCounter();

        const not = _.flags.not;
        let pass = false;

        if (not) pass = !pass;
        if (pass) return JSValue.js_undefined;

        let msg = DeinitGuard<ZigString.Slice>{ val: _msg.toSlice(default_allocator) };

        const signature = comptime getSignature("fail", "", true);
        return _.throw(globalThis, signature, "\n\n{s}\n", .{ msg.val.slice() });
    }

    pub const toBe = toBeMod.toBe;
    pub const toBeArray = toBeArrayMod.toBeArray;
    pub const toBeArrayOfSize = toBeArrayOfSizeMod.toBeArrayOfSize;
    pub const toBeBoolean = toBeBooleanMod.toBeBoolean;
    pub const toBeCloseTo = toBeCloseToMod.toBeCloseTo;
    pub const toBeDate = toBeDateMod.toBeDate;
    pub const toBeDefined = toBeDefinedMod.toBeDefined;
    pub const toBeEmpty = toBeEmptyMod.toBeEmpty;
    pub const toBeEmptyObject = toBeEmptyObjectMod.toBeEmptyObject;
    pub const toBeEven = toBeEvenMod.toBeEven;
    pub const toBeFalse = toBeFalseMod.toBeFalse;
    pub const toBeFalsy = toBeFalsyMod.toBeFalsy;
    pub const toBeFinite = toBeFiniteMod.toBeFinite;
    pub const toBeFunction = toBeFunctionMod.toBeFunction;
    pub const toBeGreaterThan = toBeGreaterThanMod.toBeGreaterThan;
    pub const toBeGreaterThanOrEqual = toBeGreaterThanOrEqualMod.toBeGreaterThanOrEqual;
    pub const toBeInstanceOf = toBeInstanceOfMod.toBeInstanceOf;
    pub const toBeInteger = toBeIntegerMod.toBeInteger;
    pub const toBeLessThan = toBeLessThanMod.toBeLessThan;
    pub const toBeLessThanOrEqual = toBeLessThanOrEqualMod.toBeLessThanOrEqual;
    pub const toBeNaN = toBeNaNMod.toBeNaN;
    pub const toBeNegative = toBeNegativeMod.toBeNegative;
    pub const toBeNil = toBeNilMod.toBeNil;
    pub const toBeNull = toBeNullMod.toBeNull;
    pub const toBeNumber = toBeNumberMod.toBeNumber;
    pub const toBeObject = toBeObjectMod.toBeObject;
    pub const toBeOdd = toBeOddMod.toBeOdd;
    pub const toBeOneOf = toBeOneOfMod.toBeOneOf;
    pub const toBePositive = toBePositiveMod.toBePositive;
    pub const toBeString = toBeStringMod.toBeString;
    pub const toBeSymbol = toBeSymbolMod.toBeSymbol;
    pub const toBeTrue = toBeTrueMod.toBeTrue;
    pub const toBeTruthy = toBeTruthyMod.toBeTruthy;
    pub const toBeTypeOf = toBeTypeOfMod.toBeTypeOf;
    pub const toBeUndefined = toBeUndefinedMod.toBeUndefined;
    pub const toBeValidDate = toBeValidDateMod.toBeValidDate;
    pub const toBeWithin = toBeWithinMod.toBeWithin;
    pub const toContain = toContainMod.toContain;
    pub const toContainAllKeys = toContainAllKeysMod.toContainAllKeys;
    pub const toContainAllValues = toContainAllValuesMod.toContainAllValues;
    pub const toContainAnyKeys = toContainAnyKeysMod.toContainAnyKeys;
    pub const toContainAnyValues = toContainAnyValuesMod.toContainAnyValues;
    pub const toContainEqual = toContainEqualMod.toContainEqual;
    pub const toContainKey = toContainKeyMod.toContainKey;
    pub const toContainKeys = toContainKeysMod.toContainKeys;
    pub const toContainValue = toContainValueMod.toContainValue;
    pub const toContainValues = toContainValuesMod.toContainValues;
    pub const toEndWith = toEndWithMod.toEndWith;
    pub const toEqual = toEqualMod.toEqual;
    pub const toEqualIgnoringWhitespace = toEqualIgnoringWhitespaceMod.toEqualIgnoringWhitespace;
    pub const toHaveBeenCalled = toHaveBeenCalledMod.toHaveBeenCalled;
    pub const toHaveBeenCalledOnce = toHaveBeenCalledOnceMod.toHaveBeenCalledOnce;
    pub const toHaveBeenCalledTimes = toHaveBeenCalledTimesMod.toHaveBeenCalledTimes;
    pub const toHaveBeenCalledWith = toHaveBeenCalledWithMod.toHaveBeenCalledWith;
    pub const toHaveBeenLastCalledWith = toHaveBeenLastCalledWithMod.toHaveBeenLastCalledWith;
    pub const toHaveBeenNthCalledWith = toHaveBeenNthCalledWithMod.toHaveBeenNthCalledWith;
    pub const toHaveLastReturnedWith = toHaveLastReturnedWithMod.toHaveLastReturnedWith;
    pub const toHaveLength = toHaveLengthMod.toHaveLength;
    pub const toHaveNthReturnedWith = toHaveNthReturnedWithMod.toHaveNthReturnedWith;
    pub const toHaveProperty = toHavePropertyMod.toHaveProperty;
    pub const toHaveReturned = toHaveReturnedMod.toHaveReturned;
    pub const toHaveReturnedTimes = toHaveReturnedTimesMod.toHaveReturnedTimes;
    pub const toHaveReturnedWith = toHaveReturnedWithMod.toHaveReturnedWith;
    pub const toInclude = toIncludeMod.toInclude;
    pub const toIncludeRepeated = toIncludeRepeatedMod.toIncludeRepeated;
    pub const toMatch = toMatchMod.toMatch;
    pub const toMatchInlineSnapshot = toMatchInlineSnapshotMod.toMatchInlineSnapshot;
    pub const toMatchObject = toMatchObjectMod.toMatchObject;
    pub const toMatchSnapshot = toMatchSnapshotMod.toMatchSnapshot;
    pub const toSatisfy = toSatisfyMod.toSatisfy;
    pub const toStartWith = toStartWithMod.toStartWith;
    pub const toStrictEqual = toStrictEqualMod.toStrictEqual;
    pub const toThrow = toThrowMod.toThrow;
    pub const toThrowErrorMatchingInlineSnapshot = toThrowErrorMatchingInlineSnapshotMod.toThrowErrorMatchingInlineSnapshot;
    pub const toThrowErrorMatchingSnapshot = toThrowErrorMatchingSnapshotMod.toThrowErrorMatchingSnapshot;

    fn getValueAsToThrow(*_, globalThis: *JSGlobalObject, value: JSValue): bun.JSError![JSValue?, JSValue] {
        const vm = globalThis.bunVM();

        let return_value_from_function: JSValue = .zero;

        if (!value.jsType().isFunction()) {
            if (_.flags.promise != .none) {
                return [value, return_value_from_function];
            }

            return globalThis.throw("Expected value must be a function", .{});
        }

        let return_value: JSValue = .zero;

        // Drain existing unhandled rejections
        vm.global.handleRejectedPromises();

        let scope = vm.unhandledRejectionScope();
        const prev_unhandled_pending_rejection_to_capture = vm.unhandled_pending_rejection_to_capture;
        vm.unhandled_pending_rejection_to_capture = &return_value;
        vm.onUnhandledRejection = &VirtualMachine.onQuietUnhandledRejectionHandlerCaptureValue;
        return_value_from_function = value.call(globalThis, .js_undefined, []) catch |e| globalThis.takeException(e);
        vm.unhandled_pending_rejection_to_capture = prev_unhandled_pending_rejection_to_capture;

        vm.global.handleRejectedPromises();

        if (return_value == .zero) {
            return_value = return_value_from_function;
        }

        if (p := return_value.asAnyPromise()) {
            let promise = p;
            vm.waitForPromise(promise);
            scope.apply(vm);
            switch (promise.unwrap(globalThis.vm(), .mark_handled)) {
                .fulfilled => {
                    return [null, return_value_from_function];
                },
                .rejected => |rejected| {
                    // since we know for sure it rejected, we should always return the error
                    return [rejected.toError() ?? rejected, return_value_from_function];
                },
                .pending => unreachable,
            }
        }

        if (return_value != return_value_from_function) {
            if (existing := return_value_from_function.asAnyPromise()) {
                existing.setHandled(globalThis.vm());
            }
        }

        scope.apply(vm);

        return [return_value.toError() ?? return_value_from_function.toError(), return_value_from_function];
    }
    fn fnToErrStringOrUndefined(*_, globalThis: *JSGlobalObject, value: JSValue): !JSValue? {
        const err_pair = _.getValueAsToThrow(globalThis, value)?;
        const err_value = err_pair[0];
        * = err_pair[1];

        if (err_value == null) return null;
        let err_value_res = err_value.?;
        if (err_value_res.isAnyError()) {
            const message: JSValue = (err_value_res.getTruthyComptime(globalThis, "message")?) ?? .js_undefined;
            err_value_res = message;
        } else {
            err_value_res = .js_undefined;
        }
        return err_value_res;
    }
    const TrimResult = _TrimResult;
    static fn trimLeadingWhitespaceForInlineSnapshot(str_in: []const u8, trimmed_buf: []u8): TrimResult {
        std.debug.assert(trimmed_buf.len == str_in.len);
        let src = str_in;
        let dst = trimmed_buf[0..];
        const give_up_1: TrimResult = { trimmed: str_in, start_indent: null, end_indent: null };
        // if the line is all whitespace, trim fully
        // the first line containing a character determines the max trim count

        // read first line (should be all-whitespace)
        const first_newline_opt = std.mem.indexOf(u8, src, "\n");
        if (first_newline_opt == null) return give_up_1;
        const first_newline = first_newline_opt.?;
        for (char of src[0..first_newline]) if (char != ' ' && char != '\t') return give_up_1;
        src = src[first_newline + 1 ..];

        // read first real line and get indent
        let indent_len: usize = src.len;
        let i_idx: usize = 0;
        for (char of src) {
            if (char != ' ' && char != '\t') {
                indent_len = i_idx;
                break;
            }
            i_idx += 1;
        }
        const indent_str = src[0..indent_len];
        const give_up_2: TrimResult = { trimmed: str_in, start_indent: indent_str, end_indent: indent_str };
        if (indent_len == 0) return give_up_2; // no indent to trim; save time
        // we're committed now
        dst[0] = '\n';
        dst = dst[1..];
        src = src[indent_len..];
        const second_newline_opt = std.mem.indexOf(u8, src, "\n");
        if (second_newline_opt == null) return give_up_2;
        const second_newline = second_newline_opt.? + 1;
        @memcpy(dst[0..second_newline], src[0..second_newline]);
        src = src[second_newline..];
        dst = dst[second_newline..];

        while (src.len > 0) {
            // try read indent
            const max_indent_len = @min(src.len, indent_len);
            let line_indent_len: usize = max_indent_len;
            let j_idx: usize = 0;
            for (char of src[0..max_indent_len]) {
                if (char != ' ' && char != '\t') {
                    line_indent_len = j_idx;
                    break;
                }
                j_idx += 1;
            }
            src = src[line_indent_len..];

            if (line_indent_len < max_indent_len) {
                if (src.len == 0) {
                    // perfect; done
                    break;
                }
                if (src[0] == '\n') {
                    // this line has less indentation than the first line, but it's empty so that's okay.
                    dst[0] = '\n';
                    src = src[1..];
                    dst = dst[1..];
                    continue;
                }
                // this line had less indentation than the first line, but wasn't empty. give up.
                return give_up_2;
            } else {
                // this line has the same or more indentation than the first line. copy it.
                let line_newline: usize = 0;
                if (nl := std.mem.indexOf(u8, src, "\n")) {
                    line_newline = nl + 1;
                } else {
                    // this is the last line. if it's not all whitespace, give up
                    for (char of src) {
                        if (char != ' ' && char != '\t') return give_up_2;
                    }
                    break;
                }
                @memcpy(dst[0..line_newline], src[0..line_newline]);
                src = src[line_newline..];
                dst = dst[line_newline..];
            }
        }
        const last_nl_opt = std.mem.lastIndexOfScalar(u8, str_in, '\n');
        if (last_nl_opt == null) return give_up_2;
        const last_nl = last_nl_opt.?;
        const end_indent = last_nl + 1;
        for (c of str_in[end_indent..]) if (c != ' ' && c != '\t') return give_up_2; // we already checked, but the last line is not all whitespace again

        // done
        return { trimmed: trimmed_buf[0 .. trimmed_buf.len - dst.len], start_indent: indent_str, end_indent: str_in[end_indent..] };
    }
    fn inlineSnapshot(
        *_,
        globalThis: *JSGlobalObject,
        callFrame: *CallFrame,
        value: JSValue,
        property_matchers: JSValue?,
        result: ([]const u8)?,
        comptime fn_name: []const u8,
    ): bun.JSError!JSValue {
        // jest counts inline snapshots towards the snapshot counter for some reason
        if (Jest.runner == null) {
            const signature = comptime getSignature(fn_name, "", false);
            return _.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used outside of a test\n", .{});
        }
        const runner = Jest.runner.?;
        * = runner.snapshots.addCount(_, "") catch e => {
            if (e == error.OutOfMemory) return error.OutOfMemory;
        };

        const update = runner.snapshots.update_snapshots;
        let needs_write = false;

        let pretty_value = DeinitGuard<std.Io.Writer.Allocating>{ val: std.Io.Writer.Allocating.init(default_allocator) };
        _.matchAndFmtSnapshot(globalThis, value, property_matchers, &pretty_value.val.writer, fn_name)?;

        let start_indent: ([]const u8)? = null;
        let end_indent: ([]const u8)? = null;
        if (saved_value := result) {
            const buf = FreeGuard<u8>{
                allocator: runner.snapshots.allocator,
                slice: runner.snapshots.allocator.alloc(u8, saved_value.len)?,
            };
            const trim_res = trimLeadingWhitespaceForInlineSnapshot(saved_value, unsafe { @constCast(buf.slice) });

            if (strings.eqlLong(pretty_value.val.written(), trim_res.trimmed, true)) {
                runner.snapshots.passed += 1;
                return JSValue.js_undefined;
            } else if (update) {
                runner.snapshots.passed += 1;
                needs_write = true;
                start_indent = trim_res.start_indent;
                end_indent = trim_res.end_indent;
            } else {
                runner.snapshots.failed += 1;
                const signature = comptime getSignature(fn_name, "<green>expected<r>", false);
                const fmt = signature ++ "\n\n{f}\n";
                const diff_format = DiffFormatter{
                    received_string: pretty_value.val.written(),
                    expected_string: trim_res.trimmed,
                    globalThis: globalThis,
                };

                return globalThis.throwPretty(fmt, .{ diff_format });
            }
        } else {
            needs_write = true;
        }

        if (needs_write) {
            if (bun.ci.isCI()) {
                if (!update) {
                    const signature = comptime getSignature(fn_name, "", false);
                    // Only creating new snapshots can reach here (updating with mismatches errors earlier with diff)
                    return _.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Inline snapshot creation is disabled in CI environments unless --update-snapshots is used.\nTo override, set the environment variable CI=false.\n\nReceived: {s}", .{ pretty_value.val.written() });
                }
            }
            const buntest_strong_opt = _.bunTest();
            if (buntest_strong_opt == null) {
                const signature = comptime getSignature(fn_name, "", false);
                return _.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used outside of a test\n", .{});
            }
            let buntest_strong = DeinitGuard<bun.jsc.Jest.bun_test.BunTestPtr>{ val: buntest_strong_opt.? };
            const buntest = buntest_strong.val.get();

            // 1. find the src loc of the snapshot
            const srcloc = callFrame.getCallerSrcLoc(globalThis);
            let srcloc_str = DerefGuard<bun.String>{ val: srcloc.str };
            const file_id = buntest.file_id;
            const fget = runner.files.get(file_id);

            if (!srcloc_str.val.eqlUTF8(fget.source.path.text)) {
                const signature = comptime getSignature(fn_name, "", false);
                return _.throw(globalThis, signature,
                    "\n\n\n<b>Matcher error<r>: Inline snapshot matchers must be called from the test file:\n" ++
                    "  Expected to be called from file: <green>\"{f}\"<r>\n" ++
                    "  {s} called from file: <red>\"{f}\"<r>\n\n",
                    .{
                        std.zig.fmtString(fget.source.path.text),
                        fn_name,
                        std.zig.fmtString(srcloc_str.val.toUTF8(runner.snapshots.allocator).slice()),
                    },
                );
            }

            // 2. save to write later
            let start_indent_dup: ([]const u8)? = null;
            if (ind := start_indent) {
                start_indent_dup = runner.snapshots.allocator.dupe(u8, ind)?;
            }
            let end_indent_dup: ([]const u8)? = null;
            if (ind := end_indent) {
                end_indent_dup = runner.snapshots.allocator.dupe(u8, ind)?;
            }
            runner.snapshots.addInlineSnapshotToWrite(file_id, {
                line: srcloc.line,
                col: srcloc.column,
                value: pretty_value.val.toOwnedSlice()?,
                has_matchers: property_matchers != null,
                is_added: result == null,
                kind: fn_name,
                start_indent: start_indent_dup,
                end_indent: end_indent_dup,
            })?;
        }

        return JSValue.js_undefined;
    }
    fn matchAndFmtSnapshot(*_, globalThis: *JSGlobalObject, value: JSValue, property_matchers: JSValue?, pretty_value: *std.Io.Writer, comptime fn_name: []const u8): bun.JSError!void {
        if (_prop_matchers := property_matchers) {
            if (!value.isObject()) {
                const signature = comptime getSignature(fn_name, "<green>properties<r><d>, <r>hint", false);
                return _.throw(globalThis, signature, "\n\n<b>Matcher error: <red>received<r> values must be an object when the matcher has <green>properties<r>\n", .{});
            }

            const prop_matchers = _prop_matchers;

            if (!(value.jestDeepMatch(prop_matchers, globalThis, true)?)) {
                // TODO: print diff with properties from propertyMatchers
                const signature = comptime getSignature(fn_name, "<green>propertyMatchers<r>", false);
                const fmt = signature ++ "\n\nExpected <green>propertyMatchers<r> to match properties from received object" ++
                    "\n\nReceived: {f}\n";

                let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis } };
                return globalThis.throwPretty(fmt, .{ value.toFmt(&formatter.val) });
            }
        }

        value.jestSnapshotPrettyFormat(pretty_value, globalThis) catch {
            let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis } };
            return globalThis.throw("Failed to pretty format value: {f}", .{ value.toFmt(&formatter.val) });
        };
    }
    fn snapshot(*_, globalThis: *JSGlobalObject, value: JSValue, property_matchers: JSValue?, hint: []const u8, comptime fn_name: []const u8): bun.JSError!JSValue {
        let pretty_value = DeinitGuard<std.Io.Writer.Allocating>{ val: std.Io.Writer.Allocating.init(default_allocator) };
        _.matchAndFmtSnapshot(globalThis, value, property_matchers, &pretty_value.val.writer, fn_name)?;

        const existing_value = Jest.runner.?.snapshots.getOrPut(_, pretty_value.val.written(), hint) catch |e| {
            const buntest_strong_opt = _.bunTest();
            if (buntest_strong_opt == null) {
                return globalThis.throw("Snapshot matchers cannot be used outside of a test", .{});
            }
            let buntest_strong = DeinitGuard<bun.jsc.Jest.bun_test.BunTestPtr>{ val: buntest_strong_opt.? };
            const buntest = buntest_strong.val.get();
            const test_file_path = Jest.runner.?.files.get(buntest.file_id).source.path.text;
            const runner = Jest.runner.?;
            return switch (e) {
                error.FailedToOpenSnapshotFile => globalThis.throw("Failed to open snapshot file for test file: {s}", .{ test_file_path }),
                error.FailedToMakeSnapshotDirectory => globalThis.throw("Failed to make snapshot directory for test file: {s}", .{ test_file_path }),
                error.FailedToWriteSnapshotFile => globalThis.throw("Failed write to snapshot file: {s}", .{ test_file_path }),
                error.SyntaxError, error.ParseError => globalThis.throw("Failed to parse snapshot file for: {s}", .{ test_file_path }),
                error.SnapshotCreationNotAllowedInCI => ci_blk: {
                    const snapshot_name = runner.snapshots.last_error_snapshot_name;
                    const slot_ptr = unsafe { &runner.snapshots.last_error_snapshot_name };
                    const snapshot_name_guard = OptSliceFreeGuard{
                        allocator: runner.snapshots.allocator,
                        slot: slot_ptr,
                    };
                    if (name := snapshot_name) {
                        break :ci_blk globalThis.throw("Snapshot creation is disabled in CI environments unless --update-snapshots is used\nTo override, set the environment variable CI=false.\n\nSnapshot name: \"{s}\"\nReceived: {s}", .{ name, pretty_value.val.written() });
                    } else {
                        break :ci_blk globalThis.throw("Snapshot creation is disabled in CI environments unless --update-snapshots is used\nTo override, set the environment variable CI=false.\n\nReceived: {s}", .{ pretty_value.val.written() });
                    }
                },
                error.SnapshotInConcurrentGroup => globalThis.throw("Snapshot matchers are not supported in concurrent tests", .{}),
                error.TestNotActive => globalThis.throw("Snapshot matchers are not supported after the test has finished executing", .{}),
                else => blk: {
                    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis } };
                    break :blk globalThis.throw("Failed to snapshot value: {f}", .{ value.toFmt(&formatter.val) });
                },
            };
        };

        if (saved_value := existing_value) {
            if (strings.eqlLong(pretty_value.val.written(), saved_value, true)) {
                Jest.runner.?.snapshots.passed += 1;
                return JSValue.js_undefined;
            }

            Jest.runner.?.snapshots.failed += 1;
            const signature = comptime getSignature(fn_name, "<green>expected<r>", false);
            const fmt = signature ++ "\n\n{f}\n";
            const diff_format = DiffFormatter{
                received_string: pretty_value.val.written(),
                expected_string: saved_value,
                globalThis: globalThis,
            };

            return globalThis.throwPretty(fmt, .{ diff_format });
        }

        return JSValue.js_undefined;
    }

    static fn getStaticNot(globalThis: *JSGlobalObject, _: JSValue, _: JSValue): bun.JSError!JSValue {
        return ExpectStatic.create(globalThis, { not: true });
    }

    static fn getStaticResolvesTo(globalThis: *JSGlobalObject, _: JSValue, _: JSValue): bun.JSError!JSValue {
        return ExpectStatic.create(globalThis, { promise: .resolves });
    }

    static fn getStaticRejectsTo(globalThis: *JSGlobalObject, _: JSValue, _: JSValue): bun.JSError!JSValue {
        return ExpectStatic.create(globalThis, { promise: .rejects });
    }

    static fn any(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return ExpectAny.call(globalThis, callFrame);
    }

    static fn anything(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return ExpectAnything.call(globalThis, callFrame);
    }

    static fn closeTo(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return ExpectCloseTo.call(globalThis, callFrame);
    }

    static fn objectContaining(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return ExpectObjectContaining.call(globalThis, callFrame);
    }

    static fn stringContaining(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return ExpectStringContaining.call(globalThis, callFrame);
    }

    static fn stringMatching(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return ExpectStringMatching.call(globalThis, callFrame);
    }

    static fn arrayContaining(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return ExpectArrayContaining.call(globalThis, callFrame);
    }

    /// Implements `expect.extend({ ... })`
    static fn extend(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const args = callFrame.arguments_old(1).slice();

        if (args.len == 0 || !args[0].isObject()) {
            return globalThis.throwPretty("<d>expect.<r>extend<d>(<r>matchers<d>)<r>\n\nExpected an object containing matchers\n", .{});
        }

        let expect_proto = Expect__getPrototype(globalThis);
        let expect_constructor = Expect.js.getConstructor(globalThis);
        let expect_static_proto = ExpectStatic__getPrototype(globalThis);

        // SAFETY: already checked that args[0] is an object
        const matchers_to_register = args[0].getObject().?;
        {
            const PropIter = jsc.JSPropertyIterator({
                skip_empty_name: false,
                include_value: true,
                own_properties_only: false,
            });
            let iter = DeinitGuard<PropIter>{ val: PropIter.init(globalThis, matchers_to_register)? };

            while (matcher_name_val := iter.val.next()?) {
                const matcher_name = &matcher_name_val;
                const matcher_fn: JSValue = iter.val.value;

                if (!matcher_fn.jsType().isFunction()) {
                    const type_name = if (matcher_fn.isNull()) bun.String.static("null") else bun.String.init(matcher_fn.jsTypeString(globalThis).getZigString(globalThis));
                    return globalThis.throwInvalidArguments("expect.extend: `{f}` is not a valid matcher. Must be a function, is \"{f}\"", { matcher_name, type_name });
                }

                // Mutate the Expect/ExpectStatic prototypes/constructor with new instances of JSCustomExpectMatcherFunction.
                // Even though they point to the same native functions for all matchers,
                // multiple instances are created because each instance will hold the matcher_fn as a property

                const wrapper_fn = Bun__JSWrappingFunction__create(globalThis, matcher_name, jsc.toJSHostFn(Expect.applyCustomMatcher), matcher_fn, true);

                expect_proto.putMayBeIndex(globalThis, matcher_name, wrapper_fn)?;
                expect_constructor.putMayBeIndex(globalThis, matcher_name, wrapper_fn)?;
                expect_static_proto.putMayBeIndex(globalThis, matcher_name, wrapper_fn)?;
            }
        }

        globalThis.bunVM().autoGarbageCollect();

        return JSValue.js_undefined;
    }

    const CustomMatcherParamsFormatter = _CustomMatcherParamsFormatter;

    static fn throwInvalidMatcherError(globalThis: *JSGlobalObject, matcher_name: bun.String, result: JSValue): bun.JSError {
        @branchHint(.cold);

        let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };

        const fmt =
            "Unexpected return from matcher function `{f}`.\n" ++
            "Matcher functions should return an object in the following format:\n" ++
            "  {{message?: string | function, pass: boolean}}\n" ++
            "'{f}' was returned";
        const e = switch (Output.enable_ansi_colors_stderr) {
            inline else => |colors| globalThis.createErrorInstance(Output.prettyFmt(fmt, colors), .{ matcher_name, result.toFmt(&formatter.val) }),
        };
        e.put(globalThis, ZigString.static("name"), bun.String.static("InvalidMatcherError").toJS(globalThis)?);
        return globalThis.throwValue(e);
    }

    /// Execute the custom matcher for the given args (the left value + the args passed to the matcher call).
    /// This function is called both for symmetric and asymmetric matching.
    /// If silent=false, throws an exception in JS if the matcher result didn't result in a pass (or if the matcher result is invalid).
    static fn executeCustomMatcher(globalThis: *JSGlobalObject, matcher_name: bun.String, matcher_fn: JSValue, args: []const JSValue, flags: Expect.Flags, silent: bool): bun.JSError!bool {
        // prepare the this object
        const matcher_context = globalThis.bunVM().allocator.create(ExpectMatcherContext)?;
        matcher_context.flags = flags;
        const matcher_context_jsvalue = matcher_context.toJS(globalThis);
        matcher_context_jsvalue.ensureStillAlive();

        // call the custom matcher implementation
        let result = matcher_fn.call(globalThis, matcher_context_jsvalue, args)?;
        // support for async matcher results
        if (promise_val := result.asAnyPromise()) {
            let promise = promise_val;
            const vm = globalThis.vm();
            promise.setHandled(vm);

            globalThis.bunVM().waitForPromise(promise);

            result = promise.result(vm);
            result.ensureStillAlive();
            assert(result != .zero);
            switch (promise.status()) {
                .pending => unreachable,
                .fulfilled => {},
                .rejected => {
                    // TODO: rewrite this code to use .then() instead of blocking the event loop
                    jsc.VirtualMachine.get().runErrorHandler(result, null);
                    return globalThis.throw("Matcher `{f}` returned a promise that rejected", .{ matcher_name });
                },
            }
        }

        let pass: bool = undefined;
        let message: JSValue = undefined;

        // Parse and validate the custom matcher result, which should conform to: { pass: boolean, message?: () => string }
        const is_valid = valid: {
            if (result.isObject()) {
                if (pass_value := result.get(globalThis, "pass")?) {
                    pass = pass_value.toBoolean();

                    if (message_value := result.fastGet(globalThis, .message)?) {
                        if (!message_value.isString() && !message_value.isCallable()) {
                            break :valid false;
                        }
                        message = message_value;
                    } else {
                        message = .js_undefined;
                    }

                    break :valid true;
                }
            }
            break :valid false;
        };
        if (!is_valid) {
            return throwInvalidMatcherError(globalThis, matcher_name, result);
        }

        if (flags.not) pass = !pass;
        if (pass || silent) return pass;

        // handle failure
        let message_text = DerefGuard<bun.String>{ val: bun.String.dead };
        if (message.isUndefined()) {
            message_text.val = bun.String.static("No message was specified for this matcher.");
        } else if (message.isString()) {
            message_text.val = message.toBunString(globalThis)?;
        } else {
            if (comptime Environment.allow_assert)
                assert(message.isCallable()); // checked above

            const message_result = message.callWithGlobalThis(globalThis, [])?;
            message_text.val = bun.String.fromJS(message_result, globalThis)?;
        }

        const matcher_params = CustomMatcherParamsFormatter{
            colors: Output.enable_ansi_colors_stderr,
            globalThis: globalThis,
            matcher_fn: matcher_fn,
        };
        return throwPrettyMatcherError(globalThis, bun.String.empty, matcher_name, matcher_params, .{}, "{f}", .{ message_text.val });
    }

    /// Function that is run for either `expect.myMatcher()` call or `expect().myMatcher` call,
    /// and we can known which case it is based on if the `callFrame.this()` value is an instance of Expect
    static fn applyCustomMatcher(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!jsc.JSValue {
        defer globalThis.bunVM().autoGarbageCollect();

        // retrieve the user-provided matcher function (matcher_fn)
        const func: JSValue = callFrame.callee();
        let matcher_fn: JSValue = getCustomMatcherFn(func, globalThis) ?? .js_undefined;
        if (!matcher_fn.jsType().isFunction()) {
            return globalThis.throw("Internal consistency error: failed to retrieve the matcher function for a custom matcher!", .{});
        }
        matcher_fn.ensureStillAlive();

        // try to retrieve the Expect instance
        const thisValue: JSValue = callFrame.@"this"();
        const expect_opt = Expect.fromJS(thisValue);
        if (expect_opt == null) {
            // if no Expect instance, assume it is a static call (`expect.myMatcher()`), so create an ExpectCustomAsymmetricMatcher instance
            return ExpectCustomAsymmetricMatcher.create(globalThis, callFrame, matcher_fn);
        }
        const expect: *Expect = expect_opt.?;

        // if we got an Expect instance, then it's a non-static call (`expect().myMatcher`),
        // so now execute the symmetric matching

        // retrieve the matcher name
        const matcher_name = matcher_fn.getName(globalThis)?;

        const matcher_params = CustomMatcherParamsFormatter{
            colors: Output.enable_ansi_colors_stderr,
            globalThis: globalThis,
            matcher_fn: matcher_fn,
        };

        // retrieve the captured expected value
        const value_opt = js.capturedValueGetCached(thisValue);
        if (value_opt == null) {
            return globalThis.throw("Internal consistency error: failed to retrieve the captured value", .{});
        }
        let value = value_opt.?;
        value = processPromise(expect.custom_label, expect.flags, globalThis, value, matcher_name, matcher_params, false)?;
        value.ensureStillAlive();

        expect.incrementExpectCallCounter();

        // prepare the args array
        const args = callFrame.arguments();
        let allocator = std.heap.stackFallback(8 * @sizeOf(JSValue), globalThis.allocator());
        let matcher_args = std.array_list.Managed(JSValue).initCapacity(allocator.get(), args.len + 1)?;
        matcher_args.appendAssumeCapacity(value);
        for (arg of args) matcher_args.appendAssumeCapacity(arg);

        * = executeCustomMatcher(globalThis, matcher_name, matcher_fn, matcher_args.items, expect.flags, false)?;

        return thisValue;
    }

    pub const addSnapshotSerializer = notImplementedStaticFn;

    static fn hasAssertions(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        * = callFrame;
        defer globalThis.bunVM().autoGarbageCollect();

        const buntest_strong_opt = bun.jsc.Jest.bun_test.cloneActiveStrong();
        if (buntest_strong_opt == null) {
            return globalThis.throw("expect.assertions() must be called within a test", .{});
        }
        let buntest_strong = DeinitGuard<bun.jsc.Jest.bun_test.BunTestPtr>{ val: buntest_strong_opt.? };
        const buntest = buntest_strong.val.get();
        const state_data = buntest.getCurrentStateData();
        const execution_opt = state_data.sequence(buntest);
        if (execution_opt == null) return globalThis.throw("expect.assertions() is not supported in the describe phase, in concurrent tests, between tests, or after test execution has completed", .{});
        const execution = execution_opt.?;
        if (execution.expect_assertions != .exact) {
            execution.expect_assertions = .at_least_one;
        }

        return JSValue.js_undefined;
    }

    static fn assertions(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        defer globalThis.bunVM().autoGarbageCollect();

        const arguments_ = callFrame.arguments_old(1);
        const arguments = arguments_.slice();

        if (arguments.len < 1) {
            return globalThis.throwInvalidArguments("expect.assertions() takes 1 argument", .{});
        }

        const expected: JSValue = arguments[0];

        if (!expected.isNumber()) {
            let fmt = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
            return globalThis.throw("Expected value must be a non-negative integer: {f}", .{ expected.toFmt(&fmt) });
        }

        const expected_assertions: f64 = expected.toNumber(globalThis)?;
        if (@round(expected_assertions) != expected_assertions || std.math.isInf(expected_assertions) || std.math.isNan(expected_assertions) || expected_assertions < 0 || expected_assertions > std.math.maxInt(u32)) {
            let fmt = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
            return globalThis.throw("Expected value must be a non-negative integer: {f}", .{ expected.toFmt(&fmt) });
        }

        const unsigned_expected_assertions: u32 = @intFromFloat(expected_assertions);

        const buntest_strong_opt2 = bun.jsc.Jest.bun_test.cloneActiveStrong();
        if (buntest_strong_opt2 == null) {
            return globalThis.throw("expect.assertions() must be called within a test", .{});
        }
        let buntest_strong2 = DeinitGuard<bun.jsc.Jest.bun_test.BunTestPtr>{ val: buntest_strong_opt2.? };
        const buntest2 = buntest_strong2.val.get();
        const state_data2 = buntest2.getCurrentStateData();
        const execution_opt = state_data2.sequence(buntest2);
        if (execution_opt == null) return globalThis.throw("expect.assertions() is not supported in the describe phase, in concurrent tests, between tests, or after test execution has completed", .{});
        const execution = execution_opt.?;
        execution.expect_assertions = { exact: unsigned_expected_assertions };

        return JSValue.js_undefined;
    }

    fn notImplementedJSCFn(_: *Expect, globalThis: *JSGlobalObject, _: *CallFrame): bun.JSError!JSValue {
        return globalThis.throw("Not implemented", .{});
    }

    static fn notImplementedStaticFn(globalThis: *JSGlobalObject, _: *CallFrame): bun.JSError!JSValue {
        return globalThis.throw("Not implemented", .{});
    }

    fn notImplementedJSCProp(_: *Expect, _: JSValue, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return globalThis.throw("Not implemented", .{});
    }

    static fn notImplementedStaticProp(globalThis: *JSGlobalObject, _: JSValue, _: JSValue): bun.JSError!JSValue {
        return globalThis.throw("Not implemented", .{});
    }

    fn postMatch(_: *Expect, globalThis: *JSGlobalObject) {
        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();
    }

    static fn doUnreachable(globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const arg = callframe.arguments_old(1).ptr[0];

        if (arg.isEmptyOrUndefinedOrNull()) {
            const error_value = bun.String.init("reached unreachable code").toErrorInstance(globalThis);
            error_value.put(globalThis, ZigString.static("name"), bun.String.init("UnreachableError").toJS(globalThis)?);
            return globalThis.throwValue(error_value);
        }

        if (arg.isString()) {
            const arg_str = arg.toBunString(globalThis)?;
            const error_value = arg_str.toErrorInstance(globalThis);
            error_value.put(globalThis, ZigString.static("name"), bun.String.init("UnreachableError").toJS(globalThis)?);
            return globalThis.throwValue(error_value);
        }

        return globalThis.throwValue(arg);
    }
}

/// Static instance of expect, holding a set of flags.
/// Returned for example when executing `expect.not`
pub struct ExpectStatic {
    pub const js = jsc.Codegen.JSExpectStatic;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn create(globalThis: *JSGlobalObject, flags: Expect.Flags): bun.JSError!JSValue {
        let expect = globalThis.bunVM().allocator.create(ExpectStatic)?;
        expect.flags = flags;

        const value = expect.toJS(globalThis);
        value.ensureStillAlive();
        return value;
    }

    fn getNot(*_, _: JSValue, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        let flags = _.flags;
        flags.not = !_.flags.not;
        return create(globalThis, flags);
    }

    fn getResolvesTo(*_, _: JSValue, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        let flags = _.flags;
        if (flags.promise != .none) return asyncChainingError(globalThis, flags, "resolvesTo");
        flags.promise = .resolves;
        return create(globalThis, flags);
    }

    fn getRejectsTo(*_, _: JSValue, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        let flags = _.flags;
        if (flags.promise != .none) return asyncChainingError(globalThis, flags, "rejectsTo");
        flags.promise = .rejects;
        return create(globalThis, flags);
    }

    static fn asyncChainingError(globalThis: *JSGlobalObject, flags: Expect.Flags, name: string): bun.JSError {
        @branchHint(.cold);
        const str = switch (flags.promise) {
            .resolves => "resolvesTo",
            .rejects => "rejectsTo",
            else => unreachable,
        };
        return globalThis.throw("expect.{s}: already called expect.{s} on this chain", .{ name, str });
    }

    fn createAsymmetricMatcherWithFlags(*_, comptime T: type, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const instance_jsvalue = T.call(globalThis, callFrame)?;
        if (instance_jsvalue != .zero && !instance_jsvalue.isAnyError()) {
            if (instance := T.fromJS(instance_jsvalue)) {
                instance.flags = _.flags;
            } else {
                return globalThis.throwOutOfMemory();
            }
        }
        return instance_jsvalue;
    }

    fn anything(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.createAsymmetricMatcherWithFlags(ExpectAnything, globalThis, callFrame);
    }

    fn any(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.createAsymmetricMatcherWithFlags(ExpectAny, globalThis, callFrame);
    }

    fn arrayContaining(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.createAsymmetricMatcherWithFlags(ExpectArrayContaining, globalThis, callFrame);
    }

    fn closeTo(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.createAsymmetricMatcherWithFlags(ExpectCloseTo, globalThis, callFrame);
    }

    fn objectContaining(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.createAsymmetricMatcherWithFlags(ExpectObjectContaining, globalThis, callFrame);
    }

    fn stringContaining(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.createAsymmetricMatcherWithFlags(ExpectStringContaining, globalThis, callFrame);
    }

    fn stringMatching(*_, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        return _.createAsymmetricMatcherWithFlags(ExpectStringMatching, globalThis, callFrame);
    }
}

pub struct ExpectAnything {
    pub const js = jsc.Codegen.JSExpectAnything;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, _: *CallFrame): bun.JSError!JSValue {
        const anything = globalThis.bunVM().allocator.create(ExpectAnything)?;
        anything.* = {};

        const anything_js_value = anything.toJS(globalThis);
        anything_js_value.ensureStillAlive();

        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();

        return anything_js_value;
    }
}

pub struct ExpectStringMatching {
    pub const js = jsc.Codegen.JSExpectStringMatching;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const args = callFrame.arguments();

        if (args.len == 0 || (!args[0].isString() && !args[0].isRegExp())) {
            return globalThis.throwPretty("<d>expect.<r>stringContaining<d>(<r>string<d>)<r>\n\nExpected a string or regular expression\n", .{});
        }

        const test_value = args[0];

        const string_matching = globalThis.bunVM().allocator.create(ExpectStringMatching)?;
        string_matching.* = {};

        const string_matching_js_value = string_matching.toJS(globalThis);
        js.testValueSetCached(string_matching_js_value, globalThis, test_value);

        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();
        return string_matching_js_value;
    }
}

pub struct ExpectCloseTo {
    pub const js = jsc.Codegen.JSExpectCloseTo;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const args = callFrame.arguments_old(2).slice();

        if (args.len == 0 || !args[0].isNumber()) {
            return globalThis.throwPretty("<d>expect.<r>closeTo<d>(<r>number<d>, precision?)<r>\n\nExpected a number value", .{});
        }
        const number_value = args[0];

        let precision_value: JSValue = if (args.len > 1) args[1] else .js_undefined;
        if (precision_value.isUndefined()) {
            precision_value = JSValue.jsNumberFromInt32(2); // default value from jest
        }
        if (!precision_value.isNumber()) {
            return globalThis.throwPretty("<d>expect.<r>closeTo<d>(number, <r>precision?<d>)<r>\n\nPrecision must be a number or undefined", .{});
        }

        const instance = globalThis.bunVM().allocator.create(ExpectCloseTo)?;
        instance.* = {};

        const instance_jsvalue = instance.toJS(globalThis);
        number_value.ensureStillAlive();
        precision_value.ensureStillAlive();
        ExpectCloseTo.js.numberValueSetCached(instance_jsvalue, globalThis, number_value);
        ExpectCloseTo.js.digitsValueSetCached(instance_jsvalue, globalThis, precision_value);

        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();
        return instance_jsvalue;
    }
}

pub struct ExpectObjectContaining {
    pub const js = jsc.Codegen.JSExpectObjectContaining;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const args = callFrame.arguments_old(1).slice();

        if (args.len == 0 || !args[0].isObject()) {
            return globalThis.throwPretty("<d>expect.<r>objectContaining<d>(<r>object<d>)<r>\n\nExpected an object\n", .{});
        }

        const object_value = args[0];

        const instance = globalThis.bunVM().allocator.create(ExpectObjectContaining)?;
        instance.* = {};

        const instance_jsvalue = instance.toJS(globalThis);
        ExpectObjectContaining.js.objectValueSetCached(instance_jsvalue, globalThis, object_value);

        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();
        return instance_jsvalue;
    }
}

pub struct ExpectStringContaining {
    pub const js = jsc.Codegen.JSExpectStringContaining;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const args = callFrame.arguments_old(1).slice();

        if (args.len == 0 || !args[0].isString()) {
            return globalThis.throwPretty("<d>expect.<r>stringContaining<d>(<r>string<d>)<r>\n\nExpected a string\n", .{});
        }

        const string_value = args[0];

        const string_containing = globalThis.bunVM().allocator.create(ExpectStringContaining)?;
        string_containing.* = {};

        const string_containing_js_value = string_containing.toJS(globalThis);
        ExpectStringContaining.js.stringValueSetCached(string_containing_js_value, globalThis, string_value);

        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();
        return string_containing_js_value;
    }
}

pub struct ExpectAny {
    pub const js = jsc.Codegen.JSExpectAny;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const _arguments = callFrame.arguments_old(1);
        const arguments: []const JSValue = _arguments.slice();

        if (arguments.len == 0) {
            return globalThis.throw("any() expects to be passed a constructor function. Please pass one or use anything() to match any object.", .{});
        }

        const constructor = arguments[0];
        constructor.ensureStillAlive();
        if (!constructor.isConstructor()) {
            return globalThis.throwPretty("<d>expect.<r>any<d>(<r>constructor<d>)<r>\n\nExpected a constructor\n", .{});
        }

        const asymmetric_matcher_constructor_type = Expect.Flags.AsymmetricMatcherConstructorType.fromJS(globalThis, constructor)?;

        // I don't think this case is possible, but just in case!
        if (globalThis.hasException()) {
            return error.JSError;
        }

        let expect_any = globalThis.bunVM().allocator.create(ExpectAny)?;
        expect_any.* = {
            flags: {
                asymmetric_matcher_constructor_type: asymmetric_matcher_constructor_type,
            },
        };

        const any_js_value = expect_any.toJS(globalThis);
        any_js_value.ensureStillAlive();
        ExpectAny.js.constructorValueSetCached(any_js_value, globalThis, constructor);
        any_js_value.ensureStillAlive();

        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();

        return any_js_value;
    }
}

pub struct ExpectArrayContaining {
    pub const js = jsc.Codegen.JSExpectArrayContaining;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn call(globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const args = callFrame.arguments_old(1).slice();

        if (args.len == 0 || !args[0].jsType().isArray()) {
            return globalThis.throwPretty("<d>expect.<r>arrayContaining<d>(<r>array<d>)<r>\n\nExpected a array\n", .{});
        }

        const array_value = args[0];

        const array_containing = globalThis.bunVM().allocator.create(ExpectArrayContaining)?;
        array_containing.* = {};

        const array_containing_js_value = array_containing.toJS(globalThis);
        ExpectArrayContaining.js.arrayValueSetCached(array_containing_js_value, globalThis, array_value);

        let vm = globalThis.bunVM();
        vm.autoGarbageCollect();
        return array_containing_js_value;
    }
}

/// An instantiated asymmetric custom matcher, returned from calls to `expect.toCustomMatch(...)`
///
/// Reference: `AsymmetricMatcher` in https://github.com/jestjs/jest/blob/main/packages/expect/src/types.ts
/// (but only created for *custom* matchers, as built-ins have their own classes)
pub struct ExpectCustomAsymmetricMatcher {
    pub const js = jsc.Codegen.JSExpectCustomAsymmetricMatcher;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    /// Implements the static call of the custom matcher (`expect.myCustomMatcher(<args>)`),
    /// which creates an asymmetric matcher instance (`ExpectCustomAsymmetricMatcher`).
    /// This will not run the matcher, but just capture the args etc.
    static fn create(globalThis: *JSGlobalObject, callFrame: *CallFrame, matcher_fn: JSValue): bun.JSError!JSValue {
        let flags: Expect.Flags = undefined;

        // try to retrieve the ExpectStatic instance (to get the flags)
        if (expect_static := ExpectStatic.fromJS(callFrame.@"this"())) {
            flags = expect_static.flags;
        } else {
            // if it's not an ExpectStatic instance, assume it was called from the Expect constructor, so use the default flags
            flags = {};
        }

        // create the matcher instance
        const instance = globalThis.bunVM().allocator.create(ExpectCustomAsymmetricMatcher)?;
        instance.* = {};

        const instance_jsvalue = instance.toJS(globalThis);
        instance_jsvalue.ensureStillAlive();

        // store the flags
        instance.flags = flags;

        // store the user-provided matcher function into the instance
        js.matcherFnSetCached(instance_jsvalue, globalThis, matcher_fn);

        // capture the args as a JS array saved in the instance, so the matcher can be executed later on with them
        const args = callFrame.arguments();
        const array = JSValue.createEmptyArray(globalThis, args.len)?;
        let i_arg: usize = 0;
        for (arg of args) {
            array.putIndex(globalThis, @truncate(i_arg), arg)?;
            i_arg += 1;
        }
        js.capturedArgsSetCached(instance_jsvalue, globalThis, array);
        array.ensureStillAlive();

        // return the same instance, now fully initialized including the captured args (previously it was incomplete)
        return instance_jsvalue;
    }

    fn executeImpl(*_, thisValue: JSValue, globalThis: *JSGlobalObject, received: JSValue): bun.JSError!bool {
        // retrieve the user-provided matcher implementation function (the function passed to expect.extend({ ... }))
        const matcher_fn_opt = js.matcherFnGetCached(thisValue);
        if (matcher_fn_opt == null) {
            return globalThis.throw("Internal consistency error: the ExpectCustomAsymmetricMatcher(matcherFn) was garbage collected but it should not have been!", .{});
        }
        const matcher_fn: JSValue = matcher_fn_opt.?;
        matcher_fn.ensureStillAlive();
        if (!matcher_fn.jsType().isFunction()) {
            return globalThis.throw("Internal consistency error: the ExpectCustomMatcher(matcherFn) is not a function!", .{});
        }

        // retrieve the matcher name
        const matcher_name = matcher_fn.getName(globalThis)?;

        // retrieve the asymmetric matcher args
        // if null, it means the function has not yet been called to capture the args, which is a misuse of the matcher
        const captured_args_opt = js.capturedArgsGetCached(thisValue);
        if (captured_args_opt == null) {
            return globalThis.throw("expect.{f} misused, it needs to be instantiated by calling it with 0 or more arguments", .{ matcher_name });
        }
        const captured_args: JSValue = captured_args_opt.?;
        captured_args.ensureStillAlive();

        // prepare the args array as `[received, ...captured_args]`
        const args_count = captured_args.getLength(globalThis)?;
        let allocator = std.heap.stackFallback(8 * @sizeOf(JSValue), globalThis.allocator());
        let matcher_args = std.array_list.Managed(JSValue).initCapacity(allocator.get(), args_count + 1) catch {
            return globalThis.throwOutOfMemory();
        };
        matcher_args.appendAssumeCapacity(received);
        let i: usize = 0;
        while (i < args_count) : (i += 1) {
            matcher_args.appendAssumeCapacity(captured_args.getIndex(globalThis, @truncate(i))?);
        }

        return Expect.executeCustomMatcher(globalThis, matcher_name, matcher_fn, matcher_args.items, _.flags, true);
    }

    /// Function called by c++ function "matchAsymmetricMatcher" to execute the custom matcher against the provided leftValue
    @callconv("c")
    fn execute(*_, thisValue: JSValue, globalThis: *JSGlobalObject, received: JSValue): bool {
        return _.executeImpl(thisValue, globalThis, received) catch false;
    }

    fn asymmetricMatch(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const arguments = callframe.arguments_old(1).slice();
        const received_value = if (arguments.len < 1) .js_undefined else arguments[0];
        const matched = _.executeImpl(callframe.@"this"(), globalThis, received_value)?;
        return JSValue.jsBoolean(matched);
    }

    static fn maybeClear(comptime dontThrow: bool, globalThis: *JSGlobalObject, e: bun.JSError): bun.JSError!bool {
        if (dontThrow) {
            globalThis.clearException();
            return false;
        }
        return e;
    }

    /// Calls a custom implementation (if provided) to stringify this asymmetric matcher, and returns true if it was provided and it succeed
    fn customPrint(*_, thisValue: JSValue, globalThis: *JSGlobalObject, writer: auto, comptime dontThrow: bool): !bool {
        const self_ref = _;
        * = self_ref;
        const matcher_fn_opt = js.matcherFnGetCached(thisValue);
        if (matcher_fn_opt == null) return false;
        const matcher_fn: JSValue = matcher_fn_opt.?;
        if (fn_value := matcher_fn.get(globalThis, "toAsymmetricMatcher") catch |e| return maybeClear(dontThrow, globalThis, e)) {
            if (fn_value.jsType().isFunction()) {
                const captured_args_opt = js.capturedArgsGetCached(thisValue);
                if (captured_args_opt == null) return false;
                const captured_args: JSValue = captured_args_opt.?;
                let stack_fallback = std.heap.stackFallback(256, globalThis.allocator());
                const args_len = captured_args.getLength(globalThis) catch |e| return maybeClear(dontThrow, globalThis, e);
                let args = std.array_list.Managed(JSValue).initCapacity(stack_fallback.get(), args_len)?;
                let iter = captured_args.arrayIterator(globalThis) catch |e| return maybeClear(dontThrow, globalThis, e);
                while (arg := iter.next() catch |e| return maybeClear(dontThrow, globalThis, e)) {
                    args.appendAssumeCapacity(arg);
                }

                const result = matcher_fn.call(globalThis, thisValue, args.items) catch |e| return maybeClear(dontThrow, globalThis, e);
                writer.print("{f}", .{ result.toBunString(globalThis) catch |e| return maybeClear(dontThrow, globalThis, e) })?;
            }
        }
        return false;
    }

    fn toAsymmetricMatcher(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        let stack_fallback = std.heap.stackFallback(512, globalThis.allocator());
        let mutable_string = DeinitGuard<bun.MutableString>{ val: bun.MutableString.init2048(stack_fallback.get())? };

        const printed = _.customPrint(callframe.@"this"(), globalThis, mutable_string.val.writer())?;
        if (printed) {
            return bun.String.init(mutable_string.val.slice()).toJS();
        }
        return ExpectMatcherUtils.printValue(globalThis, _, null);
    }
}

/// Reference: `MatcherContext` in https://github.com/jestjs/jest/blob/main/packages/expect/src/types.ts
pub struct ExpectMatcherContext {
    pub const js = jsc.Codegen.JSExpectMatcherContext;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    flags: Expect.Flags = {},

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    fn getUtils(_: *ExpectMatcherContext, globalThis: *JSGlobalObject): JSValue {
        return ExpectMatcherUtils__getSingleton(globalThis);
    }

    fn getIsNot(*_, _: *JSGlobalObject): JSValue {
        return JSValue.jsBoolean(_.flags.not);
    }

    fn getPromise(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return switch (_.flags.promise) {
            .rejects => bun.String.static("rejects").toJS(globalThis),
            .resolves => bun.String.static("resolves").toJS(globalThis),
            else => bun.String.empty.toJS(globalThis),
        };
    }

    fn getExpand(_: *ExpectMatcherContext, globalThis: *JSGlobalObject): JSValue {
        * = globalThis;
        // TODO: this should return whether running tests in verbose mode or not (jest flag --expand), but bun currently doesn't have this switch
        return .false;
    }

    fn equals(_: *ExpectMatcherContext, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        let arguments = callframe.arguments_old(3);
        if (arguments.len < 2) {
            return globalThis.throw("expect.extends matcher: this.util.equals expects at least 2 arguments", .{});
        }
        const args = arguments.slice();
        return JSValue.jsBoolean(args[0].jestDeepEquals(args[1], globalThis)?);
    }
}

/// Reference: `MatcherUtils` in https://github.com/jestjs/jest/blob/main/packages/expect/src/types.ts
pub struct ExpectMatcherUtils {
    pub const js = jsc.Codegen.JSExpectMatcherUtils;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    @callconv("c")
    static fn createSingleton(globalThis: *JSGlobalObject): JSValue {
        let instance = globalThis.bunVM().allocator.create(ExpectMatcherUtils) catch {
            return globalThis.throwOutOfMemoryValue();
        };
        return instance.toJS(globalThis);
    }

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn printValue(globalThis: *JSGlobalObject, value: JSValue, comptime color_or_null: ([]const u8)?): !JSValue {
        let stack_fallback = std.heap.stackFallback(512, globalThis.allocator());
        let mutable_string = DeinitGuard<bun.MutableString>{ val: bun.MutableString.init2048(stack_fallback.get())? };

        let buffered_writer = bun.MutableString.BufferedWriter{ context: &mutable_string.val };
        let writer = buffered_writer.writer();

        if (color := color_or_null) {
            if (Output.enable_ansi_colors_stderr) {
                writer.writeAll(Output.prettyFmt(color, true))?;
            }
        }

        let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
        writer.print("{f}", .{ value.toFmt(&formatter.val) })?;

        if (color_or_null != null) {
            if (Output.enable_ansi_colors_stderr) {
                writer.writeAll(Output.prettyFmt("<r>", true))?;
            }
        }

        buffered_writer.flush()?;

        return bun.String.createUTF8ForJS(globalThis, mutable_string.val.slice());
    }

    static inline fn printValueCatched(globalThis: *JSGlobalObject, value: JSValue, comptime color_or_null: ([]const u8)?): JSValue {
        return printValue(globalThis, value, color_or_null) catch {
            return globalThis.throwOutOfMemoryValue();
        };
    }

    fn stringify(_: *ExpectMatcherUtils, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const self_ref = _;
        * = self_ref;
        const arguments = callframe.arguments_old(1).slice();
        const value = if (arguments.len < 1) .js_undefined else arguments[0];
        return printValueCatched(globalThis, value, null);
    }

    fn printExpected(_: *ExpectMatcherUtils, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const self_ref = _;
        * = self_ref;
        const arguments = callframe.arguments_old(1).slice();
        const value = if (arguments.len < 1) .js_undefined else arguments[0];
        return printValueCatched(globalThis, value, "<green>");
    }

    fn printReceived(_: *ExpectMatcherUtils, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const self_ref = _;
        * = self_ref;
        const arguments = callframe.arguments_old(1).slice();
        const value = if (arguments.len < 1) .js_undefined else arguments[0];
        return printValueCatched(globalThis, value, "<red>");
    }

    fn matcherHint(_: *ExpectMatcherUtils, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
        const arguments = callframe.arguments_old(4).slice();

        if (arguments.len == 0 || !arguments[0].isString()) {
            return globalThis.throw("matcherHint: the first argument (matcher name) must be a string", .{});
        }
        const matcher_name = DerefGuard<bun.String>{ val: arguments[0].toBunString(globalThis)? };

        const received = if (arguments.len > 1) arguments[1] else bun.String.static("received").toJS(globalThis)?;
        const expected = if (arguments.len > 2) arguments[2] else bun.String.static("expected").toJS(globalThis)?;
        const options = if (arguments.len > 3) arguments[3] else .js_undefined;

        let is_not = false;
        let comment: (*jsc.JSString)? = null; // TODO support
        let promise: (*jsc.JSString)? = null; // TODO support
        let second_argument: (*jsc.JSString)? = null; // TODO support
        // TODO support "chalk" colors (they are actually functions like: (value: string) => string;)
        //let second_argument_color: string? = null;
        //let expected_color: string? = null;
        //let received_color: string? = null;

        if (!options.isUndefinedOrNull()) {
            if (!options.isObject()) {
                return globalThis.throw("matcherHint: options must be an object (or undefined)", .{});
            }
            if (val := options.get(globalThis, "isNot")?) {
                is_not = val.toBoolean();
            }
            if (val := options.get(globalThis, "comment")?) {
                comment = val.toJSString(globalThis)?;
            }
            if (val := options.get(globalThis, "promise")?) {
                promise = val.toJSString(globalThis)?;
            }
            if (val := options.get(globalThis, "secondArgument")?) {
                second_argument = val.toJSString(globalThis)?;
            }
        }

        const diff_formatter = DiffFormatter{
            received: received,
            expected: expected,
            globalThis: globalThis,
            not: is_not,
        };

        if (is_not) {
            const signature = comptime Expect.getSignature("{f}", "<green>expected<r>", true);
            const fmt = signature ++ "\n\n{f}\n";
            return JSValue.printStringPretty(globalThis, 2048, fmt, .{ matcher_name.val, diff_formatter })?;
        } else {
            const signature = comptime Expect.getSignature("{f}", "<green>expected<r>", false);
            const fmt = signature ++ "\n\n{f}\n";
            return JSValue.printStringPretty(globalThis, 2048, fmt, .{ matcher_name.val, diff_formatter })?;
        }
    }
}

pub struct ExpectTypeOf {
    pub const js = jsc.Codegen.JSExpectTypeOf;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    @callconv("c")
    fn finalize(*_) {
        VirtualMachine.get().allocator.destroy(_);
    }

    static fn create(globalThis: *JSGlobalObject): bun.JSError!JSValue {
        let expect = globalThis.bunVM().allocator.create(ExpectTypeOf)?;

        const value = expect.toJS(globalThis);
        value.ensureStillAlive();
        return value;
    }

    fn fnOneArgumentReturnsVoid(_: *ExpectTypeOf, _: *JSGlobalObject, _: *CallFrame): bun.JSError!JSValue {
        return JSValue.js_undefined;
    }
    fn fnOneArgumentReturnsExpectTypeOf(_: *ExpectTypeOf, globalThis: *JSGlobalObject, _: *CallFrame): bun.JSError!JSValue {
        const self_ref = _;
        * = self_ref;
        return create(globalThis);
    }
    fn getReturnsExpectTypeOf(_: *ExpectTypeOf, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        const self_ref = _;
        * = self_ref;
        return create(globalThis);
    }

    static fn constructor(globalThis: *JSGlobalObject, _: *CallFrame): bun.JSError!*ExpectTypeOf {
        return globalThis.throw("expectTypeOf() cannot be called with new", .{});
    }
    static fn call(globalThis: *JSGlobalObject, _: *CallFrame): bun.JSError!JSValue {
        return create(globalThis);
    }
}

pub struct _AllCallsWithArgsFormatter {
    globalThis: *JSGlobalObject,
    calls: JSValue,
    formatter: *jsc.ConsoleObject.Formatter,

    fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        let printed_once = false;

        const calls_count = @as(u32, i32(_.calls.getLength(_.globalThis) catch |e| return bun.deprecated.jsErrorToWriteError(e)));
        if (calls_count == 0) {
            writer.writeAll("(no calls)")?;
            return;
        }

        let i: u32 = 0;
        while (i < calls_count) : (i += 1) {
            if (printed_once) writer.writeAll("\n")?;
            printed_once = true;

            writer.print("           {d: >4}: ", .{ i + 1 })?;
            const call_args = _.calls.getIndex(_.globalThis, i) catch |e| return bun.deprecated.jsErrorToWriteError(e);
            writer.print("{f}", .{ call_args.toFmt(_.formatter) })?;
        }
    }
}

pub enum _ReturnStatus {
    throw,
    @"return",
    incomplete,

    const Map = bun.ComptimeEnumMap(_ReturnStatus);
}

pub struct _AllCallsFormatter {
    globalThis: *JSGlobalObject,
    returns: JSValue,
    formatter: *jsc.ConsoleObject.Formatter,

    fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        let printed_once = false;

        let num_returns: i32 = 0;
        let num_calls: i32 = 0;

        let iter = _.returns.arrayIterator(_.globalThis) catch |e| return bun.deprecated.jsErrorToWriteError(e);
        while (item := iter.next() catch |e| return bun.deprecated.jsErrorToWriteError(e)) {
            if (printed_once) writer.writeAll("\n")?;
            printed_once = true;

            num_calls += 1;
            writer.print("           {d: >2}: ", .{ num_calls })?;

            const value = mock.jestMockReturnObject_value(_.globalThis, item) catch |e| return bun.deprecated.jsErrorToWriteError(e);
            switch (mock.jestMockReturnObject_type(_.globalThis, item) catch |e| return bun.deprecated.jsErrorToWriteError(e)) {
                .@"return" => {
                    writer.print("{f}", .{ value.toFmt(_.formatter) })?;
                    num_returns += 1;
                },
                .throw => {
                    writer.print("function call threw an error: {f}", .{ value.toFmt(_.formatter) })?;
                },
                .incomplete => {
                    writer.print("<incomplete call>", .{})?;
                },
            }
        }
    }
}

pub struct _SuccessfulReturnsFormatter {
    globalThis: *JSGlobalObject,
    successful_returns: *const std.array_list.Managed(JSValue),
    formatter: *jsc.ConsoleObject.Formatter,

    fn format(writer: *std.Io.Writer)! {
        const len = _.successful_returns.items.len;
        if (len == 0) return;

        let printed_once = false;

        let i_val: usize = 1;
        for (val of _.successful_returns.items) {
            if (printed_once) writer.writeAll("\n")?;
            printed_once = true;

            writer.print("           {d: >4}: ", .{ i_val })?;
            writer.print("{f}", .{ val.toFmt(_.formatter) })?;
            i_val += 1;
        }
    }
}

pub struct mock {
    static fn jestMockIterator(globalThis: *JSGlobalObject, value: bun.jsc.JSValue): bun.JSError!bun.jsc.JSArrayIterator {
        const returns: bun.jsc.JSValue = bun.cpp.JSMockFunction__getReturns(globalThis, value)?;
        if (!returns.jsType().isArray()) {
            let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
            return globalThis.throw("Expected value must be a mock function: {f}", .{ value.toFmt(&formatter.val) });
        }

        return returns.arrayIterator(globalThis)?;
    }
    static fn jestMockReturnObject_type(globalThis: *JSGlobalObject, value: bun.jsc.JSValue): bun.JSError!ReturnStatus {
        if (type_string := value.fastGet(globalThis, .@"type")?) {
            if (type_string.isString()) {
                if (val := ReturnStatus.Map.fromJS(globalThis, type_string)?) return val;
            }
        }
        let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
        return globalThis.throw("Expected value must be a mock function with returns: {f}", .{ value.toFmt(&formatter.val) });
    }
    static fn jestMockReturnObject_value(globalThis: *JSGlobalObject, value: bun.jsc.JSValue): bun.JSError!JSValue {
        return (value.get(globalThis, "value")?) ?? .js_undefined;
    }

    pub const AllCallsWithArgsFormatter = _AllCallsWithArgsFormatter;
    pub const ReturnStatus = _ReturnStatus;
    pub const AllCallsFormatter = _AllCallsFormatter;
    pub const SuccessfulReturnsFormatter = _SuccessfulReturnsFormatter;
}

// Extract the matcher_fn from a JSCustomExpectMatcherFunction instance
inline fn getCustomMatcherFn(thisValue: JSValue, globalThis: *JSGlobalObject): JSValue? {
    const matcher_fn = Bun__JSWrappingFunction__getWrappedFunction(thisValue, globalThis);
    return if (matcher_fn == .zero) null else matcher_fn;
}

/// JSValue.zero is used to indicate it was not a JSMockFunction
/// If there were no calls, it returns an empty JSArray*
extern fn JSMockFunction__getReturns(val: JSValue): JSValue;

extern fn Bun__JSWrappingFunction__create(globalThis: *JSGlobalObject, symbolName: *const bun.String, functionPointer: *const jsc.JSHostFn, wrappedFn: JSValue, strong: bool): JSValue ;
extern fn Bun__JSWrappingFunction__getWrappedFunction(client: JSValue, globalThis: *JSGlobalObject): JSValue ;

extern fn ExpectMatcherUtils__getSingleton(globalThis: *JSGlobalObject): JSValue ;

extern fn Expect__getPrototype(globalThis: *JSGlobalObject): JSValue ;
extern fn ExpectStatic__getPrototype(globalThis: *JSGlobalObject): JSValue ;

comptime {
    @export(&ExpectMatcherUtils.createSingleton, { name: "ExpectMatcherUtils_createSigleton" });
    @export(&Expect.readFlagsAndProcessPromise, { name: "Expect_readFlagsAndProcessPromise" });
    @export(&ExpectCustomAsymmetricMatcher.execute, { name: "ExpectCustomAsymmetricMatcher__execute" });
}

fn testTrimLeadingWhitespaceForSnapshot(src: []const u8, expected: []const u8)! {
    const cpy = FreeGuard<u8>{
        allocator: std.testing.allocator,
        slice: std.testing.allocator.alloc(u8, src.len)?,
    };

    const res = Expect.trimLeadingWhitespaceForInlineSnapshot(src, cpy.slice);
    sanityCheck(src, res);

    std.testing.expectEqualStrings(expected, res.trimmed)?;
}
fn sanityCheck(input: []const u8, res: Expect.TrimResult) {
    // sanity check: output has same number of lines & all input lines endWith output lines
    let input_iter = std.mem.splitScalar(u8, input, '\n');
    let output_iter = std.mem.splitScalar(u8, res.trimmed, '\n');
    while (true) {
        const next_input = input_iter.next();
        const next_output = output_iter.next();
        if (next_input == null) {
            std.debug.assert(next_output == null);
            break;
        }
        std.debug.assert(next_output != null);
        std.debug.assert(std.mem.endsWith(u8, next_input.?, next_output.?));
    }
}
fn testOne(input: []const u8): anyerror!void {
    const cpy = FreeGuard<u8>{
        allocator: std.testing.allocator,
        slice: std.testing.allocator.alloc(u8, input.len)?,
    };
    const res = Expect.trimLeadingWhitespaceForInlineSnapshot(input, cpy.slice);
    sanityCheck(input, res);
}

fn testTrimLeadingWhitespaceForInlineSnapshot(): ! {
    testTrimLeadingWhitespaceForSnapshot(
        "\nHello, world!\n",
        "\nHello, world!\n",
    )?;
    testTrimLeadingWhitespaceForSnapshot(
        "\n  Hello, world!\n",
        "\nHello, world!\n",
    )?;
    testTrimLeadingWhitespaceForSnapshot(
        "\n  Object{\n    key: value\n  }\n",
        "\nObject{\n  key: value\n}\n",
    )?;
    testTrimLeadingWhitespaceForSnapshot(
        "\n  Object{\n  key: value\n\n  }\n",
        "\nObject{\nkey: value\n\n}\n",
    )?;
    testTrimLeadingWhitespaceForSnapshot(
        "\n    Object{\n  key: value\n  }\n",
        "\n    Object{\n  key: value\n  }\n",
    )?;
    testTrimLeadingWhitespaceForSnapshot(
        "\n  \"æ™\n\n  !!!!*5897yhduN\"'\\`Il\"\n",
        "\n\"æ™\n\n!!!!*5897yhduN\"'\\`Il\"\n",
    )?;
}

fn testFuzzExpectTrimLeadingWhitespaceForInlineSnapshot(): ! {
    std.testing.@"fuzz"(testOne, {})?;
}

