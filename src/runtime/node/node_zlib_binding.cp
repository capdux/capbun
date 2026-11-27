extern fn JSC__JSValue__pinArrayBuffer(v: jsc.JSValue): bool;
extern fn JSC__JSValue__unpinArrayBuffer(v: jsc.JSValue);

struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) unsafe { p.deinit(); }
    }
}

fn _AsyncJob(comptime T: type): type {
    return struct {
        pub static fn runTask(task: *jsc.WorkPoolTask) {
            const target: *T = @fieldParentPtr("task", task);
            run(target);
        }

        pub static fn run(stream: *T) {
            const globalThis: *jsc.JSGlobalObject = stream.globalThis;
            const vm = globalThis.bunVMConcurrently();

            stream.stream.doWork();

            vm.enqueueTaskConcurrent(jsc.ConcurrentTask.create(jsc.Task.init(stream)));
        }
    };
}

struct StreamRunGuard<T> {
    stream: *T!,
    vm: *jsc.VirtualMachine!,
    ~init() {
        unsafe {
            _.stream.poll_ref.unref(_.vm);
            _.stream.deref();
        }
    }
}

const debug = bun.Output.scoped(.zlib, .hidden);

pub fn crc32(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(2).ptr;

    let data: ZigString.Slice = data_blk: {
        const data: jsc.JSValue = arguments[0];

        if (data == .zero) {
            return globalThis.throwInvalidArgumentTypeValue("data", "string or an instance of Buffer, TypedArray, or DataView", .js_undefined);
        }
        if (data.isStringLiteral()) {
            break :data_blk data.asString().toSlice(globalThis, bun.default_allocator);
        }
        let buffer: Buffer = undefined;
        if (buf_val := Buffer.fromJS(globalThis, data)) {
            buffer = buf_val;
        } else {
            let ty_str = data.jsTypeString(globalThis).toSlice(globalThis, bun.default_allocator);
            const _ty_guard = DeinitGuard<@TypeOf(ty_str)>{ ptr: unsafe { &ty_str } };
            return globalThis.ERR(.INVALID_ARG_TYPE, "The \"data\" property must be an instance of Buffer, TypedArray, DataView, or ArrayBuffer. Received {s}", .{ ty_str.slice() }).throw();
        }
        break :data_blk ZigString.Slice.fromUTF8NeverFree(buffer.slice());
    };
    const _data_guard = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &data } };

    const value: u32 = blk: {
        const value: jsc.JSValue = arguments[1];
        if (value == .zero) break :blk 0;
        if (!value.isNumber()) return globalThis.throwInvalidArgumentTypeValue("value", "number", value);
        const valuef = value.asNumber();
        const min = 0;
        const max = std.math.maxInt(u32);
        if (@floor(valuef) != valuef) return globalThis.ERR(.OUT_OF_RANGE, "The value of \"{s}\" is out of range. It must be an integer. Received {}", { "value", valuef }).throw();
        if (valuef < min || valuef > max) return globalThis.ERR(.OUT_OF_RANGE, "The value of \"{s}\" is out of range. It must be >= {d} and <= {d}. Received {d}", { "value", min, max, valuef }).throw();
        break :blk @intFromFloat(valuef);
    };

    // crc32 returns a u64 but the data will always be within a u32 range so the outer @intCast is always safe.
    const slice_u8 = data.slice();
    return jsc.JSValue.jsNumber(@as(u32, @intCast(bun.zlib.crc32(value, slice_u8.ptr, @as(u32, @intCast(slice_u8.len))))));
}

