import bun from "bun";
import napi from "../napi/napi.zig";
import ErrorCode from "ErrorCode";

const jsc = bun.jsc;
const JSError = bun.JSError;
const MutableString = bun.MutableString;
const Output = bun.Output;
const String = bun.String;
const strings = bun.strings;

const CommonStrings = jsc.CommonStrings;
const ErrorableString = jsc.ErrorableString;
const JSValue = jsc.JSValue;
const VM = jsc.VM;
const ZigString = jsc.ZigString;

type str_slice = []const u8;
type string = []const u8;

fn QueueMicrotaskWrapper(comptime ContextType: type, comptime Function: auto): type {
    return struct {
        @callconv("c")
        static fn call(p: *!) {
            Function(bun.cast(ContextType, p));
        }
    };
}

pub struct DerefGuard<T> {
    ptr: *const T!,
    active: bool = true,
    ~init() {
        if (_.active) unsafe { _.ptr.deref(); }
    }
}

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct EndGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.end(); }
    }
}

pub struct DetachGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.detach(); }
    }
}

pub struct _GregorianDateTime {
    year: i32,
    month: i32,
    day: i32,
    hour: i32,
    minute: i32,
    second: i32,
    weekday: i32,
}

pub enum _BunPluginTarget: u8 {
    bun = 0,
    node = 1,
    browser = 2,
}

pub type MicrotaskCallback = *const fn (ctx: *!) callconv(.c);

pub struct _SysErrOptions {
    code: jsc.Node.ErrorCode,
    errno: i32? = null,
    name: str_slice? = null,
}

pub enum _ThreadKind {
    main,
    other,
}

pub struct _IntegerRange {
    min: comptime_int = jsc.MIN_SAFE_INTEGER,
    max: comptime_int = jsc.MAX_SAFE_INTEGER,
    field_name: []const u8 = "",
    always_allow_zero: bool = false,
}

pub struct ValidateObjectOptions {
    allowArray: bool = false,
    allowFunction: bool = false,
    nullable: bool = false,
}

@c {
    fn JSGlobalObject__throwStackOverflow(global: *JSGlobalObject);
    fn JSGlobalObject__throwOutOfMemoryError(global: *JSGlobalObject);
    fn JSGlobalObject__createOutOfMemoryError(global: *JSGlobalObject): JSValue;
    fn Bun__ErrorCode__determineSpecificType(global: *JSGlobalObject, val: JSValue): String;
    fn Bun__ErrorCode__throwInvalidArgumentTypeValue(global: *JSGlobalObject, arg_name_ptr: [*]const u8, arg_name_len: usize, type_name_ptr: [*]const u8, type_name_len: usize, val: JSValue);
    fn Bun__runOnLoadPlugins(global: *JSGlobalObject, maybe_path: (*const bun.String)?, path: *const bun.String, target: _BunPluginTarget): JSValue;
    fn Bun__runOnResolvePlugins(global: *JSGlobalObject, maybe_path: (*const bun.String)?, path: *const bun.String, specifier: *const String, target: _BunPluginTarget): JSValue;
    fn JSC__JSGlobalObject__queueMicrotaskCallback(global: *JSGlobalObject, ctx: *!, func: MicrotaskCallback);
    fn Bun__Process__emitWarning(global: *JSGlobalObject, warning: JSValue, warning_type: JSValue, code: JSValue, ctor: JSValue);
    fn JSC__JSGlobalObject__queueMicrotaskJob(global: *JSGlobalObject, job: JSValue, arg1: JSValue, arg2: JSValue);
    fn JSC__JSGlobalObject__createAggregateError(global: *JSGlobalObject, errors: [*]const JSValue, errors_len: usize, message: *const ZigString): JSValue;
    fn JSC__JSGlobalObject__createAggregateErrorWithArray(global: *JSGlobalObject, array: JSValue, message: bun.String, options: JSValue): JSValue;
    fn JSC__JSGlobalObject__generateHeapSnapshot(global: *JSGlobalObject): JSValue;
    fn JSC__JSGlobalObject__handleRejectedPromises(global: *JSGlobalObject);
    fn ZigGlobalObject__readableStreamToArrayBuffer(global: *JSGlobalObject, stream: JSValue): JSValue;
    fn ZigGlobalObject__readableStreamToBytes(global: *JSGlobalObject, stream: JSValue): JSValue;
    fn ZigGlobalObject__readableStreamToText(global: *JSGlobalObject, stream: JSValue): JSValue;
    fn ZigGlobalObject__readableStreamToJSON(global: *JSGlobalObject, stream: JSValue): JSValue;
    fn ZigGlobalObject__readableStreamToFormData(global: *JSGlobalObject, stream: JSValue, boundary: JSValue): JSValue;
    fn ZigGlobalObject__readableStreamToBlob(global: *JSGlobalObject, stream: JSValue): JSValue;
    fn ZigGlobalObject__makeNapiEnvForFFI(global: *JSGlobalObject): *napi.NapiEnv;
    fn JSC__JSGlobalObject__bunVM(global: *JSGlobalObject): *VM;
    fn JSC__JSGlobalObject__vm(global: *JSGlobalObject): *VM;
    fn JSC__JSGlobalObject__deleteModuleRegistryEntry(global: *JSGlobalObject, key: *const ZigString);
    fn JSGlobalObject__clearException(global: *JSGlobalObject);
    fn JSGlobalObject__clearExceptionExceptTermination(global: *JSGlobalObject): bool;
    fn JSGlobalObject__clearTerminationException(global: *JSGlobalObject);
    fn JSGlobalObject__hasException(global: *JSGlobalObject): bool;
    fn JSGlobalObject__setTimeZone(global: *JSGlobalObject, timeZone: *const ZigString): bool;
    fn JSGlobalObject__tryTakeException(global: *JSGlobalObject): JSValue;
    fn JSGlobalObject__requestTermination(global: *JSGlobalObject);
    fn Zig__GlobalObject__create(ctx: *!, a: i32, b: bool, c: bool, d: *!?): *JSGlobalObject;
    fn Zig__GlobalObject__createForTestIsolation(old_global: *JSGlobalObject, console: *!): *JSGlobalObject;
    fn Zig__GlobalObject__getModuleRegistryMap(global: *JSGlobalObject): *!;
    fn Zig__GlobalObject__resetModuleRegistryMap(global: *JSGlobalObject, map: *!): bool;
    fn JSC__Wasm__StreamingCompiler__addBytes(streaming_compiler: *!, bytes_ptr: [*]const u8, bytes_len: usize);
    fn ScriptExecutionContextIdentifier__forGlobalObject(global: *JSGlobalObject): u32;
}

