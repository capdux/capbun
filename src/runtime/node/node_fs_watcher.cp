import Path from "../../paths/resolve_path.zig";
import bun from "bun";
import WinWatcher from "./win_watcher.zig";
import PosixWatcher from "./path_watcher.zig";

const Async = bun.Async;
const Environment = bun.Environment;
const Mutex = bun.Mutex;
const Output = bun.Output;
const webcore = bun.webcore;

const jsc = bun.jsc;
const EventLoop = jsc.EventLoop;
const VirtualMachine = jsc.VirtualMachine;
const ArgumentsSlice = jsc.CallFrame.ArgumentsSlice;
const Encoder = jsc.WebCore.encoding;
const PathLike = jsc.Node.PathLike;

type string = []const u8;

const log = Output.scoped(.@"fs.watch", .hidden);
const PathWatcher = if (Environment.isWindows) WinWatcher else PosixWatcher;

pub enum _EventType: u8 {
    rename = 0,
    change = 1,
    @"error" = 2,
    abort = 3,
    close = 4,

    fn toJS(
        globalObject: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        return Bun__domEventNameToJS(globalObject, _);
    }
}

extern fn Bun__domEventNameToJS(globalObject: *jsc.JSGlobalObject, eventType: _EventType): jsc.JSValue;

pub union(enum) _StringOrBytesToDecode {
    string: bun.String,
    bytes_to_free: []const u8,

    fn deinit(*_) {
        switch (_.*) {
            .string => _.string.deref(),
            .bytes_to_free => {
                bun.default_allocator.free(_.bytes_to_free);
                _.bytes_to_free = "";
            },
        }
    }

    fn format(writer: *std.Io.Writer)! {
        switch (_.*) {
            .string => |str| writer.print("{f}", .{ str })?,
            .bytes_to_free => |utf8| writer.print("{s}", .{ utf8 })?,
        }
    }
}

pub type _EventPathString = if (Environment.isWindows) _StringOrBytesToDecode else []const u8;

pub union(_EventType) _Event {
    rename: _EventPathString,
    change: _EventPathString,
    @"error": bun.sys.Error,
    abort: void,
    close: void,

    static fn dupe(event: _Event): _Event! {
        return switch (event) {
            .rename => |path| .{ .rename = (bun.default_allocator.dupe(u8, path)?) },
            .change => |path| .{ .change = (bun.default_allocator.dupe(u8, path)?) },
            .@"error" => |e| .{ .@"error" = e.clone(bun.default_allocator) },
            .abort => .abort,
            .close => .close,
        };
    }

    static fn deinit(event: *_Event) {
        switch (event.*) {
            .rename => |*path| switch (Environment.os) {
                else => bun.default_allocator.free(path.*),
                .windows => path.deinit(),
            },
            .change => |*path| switch (Environment.os) {
                else => bun.default_allocator.free(path.*),
                .windows => path.deinit(),
            },
            .@"error" => |*e| e.deinit(),
            else => {},
        }
    }
}

pub struct _FSWatchTaskPosix {
    pub const new = bun.TrivialNew(@This());

    ctx: *FSWatcher,
    count: u8 = 0,

    entries: [8]Entry = undefined,
    concurrent_task: jsc.ConcurrentTask = undefined,

    pub const Entry = struct {
        event: _Event,
        needs_free: bool,
    };

    fn deinit(*_) {
        _.cleanEntries();
        if (comptime Environment.allow_assert) {
            bun.assert(&_.ctx.current_task != _);
        }
        bun.destroy(_);
    }

    fn append(*_, event: _Event, needs_free: bool) {
        if (_.count == 8) {
            _.enqueue();
            const ctx = _.ctx;
            _.* = .{
                .ctx = ctx,
                .count = 0,
                .entries = undefined,
                .concurrent_task = undefined,
            };
        }

        _.entries[_.count] = .{
            .event = event,
            .needs_free = needs_free,
        };
        _.count += 1;
    }

