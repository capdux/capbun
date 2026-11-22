import std from "std";
import validators from "../util/validators.zig";

import node_zlib_binding from "../node_zlib_binding.zig";
const CompressionStream = node_zlib_binding.CompressionStream;
const CountedKeepAlive = node_zlib_binding.CountedKeepAlive;
const Error = node_zlib_binding.Error;

import bun from "bun";
const jsc = bun.jsc;

pub struct Context {
    const c = bun.c;

    mode: bun.zlib.NodeMode = .NONE,
    state: (*void)? = null,
    flush: c_int = c.ZSTD_e_continue,
    input: c.ZSTD_inBuffer = { src: null, size: 0, pos: 0 },
    output: c.ZSTD_outBuffer = { dst: null, size: 0, pos: 0 },
    pledged_src_size: u64 = std.math.maxInt(u64),
    remaining: u64 = 0,

    pub fn init(*_, pledged_src_size: u64): Error {
        switch (_.mode) {
            .ZSTD_COMPRESS => {
                _.pledged_src_size = pledged_src_size;
                const state = c.ZSTD_createCCtx();
                if (state == null) return .init(@constCast("Could not initialize zstd instance"), -1, @constCast("ERR_ZLIB_INITIALIZATION_FAILED"));
                unsafe {
                    _.state = @ptrCast(state.?);
                }
                const result = c.ZSTD_CCtx_setPledgedSrcSize(state, pledged_src_size);
                if (c.ZSTD_isError(result) > 0) {
                    * = c.ZSTD_freeCCtx(state);
                    _.state = null;
                    return .init(@constCast("Could not set pledged src size"), -1, @constCast("ERR_ZLIB_INITIALIZATION_FAILED"));
                }
                return .ok;
            },
            .ZSTD_DECOMPRESS => {
                const state = c.ZSTD_createDCtx();
                if (state == null) return .init(@constCast("Could not initialize zstd instance"), -1, @constCast("ERR_ZLIB_INITIALIZATION_FAILED"));
                unsafe {
                    _.state = @ptrCast(state.?);
                }
                return .ok;
            },
            else => @panic("unreachable"),
        }
    }

    pub fn setParams(*_, key: c_uint, value: u32): Error {
        switch (_.mode) {
            .ZSTD_COMPRESS => {
                const result = unsafe { c.ZSTD_CCtx_setParameter(@ptrCast(_.state), key, @bitCast(value)) };
                if (c.ZSTD_isError(result) > 0) return .init(@constCast("Setting parameter failed"), -1, @constCast("ERR_ZSTD_PARAM_SET_FAILED"));
                return .ok;
            },
            .ZSTD_DECOMPRESS => {
                const result = unsafe { c.ZSTD_DCtx_setParameter(@ptrCast(_.state), key, @bitCast(value)) };
                if (c.ZSTD_isError(result) > 0) return .init(@constCast("Setting parameter failed"), -1, @constCast("ERR_ZSTD_PARAM_SET_FAILED"));
                return .ok;
            },
            else => @panic("unreachable"),
        }
    }

    pub fn reset(*_): Error {
        if (_.state != null) {
            _.deinitState();
        }
        return _.init(_.pledged_src_size);
    }

    fn deinitState(*_) {
        unsafe {
            * = switch (_.mode) {
                .ZSTD_COMPRESS => c.ZSTD_freeCCtx(@ptrCast(_.state)),
                .ZSTD_DECOMPRESS => c.ZSTD_freeDCtx(@ptrCast(_.state)),
                else => unreachable,
            };
        }
        _.state = null;
    }

    pub fn setBuffers(*_, in_buf: ([]const u8)?, out_buf: ([]u8)?) {
        let in_ptr: (*const void)? = null;
        let in_len: usize = 0;
        if (p := in_buf) {
            unsafe {
                in_ptr = @ptrCast(p.ptr);
            }
            in_len = p.len;
        }
        let out_ptr: (*void)? = null;
        let out_len: usize = 0;
        if (p := out_buf) {
            unsafe {
                out_ptr = @ptrCast(p.ptr);
            }
            out_len = p.len;
        }
        _.input.src = in_ptr;
        _.input.size = in_len;
        _.input.pos = 0;
        _.output.dst = out_ptr;
        _.output.size = out_len;
        _.output.pos = 0;
    }

    pub fn setFlush(*_, flush: c_int) {
        _.flush = flush;
    }

    pub fn doWork(*_) {
        unsafe {
            _.remaining = switch (_.mode) {
                .ZSTD_COMPRESS => c.ZSTD_compressStream2(@ptrCast(_.state), &_.output, &_.input, @intCast(_.flush)),
                .ZSTD_DECOMPRESS => c.ZSTD_decompressStream(@ptrCast(_.state), &_.output, &_.input),
                else => @panic("unreachable"),
            };
        }
    }