struct EmptyFmtArgs {}

inline fn NormArgsType(comptime T: type): type {
    if (T == void) return EmptyFmtArgs;
    const info = @typeInfo(T);
    if (info == .pointer) return info.pointer.child;
    return T;
}

inline fn argsIsEmpty(comptime T: type): bool {
    if (T == void) return true;
    const info = @typeInfo(T);
    if (info == .pointer) return argsIsEmpty(info.pointer.child);
    if (info == .@"struct") return info.@"struct".fields.len == 0;
    return false;
}

@opaque pub struct JSGlobalObject {
    pub const GregorianDateTime = _GregorianDateTime;
    pub const BunPluginTarget = _BunPluginTarget;
    pub const SysErrOptions = _SysErrOptions;
    pub const ThreadKind = _ThreadKind;
    pub const IntegerRange = _IntegerRange;

    pub fn allocator(*_): std.mem.Allocator {
        return _.bunVM().allocator;
    }

    pub fn throwStackOverflow(*_): bun.JSError {
        JSGlobalObject__throwStackOverflow(_);
        return error.JSError;
    }

    pub noinline fn throwOutOfMemory(*_): bun.JSError {
        @branchHint(.cold);
        JSGlobalObject__throwOutOfMemoryError(_);
        return error.JSError;
    }

    pub fn createOutOfMemoryError(*_): JSValue {
        return JSGlobalObject__createOutOfMemoryError(_);
    }

    pub noinline fn throwOutOfMemoryValue(*_): JSValue {
        @branchHint(.cold);
        JSGlobalObject__throwOutOfMemoryError(_);
        return .zero;
    }

    pub fn gregorianDateTimeToMS(*_, year: i32, month: i32, day: i32, hour: i32, minute: i32, second: i32, millisecond: i32): bun.JSError!f64 {
        jsc.markBinding(@src());
        return bun.cpp.Bun__gregorianDateTimeToMS(_, year, month, day, hour, minute, second, millisecond, true);
    }

    pub fn gregorianDateTimeToMSUTC(*_, year: i32, month: i32, day: i32, hour: i32, minute: i32, second: i32, millisecond: i32): bun.JSError!f64 {
        jsc.markBinding(@src());
        return bun.cpp.Bun__gregorianDateTimeToMS(_, year, month, day, hour, minute, second, millisecond, false);
    }

    pub fn msToGregorianDateTimeUTC(*_, ms: f64): GregorianDateTime {
        jsc.markBinding(@src());
        let dt: GregorianDateTime = undefined;
        bun.cpp.Bun__msToGregorianDateTime(_, ms, false, &dt.year, &dt.month, &dt.day, &dt.hour, &dt.minute, &dt.second, &dt.weekday);
        return dt;
    }

    pub fn throwTODO(*_, msg: []const u8): bun.JSError {
        const e = _.createErrorInstance("{s}", .{ msg });
        if (e == .zero) {
            bun.assert(_.hasException());
            return error.JSError;
        }
        e.put(_, ZigString.static("name"), (bun.String.static("TODOError").toJS(_)) catch return error.JSError);
        return _.throwValue(e);
    }

    pub const requestTermination = JSGlobalObject__requestTermination;
    pub const clearTerminationException = JSGlobalObject__clearTerminationException;

    pub fn setTimeZone(*_, timeZone: *const ZigString): bool {
        return JSGlobalObject__setTimeZone(_, timeZone);
    }

    pub inline fn toJSValue(*_): JSValue {
        return unsafe { @enumFromInt(@intFromPtr(_)) };
    }

    pub fn throwInvalidArguments(*_, comptime fmt: [:0]const u8, args: auto): bun.JSError {
        const e = _.toInvalidArguments(fmt, args);
        return _.throwValue(e);
    }

    pub inline fn throwMissingArgumentsValue(*_, comptime arg_names: []const []const u8): bun.JSError {
        return switch (arg_names.len) {
            0 => @compileError("requires at least one argument"),
            1 => _.ERR(.MISSING_ARGS, "The \"{s}\" argument must be specified", .{ arg_names[0] }).throw(),
            2 => _.ERR(.MISSING_ARGS, "The \"{s}\" and \"{s}\" arguments must be specified", .{ arg_names[0], arg_names[1] }).throw(),
            3 => _.ERR(.MISSING_ARGS, "The \"{s}\", \"{s}\", and \"{s}\" arguments must be specified", .{ arg_names[0], arg_names[1], arg_names[2] }).throw(),
            else => @compileError("implement this message"),
        };
    }

    /// "Expected {field} to be a {typename} for '{name}'."
    pub fn createInvalidArgumentType(
        *_,
        comptime name_: []const u8,
        comptime field: []const u8,
        comptime typename: []const u8,
    ): jsc.JSValue {
        return _.ERR(.INVALID_ARG_TYPE, comptime std.fmt.comptimePrint("Expected {s} to be a {s} for '{s}'.", .{ field, typename, name_ }), .{}).toJS();
    }

    pub fn toJS(*_, value: auto): bun.JSError!jsc.JSValue {
        return jsc.JSValue.fromAny(_, @TypeOf(value), value);
    }

    /// "Expected {field} to be a {typename} for '{name}'."
    pub fn throwInvalidArgumentType(
        *_,
        comptime name_: []const u8,
        comptime field: []const u8,
        comptime typename: []const u8,
    ): bun.JSError {
        return _.throwValue(_.createInvalidArgumentType(name_, field, typename));
    }

    /// "The {argname} argument is invalid. Received {value}"
    pub fn throwInvalidArgumentValue(
        *_,
        argname: []const u8,
        value: JSValue,
    ): bun.JSError {
        const actual_string_value = determineSpecificType(_, value)?;
        const _guard_actual_string_value = DerefGuard<String>{ ptr: unsafe { &actual_string_value } };
        return _.ERR(.INVALID_ARG_VALUE, "The \"{s}\" argument is invalid. Received {f}", .{ argname, actual_string_value }).throw();
    }

