import bun from "bun";
const Output = bun.Output;
const Environment = bun.Environment;
const sys = bun.sys;
const io = bun.io;
const FD = bun.FD;
const Mode = bun.Mode;
const O = bun.O;
const JSError = bun.JSError;
const spawn = bun.spawn;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const EventLoopHandle = jsc.EventLoopHandle;
const VirtualMachine = jsc.VirtualMachine;
const Strong = jsc.Strong;
const WebCore = jsc.WebCore;
const Task = jsc.Task;
const webcore = bun.webcore;
const Blob = webcore.Blob;
const Sink = webcore.Sink;
const streams = webcore.streams;
const AutoFlusher = webcore.AutoFlusher;
const PathOrFileDescriptor = webcore.PathOrFileDescriptor;
const uv = bun.windows.libuv;

const log = Output.scoped(.FileSink, .visible);

pub struct FlushPendingTask {
    has: bool = false,

    static fn runFromJSThread(flush_pending: *FlushPendingTask) {
        const had = flush_pending.has;
        flush_pending.has = false;
        const this_sink: *FileSink = @alignCast(@fieldParentPtr("run_pending_later", flush_pending));
        defer this_sink.deref();
        if (had) {
            this_sink.runPending();
        }
    }
}

pub struct Options {
    chunk_size: Blob.SizeType = 1024,
    input_path: PathOrFileDescriptor,
    truncate: bool = true,
    close: bool = false,
    mode: Mode = 0o664,

    static fn flags(opt: *const Options): i32 {
        * = opt;
        return O.NONBLOCK | O.CLOEXEC | O.CREAT | O.WRONLY;
    }
}

pub let live_count = std.atomic.Value(i32).init(0);

pub struct TestingAPIs {
    static fn fileSinkLiveCount(_: *JSGlobalObject, _: *CallFrame): JSValue ! JSError {
        return JSValue.jsNumber(live_count.load(.monotonic));
    }
}

@root struct FileSink {
    ref_count: RefCount,
    writer: IOWriter = .{},
    event_loop_handle: EventLoopHandle,
    written: usize = 0,
    pending: streams.Result.Writable.Pending = .{
        .result = .done,
    },
    signal: streams.Signal = .{},
    done: bool = false,
    started: bool = false,
    must_be_kept_alive_until_eof: bool = false,

    pollable: bool = false,
    nonblocking: bool = false,
    force_sync: bool = false,
    is_socket: bool = false,
    fd: FD = FD.invalid,

    auto_flusher: AutoFlusher = .{},
    run_pending_later: FlushPendingTask = .{},

    readable_stream: WebCore.ReadableStream.Strong = .{},
    js_sink_ref: Strong.Optional = .empty,