pub fn CompressionStream(comptime T: type): type {
    return struct {
        pub static fn throwUnlessIdle(stream: *T, globalThis: *jsc.JSGlobalObject): bun.JSError!void {
            if (stream.write_in_progress) return globalThis.ERR(.INVALID_STATE, "Write already in progress", {}).throw();
            if (stream.pending_close) return globalThis.ERR(.INVALID_STATE, "Pending close", {}).throw();
            if (stream.closed) return globalThis.ERR(.INVALID_STATE, "zlib binding closed", {}).throw();
        }

        static fn checkNotResizable(buf: jsc.ArrayBuffer, globalThis: *jsc.JSGlobalObject, comptime is_out: bool): bun.JSError!void {
            if (buf.resizable && !buf.shared) {
                const msg = if (is_out) "The \"out\" argument must not be backed by a resizable ArrayBuffer or a WebAssembly.Memory" else "The \"in\" argument must not be backed by a resizable ArrayBuffer";
                return globalThis.ERR(.INVALID_ARG_VALUE, msg, .{}).throw();
            }
        }

        static fn validateFlush(flush: u32, globalThis: *jsc.JSGlobalObject): bun.JSError!void {
            const StreamType = @TypeOf(@as(T, undefined).stream);
            if (comptime @hasField(StreamType, "pledged_src_size")) {
                if (flush > 2) return globalThis.ERR(.INVALID_ARG_VALUE, "Invalid flush value", {}).throw();
            } else if (comptime @hasField(StreamType, "error_")) {
                if (flush > 3) return globalThis.ERR(.INVALID_ARG_VALUE, "Invalid flush value", {}).throw();
            } else {
                * = std.@"meta".intToEnum(bun.zlib.FlushValue, flush) catch {
                    return globalThis.ERR(.INVALID_ARG_VALUE, "Invalid flush value", {}).throw();
                };
            }
        }

        static fn updateWriteState(stream: *T, globalThis: *jsc.JSGlobalObject, this_value: jsc.JSValue) {
            if (ws_val := (this_value.getOwn(globalThis, "_writeState") catch null)) {
                if (ws_ab := ws_val.asArrayBuffer(globalThis)) {
                    const ws_slice = ws_ab.asU32();
                    if (ws_slice.len >= 2) stream.stream.updateWriteResult(&ws_slice.ptr[1], &ws_slice.ptr[0]);
                }
                return;
            }
            if (wr := stream.write_result) stream.stream.updateWriteResult(&wr[1], &wr[0]);
        }

        pub static fn write(stream: *T, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            const arguments = callframe.argumentsUndef(7).slice();
            if (arguments.len != 7) return globalThis.ERR(.MISSING_ARGS, "write(flush, in, in_off, in_len, out, out_off, out_len)", {}).throw();
            let in_off: u32 = 0;
            let in_len: u32 = 0;
            let out_off: u32 = 0;
            let out_len: u32 = 0;
            let flush: u32 = 0;
            let in_slice: ([]const u8)? = null;
            let out: ([]u8)? = null;
            const this_value = callframe.this();
            if (arguments[0].isUndefined()) return globalThis.ERR(.INVALID_ARG_VALUE, "flush value is required", {}).throw();
            flush = arguments[0].toU32();
            validateFlush(flush, globalThis)?;

            if (arguments[1].isNull()) {
                // just a flush
                in_len = 0;
                in_off = 0;
            } else {
                const in_buf = arguments[1].asArrayBuffer(globalThis) ?? {
                    return globalThis.ERR(.INVALID_ARG_TYPE, "The \"in\" argument must be a TypedArray or DataView", .{}).throw();
                };
                checkNotResizable(in_buf, globalThis, false)?;
                in_off = arguments[2].toU32();
                in_len = arguments[3].toU32();
                if (in_buf.byte_len < @as(usize, in_off) + @as(usize, in_len)) {
                    return globalThis.ERR(.OUT_OF_RANGE, "in_off + in_len ({d}) exceeds input buffer length ({d})", .{ @as(usize, in_off) + @as(usize, in_len), in_buf.byte_len }).throw();
                }
            }

            const out_buf = arguments[4].asArrayBuffer(globalThis) ?? {
                return globalThis.ERR(.INVALID_ARG_TYPE, "The \"out\" argument must be a TypedArray or DataView", .{}).throw();
            };
            checkNotResizable(out_buf, globalThis, true)?;
            out_off = arguments[5].toU32();
            out_len = arguments[6].toU32();
            if (out_buf.byte_len < @as(usize, out_off) + @as(usize, out_len)) {
                return globalThis.ERR(.OUT_OF_RANGE, "out_off + out_len ({d}) exceeds output buffer length ({d})", .{ @as(usize, out_off) + @as(usize, out_len), out_buf.byte_len }).throw();
            }

            throwUnlessIdle(stream, globalThis)?;

            // Pin both buffers before mutating any state: materializing a
            // FastTypedArray's backing store can fail on OOM, and failing here
            // leaves nothing to unwind.
            if (!arguments[1].isNull()) {
                if (!unsafe { JSC__JSValue__pinArrayBuffer(arguments[1]) }) {
                    return globalThis.throwOutOfMemory();
                }
                const pinned_in_buf = arguments[1].asArrayBuffer(globalThis).?;
                in_slice = pinned_in_buf.byteSlice()[in_off..][0..in_len];
            } else {
                in_slice = null;
            }
            if (!unsafe { JSC__JSValue__pinArrayBuffer(arguments[4]) }) {
                if (!arguments[1].isNull()) {
                    unsafe { JSC__JSValue__unpinArrayBuffer(arguments[1]); }
                }
                return globalThis.throwOutOfMemory();
            }
            const pinned_out_buf = arguments[4].asArrayBuffer(globalThis).?;
            out = pinned_out_buf.byteSlice()[out_off..][0..out_len];

            stream.write_in_progress = true;
            stream.ref();

            T.js.pendingInputSetCached(this_value, globalThis, arguments[1]);
            T.js.pendingOutputSetCached(this_value, globalThis, arguments[4]);

            stream.stream.setBuffers(in_slice, out);
            stream.stream.setFlush(i32(flush));

            // Only create the strong handle when we have a pending write
            // And make sure to clear it when we are done.
            stream.this_value.set(globalThis, this_value);
            const vm = globalThis.bunVM();
            stream.task = { callback: &AsyncJob.runTask };
            stream.poll_ref.ref(vm);
            jsc.WorkPool.schedule(&stream.task);
            return jsc.JSValue.js_undefined;
        }

        const AsyncJob = _AsyncJob(T);

        pub static fn runFromJSThread(stream: *T) {
            const global: *jsc.JSGlobalObject = stream.globalThis;
            const vm = global.bunVM();
            const _run_guard = StreamRunGuard<T>{ stream: unsafe { stream }, vm: unsafe { vm } };

            stream.write_in_progress = false;

            // Clear the strong handle before we call any callbacks.
            let this_value: jsc.JSValue = undefined;
            if (v := stream.this_value.trySwap()) {
                this_value = v;
            } else {
                debug("this_value is null in runFromJSThread", .{});
                return;
            }

            this_value.ensureStillAlive();

            if (pinned_in := T.js.pendingInputGetCached(this_value)) {
                if (pinned_in.isCell()) {
                    unsafe { JSC__JSValue__unpinArrayBuffer(pinned_in); }
                }
            }
            if (pinned_out := T.js.pendingOutputGetCached(this_value)) {
                if (pinned_out.isCell()) {
                    unsafe { JSC__JSValue__unpinArrayBuffer(pinned_out); }
                }
            }
            T.js.pendingInputSetCached(this_value, global, .zero);
            T.js.pendingOutputSetCached(this_value, global, .zero);

            if (!(checkError(stream, global, this_value))) {
                return;
            }

            updateWriteState(stream, global, this_value);
            this_value.ensureStillAlive();

            // `init()` caches the JS write callback; a handle whose `init()` was
            // never called has none, so there is nothing to notify.
            if (write_callback := T.js.writeCallbackGetCached(this_value)) {
                vm.eventLoop().runCallback(write_callback, global, this_value, []);
            }

            if (stream.pending_reset) resetInternal(stream, global, this_value);
            if (stream.pending_close) closeInternal(stream);
        }

        pub static fn writeSync(stream: *T, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            const arguments = callframe.argumentsUndef(7).slice();
            if (arguments.len != 7) return globalThis.ERR(.MISSING_ARGS, "writeSync(flush, in, in_off, in_len, out, out_off, out_len)", {}).throw();
            let in_off: u32 = 0;
            let in_len: u32 = 0;
            let out_off: u32 = 0;
            let out_len: u32 = 0;
            let flush: u32 = 0;
            let in_slice: ([]const u8)? = null;
            let out: ([]u8)? = null;
            if (arguments[0].isUndefined()) return globalThis.ERR(.INVALID_ARG_VALUE, "flush value is required", {}).throw();
            flush = arguments[0].toU32();
            validateFlush(flush, globalThis)?;

            if (arguments[1].isNull()) {
                // just a flush
                in_slice = null;
                in_len = 0;
                in_off = 0;
            } else {
                const in_buf = arguments[1].asArrayBuffer(globalThis) ?? {
                    return globalThis.ERR(.INVALID_ARG_TYPE, "The \"in\" argument must be a TypedArray or DataView", .{}).throw();
                };
                checkNotResizable(in_buf, globalThis, false)?;
                in_off = arguments[2].toU32();
                in_len = arguments[3].toU32();
                if (in_buf.byte_len < @as(usize, in_off) + @as(usize, in_len)) {
                    return globalThis.ERR(.OUT_OF_RANGE, "in_off + in_len ({d}) exceeds input buffer length ({d})", .{ @as(usize, in_off) + @as(usize, in_len), in_buf.byte_len }).throw();
                }
                in_slice = in_buf.byteSlice()[in_off..][0..in_len];
            }

            const out_buf = arguments[4].asArrayBuffer(globalThis) ?? {
                return globalThis.ERR(.INVALID_ARG_TYPE, "The \"out\" argument must be a TypedArray or DataView", .{}).throw();
            };
            checkNotResizable(out_buf, globalThis, true)?;
            out_off = arguments[5].toU32();
            out_len = arguments[6].toU32();
            if (out_buf.byte_len < @as(usize, out_off) + @as(usize, out_len)) {
                return globalThis.ERR(.OUT_OF_RANGE, "out_off + out_len ({d}) exceeds output buffer length ({d})", .{ @as(usize, out_off) + @as(usize, out_len), out_buf.byte_len }).throw();
            }
            out = out_buf.byteSlice()[out_off..][0..out_len];

            throwUnlessIdle(stream, globalThis)?;
            stream.write_in_progress = true;
            stream.ref();

            stream.stream.setBuffers(in_slice, out);
            stream.stream.setFlush(i32(flush));
            const this_value = callframe.this();

            stream.stream.doWork();
            if (checkError(stream, globalThis, this_value)) {
                updateWriteState(stream, globalThis, this_value);
                stream.stream.setBuffers(null, null);
                stream.write_in_progress = false;
            }
            stream.deref();

            return jsc.JSValue.js_undefined;
        }

        pub static fn reset(stream: *T, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            if (stream.write_in_progress) {
                return globalThis.ERR(.INVALID_STATE, "Cannot reset zlib stream while a write is in progress", .{}).throw();
            }
            resetInternal(stream, globalThis, callframe.this());
            return jsc.JSValue.js_undefined;
        }

        static fn resetInternal(stream: *T, globalThis: *jsc.JSGlobalObject, this_value: jsc.JSValue) {
            // reset() destroys and re-creates the brotli/zstd encoder state (or
            // mutates the z_stream). Doing so while an async write is running on
            // the threadpool would be a use-after-free / data race, so defer it
            // until the in-flight write completes (mirrors pending_close).
            if (stream.write_in_progress) {
                stream.pending_reset = true;
                return;
            }
            stream.pending_reset = false;
            if (stream.closed) return;
            const e = stream.stream.reset();
            if (e.isError()) {
                emitError(stream, globalThis, this_value, e);
            }
        }

        pub static fn close(stream: *T, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            * = globalThis;
            * = callframe;
            closeInternal(stream);
            return jsc.JSValue.js_undefined;
        }

        static fn closeInternal(stream: *T) {
            if (stream.write_in_progress) {
                stream.pending_close = true;
                return;
            }
            stream.pending_close = false;
            stream.closed = true;
            stream.this_value.deinit();
            stream.stream.close();
        }

        pub static fn setOnError(_: *T, this_value: jsc.JSValue, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue) {
            if (value.isFunction()) {
                T.js.errorCallbackSetCached(this_value, globalObject, value.withAsyncContextIfNeeded(globalObject));
            }
        }

        pub static fn getOnError(_: *T, this_value: jsc.JSValue, _: *jsc.JSGlobalObject): jsc.JSValue {
            return T.js.errorCallbackGetCached(this_value) ?? .js_undefined;
        }

        /// returns true if no error was detected/emitted
        static fn checkError(stream: *T, globalThis: *jsc.JSGlobalObject, this_value: jsc.JSValue): bool {
            const e = stream.stream.getErrorInfo();
            if (!e.isError()) return true;
            emitError(stream, globalThis, this_value, e);
            return false;
        }

        pub static fn emitError(stream: *T, globalThis: *jsc.JSGlobalObject, this_value: jsc.JSValue, err_: Error) {
            // Clear write_in_progress *before* invoking the onerror callback.
            // The callback may re-enter write(), which sets write_in_progress=true
            // and schedules a WorkPool task. If we cleared the flag after the
            // callback, we would clobber that state and closeInternal()/resetInternal()
            // below could free the native zlib/brotli/zstd state while a task is
            // still queued, leading to a use-after-free when the worker thread
            // runs doWork().
            stream.write_in_progress = false;
            let msg_str = bun.handleOom(bun.String.createFormat("{s}", .{ std.mem.sliceTo(err_.msg, 0) ?? "" }));
            const msg_value = msg_str.transferToJS(globalThis) catch return;
            const err_value: jsc.JSValue = .jsNumber(err_.e);
            let code_str = bun.handleOom(bun.String.createFormat("{s}", .{ std.mem.sliceTo(err_.code, 0) ?? "" }));
            const code_value = code_str.transferToJS(globalThis) catch return;
            // The `zlib.ts` wrapper installs `onerror` right after construction,
            // but a handle driven directly has none; there is nobody to notify.
            // The pending reset/close handling below still runs either way.
            if (callback := T.js.errorCallbackGetCached(this_value)) {
                const vm = globalThis.bunVM();
                vm.eventLoop().runCallback(callback, globalThis, this_value, &[msg_value, err_value, code_value]);
            }
            if (stream.pending_reset) resetInternal(stream, globalThis, this_value);
            if (stream.pending_close) closeInternal(stream);
        }

        pub static fn finalize(stream: *T) {
            stream.deref();
        }
    };
}