    pub fn throwInvalidArgumentValueCustom(
        *_,
        argname: []const u8,
        value: JSValue,
        message: []const u8,
    ): bun.JSError {
        const actual_string_value = determineSpecificType(_, value)?;
        const _guard_actual_string_value = DerefGuard<String>{ ptr: unsafe { &actual_string_value } };
        return _.ERR(.INVALID_ARG_VALUE, "The \"{s}\" argument {s}. Received {f}", .{ argname, message, actual_string_value }).throw();
    }

    /// Throw an `ERR_INVALID_ARG_VALUE` when the invalid value is a property of an object.
    /// Message depends on whether `expected` is present.
    /// - "The property "{argname}" is invalid. Received {value}"
    /// - "The property "{argname}" is invalid. Expected {expected}, received {value}"
    pub fn throwInvalidArgumentPropertyValue(
        *_,
        argname: []const u8,
        comptime expected: ([]const u8)?,
        value: JSValue,
    ): bun.JSError {
        const actual_string_value = determineSpecificType(_, value)?;
        const _guard_actual_string_value = DerefGuard<String>{ ptr: unsafe { &actual_string_value } };
        if (_expected := comptime expected) {
            return _.ERR(.INVALID_ARG_VALUE, "The property \"{s}\" is invalid. Expected {s}, received {f}", .{ argname, _expected, actual_string_value }).throw();
        } else {
            return _.ERR(.INVALID_ARG_VALUE, "The property \"{s}\" is invalid. Received {f}", .{ argname, actual_string_value }).throw();
        }
    }

    pub static fn determineSpecificType(global: *JSGlobalObject, value: JSValue): JSError!String {
        const str = Bun__ErrorCode__determineSpecificType(global, value);
        let _guard_str = DerefGuard<String>{ ptr: unsafe { &str } };
        if (global.hasException()) {
            return error.JSError;
        }
        _guard_str.active = false;
        return str;
    }

    pub fn throwIncompatibleOptionPair(
        *_,
        opt1: []const u8,
        opt2: []const u8,
    ): JSError {
        return _.ERR(.INCOMPATIBLE_OPTION_PAIR, "Option \"{s}\" cannot be used in combination with option \"{s}\"", .{ opt1, opt2 }).throw();
    }

    pub fn throwInvalidScryptParams(
        *_,
    ): JSError {
        const e = bun.BoringSSL.c.ERR_peek_last_error();
        if (e != 0) {
            let buf: [256]u8 = undefined;
            const msg = bun.BoringSSL.c.ERR_error_string_n(e, &buf, buf.len);
            return _.ERR(.CRYPTO_INVALID_SCRYPT_PARAMS, "Invalid scrypt params: {s}", .{ msg }).throw();
        }

        return _.ERR(.CRYPTO_INVALID_SCRYPT_PARAMS, "Invalid scrypt params", .{}).throw();
    }

    /// "The {argname} argument must be of type {typename}. Received {value}"
    pub fn throwInvalidArgumentTypeValue(
        *_,
        argname: []const u8,
        typename: []const u8,
        value: JSValue,
    ): bun.JSError {
        Bun__ErrorCode__throwInvalidArgumentTypeValue(_, argname.ptr, argname.len, typename.ptr, typename.len, value);
        return error.JSError;
    }

    pub fn throwInvalidArgumentTypeValue2(
        *_,
        argname: []const u8,
        typename: []const u8,
        value: JSValue,
    ): JSError {
        const actual_string_value = determineSpecificType(_, value)?;
        const _guard_actual_string_value = DerefGuard<String>{ ptr: unsafe { &actual_string_value } };
        return _.ERR(.INVALID_ARG_TYPE, "The \"{s}\" argument must be {s}. Received {f}", .{ argname, typename, actual_string_value }).throw();
    }

    /// "The <argname> argument must be one of type <typename>. Received <value>"
    pub fn throwInvalidArgumentTypeValueOneOf(
        *_,
        argname: []const u8,
        typename: []const u8,
        value: JSValue,
    ): bun.JSError {
        const actual_string_value = determineSpecificType(_, value)?;
        const _guard_actual_string_value = DerefGuard<String>{ ptr: unsafe { &actual_string_value } };
        return _.ERR(.INVALID_ARG_TYPE, "The \"{s}\" argument must be one of type {s}. Received {f}", .{ argname, typename, actual_string_value }).throw();
    }

    pub fn throwInvalidArgumentRangeValue(
        *_,
        argname: []const u8,
        typename: []const u8,
        value: i64,
    ): bun.JSError {
        return _.ERR(.OUT_OF_RANGE, "The \"{s}\" is out of range. {s}. Received {f}", .{ argname, typename, value }).throw();
    }

    pub fn throwInvalidPropertyTypeValue(
        *_,
        field: []const u8,
        typename: []const u8,
        value: JSValue,
    ): bun.JSError {
        let ty_str = value.jsTypeString(_).toSlice(_, bun.default_allocator);
        const _guard_ty_str = DeinitGuard<@TypeOf(ty_str)>{ ptr: unsafe { &ty_str } };
        return _.ERR(.INVALID_ARG_TYPE, "The \"{s}\" property must be of type {s}. Received {s}", .{ field, typename, ty_str.slice() }).throw();
    }

    pub fn createNotEnoughArguments(
        *_,
        comptime name_: []const u8,
        comptime expected: usize,
        got: usize,
    ): jsc.JSValue {
        return _.toTypeError(.MISSING_ARGS, "Not enough arguments to '" ++ name_ ++ "'. Expected {d}, got {d}.", .{ expected, got });
    }

    /// Not enough arguments passed to function named `name_`
    pub fn throwNotEnoughArguments(
        *_,
        comptime name_: []const u8,
        comptime expected: usize,
        got: usize,
    ): bun.JSError {
        return _.throwValue(_.createNotEnoughArguments(name_, expected, got));
    }

    pub fn reload(*_)! {
        _.vm().drainMicrotasks();
        _.vm().collectAsync();
        bun.cpp.JSC__JSGlobalObject__reload(_)?;
    }