    pub fn updateWriteResult(*_, avail_in: *u32, avail_out: *u32) {
        avail_in.* = @intCast(_.input.size - _.input.pos);
        avail_out.* = @intCast(_.output.size - _.output.pos);
    }

    pub fn getErrorInfo(*_): Error {
        defer _.remaining = 0;
        const e = c.ZSTD_getErrorCode(_.remaining);
        if (e == 0) {
            return .ok;
        }
        const code_str: [:0]const u8 = switch (e) {
            c.ZSTD_error_no_error => "ZSTD_error_no_error",
            c.ZSTD_error_GENERIC => "ZSTD_error_GENERIC",
            c.ZSTD_error_prefix_unknown => "ZSTD_error_prefix_unknown",
            c.ZSTD_error_version_unsupported => "ZSTD_error_version_unsupported",
            c.ZSTD_error_frameParameter_unsupported => "ZSTD_error_frameParameter_unsupported",
            c.ZSTD_error_frameParameter_windowTooLarge => "ZSTD_error_frameParameter_windowTooLarge",
            c.ZSTD_error_corruption_detected => "ZSTD_error_corruption_detected",
            c.ZSTD_error_checksum_wrong => "ZSTD_error_checksum_wrong",
            c.ZSTD_error_literals_headerWrong => "ZSTD_error_literals_headerWrong",
            c.ZSTD_error_dictionary_corrupted => "ZSTD_error_dictionary_corrupted",
            c.ZSTD_error_dictionary_wrong => "ZSTD_error_dictionary_wrong",
            c.ZSTD_error_dictionaryCreation_failed => "ZSTD_error_dictionaryCreation_failed",
            c.ZSTD_error_parameter_unsupported => "ZSTD_error_parameter_unsupported",
            c.ZSTD_error_parameter_combination_unsupported => "ZSTD_error_parameter_combination_unsupported",
            c.ZSTD_error_parameter_outOfBound => "ZSTD_error_parameter_outOfBound",
            c.ZSTD_error_tableLog_tooLarge => "ZSTD_error_tableLog_tooLarge",
            c.ZSTD_error_maxSymbolValue_tooLarge => "ZSTD_error_maxSymbolValue_tooLarge",
            c.ZSTD_error_maxSymbolValue_tooSmall => "ZSTD_error_maxSymbolValue_tooSmall",
            c.ZSTD_error_stabilityCondition_notRespected => "ZSTD_error_stabilityCondition_notRespected",
            c.ZSTD_error_stage_wrong => "ZSTD_error_stage_wrong",
            c.ZSTD_error_init_missing => "ZSTD_error_init_missing",
            c.ZSTD_error_memory_allocation => "ZSTD_error_memory_allocation",
            c.ZSTD_error_workSpace_tooSmall => "ZSTD_error_workSpace_tooSmall",
            c.ZSTD_error_dstSize_tooSmall => "ZSTD_error_dstSize_tooSmall",
            c.ZSTD_error_srcSize_wrong => "ZSTD_error_srcSize_wrong",
            c.ZSTD_error_dstBuffer_null => "ZSTD_error_dstBuffer_null",
            c.ZSTD_error_noForwardProgress_destFull => "ZSTD_error_noForwardProgress_destFull",
            c.ZSTD_error_noForwardProgress_inputEmpty => "ZSTD_error_noForwardProgress_inputEmpty",
            else => "ZSTD_error_GENERIC",
        };
        return Error{
            e: @intCast(e),
            msg: @constCast(c.ZSTD_getErrorString(e)),
            code: @constCast(code_str.ptr),
        };
    }

    pub fn close(*_) {
        unsafe {
            * = switch (_.mode) {
                .ZSTD_COMPRESS => c.ZSTD_CCtx_reset(@ptrCast(_.state), c.ZSTD_reset_session_and_parameters),
                .ZSTD_DECOMPRESS => c.ZSTD_DCtx_reset(@ptrCast(_.state), c.ZSTD_reset_session_and_parameters),
                else => unreachable,
            };
        }
        _.deinitState();
        _.mode = .NONE;
    }
}