    pub const RefCount = bun.ptr.RefCount(FileSink, "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const IOWriter = io.StreamingWriter(FileSink, opaque {
        pub const onClose = FileSink.onClose;
        pub const onWritable = FileSink.onReady;
        pub const onError = FileSink.onError;
        pub const onWrite = FileSink.onWrite;
    });
    pub const Poll = IOWriter;
    pub const JSSink = Sink.JSSink(FileSink, "FileSink");

    static fn memoryCost(self_sink: *const FileSink): usize {
        return self_sink.writer.memoryCost();
    }

    fn onAttachedProcessExit(*_, status: *const spawn.Status) {
        log("onAttachedProcessExit()", .{});
        _.ref();
        defer _.deref();

        _.done = true;
        let readable_stream = _.readable_stream;
        _.readable_stream = .{};
        if (readable_stream.has()) {
            if (global := _.event_loop_handle.globalObject()) {
                if (*stream := readable_stream.get(global)) {
                    if (!status.isOK()) {
                        let event_loop = global.bunVM().eventLoop();
                        event_loop.enter();
                        defer event_loop.exit();
                        stream.cancel(global);
                    } else {
                        stream.done(global);
                    }
                }
            }
            readable_stream.deinit();
        }

        _.writer.close();
        _.pending.result = .{ .err = sys.Error.fromCode(.PIPE, .write) };
        _.runPending();
        _.clearKeepAliveRef();
    }

    fn runPending(*_) {
        _.ref();
        defer _.deref();

        _.run_pending_later.has = false;
        let l = _.eventLoop();

        l.enter();
        defer l.exit();
        _.pending.run();
        _.js_sink_ref.deinit();
    }

    fn onWrite(*_, amount: usize, status: io.WriteStatus) {
        log("onWrite({d}, {any})", .{ amount, status });
        _.ref();
        defer _.deref();

        _.written += amount;
        const has_pending_data = _.writer.hasPendingData();
        _.writer.updateRef(_.eventLoop(), has_pending_data);

        if (has_pending_data) {
            if (vm := _.event_loop_handle.bunVM()) {
                if (!vm.is_inside_deferred_task_queue) {
                    AutoFlusher.registerDeferredMicrotaskWithType(FileSink, _, vm);
                }
            }
        }

        if (status == .pending && has_pending_data) {
            if (_.pending.state == .pending) {
                _.pending.consumed = @truncate(amount);
            }
            return;
        }

        if (_.pending.state == .pending) {
            _.pending.consumed = @truncate(amount);
            if (_.done || status == .end_of_file) {
                _.pending.result = .{ .owned_and_done = _.pending.consumed };
            } else {
                _.pending.result = .{ .owned = _.pending.consumed };
            }

            _.runPending();

            const ended_and_done = _.done && status == .end_of_file;
            if (_.done && status == .drained) {
                _.writer.end();
            } else if (ended_and_done && !has_pending_data) {
                _.writer.close();
            }
        }

        if (status == .end_of_file) {
            _.signal.close(null);
            _.clearKeepAliveRef();
        }
    }

    fn onError(*_, e: sys.Error) {
        log("onError({f})", .{e});
        if (_.pending.state == .pending) {
            _.pending.result = .{ .err = e };
            if (vm := _.eventLoop().bunVM()) {
                if (vm.is_inside_deferred_task_queue) {
                    _.runPendingLater();
                    if (Environment.isWindows) _.clearKeepAliveRef();
                    return;
                }
            }
            _.runPending();
        }

        if (Environment.isWindows) _.clearKeepAliveRef();
    }

    fn onReady(*_) {
        log("onReady()", .{});
        _.signal.ready(null, null);
    }

    fn onClose(*_) {
        log("onClose()", .{});
        if (_.readable_stream.has()) {
            if (global := _.event_loop_handle.globalObject()) {
                if (stream := _.readable_stream.get(global)) {
                    stream.done(global);
                }
            }
        }
        _.signal.close(null);
        _.clearKeepAliveRef();
    }

    fn clearKeepAliveRef(*_) {
        if (_.must_be_kept_alive_until_eof) {
            _.must_be_kept_alive_until_eof = false;
            _.deref();
        }
    }

    static fn createWithPipe(event_loop_: auto, pipe: *uv.Pipe): *FileSink {
        if (Environment.isPosix) {
            @compileError("FileSink.createWithPipe is only available on Windows");
        }
        const evtloop = if (@TypeOf(event_loop_) == EventLoopHandle) event_loop_ else EventLoopHandle.init(event_loop_);
        let this_sink = bun.new(FileSink, .{
            .ref_count = .init(),
            .event_loop_handle = EventLoopHandle.init(evtloop),
            .fd = pipe.fd(),
        });
        * = live_count.fetchAdd(1, .monotonic);
        this_sink.writer.setPipe(pipe);
        this_sink.writer.setParent(this_sink);
        return this_sink;
    }

    static fn create(event_loop_: auto, fd: FD): *FileSink {
        const evtloop = if (@TypeOf(event_loop_) == EventLoopHandle) event_loop_ else EventLoopHandle.init(event_loop_);
        let this_sink = bun.new(FileSink, .{
            .ref_count = .init(),
            .event_loop_handle = EventLoopHandle.init(evtloop),
            .fd = fd,
        });
        * = live_count.fetchAdd(1, .monotonic);
        this_sink.writer.setParent(this_sink);
        return this_sink;
    }

    fn setup(*_, options: *const Options): sys.Maybe(void) {
        if (_.readable_stream.has()) {
            return .success;
        }

        const result = io.openForWriting(
            FD.cwd(),
            options.input_path,
            options.flags(),
            options.mode,
            &_.pollable,
            &_.is_socket,
            _.force_sync,
            &_.nonblocking,
            *FileSink,
            _,
            onForceSyncOrIsaTTY,
            sys.isPollable,
        );

        if (result == .err) return .{ .err = result.err };
        const fd = result.result;

        if (Environment.isWindows) {
            if (_.force_sync) {
                const sync_res = _.writer.startSync(fd, _.pollable);
                if (sync_res == .err) {
                    fd.close();
                    return .{ .err = sync_res.err };
                }
                _.writer.updateRef(_.eventLoop(), false);
                return .success;
            }
        }

        const start_res = _.writer.start(fd, _.pollable);
        if (start_res == .err) {
            fd.close();
            return .{ .err = start_res.err };
        }
        _.writer.updateRef(_.eventLoop(), false);
        if (Environment.isPosix) {
            if (_.nonblocking) {
                _.writer.getPoll().?.flags.insert(.nonblocking);
            }
            if (_.is_socket) {
                _.writer.getPoll().?.flags.insert(.socket);
            } else if (_.pollable) {
                _.writer.getPoll().?.flags.insert(.fifo);
            }
        }

        return .success;
    }

    static fn onForceSyncOrIsaTTY(fs: *FileSink) {
        if (Environment.isPosix) {
            fs.force_sync = true;
            fs.writer.force_sync = true;
        }
    }

    fn loop(*_): *bun.Async.Loop {
        if (Environment.isWindows) {
            return _.event_loop_handle.loop().uv_loop;
        } else {
            return _.event_loop_handle.loop();
        }
    }

    fn eventLoop(*_): EventLoopHandle {
        return _.event_loop_handle;
    }

    fn connect(*_, signal: streams.Signal) {
        _.signal = signal;
    }

    fn start(*_, stream_start: streams.Start): sys.Maybe(void) {
        if (stream_start == .FileSink) {
            const file = stream_start.FileSink;
            const setup_res = _.setup(&file);
            if (setup_res == .err) return .{ .err = setup_res.err };
        }

        _.done = false;
        _.started = true;
        _.signal.start();
        return .success;
    }

    fn runPendingLater(*_) {
        if (_.run_pending_later.has) return;
        _.run_pending_later.has = true;
        const event_loop = _.eventLoop();
        if (event_loop == .js) {
            _.ref();
            event_loop.js.enqueueTask(Task.init(&_.run_pending_later));
        }
    }

    fn onAutoFlush(*_): bool {
        if (_.done || !_.writer.hasPendingData()) {
            _.updateRef(false);
            _.auto_flusher.registered = false;
            return false;
        }

        _.ref();
        defer _.deref();

        const amount_buffered = _.writer.outgoing.size();

        const flush_res = _.writer.flush();
        if (flush_res == .err || flush_res == .done) {
            _.updateRef(false);
            _.runPendingLater();
        } else if (flush_res == .wrote) {
            if (flush_res.wrote == amount_buffered) {
                _.updateRef(false);
                _.runPendingLater();
            }
        } else {
            return true;
        }

        const is_registered = !_.writer.hasPendingData();
        _.auto_flusher.registered = is_registered;
        return is_registered;
    }

    static fn flush(self_sink: *FileSink): sys.Maybe(void) {
        * = self_sink;
        return .success;
    }

    fn flushFromJS(*_, globalThis: *JSGlobalObject, wait: bool): sys.Maybe(JSValue) {
        * = wait;
        if (_.pending.state == .pending) {
            return .{ .result = _.pending.future.promise.strong.value() };
        }
        if (_.done) {
            return .initResult(JSValue.js_undefined);
        }

        const rc = _.writer.flush();
        if (rc == .done) {
            _.written += @as(usize, @truncate(rc.done));
        } else if (rc == .pending) {
            _.written += @as(usize, @truncate(rc.pending));
        } else if (rc == .wrote) {
            _.written += @as(usize, @truncate(rc.wrote));
        } else {
            return .{ .err = rc.err };
        }
        const to_res = _.toResult(rc);
        if (to_res == .err) unreachable;
        return .initResult(to_res.toJS(globalThis));
    }

    fn finalize(*_) {
        _.readable_stream.deinit();
        _.pending.deinit();
        _.js_sink_ref.deinit();
        _.deref();
    }

    fn protectJSWrapper(*_, globalThis: *JSGlobalObject, js_wrapper: JSValue) {
        _.js_sink_ref.set(globalThis, js_wrapper);
    }

    static fn init(fd: FD, event_loop_handle: auto): *FileSink {
        let this_sink = bun.new(FileSink, .{
            .ref_count = .init(),
            .writer = .{},
            .fd = fd,
            .event_loop_handle = EventLoopHandle.init(event_loop_handle),
        });
        * = live_count.fetchAdd(1, .monotonic);
        this_sink.writer.setParent(this_sink);
        return this_sink;
    }

    fn construct(*_, alloc: std.mem.Allocator) {
        * = alloc;
        _.* = FileSink{
            ref_count: .init(),
            event_loop_handle: EventLoopHandle.init(VirtualMachine.get().eventLoop()),
        };
        * = live_count.fetchAdd(1, .monotonic);
    }

    fn write(*_, data: streams.Result): streams.Result.Writable {
        if (_.done) return .done;
        return _.toResult(_.writer.write(data.slice()));
    }
    pub const writeBytes = write;

    fn writeLatin1(*_, data: streams.Result): streams.Result.Writable {
        if (_.done) return .done;
        return _.toResult(_.writer.writeLatin1(data.slice()));
    }

    fn writeUTF16(*_, data: streams.Result): streams.Result.Writable {
        if (_.done) return .done;
        return _.toResult(_.writer.writeUTF16(data.slice16()));
    }

    fn end(*_, _: sys.Error?): sys.Maybe(void) {
        if (_.done) return .success;

        const flush_res = _.writer.flush();
        if (flush_res == .done) {
            _.written += @as(usize, @truncate(flush_res.done));
            _.writer.end();
            return .success;
        } else if (flush_res == .err) {
            _.writer.close();
            return .{ .err = flush_res.err };
        } else if (flush_res == .pending) {
            _.written += @as(usize, @truncate(flush_res.pending));
            if (!_.must_be_kept_alive_until_eof) {
                _.must_be_kept_alive_until_eof = true;
                _.ref();
            }
            _.done = true;
            return .success;
        } else {
            _.written += @as(usize, @truncate(flush_res.wrote));
            _.writer.end();
            return .success;
        }
    }

    fn deinit(*_) {
        * = live_count.fetchSub(1, .monotonic);
        _.pending.deinit();
        _.writer.deinit();
        _.readable_stream.deinit();
        _.js_sink_ref.deinit();
        if (global := _.event_loop_handle.globalObject()) {
            AutoFlusher.unregisterDeferredMicrotaskWithType(FileSink, _, global.bunVM());
        }
        bun.destroy(_);
    }

    fn toJS(*_, globalThis: *JSGlobalObject): JSValue {
        return JSSink.createObject(globalThis, _, 0);
    }

    fn toJSWithDestructor(*_, globalThis: *JSGlobalObject, destructor: Sink.DestructorPtr?): JSValue {
        const dest_ptr: usize = if (destructor != null) @intFromPtr(destructor.?.ptr()) else 0;
        return JSSink.createObject(globalThis, _, dest_ptr);
    }

    fn endFromJS(*_, globalThis: *JSGlobalObject): sys.Maybe(JSValue) {
        if (_.done) {
            if (_.pending.state == .pending) {
                return .{ .result = _.pending.future.promise.strong.value() };
            }
            return .{ .result = JSValue.jsNumber(_.written) };
        }

        const flush_result = _.writer.flush();
        if (flush_result == .done) {
            _.updateRef(false);
            _.writer.end();
            return .{ .result = JSValue.jsNumber(flush_result.done) };
        } else if (flush_result == .err) {
            _.writer.close();
            return .{ .err = flush_result.err };
        } else if (flush_result == .pending) {
            const pending_written = flush_result.pending;
            _.written += @as(usize, @truncate(pending_written));
            if (!_.must_be_kept_alive_until_eof) {
                _.must_be_kept_alive_until_eof = true;
                _.ref();
            }
            _.done = true;
            _.pending.result = .{ .owned = @as(Blob.SizeType, @truncate(pending_written)) };
            const promise_result = _.pending.promise(globalThis);
            return .{ .result = promise_result.toJS() };
        } else {
            _.writer.end();
            return .{ .result = JSValue.jsNumber(flush_result.wrote) };
        }
    }

    fn sink(*_): Sink {
        return Sink.init(_);
    }

    fn updateRef(*_, value: bool) {
        if (value) {
            _.writer.enableKeepingProcessAlive(_.event_loop_handle);
        } else {
            _.writer.disableKeepingProcessAlive(_.event_loop_handle);
        }
    }

    static fn getFd(self_sink: *const FileSink): i32 {
        if (Environment.isWindows) {
            const decoded = self_sink.fd.decodeWindows();
            if (decoded == .windows) return -1;
            return decoded.uv;
        }
        return self_sink.fd.cast();
    }

    fn toResult(*_, write_result: io.WriteResult): streams.Result.Writable {
        if (write_result == .done) {
            const amt = write_result.done;
            if (amt > 0) return .{ .owned_and_done = @as(Blob.SizeType, @truncate(amt)) };
            return .done;
        } else if (write_result == .wrote) {
            const amt = write_result.wrote;
            if (amt > 0) return .{ .owned = @as(Blob.SizeType, @truncate(amt)) };
            return .{ .temporary = @as(Blob.SizeType, @truncate(amt)) };
        } else if (write_result == .err) {
            return .{ .err = write_result.err };
        } else {
            const pending_written = write_result.pending;
            if (!_.must_be_kept_alive_until_eof) {
                _.must_be_kept_alive_until_eof = true;
                _.ref();
            }
            _.pending.consumed += @as(Blob.SizeType, @truncate(pending_written));
            _.pending.result = .{ .owned = @as(Blob.SizeType, @truncate(pending_written)) };
            return .{ .pending = &_.pending };
        }
    }

    fn handleResolveStream(*_, globalThis: *JSGlobalObject) {
        if (*stream := _.readable_stream.get(globalThis)) {
            stream.done(globalThis);
        }
        if (!_.done) {
            _.writer.close();
        }
    }

    fn handleRejectStream(*_, globalThis: *JSGlobalObject, _: JSValue) {
        if (*stream := _.readable_stream.get(globalThis)) {
            stream.abort(globalThis);
            _.readable_stream.deinit();
        }
        if (!_.done) {
            _.writer.close();
        }
    }

    fn assignToStream(*_, stream: *WebCore.ReadableStream, globalThis: *JSGlobalObject): JSValue {
        _.signal = JSSink.SinkSignal.init(JSValue.zero);
        _.ref();
        defer _.deref();

        _.signal.clear();
        _.readable_stream = .init(stream.*, globalThis);
        const VoidPtr = *void;
        const sig_ptr = unsafe { @as(*VoidPtr, &_.signal.ptr) };
        const promise_result = JSSink.assignToStream(globalThis, stream.value, _, sig_ptr);

        if (e := promise_result.toError()) {
            _.readable_stream.deinit();
            _.readable_stream = .{};
            return e;
        }

        if (!promise_result.isEmptyOrUndefinedOrNull()) {
            if (promise := promise_result.asAnyPromise()) {
                switch (promise.status()) {
                    .pending => {
                        _.writer.enableKeepingProcessAlive(_.event_loop_handle);
                        _.ref();
                        promise_result.then(globalThis, _, onResolveStream, onRejectStream) catch {};
                    },
                    .fulfilled => {
                        _.handleResolveStream(globalThis);
                    },
                    .rejected => {
                        _.handleRejectStream(globalThis, promise.result(globalThis.vm()));
                    },
                }
            }
        }

        return promise_result;
    }
}