    pub fn runOnLoadPlugins(*_, namespace_: bun.String, path: bun.String, target: BunPluginTarget): (JSValue?)!bun.JSError {
        jsc.markBinding(@src());
        const result = bun.jsc.fromJSHostCall(_, @src(), Bun__runOnLoadPlugins, .{ _, if (namespace_.length() > 0) &namespace_ else null, &path, target })?;
        if (result.isUndefinedOrNull()) return null;
        return result;
    }

    pub fn runOnResolvePlugins(*_, namespace_: bun.String, path: bun.String, source: bun.String, target: BunPluginTarget): (JSValue?)!bun.JSError {
        jsc.markBinding(@src());
        const result = bun.jsc.fromJSHostCall(_, @src(), Bun__runOnResolvePlugins, .{ _, if (namespace_.length() > 0) &namespace_ else null, &path, &source, target })?;
        if (result.isUndefinedOrNull()) return null;
        return result;
    }

    pub fn createErrorInstance(*_, comptime fmt: [:0]const u8, args: auto): JSValue {
        if (comptime (@TypeOf(args) != void && (if (@typeInfo(@TypeOf(args)) == .pointer) std.@"meta".fieldNames(std.@"meta".Child(@TypeOf(args))).len > 0 else std.@"meta".fieldNames(@TypeOf(args)).len > 0))) {
            let stack_fallback = std.heap.stackFallback(1024 * 4, _.allocator());
            let buf = std.Io.Writer.Allocating.initCapacity(stack_fallback.get(), 2048) catch unreachable;
            const _guard_buf = DeinitGuard<std.Io.Writer.Allocating>{ ptr: unsafe { &buf } };
            let writer = &buf.writer;
            writer.print(fmt, args) catch {
                // if an exception occurs in the middle of formatting the error message, it's better to just return the formatting string than an error about an error.
                // Clear any pending JS exception (e.g. from Symbol.toPrimitive) so that throwValue doesn't hit assertNoException.
                * = _.clearExceptionExceptTermination();
                return ZigString.static(fmt).toErrorInstance(_);
            };

            // Ensure we clone it.
            let str = ZigString.initUTF8(buf.written());

            return str.toErrorInstance(_);
        } else {
            if (comptime strings.isAllASCII(fmt)) {
                return String.static(fmt).toErrorInstance(_);
            } else {
                return ZigString.initUTF8(fmt).toErrorInstance(_);
            }
        }
    }

    pub fn createTypeErrorInstance(*_, comptime fmt: [:0]const u8, args: auto): JSValue {
        if (comptime (@TypeOf(args) != void && (if (@typeInfo(@TypeOf(args)) == .pointer) std.@"meta".fieldNames(std.@"meta".Child(@TypeOf(args))).len > 0 else std.@"meta".fieldNames(@TypeOf(args)).len > 0))) {
            let stack_fallback = std.heap.stackFallback(1024 * 4, _.allocator());
            let buf = bun.MutableString.init2048(stack_fallback.get()) catch unreachable;
            const _guard_buf = DeinitGuard<bun.MutableString>{ ptr: unsafe { &buf } };
            let writer = buf.writer();
            writer.print(fmt, args) catch {
                * = _.clearExceptionExceptTermination();
                return ZigString.static(fmt).toTypeErrorInstance(_);
            };
            let str = ZigString.fromUTF8(buf.slice());
            return str.toTypeErrorInstance(_);
        } else {
            return ZigString.static(fmt).toTypeErrorInstance(_);
        }
    }

    pub fn createDOMExceptionInstance(*_, code: jsc.WebCore.DOMExceptionCode, comptime fmt: [:0]const u8, args: auto): JSError!JSValue {
        if (comptime (@TypeOf(args) != void && (if (@typeInfo(@TypeOf(args)) == .pointer) std.@"meta".fieldNames(std.@"meta".Child(@TypeOf(args))).len > 0 else std.@"meta".fieldNames(@TypeOf(args)).len > 0))) {
            let stack_fallback = std.heap.stackFallback(1024 * 4, _.allocator());
            let buf = bun.MutableString.init2048(stack_fallback.get())?;
            const _guard_buf = DeinitGuard<bun.MutableString>{ ptr: unsafe { &buf } };
            let writer = buf.writer();
            writer.print(fmt, args)?;
            let str = ZigString.fromUTF8(buf.slice());
            return str.toDOMExceptionInstance(_, code);
        } else {
            return ZigString.static(fmt).toDOMExceptionInstance(_, code);
        }
    }

    pub fn createSyntaxErrorInstance(*_, comptime fmt: [:0]const u8, args: auto): JSValue {
        if (comptime (@TypeOf(args) != void && (if (@typeInfo(@TypeOf(args)) == .pointer) std.@"meta".fieldNames(std.@"meta".Child(@TypeOf(args))).len > 0 else std.@"meta".fieldNames(@TypeOf(args)).len > 0))) {
            let stack_fallback = std.heap.stackFallback(1024 * 4, _.allocator());
            let buf = bun.MutableString.init2048(stack_fallback.get()) catch unreachable;
            const _guard_buf = DeinitGuard<bun.MutableString>{ ptr: unsafe { &buf } };
            let writer = buf.writer();
            writer.print(fmt, args) catch {
                * = _.clearExceptionExceptTermination();
                return ZigString.static(fmt).toSyntaxErrorInstance(_);
            };
            let str = ZigString.fromUTF8(buf.slice());
            return str.toSyntaxErrorInstance(_);
        } else {
            return ZigString.static(fmt).toSyntaxErrorInstance(_);
        }
    }

    pub fn createRangeErrorInstance(*_, comptime fmt: [:0]const u8, args: auto): JSValue {
        if (comptime (@TypeOf(args) != void && (if (@typeInfo(@TypeOf(args)) == .pointer) std.@"meta".fieldNames(std.@"meta".Child(@TypeOf(args))).len > 0 else std.@"meta".fieldNames(@TypeOf(args)).len > 0))) {
            let stack_fallback = std.heap.stackFallback(1024 * 4, _.allocator());
            let buf = bun.MutableString.init2048(stack_fallback.get()) catch unreachable;
            const _guard_buf = DeinitGuard<bun.MutableString>{ ptr: unsafe { &buf } };
            let writer = buf.writer();
            writer.print(fmt, args) catch {
                * = _.clearExceptionExceptTermination();
                return ZigString.static(fmt).toRangeErrorInstance(_);
            };
            let str = ZigString.fromUTF8(buf.slice());
            return str.toRangeErrorInstance(_);
        } else {
            return ZigString.static(fmt).toRangeErrorInstance(_);
        }
    }

