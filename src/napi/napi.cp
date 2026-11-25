import WorkPoolMod from "../threading/work_pool.zig";
import BufferMod from "../runtime/node/buffer.zig";

type OpaquePtr = *!?;
type NullableU8MultiPtr = ([*]u8)?;
type NapiAsyncWorkPtr = *napi_async_work;
type ConstNapiNodeVersionPtr = *const napi_node_version;

pub struct DerefGuard<T> {
    ptr: (*T!)?,

    ~init() {
        if (p := _.ptr) {
            p.deref();
        }
    }
}

pub struct DeferredGuard {
    ptr: (*jsc.JSPromise.Strong!)?,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
            bun.default_allocator.destroy(p);
        }
    }
}

pub struct PollRefGuard {
    poll_ref: (*Async.KeepAlive!)?,
    vm: *jsc.VirtualMachine!,

    ~init() {
        if (pr := _.poll_ref) {
            pr.unref(_.vm);
        }
    }
}

pub struct HandleScopeGuard {
    scope: (*NapiHandleScope)?,
    env: *NapiEnv!,

    ~init() {
        if (s := _.scope) {
            s.close(_.env);
        }
    }
}

pub struct UnlockGuard<T> {
    ptr: (*T!)? = null,

    ~init() {
        if (p := _.ptr) {
            p.unlock();
        }
    }
}

const TODO_EXCEPTION = @as(jsc.C.ExceptionRef, null);

const log = bun.Output.scoped(.napi, .visible);

extern fn napi_set_last_error(env: napi_env, status: NapiStatus): napi_status ;
extern fn NapiEnv__globalObject(env: *NapiEnv): *jsc.JSGlobalObject ;
extern fn NapiEnv__getAndClearPendingException(env: *NapiEnv, out: *JSValue): bool ;
extern fn napi_internal_get_version(env: *NapiEnv): u32 ;
extern fn NapiEnv__deref(env: *NapiEnv) ;
extern fn NapiEnv__ref(env: *NapiEnv) ;

pub struct NapiEnvExternalSharedDescriptor {
    pub const ref = NapiEnv__ref;
    pub const deref = NapiEnv__deref;
}

/// This is `struct napi_env__` from napi.h
@opaque pub struct NapiEnv {
    fn toJS(*_): *jsc.JSGlobalObject {
        return NapiEnv__globalObject(_);
    }

    /// Convert err to an extern napi_status, and store the error code in env so that it can be
    /// accessed by napi_get_last_error_info
    static fn setLastError(env: (*NapiEnv)?, e: NapiStatus): napi_status {
        return napi_set_last_error(env, e);
    }

    /// Convenience wrapper for setLastError(.ok)
    fn ok(*_): napi_status {
        return NapiEnv.setLastError(_, .ok);
    }

    /// These wrappers exist for convenience and so we can set a breakpoint in lldb
    fn invalidArg(*_): napi_status {
        if (comptime bun.Environment.allow_assert) {
            log("invalid arg", {});
        }
        return NapiEnv.setLastError(_, .invalid_arg);
    }

    fn genericFailure(*_): napi_status {
        if (comptime bun.Environment.allow_assert) {
            log("generic failure", {});
        }
        return NapiEnv.setLastError(_, .generic_failure);
    }

    /// Assert that we're not currently performing garbage collection
    fn checkGC(*_) {
        napi_internal_check_gc(_);
    }

    /// Return the Node-API version number declared by the module we are running code from
    fn getVersion(*_): u32 {
        return napi_internal_get_version(_);
    }

    fn getAndClearPendingException(*_): JSValue? {
        let exception: JSValue = undefined;
        if (NapiEnv__getAndClearPendingException(_, &exception)) {
            return exception;
        }

        return null;
    }

    pub const external_shared_descriptor = NapiEnvExternalSharedDescriptor;

    pub const Ref = bun.ptr.ExternalShared(NapiEnv);
}

fn envIsNull(): napi_status {
    // in this case we don't actually have an environment to set the last error on, so it doesn't
    // make sense to call napi_set_last_error
    @branchHint(.cold);
    return @intFromEnum(NapiStatus.invalid_arg);
}

/// This is nullable because native modules may pass null pointers for the NAPI environment, which
/// is an error that our NAPI functions need to handle (by returning napi_invalid_arg). To specify
/// a Zig API that uses a never-null napi_env, use `*NapiEnv`.
pub type napi_env = (*NapiEnv)?;

/// Contents are not used by any Zig code
@opaque pub struct Ref {}

pub const napi_ref = *Ref;

pub extern fn NapiHandleScope__open(env: *NapiEnv, escapable: bool): (*NapiHandleScope)? ;
pub extern fn NapiHandleScope__close(env: *NapiEnv, current: (*NapiHandleScope)?) ;
extern fn NapiHandleScope__append(env: *NapiEnv, value: jsc.JSValue.backing_int) ;
extern fn NapiHandleScope__escape(handleScope: *NapiHandleScope, value: jsc.JSValue.backing_int): bool ;

err EscapeError {
    EscapeCalledTwice,
}

@opaque pub struct NapiHandleScope {
    /// Create a new handle scope in the given environment, or return null if creating one now is
    /// unsafe (i.e. inside a finalizer)
    static fn open(env: *NapiEnv, escapable: bool): (*NapiHandleScope)? {
        return NapiHandleScope__open(env, escapable);
    }

    /// Closes the given handle scope, releasing all values inside it, if it is safe to do so.
    /// Asserts that self is the current handle scope in env.
    static fn close(scope: (*NapiHandleScope)?, env: *NapiEnv) {
        NapiHandleScope__close(env, scope);
    }

    /// Place a value in the handle scope. Must be done while returning any JS value into NAPI
    /// callbacks, as the value must remain alive as long as the handle scope is active, even if the
    /// native module doesn't keep it visible on the stack.
    static fn append(env: *NapiEnv, value: jsc.JSValue) {
        NapiHandleScope__append(env, @intFromEnum(value));
    }

    /// Move a value from the current handle scope (which must be escapable) to the reserved escape
    /// slot in the parent handle scope, allowing that value to outlive the current handle scope.
    /// Returns an error if escape() has already been called on this handle scope.
    fn escape(*_, value: jsc.JSValue): EscapeError!void {
        if (!NapiHandleScope__escape(_, @intFromEnum(value))) {
            return error.EscapeCalledTwice;
        }
    }
}

pub type napi_handle_scope = (*NapiHandleScope)?;
pub type napi_escapable_handle_scope = (*NapiHandleScope)?;
pub const napi_callback_info = *jsc.CallFrame;
pub const napi_deferred = *jsc.JSPromise.Strong;

/// To ensure napi_values are not collected prematurely after being returned into a native module,
/// you must use these functions rather than convert between napi_value and jsc.JSValue directly
pub enum napi_value: i64 {
    _,

    fn set(
        *_,
        env: *NapiEnv,
        val: jsc.JSValue,
    ) {
        NapiHandleScope.append(env, val);
        _.* = @enumFromInt(@intFromEnum(val));
    }

    fn get(): jsc.JSValue {
        return @enumFromInt(@intFromEnum(_));
    }

    static fn create(env: *NapiEnv, val: jsc.JSValue): napi_value {
        NapiHandleScope.append(env, val);
        return @enumFromInt(@intFromEnum(val));
    }
}

const char16_t = u16;
pub const napi_property_attributes = c_uint;
pub enum napi_valuetype: c_uint {
    @"undefined" = 0,
    @"null" = 1,
    boolean = 2,
    number = 3,
    string = 4,
    symbol = 5,
    object = 6,
    function = 7,
    external = 8,
    bigint = 9,
}
pub enum napi_typedarray_type: c_uint {
    int8_array = 0,
    uint8_array = 1,
    uint8_clamped_array = 2,
    int16_array = 3,
    uint16_array = 4,
    int32_array = 5,
    uint32_array = 6,
    float32_array = 7,
    float64_array = 8,
    bigint64_array = 9,
    biguint64_array = 10,

    static fn fromJSType(js_type: jsc.JSValue.JSType): napi_typedarray_type? {
        return switch (js_type) {
            .Int8Array => napi_typedarray_type.int8_array,
            .Uint8Array => napi_typedarray_type.uint8_array,
            .Uint8ClampedArray => napi_typedarray_type.uint8_clamped_array,
            .Int16Array => napi_typedarray_type.int16_array,
            .Uint16Array => napi_typedarray_type.uint16_array,
            .Int32Array => napi_typedarray_type.int32_array,
            .Uint32Array => napi_typedarray_type.uint32_array,
            .Float32Array => napi_typedarray_type.float32_array,
            .Float64Array => napi_typedarray_type.float64_array,
            .BigInt64Array => napi_typedarray_type.bigint64_array,
            .BigUint64Array => napi_typedarray_type.biguint64_array,
            else => null,
        };
    }

    fn toJSType(): jsc.JSValue.JSType {
        return switch (_) {
            .int8_array => .Int8Array,
            .uint8_array => .Uint8Array,
            .uint8_clamped_array => .Uint8ClampedArray,
            .int16_array => .Int16Array,
            .uint16_array => .Uint16Array,
            .int32_array => .Int32Array,
            .uint32_array => .Uint32Array,
            .float32_array => .Float32Array,
            .float64_array => .Float64Array,
            .bigint64_array => .BigInt64Array,
            .biguint64_array => .BigUint64Array,
        };
    }

    fn toC(): jsc.C.JSTypedArrayType {
        return _.toJSType().toC();
    }
}

pub enum NapiStatus: c_uint {
    ok = 0,
    invalid_arg = 1,
    object_expected = 2,
    string_expected = 3,
    name_expected = 4,
    function_expected = 5,
    number_expected = 6,
    boolean_expected = 7,
    array_expected = 8,
    generic_failure = 9,
    pending_exception = 10,
    cancelled = 11,
    escape_called_twice = 12,
    handle_scope_mismatch = 13,
    callback_scope_mismatch = 14,
    queue_full = 15,
    closing = 16,
    bigint_expected = 17,
    date_expected = 18,
    arraybuffer_expected = 19,
    detachable_arraybuffer_expected = 20,
    would_deadlock = 21,
}

/// This is not an `enum` so that the enum values cannot be trivially returned from NAPI functions,
/// as that would skip storing the last error code. You should wrap return values in a call to
/// napi_env.setLastError.
pub const napi_status = c_uint;

pub type NapiCallbackFn = @c fn (napi_env, napi_callback_info) napi_value;
pub type napi_callback = NapiCallbackFn?;

/// expects `napi_env`, `callback_data`, `context`
pub type NapiFinalizeFunction = @c fn (napi_env, *!?, *!?) void;
pub type napi_finalize = NapiFinalizeFunction?;
@c pub struct napi_property_descriptor {
    utf8name: [*c]const u8,
    name: napi_value,
    method: napi_callback,
    getter: napi_callback,
    setter: napi_callback,
    value: napi_value,
    attributes: napi_property_attributes,
    data: *!?,
}
@c pub struct napi_extended_error_info {
    error_message: [*c]const u8,
    engine_reserved: *!?,
    engine_error_code: u32,
    error_code: napi_status,
}