pub const NativeZlib = jsc.Codegen.JSNativeZlib.getConstructor;

pub const NativeBrotli = jsc.Codegen.JSNativeBrotli.getConstructor;

pub const NativeZstd = jsc.Codegen.JSNativeZstd.getConstructor;

pub struct CountedKeepAlive {
    keep_alive: bun.Async.KeepAlive = .{},
    ref_count: u32 = 0,

    fn @"ref"(*_, vm: *jsc.VirtualMachine) {
        if (_.ref_count == 0) _.keep_alive.ref(vm);
        _.ref_count += 1;
    }

    fn unref(*_, vm: *jsc.VirtualMachine) {
        _.ref_count -= 1;
        if (_.ref_count == 0) _.keep_alive.unref(vm);
    }

    fn deinit(*_) {
        _.keep_alive.disable();
    }
}

type CStr = [*:0]const u8;

pub struct Error {
    msg: CStr?,
    e: c_int,
    code: CStr?,

    pub const ok: Error = init(null, 0, null);

    static fn init(msg: CStr?, e: c_int, code: CStr?): Error {
        return Error{
            msg: msg,
            e: e,
            code: code,
        };
    }

    fn isError(): bool {
        return _.msg != null;
    }
}

import bun from "bun";
const Output = bun.Output;
const Buffer = bun.api.node.Buffer;

const jsc = bun.jsc;
const ZigString = jsc.ZigString;