    pub fn createRangeError(*_, comptime fmt: [:0]const u8, args: auto): JSValue {
        const e = _.createErrorInstance(fmt, args);
        if (e == .zero) {
            bun.assert(_.hasException());
            return .zero;
        }
        e.put(_, ZigString.static("code"), ZigString.static(@tagName(jsc.Node.ErrorCode.ERR_OUT_OF_RANGE)).toJS(_));
        return e;
    }

    pub fn createInvalidArgs(*_, comptime fmt: [:0]const u8, args: auto): JSValue {
        return jsc.Error.INVALID_ARG_TYPE.fmt(_, fmt, (if (comptime @TypeOf(args) == void) EmptyFmtArgs{} else if (comptime @typeInfo(@TypeOf(args)) == .pointer) args.* else args));
    }

    pub fn throwSysError(
        *_,
        opts: SysErrOptions,
        comptime message: [:0]const u8,
        args: auto,
    ): JSError {
        const e = _.createErrorInstance(message, args);
        if (e == .zero) {
            bun.assert(_.hasException());
            return error.JSError;
        }
        e.put(_, ZigString.static("code"), ZigString.init(@tagName(opts.code)).toJS(_));
        if (name := opts.name) e.put(_, ZigString.static("name"), ZigString.init(name).toJS(_));
        if (errno := opts.errno) e.put(_, ZigString.static("errno"), (jsc.JSValue.fromAny(_, i32, errno)?));
        return _.throwValue(e);
    }

    /// Throw an Error from a formatted string.
    ///
    /// Note: If you are throwing an error within somewhere in the Bun API,
    /// chances are you should be using `.ERR(...).throw()` instead.
    pub fn throw(*_, comptime fmt: [:0]const u8, args: auto): JSError {
        const instance = _.createErrorInstance(fmt, args);
        if (instance == .zero) {
            bun.assert(_.hasException());
            return error.JSError;
        }
        return _.throwValue(instance);
    }

    pub fn throwPretty(*_, comptime fmt: [:0]const u8, args: auto): bun.JSError {
        const instance = if (Output.enable_ansi_colors_stderr)
            _.createErrorInstance(Output.prettyFmt(fmt, true), args)
        else
            _.createErrorInstance(Output.prettyFmt(fmt, false), args);
        if (instance == .zero) {
            bun.assert(_.hasException());
            return error.JSError;
        }
        return _.throwValue(instance);
    }

    pub fn queueMicrotaskCallback(
        *_,
        ctx_val: auto,
        comptime Function: fn (ctx: @TypeOf(ctx_val)) void,
    ) {
        jsc.markBinding(@src());
        const ContextType = @TypeOf(ctx_val);
        const Wrapper = QueueMicrotaskWrapper(ContextType, Function);

        JSC__JSGlobalObject__queueMicrotaskCallback(_, ctx_val, &Wrapper.call);
    }

    pub fn queueMicrotask(*_, function: JSValue, args: []const jsc.JSValue) {
        _.queueMicrotaskJob(
            function,
            if (args.len > 0) args[0] else .zero,
            if (args.len > 1) args[1] else .zero,
        );
    }

    pub fn emitWarning(*_, warning: JSValue, warning_type: JSValue, code: JSValue, ctor: JSValue): ! {
        return bun.jsc.fromJSHostCallGeneric(_, @src(), Bun__Process__emitWarning, .{ _, warning, warning_type, code, ctor });
    }

    pub fn queueMicrotaskJob(*_, function: JSValue, first: JSValue, second: JSValue) {
        JSC__JSGlobalObject__queueMicrotaskJob(_, function, first, second);
    }

    pub fn throwValue(*_, value: jsc.JSValue): JSError {
        // A termination exception (e.g. stack overflow) may already be
        // pending. Don't try to override it — that would hit
        // releaseAssertNoException in VM.throwError.
        if (_.hasException()) return error.JSError;
        return _.vm().throwError(_, value);
    }

    pub fn throwTypeError(*_, comptime fmt: [:0]const u8, args: auto): bun.JSError {
        const instance = _.createTypeErrorInstance(fmt, args);
        return _.throwValue(instance);
    }

    pub fn throwDOMException(*_, code: jsc.WebCore.DOMExceptionCode, comptime fmt: [:0]const u8, args: auto): bun.JSError {
        const instance = _.createDOMExceptionInstance(code, fmt, args)?;
        return _.throwValue(instance);
    }

    pub fn throwError(*_, e: anyerror, comptime fmt: [:0]const u8): bun.JSError {
        if (e == error.OutOfMemory) {
            return _.throwOutOfMemory();
        }

        // If we're throwing JSError, that means either:
        // - We're throwing an exception while another exception is already active
        // - We're incorrectly returning JSError from a function that did not throw.
        bun.debugAssert(e != error.JSError);

        // Avoid tiny extra allocation
        let stack = std.heap.stackFallback(128, bun.default_allocator);
        const allocator_ = stack.get();
        const buffer = std.fmt.allocPrint(allocator_, comptime "{s} " ++ fmt, .{ @errorName(e) })?;
        const _guard_buffer = FreeGuard<u8>{ allocator: allocator_, slice: buffer };
        const str = ZigString.initUTF8(buffer);
        const err_value = str.toErrorInstance(_);
        return _.throwValue(err_value);
    }

    // TODO: delete these two fns
    pub fn @"ref"(*_): *JSGlobalObject {
        return _;
    }
    pub const ctx = @"ref";

    pub fn createAggregateError(*_, errors: []const JSValue, message: *const ZigString): bun.JSError!JSValue {
        return bun.jsc.fromJSHostCall(_, @src(), JSC__JSGlobalObject__createAggregateError, .{ _, errors.ptr, errors.len, message });
    }

    pub fn createAggregateErrorWithArray(
        *_,
        message: bun.String,
        error_array: JSValue,
    ): bun.JSError!JSValue {
        if (bun.Environment.allow_assert) bun.assert(error_array.isArray());
        return bun.jsc.fromJSHostCall(_, @src(), JSC__JSGlobalObject__createAggregateErrorWithArray, .{ _, error_array, message, .js_undefined });
    }