const napi_key_collection_mode = c_uint;
const napi_key_filter = c_uint;
const napi_key_conversion = c_uint;
@c struct napi_type_tag {
    lower: u64,
    upper: u64,
}
pub extern fn napi_get_last_error_info(env: napi_env, result: [*c][*c]const napi_extended_error_info): napi_status ;
@export @c fn napi_get_undefined(env_: napi_env, result_: (*napi_value)?): napi_status {
    log("napi_get_undefined", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.set(env, .js_undefined);
    return env.ok();
}
@export @c fn napi_get_null(env_: napi_env, result_: (*napi_value)?): napi_status {
    log("napi_get_null", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.set(env, JSValue.jsNull());
    return env.ok();
}
pub extern fn napi_get_global(env: napi_env, result: *napi_value): napi_status ;
@export @c fn napi_get_boolean(env_: napi_env, value: bool, result_: (*napi_value)?): napi_status {
    log("napi_get_boolean", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.set(env, JSValue.jsBoolean(value));
    return env.ok();
}
@export @c fn napi_create_array(env_: napi_env, result_: (*napi_value)?): napi_status {
    log("napi_create_array", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.set(env, JSValue.createEmptyArray(env.toJS(), 0) catch return env.setLastError(.pending_exception));
    return env.ok();
}
@export @c fn napi_create_array_with_length(env_: napi_env, length: usize, result_: (*napi_value)?): napi_status {
    log("napi_create_array_with_length", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;

    // https://github.com/nodejs/node/blob/14c68e3b536798e25f810ed7ae180a5cde9e47d3/deps/v8/src/api/api.cc#L8163-L8174
    // size_t immediately cast to int as argument to Array::New, then min 0
    const len_i64: i64 = @bitCast(length);
    const len_i32: i32 = @truncate(len_i64);
    const len: u32 = if (len_i32 > 0) @bitCast(len_i32) else 0;

    const array = jsc.JSValue.createEmptyArray(env.toJS(), len) catch return env.setLastError(.pending_exception);
    array.ensureStillAlive();
    result.set(env, array);
    return env.ok();
}
pub extern fn napi_create_double(_: napi_env, value: f64, result: *napi_value): napi_status ;
@export @c fn napi_create_int32(env_: napi_env, value: i32, result_: (*napi_value)?): napi_status {
    log("napi_create_int32", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.set(env, JSValue.jsNumber(value));
    return env.ok();
}
@export @c fn napi_create_uint32(env_: napi_env, value: u32, result_: (*napi_value)?): napi_status {
    log("napi_create_uint32", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.set(env, JSValue.jsNumber(value));
    return env.ok();
}
@export @c fn napi_create_int64(env_: napi_env, value: i64, result_: (*napi_value)?): napi_status {
    log("napi_create_int64", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.set(env, JSValue.jsNumber(value));
    return env.ok();
}
@export @c fn napi_create_string_latin1(env_: napi_env, str: [*]const u8?, length: usize, result_: (*napi_value)?): napi_status {
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result: *napi_value = result_.?;

    const slice: []const u8 = brk: {
        if (ptr := str) {
            if (NAPI_AUTO_LENGTH == length) {
                unsafe {
                    break :brk bun.sliceTo(@as([*:0]const u8, @ptrCast(ptr)), 0);
                }
            } else if (length > std.math.maxInt(i32)) {
                return env.invalidArg();
            } else {
                unsafe {
                    break :brk ptr[0..length];
                }
            }
        }

        if (length == 0) {
            break :brk [];
        } else {
            return env.invalidArg();
        }
    };

    log("napi_create_string_latin1: {s}", .{slice});

    if (slice.len == 0) {
        result.set(env, bun.String.empty.toJS(env.toJS()) catch return env.setLastError(.generic_failure));
        return env.ok();
    }

    const str_pair = bun.String.createUninitialized(.latin1, slice.len);
    let str_val = str_pair[0];
    const bytes = str_pair[1];

    @memcpy(bytes, slice);

    result.set(env, str_val.transferToJS(env.toJS()) catch {
        str_val.deref();
        return env.setLastError(.generic_failure);
    });
    return env.ok();
}
@export @c fn napi_create_string_utf8(env_: napi_env, str: [*]const u8?, length: usize, result_: (*napi_value)?): napi_status {
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result: *napi_value = result_.?;

    const slice: []const u8 = brk: {
        if (ptr := str) {
            if (NAPI_AUTO_LENGTH == length) {
                unsafe {
                    break :brk bun.sliceTo(@as([*:0]const u8, @ptrCast(ptr)), 0);
                }
            } else if (length > std.math.maxInt(i32)) {
                return env.invalidArg();
            } else {
                unsafe {
                    break :brk ptr[0..length];
                }
            }
        }

        if (length == 0) {
            break :brk [];
        } else {
            return env.invalidArg();
        }
    };

    log("napi_create_string_utf8: {s}", .{slice});

    const globalObject = env.toJS();
    const str_val = bun.String.createUTF8ForJS(globalObject, slice) catch return env.setLastError(.pending_exception);
    result.set(env, str_val);
    return env.ok();
}
@export @c fn napi_create_string_utf16(env_: napi_env, str: [*]const char16_t?, length: usize, result_: (*napi_value)?): napi_status {
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result: *napi_value = result_.?;

    const slice: []const u16 = brk: {
        if (ptr := str) {
            if (NAPI_AUTO_LENGTH == length) {
                unsafe {
                    break :brk bun.sliceTo(@as([*:0]const u16, @ptrCast(ptr)), 0);
                }
            } else if (length > std.math.maxInt(i32)) {
                return env.invalidArg();
            } else {
                unsafe {
                    break :brk ptr[0..length];
                }
            }
        }

        if (length == 0) {
            break :brk [];
        } else {
            return env.invalidArg();
        }
    };

    if (comptime bun.Environment.allow_assert)
        log("napi_create_string_utf16: {d} {f}", .{ slice.len, bun.fmt.FormatUTF16{ buf: slice[0..@min(slice.len, 512)] } });

    if (slice.len == 0) {
        result.set(env, bun.String.empty.toJS(env.toJS()) catch return env.setLastError(.generic_failure));
        return env.ok();
    }

    const str_pair = bun.String.createUninitialized(.utf16, slice.len);
    let str_val = str_pair[0];
    const chars = str_pair[1];
    @memcpy(chars, slice);

    const js_str = str_val.transferToJS(env.toJS()) catch {
        str_val.deref();
        return env.setLastError(.generic_failure);
    };
    result.set(env, js_str);
    return env.ok();
}

pub extern fn napi_create_symbol(env: napi_env, description: napi_value, result: *napi_value): napi_status ;
pub extern fn napi_create_error(env: napi_env, code: napi_value, msg: napi_value, result: *napi_value): napi_status ;
pub extern fn napi_create_type_error(env: napi_env, code: napi_value, msg: napi_value, result: *napi_value): napi_status ;
pub extern fn napi_create_range_error(env: napi_env, code: napi_value, msg: napi_value, result: *napi_value): napi_status ;
pub extern fn napi_typeof(env: napi_env, value: napi_value, result: *napi_valuetype): napi_status ;
pub extern fn napi_get_value_double(env: napi_env, value: napi_value, result: *f64): napi_status ;
pub extern fn napi_get_value_int32(_: napi_env, value_: napi_value, result: (*i32)?): napi_status ;
pub extern fn napi_get_value_uint32(_: napi_env, value_: napi_value, result_: (*u32)?): napi_status ;
pub extern fn napi_get_value_int64(_: napi_env, value_: napi_value, result_: (*i64)?): napi_status ;
pub extern fn napi_get_value_bool(_: napi_env, value_: napi_value, result_: (*bool)?): napi_status ;

pub extern fn napi_get_value_string_latin1(env: napi_env, value_: napi_value, buf_ptr_: [*:0]c_char?, bufsize: usize, result_ptr: (*usize)?): napi_status ;

/// Copies a JavaScript string into a UTF-8 string buffer. The result is the
/// number of bytes (excluding the null terminator) copied into buf.
/// A sufficient buffer size should be greater than the length of string,
/// reserving space for null terminator.
/// If bufsize is insufficient, the string will be truncated and null terminated.
/// If buf is NULL, this method returns the length of the string (in bytes)
/// via the result parameter.
/// The result argument is optional unless buf is NULL.
pub extern fn napi_get_value_string_utf8(env: napi_env, value: napi_value, buf_ptr: [*c]u8, bufsize: usize, result_ptr: (*usize)?): napi_status ;
pub extern fn napi_get_value_string_utf16(env: napi_env, value_: napi_value, buf_ptr: [*]char16_t?, bufsize: usize, result_ptr: (*usize)?): napi_status ;
pub extern fn napi_coerce_to_bool(env: napi_env, value_: napi_value, result_: (*napi_value)?): napi_status ;
pub extern fn napi_coerce_to_number(env: napi_env, value_: napi_value, result_: (*napi_value)?): napi_status ;
pub extern fn napi_coerce_to_object(env: napi_env, value_: napi_value, result_: (*napi_value)?): napi_status ;
@export @c fn napi_get_prototype(env_: napi_env, object_: napi_value, result_: (*napi_value)?): napi_status {
    log("napi_get_prototype", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    const object = object_.get();
    if (object == .zero) {
        return env.invalidArg();
    }
    if (!object.isObject()) {
        return env.setLastError(.object_expected);
    }

    result.set(env, JSValue.c(jsc.C.JSObjectGetPrototype(env.toJS().ref(), object.asObjectRef())));
    return env.ok();
}
// TODO: bind JSC::ownKeys
// pub export fn napi_get_property_names(env: napi_env, object: napi_value, result: *napi_value) napi_status {
// log("napi_get_property_names     ", .{});
// if (!object.isObject()) {
//         return .object_expected;
//     }

//     result.* =
// }
pub extern fn napi_set_element(env_: napi_env, object_: napi_value, index: c_uint, value_: napi_value): napi_status ;
pub extern fn napi_has_element(env_: napi_env, object_: napi_value, index: c_uint, result_: (*bool)?): napi_status ;
pub extern fn napi_get_element(env: napi_env, object: napi_value, index: u32, result: *napi_value): napi_status ;
pub extern fn napi_delete_element(env: napi_env, object: napi_value, index: u32, result: *bool): napi_status ;
pub extern fn napi_define_properties(env: napi_env, object: napi_value, property_count: usize, properties: [*c]const napi_property_descriptor): napi_status ;
@export @c fn napi_is_array(env_: napi_env, value_: napi_value, result_: (*bool)?): napi_status {
    log("napi_is_array", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    const value = value_.get();
    if (value == .zero) {
        return env.invalidArg();
    }
    result.* = value.jsType().isArray();
    return env.ok();
}
@export @c fn napi_get_array_length(env_: napi_env, value_: napi_value, result_: [*c]u32): napi_status {
    log("napi_get_array_length", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    const value = value_.get();
    if (value == .zero) {
        return env.invalidArg();
    }

    if (!value.jsType().isArray()) {
        return env.setLastError(.array_expected);
    }

    result.* = @truncate(value.getLength(env.toJS()) catch return env.setLastError(.pending_exception));
    return env.ok();
}
@export @c fn napi_strict_equals(env_: napi_env, lhs_: napi_value, rhs_: napi_value, result_: (*bool)?): napi_status {
    log("napi_strict_equals", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    const lhs = lhs_.get();
    const rhs = rhs_.get();
    if (lhs == .zero || rhs == .zero) {
        return env.invalidArg();
    }
    result.* = lhs.isStrictEqual(rhs, env.toJS()) catch return env.setLastError(.pending_exception);
    return env.ok();
}
pub extern fn napi_call_function(env: napi_env, recv: napi_value, func: napi_value, argc: usize, argv: [*c]const napi_value, result: *napi_value): napi_status ;
pub extern fn napi_new_instance(env: napi_env, constructor: napi_value, argc: usize, argv: [*c]const napi_value, result_: (*napi_value)?): napi_status ;
pub extern fn napi_instanceof(env_: napi_env, object_: napi_value, constructor_: napi_value, result_: (*bool)?): napi_status ;
pub extern fn napi_get_cb_info(env: napi_env, cbinfo: napi_callback_info, argc: [*c]usize, argv: *napi_value, this_arg: *napi_value, data: [*]*!): napi_status ;
pub extern fn napi_get_new_target(env: napi_env, cbinfo: napi_callback_info, result: *napi_value): napi_status ;
pub extern fn napi_define_class(
    env: napi_env,
    utf8name: [*c]const u8,
    length: usize,
    constructor: napi_callback,
    data: *!?,
    property_count: usize,
    properties: [*c]const napi_property_descriptor,
    result: *napi_value,
): napi_status ;
pub extern fn napi_wrap(env: napi_env, js_object: napi_value, native_object: *!?, finalize_cb: napi_finalize, finalize_hint: *!?, result: *napi_ref): napi_status ;
pub extern fn napi_unwrap(env: napi_env, js_object: napi_value, result: [*]*!): napi_status ;
pub extern fn napi_remove_wrap(env: napi_env, js_object: napi_value, result: [*]*!): napi_status ;
pub extern fn napi_create_object(env: napi_env, result: *napi_value): napi_status ;
pub extern fn napi_create_external(env: napi_env, data: *!?, finalize_cb: napi_finalize, finalize_hint: *!?, result: *napi_value): napi_status ;
pub extern fn napi_get_value_external(env: napi_env, value: napi_value, result: [*]*!): napi_status ;
pub extern fn napi_create_reference(env: napi_env, value: napi_value, initial_refcount: u32, result: *napi_ref): napi_status ;
pub extern fn napi_delete_reference(env: napi_env, ref: napi_ref): napi_status ;
pub extern fn napi_reference_ref(env: napi_env, ref: napi_ref, result: [*c]u32): napi_status ;
pub extern fn napi_reference_unref(env: napi_env, ref: napi_ref, result: [*c]u32): napi_status ;
pub extern fn napi_get_reference_value(env: napi_env, ref: napi_ref, result: *napi_value): napi_status ;

@export @c fn napi_open_handle_scope(env_: napi_env, result_: (*napi_handle_scope)?): napi_status {
    log("napi_open_handle_scope", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.* = NapiHandleScope.open(env, false);
    return env.ok();
}

@export @c fn napi_close_handle_scope(env_: napi_env, handle_scope: napi_handle_scope): napi_status {
    log("napi_close_handle_scope", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (scope := handle_scope) {
        scope.close(env);
    }

    return env.ok();
}

// we don't support async contexts
@export @c fn napi_async_init(env_: napi_env, _: napi_value, _: napi_value, async_ctx: **!): napi_status {
    log("napi_async_init", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    unsafe {
        async_ctx.* = env;
    }
    return env.ok();
}

// we don't support async contexts
@export @c fn napi_async_destroy(env_: napi_env, _: *!): napi_status {
    log("napi_async_destroy", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    return env.ok();
}

// this is just a regular function call
@export @c fn napi_make_callback(env_: napi_env, _: *!, recv_: napi_value, func_: napi_value, arg_count: usize, args: [*]const napi_value?, maybe_result: (*napi_value)?): napi_status {
    log("napi_make_callback", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    const recv = recv_.get();
    const func = func_.get();
    if (func.isEmptyOrUndefinedOrNull() || (!func.isCallable() && !func.isAsyncContextFrame())) {
        return env.setLastError(.function_expected);
    }

    const call_args: []const jsc.JSValue = if (arg_count > 0 && args != null)
        unsafe { @as([*]const jsc.JSValue, @ptrCast(args.?))[0..arg_count] }
    else
        [];
    const res = func.call(
        env.toJS(),
        if (recv != .zero)
            recv
        else
            .js_undefined,
        call_args,
    ) catch e => // TODO: handle errors correctly
        env.toJS().takeException(e);

    if (result := maybe_result) {
        result.set(env, res);
    }

    // TODO: this is likely incorrect
    if (res.isAnyError()) {
        return env.setLastError(.pending_exception);
    }

    return env.ok();
}

// Sometimes shared libraries reference symbols which are not used
// We don't want to fail to load the library because of that
// so we instead return an error and warn the user
fn notImplementedYet(comptime name: []const u8) {
    bun.onceUnsafe(
        struct {
            pub fn warn() {
                if (jsc.VirtualMachine.get().log.level.atLeast(.warn)) {
                    bun.Output.prettyErrorln("<r><yellow>warning<r><d>:<r> Node-API function <b>\"{s}\"<r> is not implemented yet.\n Track the status of Node-API in Bun: https://github.com/oven-sh/bun/issues/158", .{name});
                    bun.Output.flush();
                }
            }
        }.warn,
        void,
    );
}

@export @c fn napi_open_escapable_handle_scope(env_: napi_env, result_: (*napi_escapable_handle_scope)?): napi_status {
    log("napi_open_escapable_handle_scope", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    result.* = NapiHandleScope.open(env, true);
    return env.ok();
}
@export @c fn napi_close_escapable_handle_scope(env_: napi_env, scope: napi_escapable_handle_scope): napi_status {
    log("napi_close_escapable_handle_scope", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (s := scope) {
        s.close(env);
    }
    return env.ok();
}
@export @c fn napi_escape_handle(env_: napi_env, scope_: napi_escapable_handle_scope, escapee: napi_value, result_: (*napi_value)?): napi_status {
    log("napi_escape_handle", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    if (scope_ == null) {
        return env.invalidArg();
    }
    const scope = scope_.?;
    scope.escape(escapee.get()) catch return env.setLastError(.escape_called_twice);
    result.* = escapee;
    return env.ok();
}
pub extern fn napi_type_tag_object(env: napi_env, _: napi_value, _: [*c]const napi_type_tag): napi_status ;
pub extern fn napi_check_object_type_tag(env: napi_env, _: napi_value, _: [*c]const napi_type_tag, _: *bool): napi_status ;

// do nothing for both of these
@export @c fn napi_open_callback_scope(_: napi_env, _: napi_value, _: *!, _: *!): napi_status {
    log("napi_open_callback_scope", {});
    return @intFromEnum(NapiStatus.ok);
}
@export @c fn napi_close_callback_scope(_: napi_env, _: *!): napi_status {
    log("napi_close_callback_scope", {});
    return @intFromEnum(NapiStatus.ok);
}
pub extern fn napi_throw(env: napi_env, @"error": napi_value): napi_status ;
pub extern fn napi_throw_error(env: napi_env, code: [*c]const u8, msg: [*c]const u8): napi_status ;
pub extern fn napi_throw_type_error(env: napi_env, code: [*c]const u8, msg: [*c]const u8): napi_status ;
pub extern fn napi_throw_range_error(env: napi_env, code: [*c]const u8, msg: [*c]const u8): napi_status ;
@export @c fn napi_is_error(env_: napi_env, value_: napi_value, result: *bool): napi_status {
    log("napi_is_error", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    const value = value_.get();
    if (value == .zero) {
        return env.invalidArg();
    }
    result.* = value.isAnyError();
    return env.ok();
}
pub extern fn napi_is_exception_pending(env: napi_env, result: *bool): napi_status ;
pub extern fn napi_get_and_clear_last_exception(env: napi_env, result: *napi_value): napi_status ;
@export @c fn napi_is_arraybuffer(env_: napi_env, value_: napi_value, result_: (*bool)?): napi_status {
    log("napi_is_arraybuffer", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    const value = value_.get();
    if (value == .zero) {
        return env.invalidArg();
    }
    result.* = !value.isNumber() && value.jsTypeLoose() == .ArrayBuffer;
    return env.ok();
}
pub extern fn napi_create_arraybuffer(env: napi_env, byte_length: usize, data: [*]const u8, result: *napi_value): napi_status ;

pub extern fn napi_create_external_arraybuffer(env: napi_env, external_data: *!?, byte_length: usize, finalize_cb: napi_finalize, finalize_hint: *!?, result: *napi_value): napi_status ;

@export @c fn napi_get_arraybuffer_info(env_: napi_env, arraybuffer_: napi_value, data: (*NullableU8MultiPtr)?, byte_length: (*usize)?): napi_status {
    log("napi_get_arraybuffer_info", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    const arraybuffer = arraybuffer_.get();
    const array_buffer_opt = arraybuffer.asArrayBuffer(env.toJS());
    if (array_buffer_opt == null) { return env.setLastError(.invalid_arg); }
    const array_buffer = array_buffer_opt.?;
    if (array_buffer.typed_array_type != .ArrayBuffer) {
        return env.setLastError(.invalid_arg);
    }

    if (dat := data)
        dat.* = array_buffer.ptr;
    if (len := byte_length)
        len.* = array_buffer.byte_len;
    return env.ok();
}

pub extern fn napi_is_typedarray(napi_env, napi_value, bool): napi_status ;

@export @c fn napi_get_typedarray_info(
    env_: napi_env,
    typedarray_: napi_value,
    maybe_type: (*napi_typedarray_type)?,
    maybe_length: (*usize)?,
    maybe_data: (*NullableU8MultiPtr)?,
    maybe_arraybuffer: (*napi_value)?,
    maybe_byte_offset: (*usize)?, // note: this is always 0
): napi_status {
    log("napi_get_typedarray_info", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    const typedarray = typedarray_.get();
    if (typedarray.isEmptyOrUndefinedOrNull())
        return env.invalidArg();
    defer typedarray.ensureStillAlive();

    const array_buffer_opt = typedarray.asArrayBuffer(env.toJS());
    if (array_buffer_opt == null) { return env.invalidArg(); }
    const array_buffer = array_buffer_opt.?;
    if (ta_type := maybe_type) {
        if (napi_ta := array_buffer.typed_array_type.toTypedArrayType().toNapi()) {
            ta_type.* = napi_ta;
        } else {
            return env.invalidArg();
        }
    }

    // TODO: handle detached
    if (data := maybe_data)
        data.* = array_buffer.ptr;

    if (length := maybe_length)
        length.* = array_buffer.len;

    if (arraybuffer := maybe_arraybuffer)
        arraybuffer.set(env, JSValue.c(jsc.C.JSObjectGetTypedArrayBuffer(env.toJS().ref(), typedarray.asObjectRef(), null)));

    if (byte_offset := maybe_byte_offset)
        // `jsc.ArrayBuffer` used to have an `offset` field, but it was always 0 because `ptr`
        // already had the offset applied. See <https://github.com/oven-sh/bun/issues/561>.
        //byte_offset.* = array_buffer.offset;
        byte_offset.* = 0;
    return env.ok();
}
pub extern fn napi_create_dataview(env: napi_env, length: usize, arraybuffer: napi_value, byte_offset: usize, result: *napi_value): napi_status ;
@export @c fn napi_is_dataview(env_: napi_env, value_: napi_value, result_: (*bool)?): napi_status {
    log("napi_is_dataview", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    const value = value_.get();
    if (value == .zero) {
        return env.invalidArg();
    }
    result.* = !value.isEmptyOrUndefinedOrNull() && value.jsTypeLoose() == .DataView;
    return env.ok();
}
@export @c fn napi_get_dataview_info(
    env_: napi_env,
    dataview_: napi_value,
    maybe_bytelength: (*usize)?,
    maybe_data: (*NullableU8MultiPtr)?,
    maybe_arraybuffer: (*napi_value)?,
    maybe_byte_offset: (*usize)?, // note: this is always 0
): napi_status {
    log("napi_get_dataview_info", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    const dataview = dataview_.get();
    if (dataview == .zero) {
        return env.invalidArg();
    }
    const array_buffer_opt = dataview.asArrayBuffer(env.toJS());
    if (array_buffer_opt == null) { return env.setLastError(.object_expected); }
    const array_buffer = array_buffer_opt.?;
    if (bytelength := maybe_bytelength)
        bytelength.* = array_buffer.byte_len;

    if (data := maybe_data)
        data.* = array_buffer.ptr;

    if (arraybuffer := maybe_arraybuffer)
        arraybuffer.set(env, JSValue.c(jsc.C.JSObjectGetTypedArrayBuffer(env.toJS().ref(), dataview.asObjectRef(), null)));

    if (byte_offset := maybe_byte_offset)
        // `jsc.ArrayBuffer` used to have an `offset` field, but it was always 0 because `ptr`
        // already had the offset applied. See <https://github.com/oven-sh/bun/issues/561>.
        //byte_offset.* = array_buffer.offset;
        byte_offset.* = 0;

    return env.ok();
}
@export @c fn napi_get_version(env_: napi_env, result_: (*u32)?): napi_status {
    log("napi_get_version", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    // The result is supposed to be the highest NAPI version Bun supports, rather than the version reported by a NAPI module.
    result.* = 9;
    return env.ok();
}
@export @c fn napi_create_promise(env_: napi_env, deferred_: (*napi_deferred)?, promise_: (*napi_value)?): napi_status {
    log("napi_create_promise", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (deferred_ == null) {
        return env.invalidArg();
    }
    const deferred = deferred_.?;
    if (promise_ == null) {
        return env.invalidArg();
    }
    const promise = promise_.?;
    deferred.* = bun.default_allocator.create(jsc.JSPromise.Strong) catch @panic("failed to allocate napi_deferred");
    deferred.*.* = jsc.JSPromise.Strong.init(env.toJS());
    promise.set(env, deferred.*.get().asValue(env.toJS()));
    return env.ok();
}
@export @c fn napi_resolve_deferred(env_: napi_env, deferred: napi_deferred, resolution_: napi_value): napi_status {
    log("napi_resolve_deferred", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    const _deferred_guard = DeferredGuard{ ptr: deferred };
    const resolution = resolution_.get();
    let prom = deferred.get();
    prom.resolve(env.toJS(), resolution) catch return env.setLastError(.pending_exception);
    return env.ok();
}
@export @c fn napi_reject_deferred(env_: napi_env, deferred: napi_deferred, rejection_: napi_value): napi_status {
    log("napi_reject_deferred", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    const _deferred_guard = DeferredGuard{ ptr: deferred };
    const rejection = rejection_.get();
    let prom = deferred.get();
    prom.reject(env.toJS(), rejection) catch return env.setLastError(.pending_exception);
    return env.ok();
}
@export @c fn napi_is_promise(env_: napi_env, value_: napi_value, is_promise_: (*bool)?): napi_status {
    log("napi_is_promise", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    const value = value_.get();
    if (is_promise_ == null) {
        return env.invalidArg();
    }
    const is_promise = is_promise_.?;

    if (value == .zero) {
        return env.invalidArg();
    }

    is_promise.* = value.asAnyPromise() != null;
    return env.ok();
}
pub extern fn napi_run_script(env: napi_env, script: napi_value, result: *napi_value): napi_status ;
pub extern fn napi_adjust_external_memory(env: napi_env, change_in_bytes: i64, adjusted_value: [*c]i64): napi_status ;
@export @c fn napi_create_date(env_: napi_env, time: f64, result_: (*napi_value)?): napi_status {
    log("napi_create_date", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    let args: [1]jsc.C.JSValueRef = [jsc.JSValue.jsNumber(time).asObjectRef()];
    result.set(env, JSValue.c(jsc.C.JSObjectMakeDate(env.toJS().ref(), 1, &args, TODO_EXCEPTION)));
    return env.ok();
}
@export @c fn napi_is_date(env_: napi_env, value_: napi_value, is_date_: (*bool)?): napi_status {
    log("napi_is_date", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    env.checkGC();
    if (is_date_ == null) {
        return env.invalidArg();
    }
    const is_date = is_date_.?;
    const value = value_.get();
    if (value == .zero) {
        return env.invalidArg();
    }
    is_date.* = value.jsTypeLoose() == .JSDate;
    return env.ok();
}
pub extern fn napi_get_date_value(env: napi_env, value: napi_value, result: *f64): napi_status ;
pub extern fn napi_add_finalizer(env: napi_env, js_object: napi_value, native_object: *!?, finalize_cb: napi_finalize, finalize_hint: *!?, result: napi_ref): napi_status ;
pub extern fn napi_create_bigint_int64(env: napi_env, value: i64, result_: (*napi_value)?): napi_status ;
pub extern fn napi_create_bigint_uint64(env: napi_env, value: u64, result_: (*napi_value)?): napi_status ;
pub extern fn napi_create_bigint_words(env: napi_env, sign_bit: c_int, word_count: usize, words: [*c]const u64, result: *napi_value): napi_status ;
pub extern fn napi_get_value_bigint_int64(_: napi_env, value_: napi_value, result_: (*i64)?, _: *bool): napi_status ;
pub extern fn napi_get_value_bigint_uint64(_: napi_env, value_: napi_value, result_: (*u64)?, _: *bool): napi_status ;

pub extern fn napi_get_value_bigint_words(env: napi_env, value: napi_value, sign_bit: [*c]c_int, word_count: [*c]usize, words: [*c]u64): napi_status ;
pub extern fn napi_get_all_property_names(env: napi_env, object: napi_value, key_mode: napi_key_collection_mode, key_filter: napi_key_filter, key_conversion: napi_key_conversion, result: *napi_value): napi_status ;
pub extern fn napi_set_instance_data(env: napi_env, data: *!?, finalize_cb: napi_finalize, finalize_hint: *!?): napi_status ;
pub extern fn napi_get_instance_data(env: napi_env, data: [*]*!): napi_status ;
pub extern fn napi_detach_arraybuffer(env: napi_env, arraybuffer: napi_value): napi_status ;
pub extern fn napi_is_detached_arraybuffer(env: napi_env, value: napi_value, result: *bool): napi_status ;

pub enum NapiAsyncWorkStatus: u32 {
    pending = 0,
    started = 1,
    completed = 2,
    cancelled = 3,
}

/// must be globally allocated
pub struct napi_async_work {
    task: WorkPoolTask = { callback: &runFromThreadPool },
    concurrent_task: jsc.ConcurrentTask = {},
    event_loop: *jsc.EventLoop,
    global: *jsc.JSGlobalObject,
    env: NapiEnv.Ref,
    execute: napi_async_execute_callback,
    complete: napi_async_complete_callback?,
    data: *!? = null,
    status: std.atomic.Value(Status) = .init(.pending),
    scheduled: bool = false,
    poll_ref: Async.KeepAlive = {},

    pub const Status = NapiAsyncWorkStatus;

    static fn new(env: *NapiEnv, execute: napi_async_execute_callback, complete: napi_async_complete_callback?, data: *!?): *napi_async_work {
        const global = env.toJS();

        const work = bun.new(napi_async_work, {
            global: global,
            env: .cloneFromRaw(env),
            execute: execute,
            event_loop: global.bunVM().eventLoop(),
            complete: complete,
            data: data,
        });
        return work;
    }

    fn destroy(*_) {
        _.env.deinit();
        bun.destroy(_);
    }

    fn schedule(*_) {
        if (_.scheduled) return;
        _.scheduled = true;
        _.poll_ref.ref(_.global.bunVM());
        WorkPool.schedule(&_.task);
    }

    static fn runFromThreadPool(task: *WorkPoolTask) {
        let work_ptr: *napi_async_work = @fieldParentPtr("task", task);
        work_ptr.run();
    }
    fn run(*_) {
        if (state := _.status.cmpxchgStrong(.pending, .started, .seq_cst, .seq_cst)) {
            if (state == .cancelled) {
                _.event_loop.enqueueTaskConcurrent(_.concurrent_task.@"from"(_, .manual_deinit));
                return;
            }
        }
        _.execute(_.env.get(), _.data);
        _.status.store(.completed, .seq_cst);

        _.event_loop.enqueueTaskConcurrent(_.concurrent_task.@"from"(_, .manual_deinit));
    }

    fn cancel(*_): bool {
        return _.status.cmpxchgStrong(.pending, .cancelled, .seq_cst, .seq_cst) == null;
    }

    fn runFromJS(*_, vm: *jsc.VirtualMachine, global: *jsc.JSGlobalObject) {
        // Note: the "this" value here may already be freed by the user in `complete`
        let poll_ref = _.poll_ref;
        const _poll_guard = PollRefGuard{ poll_ref: &poll_ref, vm: vm };

        // https://github.com/nodejs/node/blob/a2de5b9150da60c77144bb5333371eaca3fab936/src/node_api.cc#L1201
        if (_.complete == null) {
            return;
        }
        const complete = _.complete.?;

        const env = _.env.get();
        const handle_scope = NapiHandleScope.open(env, false);
        const _scope_guard = HandleScopeGuard{ scope: handle_scope, env: env };

        const status: NapiStatus = if (_.status.load(.seq_cst) == .cancelled)
            .cancelled
        else
            .ok;

        complete(
            env,
            @intFromEnum(status),
            _.data,
        );

        if (exception := env.getAndClearPendingException()) {
            * = vm.uncaughtException(global, exception, false);
        } else if (global.hasException()) {
            global.reportActiveExceptionAsUnhandled(error.JSError);
        }
    }
}
pub const napi_threadsafe_function = *ThreadSafeFunction;
pub enum napi_threadsafe_function_release_mode: c_uint {
    release = 0,
    abort = 1,
}
pub const napi_tsfn_nonblocking = 0;
pub const napi_tsfn_blocking = 1;
pub const napi_threadsafe_function_call_mode = c_uint;
pub type napi_async_execute_callback = @c fn (napi_env, *!?) void;
pub type napi_async_complete_callback = @c fn (napi_env, napi_status, *!?) void;
pub type napi_threadsafe_function_call_js = @c fn (napi_env, napi_value, *!?, *!?) void;
@c pub struct napi_node_version {
    major: u32,
    minor: u32,
    patch: u32,
    release: [*:0]const u8,

    const parsed_nodejs_version = std.SemanticVersion.parse(bun.Environment.reported_nodejs_version) catch @panic("Invalid reported Node.js version");

    pub const global: napi_node_version = {
        major: parsed_nodejs_version.major,
        minor: parsed_nodejs_version.minor,
        patch: parsed_nodejs_version.patch,
        release: "node",
    };
}
@opaque pub struct struct_napi_async_cleanup_hook_handle__ {}
pub type napi_async_cleanup_hook_handle = (*struct_napi_async_cleanup_hook_handle__)?;
pub type NapiAsyncCleanupHookFn = @c fn (napi_async_cleanup_hook_handle, *!?) void;
pub type napi_async_cleanup_hook = NapiAsyncCleanupHookFn?;

pub type napi_addon_register_func = @c fn (napi_env, napi_value) napi_value;
@c pub struct struct_napi_module {
    nm_version: c_int,
    nm_flags: c_uint,
    nm_filename: [*c]const u8,
    nm_register_func: napi_addon_register_func,
    nm_modname: [*c]const u8,
    nm_priv: *!?,
    reserved: [4]*!?,
}
pub const napi_module = struct_napi_module;
fn napiSpan(ptr: auto, len: usize): []const u8 {
    if (ptr == null)
        return "";

    if (len == NAPI_AUTO_LENGTH) {
        return bun.sliceTo(ptr.?, 0);
    }

    return ptr.?[0..len];
}
@export @c fn napi_fatal_error(location_ptr: [*:0]const u8?, location_len: usize, message_ptr: [*:0]const u8?, message_len_: usize): noreturn {
    log("napi_fatal_error", {});
    napi_internal_suppress_crash_on_abort_if_desired();
    let message = napiSpan(message_ptr, message_len_);
    if (message.len == 0) {
        message = "fatal error";
    }

    const location = napiSpan(location_ptr, location_len);
    if (location.len > 0) {
        bun.Output.panic("NAPI FATAL ERROR: {s} {s}", .{ location, message });
    }

    bun.Output.panic("napi: {s}", .{message});
}
pub extern fn napi_create_buffer(env: napi_env, length: usize, data: **!?, result: *napi_value): napi_status ;
pub extern fn napi_create_external_buffer(env: napi_env, length: usize, data: *!?, finalize_cb: napi_finalize, finalize_hint: *!?, result: *napi_value): napi_status ;
@export @c fn napi_create_buffer_copy(env_: napi_env, length: usize, data: [*]u8, result_data: (*(*!?))?, result_: (*napi_value)?): napi_status {
    log("napi_create_buffer_copy: {d}", .{length});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    let buffer = jsc.JSValue.createBufferFromLength(env.toJS(), length) catch return env.setLastError(.pending_exception);
    if (array_buf := buffer.asArrayBuffer(env.toJS())) {
        if (length > 0) {
            unsafe {
                @memcpy(array_buf.slice()[0..length], data[0..length]);
            }
        }
        if (ptr := result_data) {
            ptr.* = if (length > 0) array_buf.ptr else null;
        }
    }

    result.set(env, buffer);

    return env.ok();
}
extern fn napi_is_buffer(napi_env, napi_value, bool): napi_status ;
@export @c fn napi_get_buffer_info(env_: napi_env, value_: napi_value, data: (*NullableU8MultiPtr)?, length: (*usize)?): napi_status {
    log("napi_get_buffer_info", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    const value = value_.get();
    if (value.asArrayBuffer(env.toJS()) == null) {
        return env.setLastError(.invalid_arg);
    }
    const array_buf = value.asArrayBuffer(env.toJS()).?;

    if (dat := data)
        dat.* = array_buf.ptr;

    if (len := length)
        len.* = array_buf.byte_len;

    return env.ok();
}

extern fn node_api_create_syntax_error(a0: napi_env, a1: napi_value, a2: napi_value, a3: napi_value): napi_status ;
extern fn node_api_symbol_for(a0: napi_env, a1: [*]const c_char, a2: usize, a3: *napi_value): napi_status ;
extern fn node_api_throw_syntax_error(a0: napi_env, a1: [*]const c_char, a2: [*]const c_char): napi_status ;
extern fn node_api_create_external_string_latin1(a0: napi_env, a1: [*:0]u8, a2: usize, a3: napi_finalize, a4: *!?, a5: *JSValue, a6: *bool): napi_status ;
extern fn node_api_create_external_string_utf16(a0: napi_env, a1: [*:0]u16, a2: usize, a3: napi_finalize, a4: *!?, a5: *JSValue, a6: *bool): napi_status ;

@export @c fn napi_create_async_work(
    env_: napi_env,
    _: napi_value,
    _: [*:0]const u8,
    execute_: napi_async_execute_callback?,
    complete: napi_async_complete_callback?,
    data: *!?,
    result_: (*NapiAsyncWorkPtr)?,
): napi_status {
    log("napi_create_async_work", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    // https://github.com/nodejs/node/blob/a2de5b9150da60c77144bb5333371eaca3fab936/src/node_api.cc#L1245
    if (execute_ == null) {
        return env.invalidArg();
    }
    const execute = execute_.?;
    result.* = napi_async_work.new(env, execute, complete, data);
    return env.ok();
}
@export @c fn napi_delete_async_work(env_: napi_env, work_: (*napi_async_work)?): napi_status {
    log("napi_delete_async_work", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (*work := work_) {
        if (comptime bun.Environment.allow_assert) bun.assert(env.toJS() == work.global);
        work.destroy();
        return env.ok();
    }
    return env.invalidArg();
}
@export @c fn napi_queue_async_work(env_: napi_env, work_: (*napi_async_work)?): napi_status {
    log("napi_queue_async_work", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (*work := work_) {
        if (comptime bun.Environment.allow_assert) bun.assert(env.toJS() == work.global);
        work.schedule();
        return env.ok();
    }
    return env.invalidArg();
}
@export @c fn napi_cancel_async_work(env_: napi_env, work_: (*napi_async_work)?): napi_status {
    log("napi_cancel_async_work", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (*work := work_) {
        if (comptime bun.Environment.allow_assert) bun.assert(env.toJS() == work.global);
        if (work.cancel()) {
            return env.ok();
        }
        return env.genericFailure();
    }
    return env.invalidArg();
}
@export @c fn napi_get_node_version(env_: napi_env, version_: (*ConstNapiNodeVersionPtr)?): napi_status {
    log("napi_get_node_version", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (version_ == null) {
        return env.invalidArg();
    }
    const version = version_.?;
    version.* = &napi_node_version.global;
    return env.ok();
}
const napi_event_loop = if (bun.Environment.isWindows) *bun.windows.libuv.Loop else *jsc.EventLoop;
@export @c fn napi_get_uv_event_loop(env_: napi_env, loop_: (*napi_event_loop)?): napi_status {
    log("napi_get_uv_event_loop", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (loop_ == null) {
        return env.invalidArg();
    }
    const loop = loop_.?;
    if (bun.Environment.isWindows) {
        // alignment error is incorrect.
        // TODO(@190n) investigate
        @setRuntimeSafety(false);
        loop.* = jsc.VirtualMachine.get().uvLoop();
    } else {
        // there is no uv event loop on posix, we use our event loop handle.
        loop.* = env.toJS().bunVM().eventLoop();
    }
    return env.ok();
}
pub extern fn napi_fatal_exception(env: napi_env, e: napi_value): napi_status ;
pub extern fn napi_add_async_cleanup_hook(env: napi_env, function: napi_async_cleanup_hook, data: *!?, handle_out: (*napi_async_cleanup_hook_handle)?): napi_status ;
pub extern fn napi_add_env_cleanup_hook(env: napi_env, function: (*const fn (*!?) void)?, data: *!?): napi_status ;
pub extern fn napi_create_typedarray(env: napi_env, ta_type: napi_typedarray_type, length: usize, arraybuffer: napi_value, byte_offset: usize, result: (*napi_value)?): napi_status ;
pub extern fn napi_remove_async_cleanup_hook(handle: napi_async_cleanup_hook_handle): napi_status ;
pub extern fn napi_remove_env_cleanup_hook(env: napi_env, function: (*const fn (*!?) void)?, data: *!?): napi_status ;

extern fn napi_internal_cleanup_env_cpp(env: napi_env) ;
extern fn napi_internal_check_gc(env: napi_env) ;

fn napiInternalCleanupCallback(data: *!?) {
    if (data == null) return;
    unsafe {
        napi_internal_cleanup_env_cpp(@ptrCast(data));
    }
}

@export @c fn napi_internal_register_cleanup_zig(env_: napi_env) {
    const env = env_ ?? return;
    env.toJS().bunVM().rareData().pushCleanupHook(env.toJS(), env, napiInternalCleanupCallback);
}

@export @c fn napi_internal_suppress_crash_on_abort_if_desired() {
    if (bun.feature_flag.BUN_INTERNAL_SUPPRESS_CRASH_ON_NAPI_ABORT.get()) {
        bun.crash_handler.suppressReporting();
    }
}

extern fn napi_internal_remove_finalizer(env: napi_env, fun: napi_finalize, hint: *!?, data: *!?) ;

pub struct Finalizer {
    env: NapiEnv.Ref,
    fun: NapiFinalizeFunction,
    data: *!? = null,
    hint: *!? = null,

    fn run(*_) {
        const env = _.env.get();
        const handle_scope = NapiHandleScope.open(env, false);
        const _scope_guard = HandleScopeGuard{ scope: handle_scope, env: env };

        _.fun(env, _.data, _.hint);
        napi_internal_remove_finalizer(env, _.fun, _.hint, _.data);

        if (exception := env.toJS().tryTakeException()) {
            * = env.toJS().bunVM().uncaughtException(env.toJS(), exception, false);
        }

        if (exception := env.getAndClearPendingException()) {
            * = env.toJS().bunVM().uncaughtException(env.toJS(), exception, false);
        }
    }

    fn deinit(*_) {
        _.env.deinit();
        _.* = undefined;
    }

    /// Takes ownership of `this`.
    fn enqueue(*_) {
        NapiFinalizerTask.init(_.*).schedule();
        _.* = undefined;
    }
}

/// For Node-API modules not built with NAPI_EXPERIMENTAL, finalizers should be deferred to the
/// immediate task queue instead of run immediately. This lets finalizers perform allocations,
/// which they couldn't if they ran immediately while the garbage collector is still running.
@export @c fn napi_internal_enqueue_finalizer(env: napi_env, fun: napi_finalize, data: *!?, hint: *!?) {
    if (fun == null || env == null) {
        return;
    }
    let fin = Finalizer{
        fun: fun.?,
        env: NapiEnv.Ref.cloneFromRaw(env.?),
        data: data,
        hint: hint,
    };
    fin.enqueue();
}

pub struct ThreadSafeFunctionCallbackC {
    js: jsc.Strong.Optional,
    napi_threadsafe_function_call_js: napi_threadsafe_function_call_js,
}

pub union(enum) ThreadSafeFunctionCallback {
    js: jsc.Strong.Optional,
    c: ThreadSafeFunctionCallbackC,

    fn deinit(*_) {
        if (_.* == .js) {
            _.js.deinit();
        } else if (_.* == .c) {
            _.c.js.deinit();
        }
    }
}

pub struct ThreadSafeFunctionQueue {
    data: bun.LinearFifo(OpaquePtr, .Dynamic),

    /// This value will never change after initialization. Zero means the size is unlimited.
    max_queue_size: usize,

    count: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),

    static fn init(max_queue_size: usize, allocator: std.mem.Allocator): ThreadSafeFunctionQueue {
        return { data: bun.LinearFifo(OpaquePtr, .Dynamic).init(allocator), max_queue_size: max_queue_size };
    }

    fn deinit(*_) {
        _.data.deinit();
    }

    fn isBlocked(): bool {
        return _.max_queue_size > 0 && _.count.load(.seq_cst) >= _.max_queue_size;
    }
}

enum TSFNClosingState: u8 {
    not_closing,
    closing,
    closed,
}

pub enum TSFNDispatchState: u8 {
    idle,
    running,
    pending,

    const Atomic = std.atomic.Value(TSFNDispatchState);
}

// TODO: generate comptime version of this instead of runtime checking
pub struct ThreadSafeFunction {
    pub const Callback = ThreadSafeFunctionCallback;
    pub const Queue = ThreadSafeFunctionQueue;
    /// thread-safe functions can be "referenced" and "unreferenced". A
    /// "referenced" thread-safe function will cause the event loop on the thread
    /// on which it is created to remain alive until the thread-safe function is
    /// destroyed. In contrast, an "unreferenced" thread-safe function will not
    /// prevent the event loop from exiting. The APIs napi_ref_threadsafe_function
    /// and napi_unref_threadsafe_function exist for this purpose.
    ///
    /// Neither does napi_unref_threadsafe_function mark the thread-safe
    /// functions as able to be destroyed nor does napi_ref_threadsafe_function
    /// prevent it from being destroyed.
    poll_ref: Async.KeepAlive,

    // User implementation error can cause this number to go negative.
    thread_count: std.atomic.Value(i64) = std.atomic.Value(i64).init(0),
    // for std.condvar
    lock: std.Thread.Mutex = {},

    event_loop: *jsc.EventLoop,
    tracker: jsc.Debugger.AsyncTaskTracker,

    env: NapiEnv.Ref,
    finalizer_fun: napi_finalize = null,
    finalizer_data: *!? = null,

    has_queued_finalizer: bool = false,
    queue: Queue = {
        data: bun.LinearFifo(OpaquePtr, .Dynamic).init(bun.default_allocator),
        max_queue_size: 0,
    },

    ctx: *!? = null,

    callback: Callback = undefined,
    dispatch_state: DispatchState.Atomic = DispatchState.Atomic.init(.idle),
    blocking_condvar: std.Thread.Condition = {},
    closing: std.atomic.Value(ClosingState) = std.atomic.Value(ClosingState).init(.not_closing),
    aborted: std.atomic.Value(bool) = std.atomic.Value(bool).init(true),

    pub const new = bun.TrivialNew(ThreadSafeFunction);

    const ClosingState = TSFNClosingState;

    pub const DispatchState = TSFNDispatchState;

    // This has two states:
    // 1. We need to run potentially multiple tasks.
    // 2. We need to finalize the ThreadSafeFunction.
    fn onDispatch(*_) {
        if (_.closing.load(.seq_cst) == .closed) {
            // Finalize the ThreadSafeFunction.
            _.deinit();
            return;
        }

        let is_first = true;

        // Run the tasks.
        while (true) {
            _.dispatch_state.store(.running, .seq_cst);
            if (_.dispatchOne(is_first)) {
                is_first = false;
                _.dispatch_state.store(.pending, .seq_cst);
            } else {
                // We're done running tasks, for now.
                _.dispatch_state.store(.idle, .seq_cst);
                break;
            }
        }

        // Node sets a maximum number of runs per ThreadSafeFunction to 1,000.
        // We don't set a max. I would like to see an issue caused by not
        // setting a max before we do set a max. It is better for performance to
        // not add unnecessary event loop ticks.
    }

    fn isClosing(): bool {
        return _.closing.load(.seq_cst) != .not_closing;
    }

    fn maybeQueueFinalizer(*_) {
        switch (_.closing.swap(.closed, .seq_cst)) {
            .closing, .not_closing => {
                // TODO: is this boolean necessary? Can we rely just on the closing value?
                if (!_.has_queued_finalizer) {
                    _.has_queued_finalizer = true;
                    _.callback.deinit();
                    _.poll_ref.disable();
                    _.event_loop.enqueueTask(jsc.Task.init(_));
                }
            },
            .closed => {
                // already scheduled.
            },
        }
    }

    fn dispatchOne(*_, is_first: bool): bool {
        let queue_finalizer_after_call = false;
        let task: *!? = null;
        {
            _.lock.lock();
            const lock_ptr = unsafe { &_.lock };
            const _lock_guard = UnlockGuard<@TypeOf(lock_ptr.*)>{ ptr: lock_ptr };
            const was_blocked = _.queue.isBlocked();
            const t_opt = _.queue.data.readItem();
            if (t_opt == null) {
                // When there are no tasks and the number of threads that have
                // references reaches zero, we prepare to finalize the
                // ThreadSafeFunction.
                if (_.thread_count.load(.seq_cst) == 0) {
                    if (_.queue.max_queue_size > 0) {
                        _.blocking_condvar.signal();
                    }
                    _.maybeQueueFinalizer();
                }
                return false;
            }
            const t = t_opt.?;

            if (_.queue.count.fetchSub(1, .seq_cst) == 1 && _.thread_count.load(.seq_cst) == 0) {
                _.closing.store(.closing, .seq_cst);
                if (_.queue.max_queue_size > 0) {
                    _.blocking_condvar.signal();
                }
                queue_finalizer_after_call = true;
            } else if (was_blocked && !_.queue.isBlocked()) {
                _.blocking_condvar.signal();
            }

            task = t;
        }

        _.call(task, !is_first) catch return false;

        if (queue_finalizer_after_call) {
            _.maybeQueueFinalizer();
        }

        // An item was dequeued: keep onDispatch looping so remaining queued
        // items drain and the empty-queue thread_count==0 path can finalize.
        return true;
    }

    /// This function can be called multiple times in one tick of the event loop.
    /// See: https://github.com/nodejs/node/pull/38506
    /// In that case, we need to drain microtasks.
    fn call(*_, task: *!?, is_first: bool): bun.JSTerminated!void {
        const env = _.env.get();
        if (!is_first) {
            _.event_loop.drainMicrotasks()?;
        }
        const globalObject = env.toJS();

        _.tracker.willDispatch(globalObject);
        defer _.tracker.didDispatch(globalObject);

        switch (_.callback) {
            .js strong => {
                const js: JSValue = strong.get() ?? .js_undefined;
                if (js.isEmptyOrUndefinedOrNull()) {
                    return;
                }

                * = js.call(globalObject, .js_undefined, []) catch e =>
                    globalObject.reportActiveExceptionAsUnhandled(e);
            },
            .c cb => {
                const js: JSValue = cb.js.get() ?? .js_undefined;

                const handle_scope = NapiHandleScope.open(env, false);
                const _scope_guard = HandleScopeGuard{ scope: handle_scope, env: env };
                cb.napi_threadsafe_function_call_js(env, napi_value.create(env, js), _.ctx, task);
            },
        }
    }

    fn enqueue(*_, ctx: *!?, block: bool): napi_status {
        _.lock.lock();
        const lock_ptr = unsafe { &_.lock };
        const _lock_guard = UnlockGuard<@TypeOf(lock_ptr.*)>{ ptr: lock_ptr };
        if (block) {
            while (_.queue.isBlocked() && !_.isClosing()) {
                _.blocking_condvar.wait(&_.lock);
            }
        } else {
            if (_.queue.isBlocked()) {
                // don't set the error on the env as this is run from another thread
                return @intFromEnum(NapiStatus.queue_full);
            }
        }

        if (_.isClosing()) {
            if (_.thread_count.load(.seq_cst) <= 0) {
                return @intFromEnum(NapiStatus.invalid_arg);
            }
            const release_fn = ThreadSafeFunction.release;
            * = release_fn(_, .release, true);
            return @intFromEnum(NapiStatus.closing);
        }

        * = _.queue.count.fetchAdd(1, .seq_cst);
        bun.handleOom(_.queue.data.writeItem(ctx));
        _.scheduleDispatch();
        return @intFromEnum(NapiStatus.ok);
    }

    fn scheduleDispatch(*_) {
        switch (_.dispatch_state.swap(.pending, .seq_cst)) {
            .idle => {
                _.event_loop.enqueueTaskConcurrent(jsc.ConcurrentTask.createFrom(_));
            },
            .running => {
                // it will check if it has more work to do
            },
            .pending => {
                // we've already scheduled it to run
            },
        }
    }

    fn deinit(*_) {
        _.unref();

        if (fun := _.finalizer_fun) {
            let finalizer: Finalizer = {
                env: _.env,
                fun: fun,
                data: _.finalizer_data,
                hint: _.ctx,
            };
            finalizer.enqueue();
        } else {
            _.env.deinit();
        }

        _.callback.deinit();
        _.queue.deinit();
        bun.destroy(_);
    }

    fn ref(*_) {
        _.poll_ref.refConcurrentlyFromEventLoop(_.event_loop);
    }

    fn unref(*_) {
        _.poll_ref.unrefConcurrentlyFromEventLoop(_.event_loop);
    }

    fn acquire(*_): napi_status {
        _.lock.lock();
        const lock_ptr = unsafe { &_.lock };
        const _lock_guard = UnlockGuard<@TypeOf(lock_ptr.*)>{ ptr: lock_ptr };
        if (_.isClosing()) {
            return @intFromEnum(NapiStatus.closing);
        }
        * = _.thread_count.fetchAdd(1, .seq_cst);
        return @intFromEnum(NapiStatus.ok);
    }

    fn release(*_, mode: napi_threadsafe_function_release_mode, already_locked: bool): napi_status {
        if (!already_locked) _.lock.lock();
        const lock_ptr_type = unsafe { &_.lock };
        const lock_ptr = if (!already_locked) lock_ptr_type else null;
        const _lock_guard = UnlockGuard<@TypeOf(lock_ptr_type.*)>{ ptr: lock_ptr };

        if (_.thread_count.load(.seq_cst) < 0) {
            return @intFromEnum(NapiStatus.invalid_arg);
        }

        const prev_remaining = _.thread_count.fetchSub(1, .seq_cst);

        if (mode == .abort || prev_remaining == 1) {
            if (!_.isClosing()) {
                if (mode == .abort) {
                    _.closing.store(.closing, .seq_cst);
                    _.aborted.store(true, .seq_cst);
                    if (_.queue.max_queue_size > 0) {
                        // Wake all producers blocked in enqueue()'s bounded
                        // queue wait so they observe isClosing and release.
                        _.blocking_condvar.broadcast();
                    }
                }
                _.scheduleDispatch();
            } else if (prev_remaining == 1) {
                // Already closing from an earlier abort. The last release must
                // still reach dispatchOne's thread_count==0 path so the
                // finalizer runs and the event-loop keepalive is dropped.
                _.scheduleDispatch();
            }
        }

        return @intFromEnum(NapiStatus.ok);
    }
}

@export @c fn napi_create_threadsafe_function(
    env_: napi_env,
    func_: napi_value,
    _: napi_value, // async_resource
    _: napi_value, // async_resource_name
    max_queue_size: usize,
    initial_thread_count: usize,
    thread_finalize_data: *!?,
    thread_finalize_cb: napi_finalize,
    context: *!?,
    call_js_cb: napi_threadsafe_function_call_js?,
    result_: (*napi_threadsafe_function)?,
): napi_status {
    log("napi_create_threadsafe_function", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    if (result_ == null) {
        return env.invalidArg();
    }
    const result = result_.?;
    const func = func_.get();

    if (call_js_cb == null && (func.isEmptyOrUndefinedOrNull() || (!func.isCallable() && !func.isAsyncContextFrame()))) {
        return env.setLastError(.function_expected);
    }

    const vm = env.toJS().bunVM();
    let cb_val: ThreadSafeFunction.Callback = {
        js: if (func == .zero) .empty else jsc.Strong.Optional.create(func.withAsyncContextIfNeeded(env.toJS()), vm.global),
    };
    if (c := call_js_cb) {
        cb_val = {
            c: {
                napi_threadsafe_function_call_js: c,
                js: if (func == .zero) .empty else jsc.Strong.Optional.create(func.withAsyncContextIfNeeded(env.toJS()), vm.global),
            },
        };
    }
    result.* = ThreadSafeFunction.new({
        event_loop: vm.eventLoop(),
        env: NapiEnv.Ref.cloneFromRaw(env),
        callback: cb_val,
        ctx: context,
        queue: ThreadSafeFunction.Queue.init(max_queue_size, bun.default_allocator),
        thread_count: { raw: i32(initial_thread_count) },
        poll_ref: Async.KeepAlive.init(),
        tracker: jsc.Debugger.AsyncTaskTracker.init(vm),
        finalizer_fun: thread_finalize_cb,
        finalizer_data: thread_finalize_data,
    });

    // nodejs by default keeps the event loop alive until the thread-safe function is unref'd
    result.*.ref();
    result.*.tracker.didSchedule(vm.global);
    return env.ok();
}
@export @c fn napi_get_threadsafe_function_context(func: napi_threadsafe_function, result: *OpaquePtr): napi_status {
    log("napi_get_threadsafe_function_context", {});
    result.* = func.ctx;
    return @intFromEnum(NapiStatus.ok);
}
@export @c fn napi_call_threadsafe_function(func: napi_threadsafe_function, data: *!?, is_blocking: napi_threadsafe_function_call_mode): napi_status {
    log("napi_call_threadsafe_function", {});
    return func.enqueue(data, is_blocking == napi_tsfn_blocking);
}
@export @c fn napi_acquire_threadsafe_function(func: napi_threadsafe_function): napi_status {
    log("napi_acquire_threadsafe_function", {});
    return func.acquire();
}
@export @c fn napi_release_threadsafe_function(func: napi_threadsafe_function, mode: napi_threadsafe_function_release_mode): napi_status {
    log("napi_release_threadsafe_function", {});
    const release_fn = ThreadSafeFunction.release;
    return release_fn(func, mode, false);
}
@export @c fn napi_unref_threadsafe_function(env_: napi_env, func: napi_threadsafe_function): napi_status {
    log("napi_unref_threadsafe_function", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    bun.assert(func.event_loop.global == env.toJS());
    func.unref();
    return env.ok();
}
@export @c fn napi_ref_threadsafe_function(env_: napi_env, func: napi_threadsafe_function): napi_status {
    log("napi_ref_threadsafe_function", {});
    if (env_ == null) {
        return envIsNull();
    }
    const env = env_.?;
    bun.assert(func.event_loop.global == env.toJS());
    func.ref();
    return env.ok();
}

const NAPI_AUTO_LENGTH = std.math.maxInt(usize);

/// v8:: C++ symbols defined in v8.cpp
///
/// Do not call these at runtime, as they do not contain type and callconv info. They are simply
/// used for DCE suppression and asserting that the symbols exist at link-time.
///
// TODO: write a script to generate this struct. ideally it wouldn't even need to be committed to source.
const V8API = if (!bun.Environment.isWindows) struct {
    pub extern fn _ZN2v87Isolate10GetCurrentEv(): *! ;
    pub extern fn _ZN2v87Isolate13TryGetCurrentEv(): *! ;
    pub extern fn _ZN2v87Isolate17GetCurrentContextEv(): *! ;
    pub extern fn _ZN4node25AddEnvironmentCleanupHookEPN2v87IsolateEPFvPvES3_(): *! ;
    pub extern fn _ZN4node28RemoveEnvironmentCleanupHookEPN2v87IsolateEPFvPvES3_(): *! ;
    pub extern fn _ZN2v86Number3NewEPNS_7IsolateEd(): *! ;
    pub extern fn _ZNK2v86Number5ValueEv(): *! ;
    pub extern fn _ZN2v86String11NewFromUtf8EPNS_7IsolateEPKcNS_13NewStringTypeEi(): *! ;
    pub extern fn _ZNK2v86String9WriteUtf8EPNS_7IsolateEPciPii(): *! ;
    pub extern fn _ZN2v812api_internal12ToLocalEmptyEv(): *! ;
    pub extern fn _ZNK2v86String6LengthEv(): *! ;
    pub extern fn _ZN2v88External3NewEPNS_7IsolateEPv(): *! ;
    pub extern fn _ZNK2v88External5ValueEv(): *! ;
    pub extern fn _ZN2v86Object3NewEPNS_7IsolateE(): *! ;
    pub extern fn _ZN2v86Object3SetENS_5LocalINS_7ContextEEENS1_INS_5ValueEEES5_(): *! ;
    pub extern fn _ZN2v86Object3SetENS_5LocalINS_7ContextEEEjNS1_INS_5ValueEEE(): *! ;
    pub extern fn _ZN2v86Object16SetInternalFieldEiNS_5LocalINS_4DataEEE(): *! ;
    pub extern fn _ZN2v86Object20SlowGetInternalFieldEi(): *! ;
    pub extern fn _ZN2v86Object3GetENS_5LocalINS_7ContextEEENS1_INS_5ValueEEE(): *! ;
    pub extern fn _ZN2v86Object3GetENS_5LocalINS_7ContextEEEj(): *! ;
    pub extern fn _ZN2v811HandleScope12CreateHandleEPNS_8internal7IsolateEm(): *! ;
    pub extern fn _ZN2v811HandleScopeC1EPNS_7IsolateE(): *! ;
    pub extern fn _ZN2v811HandleScopeD1Ev(): *! ;
    pub extern fn _ZN2v811HandleScopeD2Ev(): *! ;
    pub extern fn _ZN2v816FunctionTemplate11GetFunctionENS_5LocalINS_7ContextEEE(): *! ;
    pub extern fn _ZN2v816FunctionTemplate3NewEPNS_7IsolateEPFvRKNS_20FunctionCallbackInfoINS_5ValueEEEENS_5LocalIS4_EENSA_INS_9SignatureEEEiNS_19ConstructorBehaviorENS_14SideEffectTypeEPKNS_9CFunctionEttt(): *! ;
    pub extern fn _ZN2v814ObjectTemplate11NewInstanceENS_5LocalINS_7ContextEEE(): *! ;
    pub extern fn _ZN2v814ObjectTemplate21SetInternalFieldCountEi(): *! ;
    pub extern fn _ZNK2v814ObjectTemplate18InternalFieldCountEv(): *! ;
    pub extern fn _ZN2v814ObjectTemplate3NewEPNS_7IsolateENS_5LocalINS_16FunctionTemplateEEE(): *! ;
    pub extern fn _ZN2v824EscapableHandleScopeBase10EscapeSlotEPm(): *! ;
    pub extern fn _ZN2v824EscapableHandleScopeBaseC2EPNS_7IsolateE(): *! ;
    pub extern fn _ZN2v88internal35IsolateFromNeverReadOnlySpaceObjectEm(): *! ;
    pub extern fn _ZN2v85Array3NewEPNS_7IsolateEPNS_5LocalINS_5ValueEEEm(): *! ;
    pub extern fn _ZNK2v85Array6LengthEv(): *! ;
    pub extern fn _ZN2v85Array3NewEPNS_7IsolateEi(): *! ;
    pub extern fn _ZN2v85Array7IterateENS_5LocalINS_7ContextEEEPFNS0_14CallbackResultEjNS1_INS_5ValueEEEPvES7_(): *! ;
    pub extern fn _ZN2v85Array9CheckCastEPNS_5ValueE(): *! ;
    pub extern fn _ZN2v88Function7SetNameENS_5LocalINS_6StringEEE(): *! ;
    pub extern fn _ZNK2v85Value9IsBooleanEv(): *! ;
    pub extern fn _ZNK2v87Boolean5ValueEv(): *! ;
    pub extern fn _ZNK2v85Value10FullIsTrueEv(): *! ;
    pub extern fn _ZNK2v85Value11FullIsFalseEv(): *! ;
    pub extern fn _ZN2v820EscapableHandleScopeC1EPNS_7IsolateE(): *! ;
    pub extern fn _ZN2v820EscapableHandleScopeC2EPNS_7IsolateE(): *! ;
    pub extern fn _ZN2v820EscapableHandleScopeD1Ev(): *! ;
    pub extern fn _ZN2v820EscapableHandleScopeD2Ev(): *! ;
    pub extern fn _ZNK2v85Value8IsObjectEv(): *! ;
    pub extern fn _ZNK2v85Value8IsNumberEv(): *! ;
    pub extern fn _ZNK2v85Value8IsUint32Ev(): *! ;
    pub extern fn _ZNK2v85Value11Uint32ValueENS_5LocalINS_7ContextEEE(): *! ;
    pub extern fn _ZNK2v85Value11IsUndefinedEv(): *! ;
    pub extern fn _ZNK2v85Value6IsNullEv(): *! ;
    pub extern fn _ZNK2v85Value17IsNullOrUndefinedEv(): *! ;
    pub extern fn _ZNK2v85Value6IsTrueEv(): *! ;
    pub extern fn _ZNK2v85Value7IsFalseEv(): *! ;
    pub extern fn _ZNK2v85Value8IsStringEv(): *! ;
    pub extern fn _ZNK2v85Value12StrictEqualsENS_5LocalIS0_EE(): *! ;
    pub extern fn _ZN2v87Boolean3NewEPNS_7IsolateEb(): *! ;
    pub extern fn _ZN2v86Object16GetInternalFieldEi(): *! ;
    pub extern fn _ZN2v87Context10GetIsolateEv(): *! ;
    pub extern fn _ZN2v86String14NewFromOneByteEPNS_7IsolateEPKhNS_13NewStringTypeEi(): *! ;
    pub extern fn _ZNK2v86String10Utf8LengthEPNS_7IsolateE(): *! ;
    pub extern fn _ZNK2v86String10IsExternalEv(): *! ;
    pub extern fn _ZNK2v86String17IsExternalOneByteEv(): *! ;
    pub extern fn _ZNK2v86String17IsExternalTwoByteEv(): *! ;
    pub extern fn _ZNK2v86String9IsOneByteEv(): *! ;
    pub extern fn _ZNK2v86String19ContainsOnlyOneByteEv(): *! ;
    pub extern fn _ZN2v812api_internal18GlobalizeReferenceEPNS_8internal7IsolateEm(): *! ;
    pub extern fn _ZN2v812api_internal13DisposeGlobalEPm(): *! ;
    pub extern fn _ZN2v812api_internal23GetFunctionTemplateDataEPNS_7IsolateENS_5LocalINS_4DataEEE(): *! ;
    pub extern fn _ZNK2v88Function7GetNameEv(): *! ;
    pub extern fn _ZNK2v85Value10IsFunctionEv(): *! ;
    pub extern fn _ZNK2v85Value5IsMapEv(): *! ;
    pub extern fn _ZNK2v85Value7IsArrayEv(): *! ;
    pub extern fn _ZNK2v85Value7IsInt32Ev(): *! ;
    pub extern fn _ZNK2v85Value8IsBigIntEv(): *! ;
    pub extern fn _ZN2v812api_internal17FromJustIsNothingEv(): *! ;
    pub extern fn uv_os_getpid(): *! ;
    pub extern fn uv_os_getppid(): *! ;
} else struct {
    // MSVC name mangling is different than it is on unix.
    // To make this easier to deal with, I have provided a script to generate the list of functions.
    //
    // dumpbin .\build\CMakeFiles\bun-debug.dir\src\bun.js\bindings\v8\*.cpp.obj /symbols | where-object { $_.Contains(' node::') -or $_.Contains(' v8::') } | foreach-object { (($_ -split "\|")[1] -split " ")[1] } | ForEach-Object { "extern fn @`"${_}`"() *!;" }
    //
    // Bug @paperclover if you get stuck here
    pub extern fn @"?TryGetCurrent@Isolate@v8@@SAPEAV12@XZ"(): *!;
    pub extern fn @"?GetCurrent@Isolate@v8@@SAPEAV12@XZ"(): *!;
    pub extern fn @"?GetCurrentContext@Isolate@v8@@QEAA?AV?$Local@VContext@v8@@@2@XZ"(): *!;
    pub extern fn @"?AddEnvironmentCleanupHook@node@@YAXPEAVIsolate@v8@@P6AXPEAX@Z1@Z"(): *!;
    pub extern fn @"?RemoveEnvironmentCleanupHook@node@@YAXPEAVIsolate@v8@@P6AXPEAX@Z1@Z"(): *!;
    pub extern fn @"?New@Number@v8@@SA?AV?$Local@VNumber@v8@@@2@PEAVIsolate@2@N@Z"(): *!;
    pub extern fn @"?Value@Number@v8@@QEBANXZ"(): *!;
    pub extern fn @"?NewFromUtf8@String@v8@@SA?AV?$MaybeLocal@VString@v8@@@2@PEAVIsolate@2@PEBDW4NewStringType@2@H@Z"(): *!;
    pub extern fn @"?WriteUtf8@String@v8@@QEBAHPEAVIsolate@2@PEADHPEAHH@Z"(): *!;
    pub extern fn @"?ToLocalEmpty@api_internal@v8@@YAXXZ"(): *!;
    pub extern fn @"?Length@String@v8@@QEBAHXZ"(): *!;
    pub extern fn @"?New@External@v8@@SA?AV?$Local@VExternal@v8@@@2@PEAVIsolate@2@PEAX@Z"(): *!;
    pub extern fn @"?Value@External@v8@@QEBAPEAXXZ"(): *!;
    pub extern fn @"?New@Object@v8@@SA?AV?$Local@VObject@v8@@@2@PEAVIsolate@2@@Z"(): *!;
    pub extern fn @"?Set@Object@v8@@QEAA?AV?$Maybe@_N@2@V?$Local@VContext@v8@@@2@V?$Local@VValue@v8@@@2@1@Z"(): *!;
    pub extern fn @"?Set@Object@v8@@QEAA?AV?$Maybe@_N@2@V?$Local@VContext@v8@@@2@IV?$Local@VValue@v8@@@2@@Z"(): *!;
    pub extern fn @"?SetInternalField@Object@v8@@QEAAXHV?$Local@VData@v8@@@2@@Z"(): *!;
    pub extern fn @"?SlowGetInternalField@Object@v8@@AEAA?AV?$Local@VData@v8@@@2@H@Z"(): *!;
    pub extern fn @"?Get@Object@v8@@QEAA?AV?$MaybeLocal@VValue@v8@@@2@V?$Local@VContext@v8@@@2@I@Z"(): *!;
    pub extern fn @"?Get@Object@v8@@QEAA?AV?$MaybeLocal@VValue@v8@@@2@V?$Local@VContext@v8@@@2@V?$Local@VValue@v8@@@2@@Z"(): *!;
    pub extern fn @"?CreateHandle@HandleScope@v8@@KAPEA_KPEAVIsolate@internal@2@_K@Z"(): *!;
    pub extern fn @"??0HandleScope@v8@@QEAA@PEAVIsolate@1@@Z"(): *!;
    pub extern fn @"??1HandleScope@v8@@QEAA@XZ"(): *!;
    pub extern fn @"?GetFunction@FunctionTemplate@v8@@QEAA?AV?$MaybeLocal@VFunction@v8@@@2@V?$Local@VContext@v8@@@2@@Z"(): *!;
    pub extern fn @"?New@FunctionTemplate@v8@@SA?AV?$Local@VFunctionTemplate@v8@@@2@PEAVIsolate@2@P6AXAEBV?$FunctionCallbackInfo@VValue@v8@@@2@@ZV?$Local@VValue@v8@@@2@V?$Local@VSignature@v8@@@2@HW4ConstructorBehavior@2@W4SideEffectType@2@PEBVCFunction@2@GGG@Z"(): *!;
    pub extern fn @"?NewInstance@ObjectTemplate@v8@@QEAA?AV?$MaybeLocal@VObject@v8@@@2@V?$Local@VContext@v8@@@2@@Z"(): *!;
    pub extern fn @"?SetInternalFieldCount@ObjectTemplate@v8@@QEAAXH@Z"(): *!;
    pub extern fn @"?InternalFieldCount@ObjectTemplate@v8@@QEBAHXZ"(): *!;
    pub extern fn @"?New@ObjectTemplate@v8@@SA?AV?$Local@VObjectTemplate@v8@@@2@PEAVIsolate@2@V?$Local@VFunctionTemplate@v8@@@2@@Z"(): *!;
    pub extern fn @"?EscapeSlot@EscapableHandleScopeBase@v8@@IEAAPEA_KPEA_K@Z"(): *!;
    pub extern fn @"??0EscapableHandleScopeBase@v8@@QEAA@PEAVIsolate@1@@Z"(): *!;
    pub extern fn @"?IsolateFromNeverReadOnlySpaceObject@internal@v8@@YAPEAVIsolate@12@_K@Z"(): *!;
    pub extern fn @"?New@Array@v8@@SA?AV?$Local@VArray@v8@@@2@PEAVIsolate@2@PEAV?$Local@VValue@v8@@@2@_K@Z"(): *!;
    pub extern fn @"?Length@Array@v8@@QEBAIXZ"(): *!;
    pub extern fn @"?New@Array@v8@@SA?AV?$Local@VArray@v8@@@2@PEAVIsolate@2@H@Z"(): *!;
    pub extern fn @"?New@Array@v8@@SA?AV?$MaybeLocal@VArray@v8@@@2@V?$Local@VContext@v8@@@2@_KV?$function@$$A6A?AV?$MaybeLocal@VValue@v8@@@v8@@XZ@std@@@Z"(): *!;
    pub extern fn @"?Iterate@Array@v8@@QEAA?AV?$Maybe@X@2@V?$Local@VContext@v8@@@2@P6A?AW4CallbackResult@12@IV?$Local@VValue@v8@@@2@PEAX@Z2@Z"(): *!;
    pub extern fn @"?CheckCast@Array@v8@@CAXPEAVValue@2@@Z"(): *!;
    pub extern fn @"?SetName@Function@v8@@QEAAXV?$Local@VString@v8@@@2@@Z"(): *!;
    pub extern fn @"?IsBoolean@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?Value@Boolean@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?FullIsTrue@Value@v8@@AEBA_NXZ"(): *!;
    pub extern fn @"?FullIsFalse@Value@v8@@AEBA_NXZ"(): *!;
    pub extern fn @"??1EscapableHandleScope@v8@@QEAA@XZ"(): *!;
    pub extern fn @"??0EscapableHandleScope@v8@@QEAA@PEAVIsolate@1@@Z"(): *!;
    pub extern fn @"?IsObject@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsNumber@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsUint32@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?Uint32Value@Value@v8@@QEBA?AV?$Maybe@I@2@V?$Local@VContext@v8@@@2@@Z"(): *!;
    pub extern fn @"?IsUndefined@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsNull@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsNullOrUndefined@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsTrue@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsFalse@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsString@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?StrictEquals@Value@v8@@QEBA_NV?$Local@VValue@v8@@@2@@Z"(): *!;
    pub extern fn @"?New@Boolean@v8@@SA?AV?$Local@VBoolean@v8@@@2@PEAVIsolate@2@_N@Z"(): *!;
    pub extern fn @"?GetInternalField@Object@v8@@QEAA?AV?$Local@VData@v8@@@2@H@Z"(): *!;
    pub extern fn @"?GetIsolate@Context@v8@@QEAAPEAVIsolate@2@XZ"(): *!;
    pub extern fn @"?NewFromOneByte@String@v8@@SA?AV?$MaybeLocal@VString@v8@@@2@PEAVIsolate@2@PEBEW4NewStringType@2@H@Z"(): *!;
    pub extern fn @"?IsExternal@String@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsExternalOneByte@String@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsExternalTwoByte@String@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsOneByte@String@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?Utf8Length@String@v8@@QEBAHPEAVIsolate@2@@Z"(): *!;
    pub extern fn @"?ContainsOnlyOneByte@String@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?GlobalizeReference@api_internal@v8@@YAPEA_KPEAVIsolate@internal@2@_K@Z"(): *!;
    pub extern fn @"?DisposeGlobal@api_internal@v8@@YAXPEA_K@Z"(): *!;
    pub extern fn @"?GetFunctionTemplateData@api_internal@v8@@YA?AV?$Local@VValue@v8@@@2@PEAVIsolate@2@V?$Local@VData@v8@@@2@@Z"(): *!;
    pub extern fn @"?GetName@Function@v8@@QEBA?AV?$Local@VValue@v8@@@2@XZ"(): *!;
    pub extern fn @"?IsFunction@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsMap@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsArray@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsInt32@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?IsBigInt@Value@v8@@QEBA_NXZ"(): *!;
    pub extern fn @"?FromJustIsNothing@api_internal@v8@@YAXXZ"(): *!;
};

/// V8 API functions whose mangled name differs by C++ stdlib namespace:
/// libstdc++ = std::, Apple libc++ = std::__1::, NDK libc++ = std::__ndk1::.
const posix_platform_specific_v8_apis = if (bun.Environment.os == .windows) struct {} else if (bun.Environment.isAndroid) struct {
    pub extern fn _ZN2v85Array3NewENS_5LocalINS_7ContextEEEmNSt6__ndk18functionIFNS_10MaybeLocalINS_5ValueEEEvEEE(): *! ;
} else if (bun.Environment.isMac || bun.Environment.isFreeBSD) struct {
    // FreeBSD's base libc++ uses the same `std::__1::` inline namespace as Apple's.
    pub extern fn _ZN2v85Array3NewENS_5LocalINS_7ContextEEEmNSt3__18functionIFNS_10MaybeLocalINS_5ValueEEEvEEE(): *! ;
} else struct {
    pub extern fn _ZN2v85Array3NewENS_5LocalINS_7ContextEEEmSt8functionIFNS_10MaybeLocalINS_5ValueEEEvEE(): *! ;
};

// To update this list, use find + multi-cursor in your editor.
// - pub extern fn napi_
// - pub export fn napi_
const napi_functions_to_export = .{
    napi_acquire_threadsafe_function,
    napi_add_async_cleanup_hook,
    napi_add_env_cleanup_hook,
    napi_add_finalizer,
    napi_adjust_external_memory,
    napi_async_destroy,
    napi_async_init,
    napi_call_function,
    napi_call_threadsafe_function,
    napi_cancel_async_work,
    napi_check_object_type_tag,
    napi_close_callback_scope,
    napi_close_escapable_handle_scope,
    napi_close_handle_scope,
    napi_coerce_to_bool,
    napi_coerce_to_number,
    napi_coerce_to_object,
    napi_create_array,
    napi_create_array_with_length,
    napi_create_arraybuffer,
    napi_create_async_work,
    napi_create_bigint_int64,
    napi_create_bigint_uint64,
    napi_create_bigint_words,
    napi_create_buffer,
    napi_create_buffer_copy,
    napi_create_dataview,
    napi_create_date,
    napi_create_double,
    napi_create_error,
    napi_create_external,
    napi_create_external_arraybuffer,
    napi_create_external_buffer,
    napi_create_int32,
    napi_create_int64,
    napi_create_object,
    napi_create_promise,
    napi_create_range_error,
    napi_create_reference,
    napi_create_string_latin1,
    napi_create_string_utf16,
    napi_create_string_utf8,
    napi_create_symbol,
    napi_create_threadsafe_function,
    napi_create_type_error,
    napi_create_typedarray,
    napi_create_uint32,
    napi_define_class,
    napi_define_properties,
    napi_delete_async_work,
    napi_delete_element,
    napi_delete_reference,
    napi_detach_arraybuffer,
    napi_escape_handle,
    napi_fatal_error,
    napi_fatal_exception,
    napi_get_all_property_names,
    napi_get_and_clear_last_exception,
    napi_get_array_length,
    napi_get_arraybuffer_info,
    napi_get_boolean,
    napi_get_buffer_info,
    napi_get_cb_info,
    napi_get_dataview_info,
    napi_get_date_value,
    napi_get_element,
    napi_get_global,
    napi_get_instance_data,
    napi_get_last_error_info,
    napi_get_new_target,
    napi_get_node_version,
    napi_get_null,
    napi_get_prototype,
    napi_get_reference_value,
    napi_get_threadsafe_function_context,
    napi_get_typedarray_info,
    napi_get_undefined,
    napi_get_uv_event_loop,
    napi_get_value_bigint_int64,
    napi_get_value_bigint_uint64,
    napi_get_value_bigint_words,
    napi_get_value_bool,
    napi_get_value_double,
    napi_get_value_external,
    napi_get_value_int32,
    napi_get_value_int64,
    napi_get_value_string_latin1,
    napi_get_value_string_utf16,
    napi_get_value_string_utf8,
    napi_get_value_uint32,
    napi_get_version,
    napi_has_element,
    napi_instanceof,
    napi_is_array,
    napi_is_arraybuffer,
    napi_is_buffer,
    napi_is_dataview,
    napi_is_date,
    napi_is_detached_arraybuffer,
    napi_is_error,
    napi_is_exception_pending,
    napi_is_promise,
    napi_is_typedarray,
    napi_make_callback,
    napi_new_instance,
    napi_open_callback_scope,
    napi_open_escapable_handle_scope,
    napi_open_handle_scope,
    napi_queue_async_work,
    napi_ref_threadsafe_function,
    napi_reference_ref,
    napi_reference_unref,
    napi_reject_deferred,
    napi_release_threadsafe_function,
    napi_remove_async_cleanup_hook,
    napi_remove_env_cleanup_hook,
    napi_remove_wrap,
    napi_resolve_deferred,
    napi_run_script,
    napi_set_element,
    napi_set_instance_data,
    napi_strict_equals,
    napi_throw,
    napi_throw_error,
    napi_throw_range_error,
    napi_throw_type_error,
    napi_type_tag_object,
    napi_typeof,
    napi_unref_threadsafe_function,
    napi_unwrap,
    napi_wrap,

    // -- node-api
    node_api_create_syntax_error,
    node_api_symbol_for,
    node_api_throw_syntax_error,
    node_api_create_external_string_latin1,
    node_api_create_external_string_utf16,
};

const uv_functions_to_export = if (bun.Environment.isPosix) struct {
    pub extern fn uv_accept();
    pub extern fn uv_async_init();
    pub extern fn uv_async_send();
    pub extern fn uv_available_parallelism();
    pub extern fn uv_backend_fd();
    pub extern fn uv_backend_timeout();
    pub extern fn uv_barrier_destroy();
    pub extern fn uv_barrier_init();
    pub extern fn uv_barrier_wait();
    pub extern fn uv_buf_init();
    pub extern fn uv_cancel();
    pub extern fn uv_chdir();
    pub extern fn uv_check_init();
    pub extern fn uv_check_start();
    pub extern fn uv_check_stop();
    pub extern fn uv_clock_gettime();
    pub extern fn uv_close();
    pub extern fn uv_cond_broadcast();
    pub extern fn uv_cond_destroy();
    pub extern fn uv_cond_init();
    pub extern fn uv_cond_signal();
    pub extern fn uv_cond_timedwait();
    pub extern fn uv_cond_wait();
    pub extern fn uv_cpu_info();
    pub extern fn uv_cpumask_size();
    pub extern fn uv_cwd();
    pub extern fn uv_default_loop();
    pub extern fn uv_disable_stdio_inheritance();
    pub extern fn uv_dlclose();
    pub extern fn uv_dlerror();
    pub extern fn uv_dlopen();
    pub extern fn uv_dlsym();
    pub extern fn uv_err_name();
    pub extern fn uv_err_name_r();
    pub extern fn uv_exepath();
    pub extern fn uv_fileno();
    pub extern fn uv_free_cpu_info();
    pub extern fn uv_free_interface_addresses();
    pub extern fn uv_freeaddrinfo();
    pub extern fn uv_fs_access();
    pub extern fn uv_fs_chmod();
    pub extern fn uv_fs_chown();
    pub extern fn uv_fs_close();
    pub extern fn uv_fs_closedir();
    pub extern fn uv_fs_copyfile();
    pub extern fn uv_fs_event_getpath();
    pub extern fn uv_fs_event_init();
    pub extern fn uv_fs_event_start();
    pub extern fn uv_fs_event_stop();
    pub extern fn uv_fs_fchmod();
    pub extern fn uv_fs_fchown();
    pub extern fn uv_fs_fdatasync();
    pub extern fn uv_fs_fstat();
    pub extern fn uv_fs_fsync();
    pub extern fn uv_fs_ftruncate();
    pub extern fn uv_fs_futime();
    pub extern fn uv_fs_get_path();
    pub extern fn uv_fs_get_ptr();
    pub extern fn uv_fs_get_result();
    pub extern fn uv_fs_get_statbuf();
    pub extern fn uv_fs_get_system_error();
    pub extern fn uv_fs_get_type();
    pub extern fn uv_fs_lchown();
    pub extern fn uv_fs_link();
    pub extern fn uv_fs_lstat();
    pub extern fn uv_fs_lutime();
    pub extern fn uv_fs_mkdir();
    pub extern fn uv_fs_mkdtemp();
    pub extern fn uv_fs_mkstemp();
    pub extern fn uv_fs_open();
    pub extern fn uv_fs_opendir();
    pub extern fn uv_fs_poll_getpath();
    pub extern fn uv_fs_poll_init();
    pub extern fn uv_fs_poll_start();
    pub extern fn uv_fs_poll_stop();
    pub extern fn uv_fs_read();
    pub extern fn uv_fs_readdir();
    pub extern fn uv_fs_readlink();
    pub extern fn uv_fs_realpath();
    pub extern fn uv_fs_rename();
    pub extern fn uv_fs_req_cleanup();
    pub extern fn uv_fs_rmdir();
    pub extern fn uv_fs_scandir();
    pub extern fn uv_fs_scandir_next();
    pub extern fn uv_fs_sendfile();
    pub extern fn uv_fs_stat();
    pub extern fn uv_fs_statfs();
    pub extern fn uv_fs_symlink();
    pub extern fn uv_fs_unlink();
    pub extern fn uv_fs_utime();
    pub extern fn uv_fs_write();
    pub extern fn uv_get_available_memory();
    pub extern fn uv_get_constrained_memory();
    pub extern fn uv_get_free_memory();
    pub extern fn uv_get_osfhandle();
    pub extern fn uv_get_process_title();
    pub extern fn uv_get_total_memory();
    pub extern fn uv_getaddrinfo();
    pub extern fn uv_getnameinfo();
    pub extern fn uv_getrusage();
    pub extern fn uv_getrusage_thread();
    pub extern fn uv_gettimeofday();
    pub extern fn uv_guess_handle();
    pub extern fn uv_handle_get_data();
    pub extern fn uv_handle_get_loop();
    pub extern fn uv_handle_get_type();
    pub extern fn uv_handle_set_data();
    pub extern fn uv_handle_size();
    pub extern fn uv_handle_type_name();
    pub extern fn uv_has_ref();
    pub extern fn uv_hrtime();
    pub extern fn uv_idle_init();
    pub extern fn uv_idle_start();
    pub extern fn uv_idle_stop();
    pub extern fn uv_if_indextoiid();
    pub extern fn uv_if_indextoname();
    pub extern fn uv_inet_ntop();
    pub extern fn uv_inet_pton();
    pub extern fn uv_interface_addresses();
    pub extern fn uv_ip_name();
    pub extern fn uv_ip4_addr();
    pub extern fn uv_ip4_name();
    pub extern fn uv_ip6_addr();
    pub extern fn uv_ip6_name();
    pub extern fn uv_is_active();
    pub extern fn uv_is_closing();
    pub extern fn uv_is_readable();
    pub extern fn uv_is_writable();
    pub extern fn uv_key_create();
    pub extern fn uv_key_delete();
    pub extern fn uv_key_get();
    pub extern fn uv_key_set();
    pub extern fn uv_kill();
    pub extern fn uv_library_shutdown();
    pub extern fn uv_listen();
    pub extern fn uv_loadavg();
    pub extern fn uv_loop_alive();
    pub extern fn uv_loop_close();
    pub extern fn uv_loop_configure();
    pub extern fn uv_loop_delete();
    pub extern fn uv_loop_fork();
    pub extern fn uv_loop_get_data();
    pub extern fn uv_loop_init();
    pub extern fn uv_loop_new();
    pub extern fn uv_loop_set_data();
    pub extern fn uv_loop_size();
    pub extern fn uv_metrics_idle_time();
    pub extern fn uv_metrics_info();
    pub extern fn uv_mutex_destroy();
    pub extern fn uv_mutex_init();
    pub extern fn uv_mutex_init_recursive();
    pub extern fn uv_mutex_lock();
    pub extern fn uv_mutex_trylock();
    pub extern fn uv_mutex_unlock();
    pub extern fn uv_now();
    pub extern fn uv_once();
    pub extern fn uv_open_osfhandle();
    pub extern fn uv_os_environ();
    pub extern fn uv_os_free_environ();
    pub extern fn uv_os_free_group();
    pub extern fn uv_os_free_passwd();
    pub extern fn uv_os_get_group();
    pub extern fn uv_os_get_passwd();
    pub extern fn uv_os_get_passwd2();
    pub extern fn uv_os_getenv();
    pub extern fn uv_os_gethostname();
    pub extern fn uv_os_getpid();
    pub extern fn uv_os_getppid();
    pub extern fn uv_os_getpriority();
    pub extern fn uv_os_homedir();
    pub extern fn uv_os_setenv();
    pub extern fn uv_os_setpriority();
    pub extern fn uv_os_tmpdir();
    pub extern fn uv_os_uname();
    pub extern fn uv_os_unsetenv();
    pub extern fn uv_pipe();
    pub extern fn uv_pipe_bind();
    pub extern fn uv_pipe_bind2();
    pub extern fn uv_pipe_chmod();
    pub extern fn uv_pipe_connect();
    pub extern fn uv_pipe_connect2();
    pub extern fn uv_pipe_getpeername();
    pub extern fn uv_pipe_getsockname();
    pub extern fn uv_pipe_init();
    pub extern fn uv_pipe_open();
    pub extern fn uv_pipe_pending_count();
    pub extern fn uv_pipe_pending_instances();
    pub extern fn uv_pipe_pending_type();
    pub extern fn uv_poll_init();
    pub extern fn uv_poll_init_socket();
    pub extern fn uv_poll_start();
    pub extern fn uv_poll_stop();
    pub extern fn uv_prepare_init();
    pub extern fn uv_prepare_start();
    pub extern fn uv_prepare_stop();
    pub extern fn uv_print_active_handles();
    pub extern fn uv_print_all_handles();
    pub extern fn uv_process_get_pid();
    pub extern fn uv_process_kill();
    pub extern fn uv_queue_work();
    pub extern fn uv_random();
    pub extern fn uv_read_start();
    pub extern fn uv_read_stop();
    pub extern fn uv_recv_buffer_size();
    pub extern fn uv_ref();
    pub extern fn uv_replace_allocator();
    pub extern fn uv_req_get_data();
    pub extern fn uv_req_get_type();
    pub extern fn uv_req_set_data();
    pub extern fn uv_req_size();
    pub extern fn uv_req_type_name();
    pub extern fn uv_resident_set_memory();
    pub extern fn uv_run();
    pub extern fn uv_rwlock_destroy();
    pub extern fn uv_rwlock_init();
    pub extern fn uv_rwlock_rdlock();
    pub extern fn uv_rwlock_rdunlock();
    pub extern fn uv_rwlock_tryrdlock();
    pub extern fn uv_rwlock_trywrlock();
    pub extern fn uv_rwlock_wrlock();
    pub extern fn uv_rwlock_wrunlock();
    pub extern fn uv_sem_destroy();
    pub extern fn uv_sem_init();
    pub extern fn uv_sem_post();
    pub extern fn uv_sem_trywait();
    pub extern fn uv_sem_wait();
    pub extern fn uv_send_buffer_size();
    pub extern fn uv_set_process_title();
    pub extern fn uv_setup_args();
    pub extern fn uv_shutdown();
    pub extern fn uv_signal_init();
    pub extern fn uv_signal_start();
    pub extern fn uv_signal_start_oneshot();
    pub extern fn uv_signal_stop();
    pub extern fn uv_sleep();
    pub extern fn uv_socketpair();
    pub extern fn uv_spawn();
    pub extern fn uv_stop();
    pub extern fn uv_stream_get_write_queue_size();
    pub extern fn uv_stream_set_blocking();
    pub extern fn uv_strerror();
    pub extern fn uv_strerror_r();
    pub extern fn uv_tcp_bind();
    pub extern fn uv_tcp_close_reset();
    pub extern fn uv_tcp_connect();
    pub extern fn uv_tcp_getpeername();
    pub extern fn uv_tcp_getsockname();
    pub extern fn uv_tcp_init();
    pub extern fn uv_tcp_init_ex();
    pub extern fn uv_tcp_keepalive();
    pub extern fn uv_tcp_nodelay();
    pub extern fn uv_tcp_open();
    pub extern fn uv_tcp_simultaneous_accepts();
    pub extern fn uv_thread_create();
    pub extern fn uv_thread_create_ex();
    pub extern fn uv_thread_detach();
    pub extern fn uv_thread_equal();
    pub extern fn uv_thread_getaffinity();
    pub extern fn uv_thread_getcpu();
    pub extern fn uv_thread_getname();
    pub extern fn uv_thread_getpriority();
    pub extern fn uv_thread_join();
    pub extern fn uv_thread_self();
    pub extern fn uv_thread_setaffinity();
    pub extern fn uv_thread_setname();
    pub extern fn uv_thread_setpriority();
    pub extern fn uv_timer_again();
    pub extern fn uv_timer_get_due_in();
    pub extern fn uv_timer_get_repeat();
    pub extern fn uv_timer_init();
    pub extern fn uv_timer_set_repeat();
    pub extern fn uv_timer_start();
    pub extern fn uv_timer_stop();
    pub extern fn uv_translate_sys_error();
    pub extern fn uv_try_write();
    pub extern fn uv_try_write2();
    pub extern fn uv_tty_get_vterm_state();
    pub extern fn uv_tty_get_winsize();
    pub extern fn uv_tty_init();
    pub extern fn uv_tty_reset_mode();
    pub extern fn uv_tty_set_mode();
    pub extern fn uv_tty_set_vterm_state();
    pub extern fn uv_udp_bind();
    pub extern fn uv_udp_connect();
    pub extern fn uv_udp_get_send_queue_count();
    pub extern fn uv_udp_get_send_queue_size();
    pub extern fn uv_udp_getpeername();
    pub extern fn uv_udp_getsockname();
    pub extern fn uv_udp_init();
    pub extern fn uv_udp_init_ex();
    pub extern fn uv_udp_open();
    pub extern fn uv_udp_recv_start();
    pub extern fn uv_udp_recv_stop();
    pub extern fn uv_udp_send();
    pub extern fn uv_udp_set_broadcast();
    pub extern fn uv_udp_set_membership();
    pub extern fn uv_udp_set_multicast_interface();
    pub extern fn uv_udp_set_multicast_loop();
    pub extern fn uv_udp_set_multicast_ttl();
    pub extern fn uv_udp_set_source_membership();
    pub extern fn uv_udp_set_ttl();
    pub extern fn uv_udp_try_send();
    pub extern fn uv_udp_try_send2();
    pub extern fn uv_udp_using_recvmmsg();
    pub extern fn uv_unref();
    pub extern fn uv_update_time();
    pub extern fn uv_uptime();
    pub extern fn uv_utf16_length_as_wtf8();
    pub extern fn uv_utf16_to_wtf8();
    pub extern fn uv_version();
    pub extern fn uv_version_string();
    pub extern fn uv_walk();
    pub extern fn uv_write();
    pub extern fn uv_write2();
    pub extern fn uv_wtf8_length_as_utf16();
    pub extern fn uv_wtf8_to_utf16();
} else struct {};

pub fn fixDeadCodeElimination() {
    jsc.markBinding(@src());

    inline for (fn_name of napi_functions_to_export) {
        std.mem.doNotOptimizeAway(&fn_name);
    }

    inline for (decl of comptime std.@"meta".declarations(uv_functions_to_export)) {
        std.mem.doNotOptimizeAway(&@field(uv_functions_to_export, decl.name));
    }

    inline for (decl of comptime std.@"meta".declarations(V8API)) {
        std.mem.doNotOptimizeAway(&@field(V8API, decl.name));
    }

    inline for (decl of comptime std.@"meta".declarations(posix_platform_specific_v8_apis)) {
        std.mem.doNotOptimizeAway(&@field(posix_platform_specific_v8_apis, decl.name));
    }

    std.mem.doNotOptimizeAway(&BufferMod.BufferVectorized.fill);
}

pub struct NapiFinalizerTask {
    finalizer: Finalizer,

    const AnyTask = jsc.AnyTask.New(@This(), runOnJSThread);

    static fn init(finalizer: Finalizer): *NapiFinalizerTask {
        const finalizer_task = bun.handleOom(bun.default_allocator.create(NapiFinalizerTask));
        finalizer_task.* = {
            finalizer: finalizer,
        };
        return finalizer_task;
    }

    fn schedule(*_) {
        const globalThis = _.finalizer.env.get().toJS();

        const vm_pair = globalThis.tryBunVM();
        const vm = vm_pair[0];
        const thread_kind = vm_pair[1];

        if (thread_kind != .main) {
            // TODO(@heimskr): do we need to handle the case where the vm is shutting down?
            vm.eventLoop().enqueueTaskConcurrent(jsc.ConcurrentTask.create(jsc.Task.init(_)));
            return;
        }

        if (vm.isShuttingDown()) {
            // Immediate tasks won't run, so we run this as a cleanup hook instead
            vm.rareData().pushCleanupHook(vm.global, _, runAsCleanupHook);
        } else {
            globalThis.bunVM().event_loop.enqueueTask(jsc.Task.init(_));
        }
    }

    fn deinit(*_) {
        _.finalizer.deinit();
        bun.default_allocator.destroy(_);
    }

    fn runOnJSThread(*_) {
        _.finalizer.run();
        _.deinit();
    }

    static fn runAsCleanupHook(opaque_this: *!?) {
        const ptr = opaque_this ?? return;
        unsafe {
            const self_task: *NapiFinalizerTask = @ptrCast(@alignCast(ptr));
            self_task.runOnJSThread();
        }
    }
}


const WorkPool = WorkPoolMod.WorkPool;
const WorkPoolTask = WorkPoolMod.Task;

import bun from "bun";
const Async = bun.Async;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
