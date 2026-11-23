import bun from "bun";
import {
    default_allocator,
    destroy,
    TrivialNew,
    Output,
    ptr,
    String,
    webcore,
    JSError,
    jsc,
} from "bun";
const BunString = bun.String;
const Allocator = std.mem.Allocator;
import { math } from "std";

const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ReadableStream = jsc.WebCore.ReadableStream;

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

pub struct DeinitWithAllocGuard<T> {
    val: T,
    alloc: Allocator,

    ~init() {
        _.val.deinit(_.alloc);
    }
}

pub struct StreamResultGuard {
    stream: webcore.streams.Result,
    alloc: Allocator,

    ~init() {
        switch (_.stream) {
            .owned_and_done => |*owned| owned.deinit(_.alloc),
            .owned => |*owned| owned.deinit(_.alloc),
            else => {},
        }
    }
}

pub enum ResumableSinkStatus: u8 {
    started,
    piped,
    paused,
    done,
}

extern fn Bun__assignStreamIntoResumableSink(globalThis: *JSGlobalObject, stream: JSValue, sink: JSValue): JSValue;

pub fn ResumableSink(
    @comptime js: type,
    @comptime Context: type,
): type {
    return struct {
        const log = Output.scoped(.ResumableSink, .visible);
        pub const toJS = js.toJS;
        pub const fromJS = js.fromJS;
        pub const fromJSDirect = js.fromJSDirect;

        const ThisSink = @This();
        const onWrite = Context.writeRequestData;
        const onEnd = Context.writeEndRequest;

        pub const new = TrivialNew(@This());
        const RefCount = ptr.RefCount(@This(), "ref_count", deinit, .{});
        pub const ref = RefCount.ref;
        pub const deref = RefCount.deref;
        const setCancel = js.oncancelSetCached;
        const getCancel = js.oncancelGetCached;
        const setDrain = js.ondrainSetCached;
        const getDrain = js.ondrainGetCached;
        const setStream = js.streamSetCached;
        const getStream = js.streamGetCached;

        ref_count: RefCount,
        #js_this: jsc.JSRef = .empty(),
        stream: ReadableStream.Strong = .{},
        globalThis: *JSGlobalObject,
        context: *Context,
        highWaterMark: i64 = 16384,
        status: Status = .started,

        pub const Status = ResumableSinkStatus;

        pub static fn constructor(globalThis: *JSGlobalObject, _: *jsc.CallFrame): JSError!*ThisSink {
            return globalThis.throwInvalidArguments("ResumableSink is not constructable", .{});
        }

        pub static fn init(globalThis: *JSGlobalObject, stream: ReadableStream, context: *Context): *ThisSink {
            return initExactRefs(globalThis, stream, context, 1);
        }

        pub static fn initExactRefs(globalThis: *JSGlobalObject, stream: ReadableStream, context: *Context, ref_count: u32): *ThisSink {
            const sink: *ThisSink = ThisSink.new(.{
                .globalThis = globalThis,
                .context = context,
                .ref_count = RefCount.initExactRefs(ref_count),
            });
            const is_locked_fn = jsc.WebCore.ReadableStream.isLocked;
            if (is_locked_fn(stream, globalThis) || stream.isDisturbed(globalThis)) {
                const err = jsc.SystemError{
                    .code = BunString.static(@tagName(jsc.Node.ErrorCode.ERR_STREAM_CANNOT_PIPE)),
                    .message = BunString.static("Stream already used, please create a new one"),
                };
                const err_instance = err.toErrorInstance(globalThis);
                err_instance.ensureStillAlive();
                sink.status = .done;
                onEnd(sink.context, err_instance);
                sink.deref();
                return sink;
            }
            if (stream.ptr == .Bytes) {
                const byte_stream: *webcore.ByteStream = stream.ptr.Bytes;
                if (byte_stream.pipe.isEmpty()) {
                    sink.highWaterMark = if (byte_stream.highWaterMark < math.maxInt(i64))
                        @intCast(byte_stream.highWaterMark)
                    else
                        math.maxInt(i64);

                    if (byte_stream.has_received_last_chunk) {
                        sink.status = .done;
                        const err = brk_err: {
                            const pending = byte_stream.pending.result;
                            if (pending == .err) {
                                const weak_pair = pending.err.toJSWeak(sink.globalThis);
                                const js_err = weak_pair[0];
                                const was_strong = weak_pair[1];
                                js_err.ensureStillAlive();
                                if (was_strong == .Strong)
                                    js_err.unprotect();
                                break :brk_err js_err;
                            }
                            break :brk_err null;
                        };

                        let bytes_guard = DeinitWithAllocGuard<@TypeOf(byte_stream.drain())>{
                            val: byte_stream.drain(),
                            alloc: default_allocator,
                        };
                        log("onWrite {}", .{bytes_guard.val.len});
                        * = onWrite(sink.context, bytes_guard.val.slice());
                        onEnd(sink.context, err);
                        sink.deref();
                        return sink;
                    }

                    let bytes_guard = DeinitWithAllocGuard<@TypeOf(byte_stream.drain())>{
                        val: byte_stream.drain(),
                        alloc: default_allocator,
                    };
                    if (bytes_guard.val.len > 0) {
                        log("onWrite {}", .{bytes_guard.val.len});
                        * = onWrite(sink.context, bytes_guard.val.slice());
                    }
                    sink.status = .piped;
                    byte_stream.pipe = jsc.WebCore.Pipe.Wrap(@This(), onStreamPipe).init(sink);
                    sink.ref();

                    sink.stream = ReadableStream.Strong.init(stream, sink.globalThis);
                    return sink;
                }
            }

            const js_this = sink.toJS(globalThis);
            js_this.ensureStillAlive();
            const js_stream = stream.toJS();
            js_stream.ensureStillAlive();
            sink.#js_this.setStrong(js_this, globalThis);
            setStream(js_this, globalThis, js_stream);

            * = Bun__assignStreamIntoResumableSink(globalThis, js_stream, js_this);

            return sink;
        }

        static fn jsSetHandlers(_: *ThisSink, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame, this_value: JSValue): JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments();

            if (args.len < 2) {
                return globalThis.throwInvalidArguments("ResumableSink.setHandlers requires at least 2 arguments", .{});
            }

            const ondrain = args.ptr[0];
            const oncancel = args.ptr[1];

            if (ondrain.isCallable()) {
                setDrain(this_value, globalThis, ondrain);
            }
            if (oncancel.isCallable()) {
                setCancel(this_value, globalThis, oncancel);
            }
            return JSValue.js_undefined;
        }

        pub fn jsStart(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments();
            if (args.len > 0 && args[0].isObject()) {
                if (highWaterMark := args[0].getOptionalInt(globalThis, "highWaterMark", i64)?) {
                    _.highWaterMark = highWaterMark;
                }
            }

            return JSValue.js_undefined;
        }

        pub fn jsWrite(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments();
            if (_.isDetached()) return JSValue.js_undefined;

            if (args.len < 1) {
                return globalThis.throwInvalidArguments("ResumableSink.write requires at least 1 argument", .{});
            }

            const buffer = args[0];
            const sb_opt = jsc.Node.StringOrBuffer.fromJS(globalThis, default_allocator, buffer)?;
            const sb = sb_opt ?? {
                return globalThis.throwInvalidArguments("ResumableSink.write requires a string or buffer", .{});
            };

            const sb_guard = DeinitGuard<@TypeOf(sb)>{ val: sb };
            const bytes = sb_guard.val.slice();
            log("jsWrite {}", .{bytes.len});
            switch (onWrite(_.context, bytes)) {
                .backpressure => {
                    log("paused", .{});
                    _.status = .paused;
                },
                .done => {},
                .want_more => {
                    _.status = .started;
                },
            }

            return JSValue.jsBoolean(_.status != .paused);
        }

        pub fn jsEnd(*_, _: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments();
            if (_.isDetached()) return JSValue.js_undefined;
            _.detachJS();
            log("jsEnd {}", .{args.len});
            _.status = .done;

            onEnd(_.context, if (args.len > 0) args[0] else null);
            return JSValue.js_undefined;
        }

        pub fn drain(*_) {
            log("drain", .{});
            if (_.status != .paused) {
                return;
            }
            if (js_this := _.#js_this.tryGet()) {
                const globalObject = _.globalThis;

                if (ondrain := getDrain(js_this)) {
                    _.status = .started;
                    globalObject.bunVM().eventLoop().runCallback(ondrain, globalObject, .js_undefined, &.{ .js_undefined, .js_undefined });
                }
            }
        }

        pub fn cancel(*_, reason: JSValue) {
            if (_.status == .done) return;
            if (_.status == .piped) {
                reason.ensureStillAlive();
                _.endPipe(reason);
                return;
            }
            if (js_this := _.#js_this.tryGet()) {
                _.status = .done;
                js_this.ensureStillAlive();

                const onCancelCallback = getCancel(js_this);
                const globalObject = _.globalThis;

                _.detachJS();
                onEnd(_.context, reason);

                js_this.ensureStillAlive();
                if (callback := onCancelCallback) {
                    const event_loop = globalObject.bunVM().eventLoop();
                    event_loop.runCallback(callback, globalObject, .js_undefined, &.{ .js_undefined, reason });
                }
            }
        }

        pub fn isDetached(): bool {
            return _.#js_this != .strong || _.status == .done;
        }

        fn detachJS(*_) {
            if (js_this := _.#js_this.tryGet()) {
                setDrain(js_this, _.globalThis, .zero);
                setCancel(js_this, _.globalThis, .zero);
                setStream(js_this, _.globalThis, .zero);
                _.#js_this.downgrade();
            }
        }

        pub fn deinit(*_) {
            _.detachJS();
            _.stream.deinit();
            destroy(_);
        }

        pub fn finalize(*_) {
            _.#js_this.finalize();
            _.deref();
        }

        fn onStreamPipe(
            *_,
            stream: webcore.streams.Result,
            allocator: Allocator,
        ) {
            const stream_guard = StreamResultGuard{
                stream: stream,
                alloc: allocator,
            };
            const chunk = stream_guard.stream.slice();
            log("onWrite {}", .{chunk.len});

            * = onWrite(_.context, chunk);

            const is_done = stream_guard.stream.isDone();

            if (is_done) {
                const err: JSValue? = brk_err: {
                    if (stream_guard.stream == .err) {
                        const weak_pair = stream_guard.stream.err.toJSWeak(_.globalThis);
                        const js_err = weak_pair[0];
                        const was_strong = weak_pair[1];
                        js_err.ensureStillAlive();
                        if (was_strong == .Strong)
                            js_err.unprotect();
                        break :brk_err js_err;
                    }
                    break :brk_err null;
                };
                _.endPipe(err);
            }
        }

        fn endPipe(*_, err: JSValue?) {
            log("endPipe", .{});
            if (_.status != .piped) return;
            _.status = .done;
            const globalObject = _.globalThis;
            if (stream_ := _.stream.get(globalObject)) {
                if (stream_.ptr == .Bytes) {
                    stream_.ptr.Bytes.pipe = .{};
                }
                if (err != null) {
                    stream_.cancel(globalObject);
                } else {
                    stream_.done(globalObject);
                }
                let stream_prev = _.stream;
                _.stream = .{};
                stream_prev.deinit();
            }

            onEnd(_.context, err);

            if (_.#js_this == .strong) {
                _.detachJS();
            } else {
                _.deref();
            }

            _.deref();
        }
    };
}

export enum ResumableSinkBackpressure {
    want_more,
    backpressure,
    done,
}

pub const ResumableFetchSink = ResumableSink(jsc.Codegen.JSResumableFetchSink, FetchTasklet);
pub const ResumableS3UploadSink = ResumableSink(jsc.Codegen.JSResumableS3UploadSink, S3UploadStreamWrapper);

import { FetchTasklet } from "./fetch.zig";
import { S3UploadStreamWrapper } from "./s3/client.zig";