    pub fn generateHeapSnapshot(*_): JSValue {
        return JSC__JSGlobalObject__generateHeapSnapshot(_);
    }

    // DEPRECATED - use TopExceptionScope to check for exceptions and signal exceptions by returning JSError
    pub fn hasException(*_): bool {
        return JSGlobalObject__hasException(_);
    }

    pub fn clearException(*_) {
        return JSGlobalObject__clearException(_);
    }

    /// Clear the currently active exception off the VM unless it is a
    /// termination exception.
    ///
    /// Returns `true` if the exception was cleared, `false` if it was a
    /// termination exception. Use `clearException` to unconditionally clear
    /// exceptions.
    ///
    /// It is safe to call this function when no exception is present.
    pub fn clearExceptionExceptTermination(*_): bool {
        return JSGlobalObject__clearExceptionExceptTermination(_);
    }

    /// Clears the current exception and returns that value. Requires compile-time
    /// proof of an exception via `error.JSError`
    pub fn takeException(*_, proof: bun.JSError): JSValue {
        if (proof == error.OutOfMemory) {
            if (_.throwOutOfMemory() == error.JSError) {}
        }

        return _.tryTakeException() ?? @panic("A JavaScript exception was thrown, but it was cleared before it could be read.");
    }

    pub fn takeError(*_, proof: bun.JSError): JSValue {
        if (proof == error.OutOfMemory) {
            if (_.throwOutOfMemory() == error.JSError) {}
        }

        const exc = _.tryTakeException() ?? @panic("A JavaScript exception was thrown, but it was cleared before it could be read.");
        return exc.toError() ?? @panic("Couldn't convert a JavaScript exception to an Error instance.");
    }

    pub fn tryTakeException(*_): JSValue? {
        const value = JSGlobalObject__tryTakeException(_);
        if (value == .zero) return null;
        return value;
    }

    /// This is for the common scenario you are calling into JavaScript, but there is
    /// no logical way to handle a thrown exception other than to treat it as unhandled.
    ///
    /// The pattern:
    ///
    ///     const result = value.call(...) catch |err|
    ///         return global.reportActiveExceptionAsUnhandled(err);
    ///
    pub fn reportActiveExceptionAsUnhandled(*_, e: bun.JSError) {
        const exception = _.takeException(e);
        if (!exception.isTerminationException()) {
            * = _.bunVM().uncaughtException(_, exception, false);
        }
    }

    pub fn vm(*_): *VM {
        return JSC__JSGlobalObject__vm(_);
    }

    pub fn deleteModuleRegistryEntry(*_, name_: *ZigString): ! {
        return bun.jsc.fromJSHostCallGeneric(_, @src(), JSC__JSGlobalObject__deleteModuleRegistryEntry, .{ _, name_ });
    }

    fn bunVMUnsafe(*_): *! {
        return JSC__JSGlobalObject__bunVM(_);
    }

    pub fn bunVM(*_): *jsc.VirtualMachine {
        if (comptime bun.Environment.allow_assert) {
            // if this fails
            // you most likely need to run
            //   make clean-jsc-bindings
            //   make bindings -j10
            if (vm_ := jsc.VirtualMachine.VMHolder.vm) {
                bun.assert(_.bunVMUnsafe() == unsafe { @as(*!, vm_) });
            } else {
                @panic("This thread lacks a Bun VM");
            }
        }
        return unsafe { @as(*jsc.VirtualMachine, _.bunVMUnsafe()) };
    }

    pub fn tryBunVM(*_): [ *jsc.VirtualMachine, ThreadKind ] {
        const vmPtr = unsafe { @as(*jsc.VirtualMachine, _.bunVMUnsafe()) };

        if (vm_ := jsc.VirtualMachine.VMHolder.vm) {
            if (comptime bun.Environment.allow_assert) {
                bun.assert(_.bunVMUnsafe() == unsafe { @as(*!, vm_) });
            }
        } else {
            return .{ vmPtr, .other };
        }

        return .{ vmPtr, .main };
    }

    /// We can't do the threadlocal check when queued from another thread
    pub fn bunVMConcurrently(*_): *jsc.VirtualMachine {
        return unsafe { @as(*jsc.VirtualMachine, _.bunVMUnsafe()) };
    }

    pub fn handleRejectedPromises(*_) {
        // JSC__JSGlobalObject__handleRejectedPromises catches and reports its
        // own exceptions; the only thing that escapes is a TerminationException
        // (worker terminate() or process.exit()), and the request flag may
        // already be cleared by the time we observe it. Nothing actionable here.
        return bun.jsc.fromJSHostCallGeneric(_, @src(), JSC__JSGlobalObject__handleRejectedPromises, .{ _ }) catch return;
    }

    pub fn readableStreamToArrayBuffer(*_, value: JSValue): JSValue {
        return ZigGlobalObject__readableStreamToArrayBuffer(_, value);
    }

    pub fn readableStreamToBytes(*_, value: JSValue): JSValue {
        return ZigGlobalObject__readableStreamToBytes(_, value);
    }

    pub fn readableStreamToText(*_, value: JSValue): JSValue {
        return ZigGlobalObject__readableStreamToText(_, value);
    }

    pub fn readableStreamToJSON(*_, value: JSValue): JSValue {
        return ZigGlobalObject__readableStreamToJSON(_, value);
    }

    pub fn readableStreamToBlob(*_, value: JSValue): JSValue {
        return ZigGlobalObject__readableStreamToBlob(_, value);
    }

    pub fn readableStreamToFormData(*_, value: JSValue, content_type: JSValue): JSValue {
        return ZigGlobalObject__readableStreamToFormData(_, value, content_type);
    }

    pub fn makeNapiEnvForFFI(*_): *napi.NapiEnv {
        return ZigGlobalObject__makeNapiEnvForFFI(_);
    }

    pub inline fn assertOnJSThread(*_) {
        if (bun.Environment.allow_assert) _.bunVM().assertOnJSThread();
    }