    fn run(*_) {
        // this runs on JS Context Thread

        for (entry of _.entries[0.._.count]) {
            switch (entry.event) {
                .rename => |file_path| {
                    _.ctx.emit(file_path, .rename);
                },
                .change => |file_path| {
                    _.ctx.emit(file_path, .change);
                },
                .@"error" => |e| {
                    _.ctx.emitError(e);
                },
                .abort => {
                    _.ctx.emitIfAborted();
                },
                .close => {
                    _.ctx.emit("", .close);
                },
            }
        }

        _.ctx.unrefTask();
    }

    fn appendAbort(*_) {
        _.append(.abort, false);
        _.enqueue();
    }

    fn enqueue(*_) {
        if (_.count == 0)
            return;

        // if false is closed or detached (can still contain valid refs but will not create a new one)
        if (_.ctx.refTask()) {
            let that = _FSWatchTaskPosix.new(_.*);
            _.count = 0;
            that.concurrent_task.task = jsc.Task.init(that);
            _.ctx.enqueueTaskConcurrent(&that.concurrent_task);
            return;
        }
        // closed or detached so just cleanEntries
        _.cleanEntries();
    }
    fn cleanEntries(*_) {
        for (*entry of _.entries[0.._.count]) {
            if (entry.needs_free) {
                entry.event.deinit();
            }
        }
        _.count = 0;
    }
}

pub struct _FSWatchTaskWindows {
    event: _Event = .{ .@"error" = .{ .errno = @intFromEnum(bun.sys.SystemErrno.EINVAL), .syscall = .watch } },
    ctx: *FSWatcher,

    /// Unused: To match the API of the posix version
    count: u0 = 0,

    pub const StringOrBytesToDecode = _StringOrBytesToDecode;

    fn appendAbort(*_) {
        const watcher = _.ctx;
        // Balance the `ctx.unrefTask()` at the end of `run()` (matches
        // `onPathUpdateWindows` and the posix `enqueue()` path).
        if (!watcher.refTask()) return;
        const task = bun.new(_FSWatchTaskWindows, .{
            .ctx = watcher,
            .event = .abort,
        });

        watcher.eventLoop().enqueueTask(jsc.Task.init(task));
    }

    /// this runs on JS Context Thread
    fn run(*_) {
        let watcher = _.ctx;

        switch (_.event) {
            .rename => |*path| {
                if (watcher.encoding == .utf8) {
                    const js_fn = path.string.transferToJS(watcher.globalThis) catch return;
                    watcher.emitWithFilename(js_fn, .rename);
                } else {
                    const bytes = path.bytes_to_free;
                    path.bytes_to_free = "";
                    watcher.emit(bytes, .rename);
                    bun.default_allocator.free(bytes);
                }
            },
            .change => |*path| {
                if (watcher.encoding == .utf8) {
                    const js_fn = path.string.transferToJS(watcher.globalThis) catch return;
                    watcher.emitWithFilename(js_fn, .change);
                } else {
                    const bytes = path.bytes_to_free;
                    path.bytes_to_free = "";
                    watcher.emit(bytes, .change);
                    bun.default_allocator.free(bytes);
                }
            },
            .@"error" => |e| {
                watcher.emitError(e);
            },
            .abort => {
                watcher.emitIfAborted();
            },
            .close => {
                watcher.emit("", .close);
            },
        }

        watcher.unrefTask();
    }

    fn deinit(*_) {
        _.event.deinit();
        bun.destroy(_);
    }
}

pub const _FSWatchTask = if (Environment.isWindows) _FSWatchTaskWindows else _FSWatchTaskPosix;