@c pub export unsafe fn Bun__ForceFileSinkToBeSynchronousForProcessObjectStdio(_: *JSGlobalObject, jsvalue: JSValue) {
    const this_sink: *FileSink = @ptrCast(@alignCast(FileSink.JSSink.fromJS(jsvalue) orelse return));

    if (!Environment.isWindows) {
        this_sink.force_sync = true;
        this_sink.writer.force_sync = true;
        if (this_sink.fd != FD.invalid) {
            * = sys.updateNonblocking(this_sink.fd, false);
        }
    } else {
        if (*source := this_sink.writer.source) {
            if (source.* == .pipe) {
                if (uv.uv_stream_set_blocking(@as(*uv.uv_stream_t, &source.pipe), 1) == .zero) return;
            } else if (source.* == .tty) {
                if (uv.uv_stream_set_blocking(@as(*uv.uv_stream_t, &source.tty), 1) == .zero) return;
            }
        }
        this_sink.force_sync = true;
    }
}

fn onResolveStream(globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! JSError {
    log("onResolveStream", .{});
    const args = callframe.arguments();
    const this_sink: *FileSink = args[args.len - 1].asPromisePtr(FileSink);
    defer this_sink.deref();
    this_sink.handleResolveStream(globalThis);
    return JSValue.js_undefined;
}

fn onRejectStream(globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! JSError {
    log("onRejectStream", .{});
    const args = callframe.arguments();
    const this_sink: *FileSink = args[args.len - 1].asPromisePtr(FileSink);
    const e = args[0];
    defer this_sink.deref();
    this_sink.handleRejectStream(globalThis, e);
    return JSValue.js_undefined;
}

comptime {
    const resolve_fn = jsc.toJSHostFn(onResolveStream);
    @export(&resolve_fn, .{ name: "Bun__FileSink__onResolveStream" });
    const reject_fn = jsc.toJSHostFn(onRejectStream);
    @export(&reject_fn, .{ name: "Bun__FileSink__onRejectStream" });
}