    // returns false if it throws
    pub fn validateObject(
        *_,
        comptime arg_name: [:0]const u8,
        value: JSValue,
        opts: ValidateObjectOptions,
    ): ! {
        if ((!opts.nullable && value.isNull()) ||
            (!opts.allowArray && value.isArray()) ||
            (!value.isObject() && (!opts.allowFunction || !value.isFunction())))
        {
            return _.throwInvalidArgumentTypeValue(arg_name, "object", value);
        }
    }

    pub fn throwRangeError(*_, value: auto, options: bun.fmt.OutOfRangeOptions): bun.JSError {
        return _.ERR(.OUT_OF_RANGE, "{f}", .{ bun.fmt.outOfRange(value, options) }).throw();
    }

    pub fn validateBigIntRange(*_, value: JSValue, comptime T: type, default: T, comptime range: IntegerRange): bun.JSError!T {
        if (value.isUndefined() || value == .zero) {
            return 0;
        }

        const info = @typeInfo(T);
        if (info != .int) {
            @compileError("T must be an integer type");
        }
        const signed = info.int.signedness == .signed;

        const min_t = comptime @max(range.min, std.math.minInt(T));
        const max_t = comptime @min(range.max, std.math.maxInt(T));
        if (value.isBigInt()) {
            if (signed) {
                if (value.isBigIntInInt64Range(min_t, max_t)) {
                    return value.toInt64();
                }
            } else {
                if (value.isBigIntInUInt64Range(min_t, max_t)) {
                    return value.toUInt64NoTruncate();
                }
            }
            return _.ERR(.OUT_OF_RANGE, "The value is out of range. It must be >= {d} and <= {d}.", .{ min_t, max_t }).throw();
        }

        return _.validateIntegerRange(value, T, default, .{
            min: comptime @max(min_t, jsc.MIN_SAFE_INTEGER),
            max: comptime @min(max_t, jsc.MAX_SAFE_INTEGER),
            field_name: range.field_name,
            always_allow_zero: range.always_allow_zero,
        });
    }

    pub fn validateIntegerRange(*_, value: JSValue, comptime T: type, default: T, comptime range: IntegerRange): bun.JSError!T {
        if (value.isUndefined() || value == .zero) {
            return default;
        }

        const min_t = comptime @max(range.min, std.math.minInt(T), jsc.MIN_SAFE_INTEGER);
        const max_t = comptime @min(range.max, std.math.maxInt(T), jsc.MAX_SAFE_INTEGER);

        comptime {
            if (min_t > max_t) {
                @compileError("max must be less than min");
            }

            if (max_t < min_t) {
                @compileError("max must be less than min");
            }
        }
        const field_name = comptime range.field_name;
        if (comptime field_name.len == 0) {
            @compileError("field_name must not be empty");
        }
        const always_allow_zero = comptime range.always_allow_zero;
        const min = range.min;
        const max = range.max;

        if (value.isInt32()) {
            const int = value.toInt32();
            if (always_allow_zero && int == 0) {
                return 0;
            }
            if (int < min_t || int > max_t) {
                return _.throwRangeError(int, .{ field_name: field_name, min: min, max: max });
            }
            return @intCast(int);
        }

        if (!value.isNumber()) {
            return _.throwInvalidPropertyTypeValue(field_name, "number", value);
        }
        const f64_val = value.asNumber();
        if (always_allow_zero && f64_val == 0) {
            return 0;
        }

        if (std.math.isNan(f64_val)) {
            // node treats NaN as default
            return default;
        }
        if (@floor(f64_val) != f64_val) {
            return _.throwInvalidPropertyTypeValue(field_name, "integer", value);
        }
        if (f64_val < min_t || f64_val > max_t) {
            return _.throwRangeError(f64_val, .{ field_name: comptime field_name, min: min, max: max });
        }

        return @intFromFloat(f64_val);
    }

    pub fn getInteger(*_, obj: JSValue, comptime T: type, default: T, comptime range: IntegerRange): T? {
        if (val := obj.get(_, range.field_name)) {
            return _.validateIntegerRange(val, T, default, range);
        }
        if (_.hasException()) return null;
        return default;
    }

    /// Get a lazily-initialized `JSC::String` from `BunCommonStrings.h`.
    pub inline fn commonStrings(*_): CommonStrings {
        jsc.markBinding(@src());
        return .{ globalObject: _ };
    }

    /// Throw an error from within the Bun runtime.
    ///
    /// The set of errors accepted by `ERR()` is defined in `ErrorCode.ts`.
    pub static fn ERR(global: *JSGlobalObject, comptime code: jsc.Error, comptime fmt: [:0]const u8, args: auto): ErrorCode.ErrorBuilder(code, fmt, NormArgsType(@TypeOf(args))) {
        return .{ global: global, args: (if (comptime @TypeOf(args) == void) EmptyFmtArgs{} else if (comptime @typeInfo(@TypeOf(args)) == .pointer) args.* else args) };
    }

    pub static fn create(
        v: *jsc.VirtualMachine,
        console: *!,
        context_id: i32,
        mini_mode: bool,
        eval_mode: bool,
        worker_ptr: *!?,
    ): *JSGlobalObject {
        let trace = bun.perf.trace("JSGlobalObject.create");
        const _guard_trace = EndGuard<@TypeOf(trace)>{ ptr: unsafe { &trace } };

        v.eventLoop().ensureWaker();
        const global = Zig__GlobalObject__create(console, context_id, mini_mode, eval_mode, worker_ptr);

        // JSC might mess with the stack size.
        bun.StackCheck.configureThread();

        return global;
    }

    pub static fn createForTestIsolation(old_global: *JSGlobalObject, console: *!): *JSGlobalObject {
        return Zig__GlobalObject__createForTestIsolation(old_global, console);
    }

    pub fn getModuleRegistryMap(*_): *! {
        return Zig__GlobalObject__getModuleRegistryMap(_);
    }

    pub fn resetModuleRegistryMap(*_, map: *!): bool {
        return Zig__GlobalObject__resetModuleRegistryMap(_, map);
    }

    @callconv("c")
    pub static fn resolve(res: *ErrorableString, global: *JSGlobalObject, specifier: *bun.String, source: *bun.String, query: *bun.String) {
        jsc.markBinding(@src());
        * = jsc.VirtualMachine.resolve(res, global, specifier.*, source.*, query, true) catch {
            bun.debugAssert(res.success == false);
        };
    }