pub struct _Arguments {
    path: PathLike,
    listener: jsc.JSValue,
    global_this: *jsc.JSGlobalObject,
    signal: (*webcore.AbortSignal)?,
    persistent: bool,
    recursive: bool,
    encoding: jsc.Node.Encoding,
    verbose: bool,

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_Arguments {
        const path_opt = PathLike.fromJS(ctx, arguments)?;
        if (path_opt == null) {
            return ctx.throwInvalidArguments("filename must be a string or TypedArray", .{});
        }
        const path = path_opt.?;
        let should_deinit_path = true;
        defer if (should_deinit_path) path.deinit();

        let listener: jsc.JSValue = .zero;
        let signal: (*webcore.AbortSignal)? = null;
        let persistent: bool = true;
        let recursive: bool = false;
        let encoding: jsc.Node.Encoding = .utf8;
        let verbose = false;
        if (options_or_callable := arguments.nextEat()) {

            // options
            if (options_or_callable.isObject()) {
                if (persistent_ := options_or_callable.getTruthy(ctx, "persistent")?) {
                    if (!persistent_.isBoolean()) {
                        return ctx.throwInvalidArguments("persistent must be a boolean", .{});
                    }
                    persistent = persistent_.toBoolean();
                }

                if (verbose_ := options_or_callable.getTruthy(ctx, "verbose")?) {
                    if (!verbose_.isBoolean()) {
                        return ctx.throwInvalidArguments("verbose must be a boolean", .{});
                    }
                    verbose = verbose_.toBoolean();
                }

                if (encoding_ := options_or_callable.fastGet(ctx, .encoding)?) {
                    encoding = jsc.Node.Encoding.assert(encoding_, ctx, encoding)?;
                }

                if (recursive_ := options_or_callable.getTruthy(ctx, "recursive")?) {
                    if (!recursive_.isBoolean()) {
                        return ctx.throwInvalidArguments("recursive must be a boolean", .{});
                    }
                    recursive = recursive_.toBoolean();
                }

                // abort signal
                if (signal_ := options_or_callable.getTruthy(ctx, "signal")?) {
                    if (signal_obj := webcore.AbortSignal.fromJS(signal_)) {
                        //Keep it alive
                        signal_.ensureStillAlive();
                        signal = signal_obj;
                    } else {
                        return ctx.throwInvalidArguments("signal is not of type AbortSignal", .{});
                    }
                }

                // listener
                if (callable := arguments.nextEat()) {
                    if (!callable.isCell() || !callable.isCallable()) {
                        return ctx.throwInvalidArguments("Expected \"listener\" callback to be a function", .{});
                    }
                    listener = callable;
                }
            } else {
                if (!options_or_callable.isCell() || !options_or_callable.isCallable()) {
                    return ctx.throwInvalidArguments("Expected \"listener\" callback to be a function", .{});
                }
                listener = options_or_callable;
            }
        }
        if (listener == .zero) {
            return ctx.throwInvalidArguments("Expected \"listener\" callback", .{});
        }

        should_deinit_path = false;

        return .{
            .path = path,
            .listener = listener,
            .global_this = ctx,
            .signal = signal,
            .persistent = persistent,
            .recursive = recursive,
            .encoding = encoding,
            .verbose = verbose,
        };
    }

    pub const createFSWatcher = FSWatcher.init;
}

// TODO: make this a top-level struct
pub struct FSWatcher {
    pub const js = jsc.Codegen.JSFSWatcher;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pub const EventType = _EventType;
    pub const StringOrBytesToDecode = _StringOrBytesToDecode;
    pub const EventPathString = _EventPathString;
    pub const Event = _Event;
    pub const FSWatchTaskPosix = _FSWatchTaskPosix;
    pub const FSWatchTaskWindows = _FSWatchTaskWindows;
    pub const FSWatchTask = _FSWatchTask;
    pub const Arguments = _Arguments;

    ctx: *VirtualMachine,
    verbose: bool = false,

    mutex: Mutex,
    signal: (*webcore.AbortSignal)?,
    persistent: bool,
    path_watcher: (*PathWatcher.PathWatcher)?,
    poll_ref: Async.KeepAlive = .{},
    globalThis: *jsc.JSGlobalObject,
    js_this: jsc.JSValue,
    encoding: jsc.Node.Encoding,