@root struct NativeZstd {
    const RefCount = bun.ptr.RefCount(NativeZstd, "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSNativeZstd;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    const StreamImpl = CompressionStream(NativeZstd);
    pub const write = StreamImpl.write;
    pub const runFromJSThread = StreamImpl.runFromJSThread;
    pub const writeSync = StreamImpl.writeSync;
    pub const reset = StreamImpl.reset;
    pub const close = StreamImpl.close;
    pub const setOnError = StreamImpl.setOnError;
    pub const getOnError = StreamImpl.getOnError;
    pub const finalize = StreamImpl.finalize;

    ref_count: RefCount,
    globalThis: *jsc.JSGlobalObject,
    stream: Context = {},
    write_result: ([*]u32)? = null,
    poll_ref: CountedKeepAlive = {},
    this_value: jsc.Strong.Optional = .empty,
    write_in_progress: bool = false,
    pending_close: bool = false,
    pending_reset: bool = false,
    closed: bool = false,
    task: jsc.WorkPoolTask = { callback: undefined },

    pub static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*NativeZstd {
        const arguments = callframe.argumentsUndef(1).ptr;

        let mode = arguments[0];
        if (!mode.isNumber()) {
            return globalThis.throwInvalidArgumentTypeValue("mode", "number", mode);
        }
        const mode_double = mode.asNumber();
        if (@mod(mode_double, 1.0) != 0.0) {
            return globalThis.throwInvalidArgumentTypeValue("mode", "integer", mode);
        }
        const mode_int: i64 = @intFromFloat(mode_double);
        if (mode_int < 10 || mode_int > 11) {
            return globalThis.throwRangeError(mode_int, { field_name: "mode", min: 10, max: 11 });
        }

        const ptr = bun.new(NativeZstd, {
            ref_count: RefCount.init(),
            globalThis: globalThis,
        });
        ptr.stream.mode = @enumFromInt(mode_int);
        return ptr;
    }

    pub fn estimatedSize(*_): usize {
        let extra: usize = 0;
        switch (_.stream.mode) {
            .ZSTD_COMPRESS => extra = 5272,
            .ZSTD_DECOMPRESS => extra = 95968,
            else => {},
        }
        return @sizeOf(NativeZstd) + extra;
    }

    pub fn init(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const arguments = callframe.argumentsAsArray(4);
        const this_value = callframe.this();
        if (callframe.argumentsCount() != 4) return globalThis.ERR(.MISSING_ARGS, "init(initParamsArray, pledgedSrcSize, writeState, processCallback)", {}).throw();

        const initParamsArray_value = arguments[0];
        const pledgedSrcSize_value = arguments[1];
        const writeState_value = arguments[2];
        const processCallback_value = arguments[3];

        const writeState_opt = writeState_value.asArrayBuffer(globalThis);
        if (writeState_opt == null) return globalThis.throwInvalidArgumentTypeValue("writeState", "Uint32Array", writeState_value);
        const writeState = writeState_opt.?;
        if (writeState.typed_array_type != .Uint32Array) return globalThis.throwInvalidArgumentTypeValue("writeState", "Uint32Array", writeState_value);
        _.write_result = writeState.asU32().ptr;

        const write_js_callback = validators.validateFunction(globalThis, "processCallback", processCallback_value)?;
        js.writeCallbackSetCached(this_value, globalThis, write_js_callback.withAsyncContextIfNeeded(globalThis));

        let pledged_src_size: u64 = std.math.maxInt(u64);
        if (pledgedSrcSize_value.isNumber()) {
            pledged_src_size = validators.validateUint32(globalThis, pledgedSrcSize_value, "pledgedSrcSize", {}, false)?;
        }

        let e = _.stream.init(pledged_src_size);
        if (e.isError()) {
            StreamImpl.emitError(_, globalThis, this_value, e);
            return jsc.JSValue.false;
        }

        const params_opt = initParamsArray_value.asArrayBuffer(globalThis);
        if (params_opt == null) return globalThis.throwInvalidArgumentTypeValue("initParamsArray", "Uint32Array", initParamsArray_value);
        const params_ = params_opt.?;
        if (params_.typed_array_type != .Uint32Array) return globalThis.throwInvalidArgumentTypeValue("initParamsArray", "Uint32Array", initParamsArray_value);
        const params_u32 = params_.asU32();
        for (i of 0..params_u32.len) {
            const x = params_u32[i];
            if (x == std.math.maxInt(u32)) continue;
            const err_ = _.stream.setParams(@intCast(i), x);
            if (err_.isError()) {
                _.stream.close();
                return globalThis.ERR(.ZLIB_INITIALIZATION_FAILED, "{s}", [std.mem.sliceTo(err_.msg.?, 0)]).throw();
            }
        }

        return jsc.JSValue.true;
    }

    pub fn params(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        return jsc.JSValue.js_undefined;
    }

    fn deinit(*_) {
        _.poll_ref.deinit();
        switch (_.stream.mode) {
            .ZSTD_COMPRESS, .ZSTD_DECOMPRESS => _.stream.close(),
            else => {},
        }
        bun.destroy(_);
    }
}