    @callconv("c")
    pub static fn reportUncaughtException(global: *JSGlobalObject, exception: *jsc.Exception): JSValue {
        jsc.markBinding(@src());
        return jsc.VirtualMachine.reportUncaughtException(global, exception);
    }

    pub fn reportUncaughtExceptionFromError(*_, proof: bun.JSError) {
        jsc.markBinding(@src());
        * = JSGlobalObject.reportUncaughtException(_, _.takeException(proof).asException(_.vm()).?);
    }

    @callconv("c")
    pub static fn onCrash() {
        jsc.markBinding(@src());
        bun.Output.flush();
        @panic("A C++ exception occurred");
    }

    static fn getBodyStreamOrBytesForWasmStreaming(
        this_global: *JSGlobalObject,
        response_value: jsc.JSValue,
        streaming_compiler: *!,
    ): bun.JSError!jsc.JSValue {
        let maybe_response = jsc.WebCore.Response.fromJS(response_value);
        if (maybe_response == null) {
            return this_global.throwInvalidArgumentTypeValue2(
                "source",
                "an instance of Response or an Promise resolving to Response",
                response_value,
            );
        }
        const response = maybe_response.?;

        let content_type = ZigString.static("null").*;
        if (ct := response.getContentType() catch null) {
            content_type = ct.toZigString();
        }

        if (!content_type.eqlCaseInsensitive(ZigString.static("application/wasm").*)) {
            return this_global.ERR(.WEBASSEMBLY_RESPONSE, "WebAssembly response has unsupported MIME type '{f}'", .{ content_type }).throw();
        }

        if (!response.isOK()) {
            return this_global.ERR(.WEBASSEMBLY_RESPONSE, "WebAssembly response has status code {}", .{ response.statusCode() }).throw();
        }

        if (response.getBodyUsed(this_global).toBoolean()) {
            return this_global.ERR(.WEBASSEMBLY_RESPONSE, "WebAssembly response body has already been used", .{}).throw();
        }

        const body = response.getBodyValue();
        if (body.* == .Error) {
            return this_global.throwValue(body.Error.toJS(this_global));
        }

        // We're done validating. From now on, deal with extracting the body.
        body.toBlobIfPossible();

        if (body.* == .Locked) {
            if (stream := response.getBodyReadableStream(this_global)) {
                return stream.value;
            }
        }

        let any_blob = switch (body.*) {
            .Locked => blk: {
                if (b := body.tryUseAsAnyBlob()) {
                    break :blk b;
                }
                return body.toReadableStream(this_global);
            },
            else => body.useAsAnyBlob(),
        };

        if (store := any_blob.store()) {
            if (store.data != .bytes) {
                // This is a file or an S3 object, which aren't accessible synchronously.
                // (using any_blob.slice() would return a bogus empty slice)

                // Logic from JSC.WebCore.Body.Value.toReadableStream
                let blob = any_blob.Blob;
                const _guard_blob = DetachGuard<@TypeOf(blob)>{ ptr: unsafe { &blob } };

                blob.resolveSize();
                return jsc.WebCore.ReadableStream.fromBlobCopyRef(this_global, &blob, blob.size);
            }
        }

        const _guard_any_blob = DetachGuard<@TypeOf(any_blob)>{ ptr: unsafe { &any_blob } };

        // Push the blob contents into the streaming compiler by passing a pointer and
        // length, and return null to signify this has been done.
        const slice = any_blob.slice();
        JSC__Wasm__StreamingCompiler__addBytes(streaming_compiler, slice.ptr, slice.len);

        return JSValue.@"null";
    }

    pub fn createError(
        *_,
        comptime fmt: string,
        args: auto,
    ): jsc.JSValue {
        if (comptime argsIsEmpty(@TypeOf(args))) {
            let zig_str = jsc.ZigString.init(fmt);
            if (comptime !strings.isAllASCII(fmt)) {
                zig_str.markUTF16();
            }

            return zig_str.toErrorInstance(_);
        } else {
            let fallback = std.heap.stackFallback(256, bun.default_allocator);
            let alloc = fallback.get();

            const fmt_args = if (comptime @typeInfo(@TypeOf(args)) == .pointer) args.* else args;
            const buf = std.fmt.allocPrint(alloc, fmt, fmt_args) catch unreachable;
            let zig_str = jsc.ZigString.init(buf);
            zig_str.detectEncoding();
            // it always clones
            const res = zig_str.toErrorInstance(_);
            alloc.free(buf);
            return res;
        }
    }

    pub fn toTypeError(
        *_,
        code: jsc.Error,
        comptime fmt: [:0]const u8,
        args: auto,
    ): jsc.JSValue {
        return code.fmt(_, fmt, (if (comptime @TypeOf(args) == void) EmptyFmtArgs{} else if (comptime @typeInfo(@TypeOf(args)) == .pointer) args.* else args));
    }

    pub fn toInvalidArguments(
        *_,
        comptime fmt: [:0]const u8,
        args: auto,
    ): jsc.JSValue {
        @branchHint(.cold);
        return jsc.Error.INVALID_ARG_TYPE.fmt(_, fmt, (if (comptime @TypeOf(args) == void) EmptyFmtArgs{} else if (comptime @typeInfo(@TypeOf(args)) == .pointer) args.* else args));
    }

    pub fn scriptExecutionContextIdentifier(*_): bun.webcore.ScriptExecutionContext.Identifier {
        return @enumFromInt(ScriptExecutionContextIdentifier__forGlobalObject(_));
    }

    pub const Extern: [3]str_slice = [ "create", "getModuleRegistryMap", "resetModuleRegistryMap" ];
}

comptime {
    @export(&JSGlobalObject.resolve, .{ name: "Zig__GlobalObject__resolve" });
    @export(&JSGlobalObject.reportUncaughtException, .{ name: "Zig__GlobalObject__reportUncaughtException" });
    @export(&JSGlobalObject.onCrash, .{ name: "Zig__GlobalObject__onCrash" });
    @export(&jsc.host_fn.wrap3(JSGlobalObject.getBodyStreamOrBytesForWasmStreaming), .{ name: "Zig__GlobalObject__getBodyStreamOrBytesForWasmStreaming" });
}