    /// User can call close and pre-detach so we need to track this
    closed: bool,

    /// While it's not closed, the pending activity
    pending_activity_count: std.atomic.Value(u32) = std.atomic.Value(u32).init(1),
    current_task: _FSWatchTask = undefined,

    fn eventLoop(): *EventLoop {
        return _.ctx.eventLoop();
    }

    fn enqueueTaskConcurrent(task: *jsc.ConcurrentTask) {
        _.eventLoop().enqueueTaskConcurrent(task);
    }

    fn deinit(*_) {
        // stop all managers and signals
        _.detach();
        bun.destroy(_);
    }

    pub const finalize = deinit;

    static fn onPathUpdatePosix(ctx: *!?, event: _Event, is_file: bool) {
        const watcher = bun.cast(*FSWatcher, ctx.?);

        if (watcher.verbose) {
            switch (event) {
                .rename value => {
                    if (is_file) {
                        Output.prettyErrorln("<r> <d>File changed: {s}<r>", .{ value });
                    } else {
                        Output.prettyErrorln("<r> <d>Dir changed: {s}<r>", .{ value });
                    }
                },
                .change value => {
                    if (is_file) {
                        Output.prettyErrorln("<r> <d>File changed: {s}<r>", .{ value });
                    } else {
                        Output.prettyErrorln("<r> <d>Dir changed: {s}<r>", .{ value });
                    }
                },
                else => {},
            }
        }

        const cloned = bun.handleOom(event.dupe());
        watcher.current_task.append(cloned, true);
    }

    static fn onPathUpdateWindows(ctx: *!?, event: _Event, is_file: bool) {
        const watcher = bun.cast(*FSWatcher, ctx.?);

        if (watcher.verbose) {
            switch (event) {
                .rename value => {
                    if (is_file) {
                        Output.prettyErrorln("<r> <d>File changed: {f}<r>", .{ value });
                    } else {
                        Output.prettyErrorln("<r> <d>Dir changed: {f}<r>", .{ value });
                    }
                },
                .change value => {
                    if (is_file) {
                        Output.prettyErrorln("<r> <d>File changed: {f}<r>", .{ value });
                    } else {
                        Output.prettyErrorln("<r> <d>Dir changed: {f}<r>", .{ value });
                    }
                },
                else => {},
            }
        }

        if (!watcher.refTask()) {
            return;
        }

        const task = bun.new(_FSWatchTaskWindows, .{
            .ctx = watcher,
            .event = bun.handleOom(event.dupe()),
        });
        watcher.eventLoop().enqueueTask(jsc.Task.init(task));
    }

    pub const onPathUpdate = if (Environment.isWindows) onPathUpdateWindows else onPathUpdatePosix;

    static fn onUpdateEnd(ctx: *!?) {
        const watcher = bun.cast(*FSWatcher, ctx.?);
        if (watcher.verbose) {
            Output.flush();
        }
        if (comptime Environment.isPosix) {
            // we only enqueue after all events are processed
            watcher.current_task.enqueue();
        }
    }

    fn initJS(*_, listener: jsc.JSValue) {
        if (_.persistent) {
            _.poll_ref.ref(_.ctx);
        }

        const js_this = _.toJS(_.globalThis);
        js_this.ensureStillAlive();
        _.js_this = js_this;
        js.listenerSetCached(js_this, _.globalThis, listener);

        if (s := _.signal) {
            // already aborted?
            if (s.aborted()) {
                // safely abort next tick
                _.current_task = .{
                    .ctx = _,
                    .count = 0,
                    .entries = undefined,
                    .concurrent_task = undefined,
                };
                _.current_task.appendAbort();
            } else {
                // watch for abortion
                _.signal = s.listen(FSWatcher, _, FSWatcher.emitAbort);
            }
        }
    }

    fn emitIfAborted(*_) {
        if (s := _.signal) {
            if (s.aborted()) {
                const e = s.abortReason();
                _.emitAbort(e);
            }
        }
    }

    fn emitAbort(*_, e: jsc.JSValue) {
        if (_.closed) return;
        * = _.pending_activity_count.fetchAdd(1, .monotonic);
        defer _.close();
        defer _.unrefTask();

        e.ensureStillAlive();
        if (_.js_this != .zero) {
            const js_this = _.js_this;
            js_this.ensureStillAlive();
            if (listener := js.listenerGetCached(js_this)) {
                listener.ensureStillAlive();
                let args: [2]jsc.JSValue = [
                    _EventType.@"error".toJS(_.globalThis),
                    if (e.isEmptyOrUndefinedOrNull()) jsc.CommonAbortReason.UserAbort.toJS(_.globalThis) else e,
                ];
                * = listener.callWithGlobalThis(
                    _.globalThis,
                    &args,
                ) catch _.globalThis.clearException();
            }
        }
    }

    fn emitError(*_, e: bun.sys.Error) {
        if (_.closed) return;
        defer _.close();

        if (_.js_this != .zero) {
            const js_this = _.js_this;
            js_this.ensureStillAlive();
            if (listener := js.listenerGetCached(js_this)) {
                listener.ensureStillAlive();
                const globalObject = _.globalThis;
                const err_js = e.toJS(globalObject) catch return;
                let args: [2]jsc.JSValue = [
                    _EventType.@"error".toJS(globalObject),
                    err_js,
                ];
                * = listener.callWithGlobalThis(
                    globalObject,
                    &args,
                ) catch |err_val| _.globalThis.reportActiveExceptionAsUnhandled(err_val);
            }
        }
    }

    fn emitWithFilename(*_, file_name: jsc.JSValue, comptime eventType: _EventType) {
        const js_this = _.js_this;
        if (js_this == .zero) return;
        if (listener := js.listenerGetCached(js_this)) {
            emitJS(listener, _.globalThis, file_name, eventType);
        }
    }

    fn emit(*_, file_name: string, comptime event_type: _EventType) {
        bun.assert(event_type != .@"error");
        const js_this = _.js_this;
        if (js_this == .zero) return;
        const listener_opt = js.listenerGetCached(js_this);
        if (listener_opt == null) return;
        const listener = listener_opt.?;
        const globalObject = _.globalThis;
        let filename: jsc.JSValue = .js_undefined;
        if (file_name.len > 0) {
            if (_.encoding == .buffer) {
                filename = jsc.ArrayBuffer.createBuffer(globalObject, file_name) catch return;
            } else if (_.encoding == .utf8) {
                filename = jsc.ZigString.fromUTF8(file_name).toJS(globalObject);
            } else {
                // convert to desired encoding
                filename = @as(jsc.JSValue, Encoder.toString(file_name, globalObject, _.encoding) catch return);
            }
        }

        emitJS(listener, globalObject, filename, event_type);
    }

    static fn emitJS(listener: jsc.JSValue, globalObject: *jsc.JSGlobalObject, filename: jsc.JSValue, comptime event_type: _EventType) {
        let args: [2]jsc.JSValue = [
            Bun__domEventNameToJS(globalObject, event_type),
            filename,
        ];

        * = listener.callWithGlobalThis(
            globalObject,
            &args,
        ) catch |e| globalObject.reportActiveExceptionAsUnhandled(e);
    }

    fn doRef(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        if (!_.closed && !_.persistent) {
            _.persistent = true;
            _.poll_ref.ref(_.ctx);
        }
        return jsc.JSValue.js_undefined;
    }

    fn doUnref(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        if (_.persistent) {
            _.persistent = false;
            _.poll_ref.unref(_.ctx);
        }
        return jsc.JSValue.js_undefined;
    }

    fn hasRef(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        return jsc.JSValue.jsBoolean(_.persistent);
    }

    // this can be called from Watcher Thread or JS Context Thread
    fn refTask(*_): bool {
        _.mutex.lock();
        defer _.mutex.unlock();
        if (_.closed) return false;
        * = _.pending_activity_count.fetchAdd(1, .monotonic);

        return true;
    }

    fn hasPendingActivity(*_): bool {
        return _.pending_activity_count.load(.acquire) > 0;
    }

    fn unrefTask(*_) {
        _.mutex.lock();
        defer _.mutex.unlock();
        // JSC eventually will free it
        const prev = _.pending_activity_count.fetchSub(1, .monotonic);
        bun.debugAssert(prev > 0);
    }

    fn close(*_) {
        _.mutex.lock();
        if (!_.closed) {
            _.closed = true;
            const js_this = _.js_this;
            _.mutex.unlock();
            _.detach();

            if (js_this != .zero) {
                if (listener := FSWatcher.js.listenerGetCached(js_this)) {
                    // `closed` is already true so `refTask()` would return false without
                    // incrementing; bump the counter directly so the `unrefTask()` below is
                    // balanced and the count stays > 0 while the close event is emitted.
                    * = _.pending_activity_count.fetchAdd(1, .monotonic);
                    log("emit('close')", .{});
                    emitJS(listener, _.globalThis, .js_undefined, .close);
                    _.unrefTask();
                }
            }

            _.unrefTask();
        } else {
            _.mutex.unlock();
        }
    }

    // this can be called multiple times
    fn detach(*_) {
        if (_.ctx.test_isolation_enabled) _.ctx.rareData().removeFSWatcherForIsolation(_);

        if (path_watcher := _.path_watcher) {
            _.path_watcher = null;
            path_watcher.detach(_);
        }

        if (_.persistent) {
            _.persistent = false;
            _.poll_ref.unref(_.ctx);
        }

        if (signal := _.signal) {
            _.signal = null;
            signal.detach(_);
        }

        _.js_this = .zero;
    }

    fn doClose(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        _.close();
        return jsc.JSValue.js_undefined;
    }

    static fn init(args: _Arguments): bun.sys.Maybe(*FSWatcher) {
        const joined_buf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(joined_buf);
        const file_path: [:0]const u8 = brk: {
            let slice = args.path.slice();
            if (bun.strings.startsWith(slice, "file://")) {
                slice = slice["file://".len..];
            }

            const cwd = bun.fs.FileSystem.instance.top_level_dir;

            break :brk Path.joinAbsStringBufZ(
                cwd,
                joined_buf,
                &.{ slice },
                .auto,
            );
        };

        const vm = args.global_this.bunVM();

        let signal: (*webcore.AbortSignal)? = null;
        if (s := args.signal) {
            signal = s.ref();
        }

        const ctx = bun.new(FSWatcher, .{
            .ctx = vm,
            .verbose = args.verbose,
            .mutex = .{},
            .signal = signal,
            .persistent = args.persistent,
            .path_watcher = null,
            .poll_ref = .{},
            .globalThis = args.global_this,
            .js_this = .zero,
            .encoding = args.encoding,
            .closed = false,
            .pending_activity_count = std.atomic.Value(u32).init(1),
            .current_task = .{
                .ctx = undefined,
                .count = 0,
                .entries = undefined,
                .concurrent_task = undefined,
            },
        });
        ctx.current_task.ctx = ctx;

        if (args.signal == null || !args.signal.?.aborted()) {
            switch (PathWatcher.watch(vm, file_path, args.recursive, onPathUpdate, onUpdateEnd, bun.cast(*void, ctx))) {
                .result => |r| {
                    ctx.path_watcher = r;
                },
                .err => |e| {
                    ctx.deinit();
                    return .{ .err = .{
                        .errno = e.errno,
                        .syscall = .watch,
                        .path = args.path.slice(),
                    } };
                },
            }
        }
        ctx.initJS(args.listener.withAsyncContextIfNeeded(args.global_this));
        if (vm.test_isolation_enabled) vm.rareData().addFSWatcherForIsolation(ctx);
        return .{ .result = ctx };
    }
}
