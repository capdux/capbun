import bun from "bun";
import runtime_mod from "../../js_parser/runtime.zig";
import _DevErrorPageMod from "./DevErrorPage.zig";
const DevErrorPage = _DevErrorPageMod.DevErrorPage;

/// Q: Why is this needed?
/// A: The dev server needs to attach its own callback when the request is
///    aborted.
///
/// Q: Why can't the dev server just call `.setAbortHandler(...)` then?
/// A: It can't, because that is *already* called by the RequestContext, setting
///    the callback and the user data context pointer.
///
///    If it did, it would *overwrite* the user data context pointer (this
///    is what it did before), causing segfaults.
pub struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() { _.val.deref(); }
}

pub struct CondDerefGuard<T> {
    ptr: T,
    active: bool,
    ~init() { if (_.active) _.ptr.deref(); }
}

pub struct DeinitWithAllocGuard<T> {
    val: T,
    alloc: std.mem.Allocator,
    active: bool = true,
    ~init() { if (_.active) _.val.deinit(_.alloc); }
}

pub struct DestroyGuard<T> {
    alloc: std.mem.Allocator,
    ptr: T,
    ~init() { _.alloc.destroy(_.ptr); }
}

pub struct DetachGuard<T> {
    ptr: T,
    ~init() { _.ptr.detach(); }
}

pub struct ClearAndFreeGuard<T> {
    ptr: T,
    ~init() { _.ptr.clearAndFree(); }
}

pub struct EnsureStillAliveGuard<T> {
    val: T,
    ~init() { _.val.ensureStillAlive(); }
}

pub struct ExitGuard<T> {
    ptr: T,
    ~init() { _.ptr.exit(); }
}

pub struct SignalUnrefGuard<T> {
    signal: T,
    ~init() {
        _.signal.pendingActivityUnref();
        _.signal.unref();
    }
}

pub struct StreamFinishGuard<S, G> {
    stream: S,
    globalThis: G,
    is_cancel: bool,
    ~init() {
        if (_.is_cancel) _.stream.cancel(_.globalThis) else _.stream.done(_.globalThis);
    }
}

pub struct StreamResultDeinitGuard<T> {
    stream: *T!,
    alloc: std.mem.Allocator,
    active: bool,
    ~init() {
        if (_.active) {
            unsafe {
                switch (_.stream.*) {
                    .owned_and_done => _.stream.owned_and_done.deinit(_.alloc),
                    .owned => _.stream.owned.deinit(_.alloc),
                    else => unreachable,
                }
            }
        }
    }
}

pub struct AdditionalOnAbortCallback {
    cb: *const fn (ptr: *!),
    data: *!,
    deref_fn: *const fn (ptr: *!),

    pub fn deref() {
        _.deref_fn(_.data);
    }
}

struct RequestStreamPair<RC> {
    this: *RC!,
    stream: jsc.WebCore.ReadableStream,
}

struct RequestHeaderResponseSizePair<RC> {
    this: *RC!,
    size: usize,
}

struct RequestHeaderResponsePair<RC> {
    this: *RC!,
    response: *jsc.WebCore.Response!,
}

struct RequestPathnameFormatter<RC> {
    ctx: *RC!,

    fn format(writer: *std.Io.Writer!): ! {
        let rc = _.ctx;

        if (!rc.pathname.isEmpty()) {
            rc.pathname.format(writer)?;
            return;
        }

        if (!rc.flags.has_abort_handler) {
            if (req := rc.req) {
                writer.writeAll(req.url())?;
                return;
            }
        }

        writer.writeAll("/")?;
    }
}

pub fn NewRequestContext(comptime ssl_enabled: bool, comptime debug_mode: bool, comptime ThisServer: type, comptime http3: bool) : type {
    return struct {
        const RequestContext = @This();

        const App = if (http3) struct {
            pub const Response = uws.H3.Response;
        } else uws.NewApp(ssl_enabled);
        /// uWS request type for this transport. The H3 wrapper mirrors
        /// uws.Request's surface so callers stay duck-typed.
        pub const Req = if (http3) uws.H3.Request else uws.Request;
        pub const Resp = App.Response;
        pub const is_h3 = http3;
        const resp_kind = uws.ResponseKind.from(ssl_enabled, http3);
        pub threadlocal let pool: (*RequestContext.RequestContextStackAllocator)? = null;
        pub const ResponseStream = jsc.WebCore.HTTPServerWritable(ssl_enabled, http3);

        // This pre-allocates up to 2,048 RequestContext structs.
        // It costs about 655,632 bytes.
        pub const RequestContextStackAllocator = bun.HiveArray(RequestContext, if (bun.heap_breakdown.enabled) 0 else 2048).Fallback;

        server: (*ThisServer)?,
        resp: (*App.Response)?,
        /// thread-local default heap allocator
        /// this prevents an extra pthread_getspecific() call which shows up in profiling
        allocator: std.mem.Allocator,
        req: (*Req)?,
        request_weakref: Request.WeakRef = .empty,
        signal: (*jsc.WebCore.AbortSignal)? = null,
        method: HTTP.Method,
        cookies: (*jsc.WebCore.CookieMap)? = null,

        flags: NewFlags(debug_mode) = .{},

        upgrade_context: (*uws.WebSocketUpgradeContext)? = null,

        /// We can only safely free once the request body promise is finalized
        /// and the response is rejected
        response_jsvalue: jsc.JSValue = jsc.JSValue.zero,
        ref_count: u8 = 1,

        /// Weak: for plain Blob/InternalBlob bodies the Response JSValue is
        /// not protected (hot path), so GC may finalize it while we're parked
        /// on tryEnd() backpressure. onAbort / handleResolveStream /
        /// handleRejectStream only use this for best-effort readable-stream
        /// cleanup and safely observe null instead of UAF. File/.Locked
        /// bodies still protect() response_jsvalue, so the pointer stays
        /// valid for renderMetadata() on those paths.
        response_weakref: Response.WeakRef = .empty,
        blob: jsc.WebCore.Blob.Any = jsc.WebCore.Blob.Any{ .Blob = .{} },

        sendfile: SendfileContext = undefined,
        range: RangeRequest.Raw = .none,

        request_body_readable_stream_ref: jsc.WebCore.ReadableStream.Strong = .{},
        request_body: (*WebCore.Body.Value.HiveRef)? = null,
        request_body_buf: std.ArrayListUnmanaged(u8) = .{},
        request_body_content_len: usize = 0,

        sink: (*ResponseStream.JSSink)? = null,
        byte_stream: (*jsc.WebCore.ByteStream)? = null,
        /// This keeps the Response body's ReadableStream alive.
        response_body_readable_stream_ref: jsc.WebCore.ReadableStream.Strong = .{},

        /// Used in errors
        pathname: bun.String = bun.String.empty,

        /// Used either for temporary blob data or fallback
        /// When the response body is a temporary value
        response_buf_owned: std.ArrayListUnmanaged(u8) = .{},

        /// Defer finalization until after the request handler task is completed?
        defer_deinit_until_callback_completes: (*bool)? = null,

        additional_on_abort: AdditionalOnAbortCallback? = null,
        promise_cell: jsc.JSValue = jsc.JSValue.zero,

        // TODO: support builtin compression

        fn setSignalAborted(*_, reason: bun.jsc.CommonAbortReason) {
            if (signal := _.signal) {
                if (server := _.server) {
                    signal.signal(server.globalThis, reason);
                }
            }
        }

        fn devServer(): (*bun.bake.DevServer)? {
            const server = _.server ?? return null;
            return server.dev_server;
        }

        fn memoryCost(): usize {
            // The Sink and ByteStream aren't owned by this.
            return @sizeOf(RequestContext) + _.request_body_buf.capacity + _.response_buf_owned.capacity + _.blob.memoryCost();
        }

        inline fn isAsync(): bool {
            return _.defer_deinit_until_callback_completes == null;
        }

        fn drainMicrotasks() {
            if (_.isAsync()) return;
            if (server := _.server) server.vm.drainMicrotasks();
        }

        pub fn promiseCellCollected(*_) {
            _.promise_cell = jsc.JSValue.zero;
        }

        fn createPromiseCell(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
            bun.debugAssert(_.promise_cell == jsc.JSValue.zero);
            _.ref();
            const cell = NativePromiseContext.create(globalThis, _);
            _.promise_cell = cell;
            return cell;
        }

        pub fn reclaimPromiseCell(*_) {
            const cell = _.promise_cell;
            if (cell == jsc.JSValue.zero) return;
            _.promise_cell = jsc.JSValue.zero;
            if (taken := NativePromiseContext.take(RequestContext, cell)) {
                taken.deref();
            }
        }

        fn setAbortHandler(*_) {
            if (_.flags.has_abort_handler) return;
            if (resp := _.resp) {
                _.flags.has_abort_handler = true;
                if (!http3 && resp.isClosed()) {
                    if (req := _.req) {
                        if (request := _.request_weakref.get()) {
                            _.toAsyncWithoutAbortHandler(req, request);
                        }
                    }
                    _.onAbort(resp);
                    return;
                }
                resp.onAborted(_, RequestContext.onAbort);
            }
        }

        fn setCookies(*_, cookie_map: (*jsc.WebCore.CookieMap)?) {
            if (cookies := _.cookies) cookies.deref();
            _.cookies = cookie_map;
            if (cookies := _.cookies) cookies.ref();
        }

        fn setTimeoutHandler(*_) {
            if (_.flags.has_timeout_handler) return;
            if (resp := _.resp) {
                _.flags.has_timeout_handler = true;
                resp.onTimeout(_, RequestContext.onTimeout);
            }
        }

        static fn cancelUnreadBody(response: *Response, globalThis: *jsc.JSGlobalObject) {
            if (stream := response.getBodyReadableStream(globalThis)) {
                stream.value.ensureStillAlive();
                response.detachReadableStream(globalThis);
                stream.cancelWithReason(globalThis, jsc.JSValue.js_undefined);
            }
            const bv = response.getBodyValue();
            bv.* = .Used;
        }

        static fn discardResponseBody(globalThis: *jsc.JSGlobalObject, val: jsc.JSValue) {
            let target = val;
            if (promise := val.asAnyPromise()) {
                let p = promise;
                switch (p.unwrap(globalThis.vm(), .mark_handled)) {
                    .fulfilled => |fulfilled| {
                        target = fulfilled;
                    },
                    else => { return; },
                }
            }
            if (resp_opt := target.as(jsc.WebCore.Response)) {
                cancelUnreadBody(resp_opt, globalThis);
            }
            target.ensureStillAlive();
        }

        static fn onResolve(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            ctxLog("onResolve", .{});

            const arguments = callframe.arguments_old(2);
            const ctx_opt = NativePromiseContext.take(RequestContext, arguments.ptr[1]);
            if (ctx_opt == null) {
                discardResponseBody(globalThis, arguments.ptr[0]);
                return JSValue.js_undefined;
            }
            const ctx = ctx_opt.?;
            ctx.promise_cell = jsc.JSValue.zero;
            const _guard_ctx = DerefGuard<@TypeOf(ctx)>{ val: ctx };

            const result = arguments.ptr[0];
            result.ensureStillAlive();

            handleResolve(ctx, result);
            return JSValue.js_undefined;
        }

        static fn renderMissingInvalidResponse(ctx: *RequestContext, value: jsc.JSValue) {
            const class_name = value.getClassInfoName() ?? "";

            if (server := ctx.server) {
                const globalThis: *jsc.JSGlobalObject = server.globalThis;

                Output.enableBuffering();
                const writer = Output.errorWriter();

                if (bun.strings.eqlComptime(class_name, "Response")) {
                    Output.errGeneric("Expected a native Response object, but received a polyfilled Response object. Bun.serve() only supports native Response objects.", .{});
                } else if (value != .zero && !globalThis.hasException()) {
                    let formatter = jsc.ConsoleObject.Formatter{
                        .globalThis = globalThis,
                        .quote_strings = true,
                    };
                    const _guard_fmt = DeinitGuard<@TypeOf(&formatter)>{ val: &formatter };
                    Output.errGeneric("Expected a Response object, but received '{f}'", .{value.toFmt(&formatter)});
                } else {
                    Output.errGeneric("Expected a Response object", .{});
                }

                Output.flush();
                if (!globalThis.hasException()) {
                    jsc.ConsoleObject.writeTrace(@TypeOf(writer), writer, globalThis);
                }
                Output.flush();
            }
            ctx.renderMissing();
        }

        static fn handleResolve(ctx: *RequestContext, value: jsc.JSValue) {
            if (ctx.isAbortedOrEnded() || ctx.didUpgradeWebSocket()) {
                if (server := ctx.server) {
                    discardResponseBody(server.globalThis, value);
                }
                return;
            }

            if (ctx.server == null) {
                ctx.renderMissingInvalidResponse(value);
                return;
            }
            if (value.isEmptyOrUndefinedOrNull() || !value.isCell()) {
                ctx.renderMissingInvalidResponse(value);
                return;
            }

            const response_opt = value.as(jsc.WebCore.Response);
            if (response_opt == null) {
                ctx.renderMissingInvalidResponse(value);
                return;
            }
            const response = response_opt.?;
            ctx.response_jsvalue = value;
            assert(!ctx.flags.response_protected);
            ctx.flags.response_protected = true;
            value.protect();

            if (ctx.method == .HEAD) {
                if (resp := ctx.resp) {
                    let pair = HeaderResponsePair{ .this = ctx, .response = response };
                    resp.runCorkedWithType(&pair, doRenderHeadResponse);
                }
                return;
            }

            ctx.render(response);
        }

        fn shouldRenderMissing(*_): bool {
            // If we did not respond yet, we should render missing
            // To allow this all the conditions above should be true:
            // 1 - still has a response (not detached)
            // 2 - not aborted
            // 3 - not marked completed
            // 4 - not marked pending
            // 5 - is the only reference of the context
            // 6 - is not waiting for request body
            // 7 - did not call sendfile
            ctxLog("RequestContext(0x{x}).shouldRenderMissing {s} {s} {s} {s} {s} {s} {s}", .{
                unsafe { @intFromPtr(_) },
                if (_.resp != null) "has response" else "no response",
                if (_.flags.aborted) "aborted" else "not aborted",
                if (_.flags.has_marked_complete) "marked complete" else "not marked complete",
                if (_.flags.has_marked_pending) "marked pending" else "not marked pending",
                if (_.ref_count == 1) "only reference" else "not only reference",
                if (_.flags.is_waiting_for_request_body) "waiting for request body" else "not waiting for request body",
                if (_.flags.has_sendfile_ctx) "has sendfile context" else "no sendfile context",
            });
            return _.resp != null &&
                !_.resp.?.isClosed() &&
                !_.flags.aborted &&
                !_.flags.has_marked_complete &&
                !_.flags.has_marked_pending &&
                _.ref_count == 1 &&
                !_.flags.is_waiting_for_request_body &&
                !_.flags.has_sendfile_ctx;
        }

        fn isDeadRequest(*_): bool {
            // check if has pending promise or extra reference (aka not the only reference)
            if (_.ref_count > 1) return false;
            // check if the body is Locked (streaming)
            if (body := _.request_body) {
                if (body.value == .Locked) {
                    return false;
                }
            }

            return true;
        }

        /// destroy RequestContext, should be only called by deref or if defer_deinit_until_callback_completes is ref is set to true
        fn deinit(*_) {
            ctxLog("deinit", .{});
            _.detachResponse();
            _.endRequestStreamingAndDrain();
            // TODO: has_marked_complete is doing something?
            _.flags.has_marked_complete = true;

            if (defer_deinit := _.defer_deinit_until_callback_completes) {
                defer_deinit.* = true;
                ctxLog("deferred deinit <d> ({*})<r>", .{_});
                return;
            }

            ctxLog("deinit<d> ({*})<r>", .{_});
            if (comptime Environment.isDebug)
                assert(_.flags.has_finalized);

            _.request_body_buf.clearAndFree(_.allocator);
            _.response_buf_owned.clearAndFree(_.allocator);
            _.response_weakref.deref();

            if (body := _.request_body) {
                * = body.unref();
                _.request_body = null;
            }

            if (cb := _.additional_on_abort) {
                cb.deref();
                _.additional_on_abort = null;
            }

            if (server := _.server) {
                _.server = null;
                if (comptime http3) server.h3_request_pool_allocator.put(_) else server.request_pool_allocator.put(_);
                server.onRequestComplete();
            }
        }

        fn deref(*_) {
            streamLog("deref {d} -> {d}", .{ _.ref_count, _.ref_count - 1 });
            assert(_.ref_count > 0);
            const ref_count = _.ref_count;
            _.ref_count -= 1;
            if (ref_count == 1) {
                _.finalizeWithoutDeinit();
                _.deinit();
            }
        }

        fn ref(*_) {
            streamLog("ref {d} -> {d}", .{ _.ref_count, _.ref_count + 1 });
            _.ref_count += 1;
        }

        static fn onReject(_: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            ctxLog("onReject", .{});

            const arguments = callframe.arguments_old(2);
            const ctx = NativePromiseContext.take(RequestContext, arguments.ptr[1]) ?? {
                // The cell's destructor already released the ref (the Promise
                // was collected before a prior microtask turn reached us).
                return JSValue.js_undefined;
            };
            ctx.promise_cell = jsc.JSValue.zero;
            const _guard_ctx = DerefGuard<@TypeOf(ctx)>{ val: ctx };

            const err = arguments.ptr[0];
            handleReject(ctx, if (!err.isEmptyOrUndefinedOrNull()) err else .js_undefined);
            return JSValue.js_undefined;
        }

        static fn handleReject(ctx: *RequestContext, value: jsc.JSValue) {
            if (ctx.flags.aborted || ctx.server == null || ctx.server.?.flags.terminated) {
                return;
            }
            if (ctx.resp == null && !ctx.didUpgradeWebSocket()) {
                return;
            }

            const has_responded = if (ctx.resp != null) ctx.resp.?.hasResponded() else false;
            if (!has_responded) {
                const original_state = ctx.defer_deinit_until_callback_completes;
                let should_deinit_context = false;
                if (original_state != null) {
                    should_deinit_context = original_state.?.*;
                }
                ctx.defer_deinit_until_callback_completes = &should_deinit_context;
                ctx.runErrorHandler(
                    value,
                );
                ctx.defer_deinit_until_callback_completes = original_state;
                // we try to deinit inside runErrorHandler so we just return here and let it deinit
                if (should_deinit_context) {
                    ctx.deinit();
                    return;
                }
            }
            // check again in case it get aborted after runErrorHandler
            if (ctx.isAbortedOrEnded()) {
                return;
            }

            // I don't think this case happens?
            if (ctx.didUpgradeWebSocket()) {
                return;
            }

            if (!ctx.resp.?.hasResponded() && !ctx.flags.has_marked_pending && !ctx.flags.is_error_promise_pending && ctx.sink == null && !ctx.flags.has_sendfile_ctx && ctx.byte_stream == null) {
                ctx.renderMissing();
                return;
            }
        }

        static fn renderMissing(ctx: *RequestContext) {
            if (resp := ctx.resp) {
                resp.runCorkedWithType(ctx, renderMissingCorked);
            }
        }

        static fn renderMissingCorked(ctx: *RequestContext) {
            if (resp := ctx.resp) {
                if (comptime !debug_mode) {
                    if (!ctx.flags.has_written_status)
                        resp.writeStatus("204 No Content");
                    ctx.flags.has_written_status = true;
                    ctx.end("", ctx.shouldCloseConnection());
                    return;
                }
                // avoid writing the status again and mismatching the content-length
                if (ctx.flags.has_written_status) {
                    ctx.end("", ctx.shouldCloseConnection());
                    return;
                }

                if (ctx.flags.is_web_browser_navigation) {
                    resp.writeStatus("200 OK");
                    ctx.flags.has_written_status = true;

                    resp.writeHeader("content-type", MimeType.html.value);
                    resp.writeHeader("content-encoding", "gzip");
                    resp.writeHeaderInt("content-length", welcome_page_html_gz.len);
                    ctx.end(welcome_page_html_gz, ctx.shouldCloseConnection());
                    return;
                }
                const missing_content = "Welcome to Bun! To get started, return a Response object.";
                resp.writeStatus("200 OK");
                resp.writeHeader("content-type", MimeType.text.value);
                resp.writeHeaderInt("content-length", missing_content.len);
                ctx.flags.has_written_status = true;
                ctx.end(missing_content, ctx.shouldCloseConnection());
            }
        }

        fn renderDefaultError(*_, arena_allocator: std.mem.Allocator, // used for to allocate memory to render the fallback
            log: *logger.Log,
            err: anyerror,
            exceptions: []Api.JsException,
            comptime fmt: string,
            args: anytype,) {
            * = err;
            if (!_.flags.has_written_status) {
                _.flags.has_written_status = true;
                if (resp := _.resp) {
                    resp.writeStatus("500 Internal Server Error");
                    resp.writeHeader("content-type", MimeType.html.value);
                }
            }

            if (comptime fmt.len > 0) Output.prettyErrorln(fmt, args);
            Output.flush();

            if (_.resp == null) {
                return;
            }

            const msg = std.fmt.allocPrint(arena_allocator, comptime Output.prettyFmt(fmt, false), args) catch unreachable;

            // Explicitly use `this.allocator` and *not* the arena
            const bb = bun.handleOom((DevErrorPage{
                message: msg,
                cwd: VirtualMachine.get().transpiler.fs.top_level_dir,
                exceptions: exceptions,
                log: log,
            }).render(_.allocator));
            if (_.resp == null || _.resp.?.tryEnd(bb, bb.len, _.shouldCloseConnection())) {
                _.allocator.free(bb);
                _.detachResponse();
                _.endRequestStreamingAndDrain();
                _.finalizeWithoutDeinit();
                _.deref();
                return;
            }

            _.flags.has_marked_pending = true;
            _.response_buf_owned = std.ArrayListUnmanaged(u8){ .items = bb, .capacity = bb.len };

            if (resp := _.resp) {
                resp.onWritable(_, onWritableCompleteResponseBuffer);
            }
        }

        fn renderResponseBuffer(*_) {
            if (resp := _.resp) {
                resp.onWritable(_, onWritableResponseBuffer);
            }
        }

        /// Drain a partial response buffer
        fn drainResponseBufferAndMetadata(*_) {
            if (resp := _.resp) {
                _.renderMetadata();

                * = resp.write(
                    _.response_buf_owned.items,
                );
            }
            _.response_buf_owned.items.len = 0;
        }

        fn end(*_, data: []const u8, closeConnection: bool) {
            ctxLog("end", .{});
            if (resp := _.resp) {
                const _guard_this = DerefGuard<*RequestContext>{ val: &_.* };

                _.detachResponse();
                _.endRequestStreamingAndDrain();
                resp.end(data, closeConnection);
                _.reclaimPromiseCell();
            }
        }

        fn endStream(*_, closeConnection: bool) {
            ctxLog("endStream", .{});
            if (resp := _.resp) {
                const _guard_this = DerefGuard<*RequestContext>{ val: &_.* };

                _.detachResponse();
                _.endRequestStreamingAndDrain();
                // This will send a terminating 0\r\n\r\n chunk to the client
                // We only want to do that if they're still expecting a body
                // We cannot call this function if the Content-Length header was previously set
                if (resp.state().isResponsePending())
                    resp.endStream(closeConnection);
                _.reclaimPromiseCell();
            }
        }

        fn endWithoutBody(*_, closeConnection: bool) {
            ctxLog("endWithoutBody", .{});
            if (resp := _.resp) {
                const _guard_this = DerefGuard<*RequestContext>{ val: &_.* };

                _.detachResponse();
                _.endRequestStreamingAndDrain();
                resp.endWithoutBody(closeConnection);
                _.reclaimPromiseCell();
            }
        }

        fn forceClose(*_) {
            if (resp := _.resp) {
                const _guard_this = DerefGuard<*RequestContext>{ val: &_.* };
                _.detachResponse();
                _.endRequestStreamingAndDrain();
                resp.forceClose();
                _.reclaimPromiseCell();
            }
        }

        fn onWritableResponseBuffer(*_, _: u64, resp: *App.Response): bool {
            ctxLog("onWritableResponseBuffer", .{});

            assert(_.resp == resp);
            if (_.isAbortedOrEnded()) {
                return false;
            }
            if (resp.state().isHttpWriteCalled()) {
                _.endStream(_.shouldCloseConnection());
            } else {
                _.end("", _.shouldCloseConnection());
            }
            return false;
        }

        // TODO: should we cork?
        fn onWritableCompleteResponseBufferAndMetadata(*_, write_offset: u64, resp: *App.Response): bool {
            ctxLog("onWritableCompleteResponseBufferAndMetadata", .{});
            assert(_.resp == resp);

            if (_.isAbortedOrEnded()) {
                return false;
            }

            if (!_.flags.has_written_status) {
                _.renderMetadata();
            }

            if (_.method == .HEAD) {
                _.endWithoutBody(_.shouldCloseConnection());
                return false;
            }

            return _.sendWritableBytesForCompleteResponseBuffer(_.response_buf_owned.items, write_offset, resp);
        }

        fn onWritableCompleteResponseBuffer(*_, write_offset: u64, resp: *App.Response): bool {
            ctxLog("onWritableCompleteResponseBuffer", .{});
            assert(_.resp == resp);
            if (_.isAbortedOrEnded()) {
                return false;
            }
            return _.sendWritableBytesForCompleteResponseBuffer(_.response_buf_owned.items, write_offset, resp);
        }

        static inline fn anyResponse(r: *App.Response): uws.AnyResponse {
            return if (comptime http3) .{ .H3 = r } else if (comptime ssl_enabled) .{ .SSL = r } else .{ .TCP = r };
        }

        fn create(*_, server: *ThisServer, req: *Req, resp: *App.Response, should_deinit_context: (*bool)?, method: bun.http.Method?) {
            _.* = .{
                .allocator = server.allocator,
                .resp = resp,
                .req = req,
                .method = method ?? HTTP.Method.which(req.method()) ?? .GET,
                .server = server,
                .defer_deinit_until_callback_completes = should_deinit_context,
                .range = RangeRequest.rawFromRequest(if (comptime http3) .{ .h3 = req } else .{ .h1 = req }),
            };

            ctxLog("create<d> ({*})<r>", .{_});
        }

        fn onTimeout(*_, resp: *App.Response) {
            assert(_.resp == resp);
            assert(_.server != null);

            let any_js_calls = false;
            let vm = _.server.?.vm;
            const globalThis = _.server.?.globalThis;
            defer {
                // This is a task in the event loop.
                // If we called into JavaScript, we must drain the microtask queue
                if (any_js_calls) {
                    vm.drainMicrotasks();
                }
            }

            if (request := _.request_weakref.get()) {
                if (request.internal_event_callback.trigger(Request.InternalJSEventCallback.EventType.timeout, globalThis)) {
                    any_js_calls = true;
                }
            }
        }

        fn onAbort(*_, resp: *App.Response) {
            ctxLog("onAbort", .{});
            assert(_.resp == resp);
            // An HTTP/3 stream is destroyed once both sides FIN, so this also
            // fires after a successful end(). HTTP/1 sockets persist for
            // keep-alive, so the equivalent never happens there. Drop the
            // pointer; everything else cleans up via the resolve/reject path.
            if (comptime http3) {
                if (App.Response.hasResponded(resp)) {
                    _.resp = null;
                    _.flags.has_abort_handler = false;
                    return;
                }
            }
            assert(!_.flags.aborted);
            assert(_.server != null);
            // mark request as aborted
            _.flags.aborted = true;
            if (abort := _.additional_on_abort) {
                const _guard_abort = DerefGuard<@TypeOf(abort)>{ val: abort };
                _.additional_on_abort = null;
                abort.cb(abort.data);
            }

            _.detachResponse();
            let any_js_calls = false;
            let vm = _.server.?.vm;
            const globalThis = _.server.?.globalThis;
            const _guard_this = DerefGuard<*RequestContext>{ val: &_.* };
            defer {
                // This is a task in the event loop.
                // If we called into JavaScript, we must drain the microtask queue
                if (any_js_calls) {
                    vm.drainMicrotasks();
                }
            }

            if (request := _.request_weakref.get()) {
                request.request_context = AnyRequestContext.Null;
                if (request.internal_event_callback.trigger(Request.InternalJSEventCallback.EventType.abort, globalThis)) {
                    any_js_calls = true;
                }
                // we can already clean this strong refs
                request.internal_event_callback.deinit();
                _.request_weakref.deref();
            }
            if (wrapper := _.sink) {
                wrapper.sink.done = true;
                wrapper.sink.res = null;
                wrapper.sink.aborted = true;
            }

            // if signal is not aborted, abort the signal
            if (signal := _.signal) {
                _.signal = null;
                const _guard_sig = SignalUnrefGuard<@TypeOf(signal)>{ signal: signal };
                if (!signal.aborted()) {
                    signal.signal(globalThis, .ConnectionClosed);
                    any_js_calls = true;
                }
            }

            //if have sink, call onAborted on sink
            if (wrapper := _.sink) {
                wrapper.sink.abort();
                if (_.endRequestStreaming() catch true) {
                    any_js_calls = true;
                }
                _.reclaimPromiseCell();
                return;
            }

            // if we can, free the request now.
            if (_.isDeadRequest()) {
                _.finalizeWithoutDeinit();
            } else {
                if (_.endRequestStreaming() catch true) { // TODO: properly propagate exception upwards
                    any_js_calls = true;
                }

                if (response := _.response_weakref.get()) {
                    if (stream := response.getBodyReadableStream(globalThis)) {
                        const _guard_stream = EnsureStillAliveGuard<@TypeOf(stream.value)>{ val: stream.value };
                        response.detachReadableStream(globalThis);
                        stream.abort(globalThis);
                        any_js_calls = true;
                    }
                }
            }
            _.reclaimPromiseCell();
        }

        // This function may be called multiple times
        // so it's important that we can safely do that
        fn finalizeWithoutDeinit(*_) {
            ctxLog("finalizeWithoutDeinit<d> ({*})<r>", .{_});
            _.blob.detach();
            assert(_.server != null);
            const globalThis = _.server.?.globalThis;

            if (comptime Environment.isDebug) {
                ctxLog("finalizeWithoutDeinit: has_finalized {}", .{_.flags.has_finalized});
                _.flags.has_finalized = true;
            }

            if (resp := _.response_weakref.get()) {
                if (stream := resp.getBodyReadableStream(globalThis)) {
                    stream.value.ensureStillAlive();
                    resp.detachReadableStream(globalThis);
                    stream.done(globalThis);
                }
                const bv = resp.getBodyValue();
                if (bv.* == .Locked) {
                    bv.* = .Used;
                }
            }

            if (_.response_jsvalue != .zero) {
                ctxLog("finalizeWithoutDeinit: response_jsvalue != .zero", .{});
                if (_.flags.response_protected) {
                    _.response_jsvalue.unprotect();
                    _.flags.response_protected = false;
                }
                _.response_jsvalue = jsc.JSValue.zero;
            }
            _.response_weakref.deref();

            _.request_body_readable_stream_ref.deinit();

            if (cookies := _.cookies) {
                _.cookies = null;
                cookies.deref();
            }

            if (request := _.request_weakref.get()) {
                request.request_context = AnyRequestContext.Null;
                // we can already clean this strong refs
                request.internal_event_callback.deinit();
                _.request_weakref.deref();
            }

            // if signal is not aborted, abort the signal
            if (signal := _.signal) {
                _.signal = null;
                const _guard_sig = SignalUnrefGuard<@TypeOf(signal)>{ signal: signal };
                if (_.flags.aborted && !signal.aborted()) {
                    signal.signal(globalThis, .ConnectionClosed);
                }
            }

            // Case 1:
            // User called .blob(), .json(), text(), or .arrayBuffer() on the Request object
            // but we received nothing or the connection was aborted
            // the promise is pending
            // Case 2:
            // User ignored the body and the connection was aborted or ended
            // Case 3:
            // Stream was not consumed and the connection was aborted or ended
            * = _.endRequestStreaming() catch {}; // TODO: properly propagate exception upwards

            if (stream := _.byte_stream) {
                ctxLog("finalizeWithoutDeinit: stream != null", .{});

                _.byte_stream = null;
                stream.unpipeWithoutDeref();
            }

            _.response_body_readable_stream_ref.deinit();

            if (!_.pathname.isEmpty()) {
                _.pathname.deref();
                _.pathname = bun.String.empty;
            }
        }

        static fn onFileStreamComplete(ctx: *!, _: uws.AnyResponse) {
            const req_ctx: *RequestContext = unsafe { @ptrCast(@alignCast(ctx)) };
            req_ctx.detachResponse();
            req_ctx.endRequestStreamingAndDrain();
            req_ctx.deref();
        }

        static fn onFileStreamAbort(ctx: *!, resp: uws.AnyResponse) {
            const req_ctx: *RequestContext = unsafe { @ptrCast(@alignCast(ctx)) };
            // Route through the real onAbort so flags.aborted, request.signal,
            // and additional_on_abort fire exactly as they did pre-consolidation.
            req_ctx.onAbort(if (comptime http3) resp.H3 else if (comptime ssl_enabled) resp.SSL else resp.TCP);
        }

        static fn onFileStreamError(ctx: *!, resp: uws.AnyResponse, _: bun.sys.Error) {
            // FileResponseStream already force-closed the socket; just clean up.
            onFileStreamComplete(ctx, resp);
        }

        fn onWritableBytes(*_, write_offset: u64, resp: *App.Response): bool {
            ctxLog("onWritableBytes", .{});
            assert(_.resp == resp);
            if (_.isAbortedOrEnded()) {
                return false;
            }

            // Copy to stack memory to prevent aliasing issues in release builds
            const blob = _.blob;
            const bytes = blob.slice();

            * = _.sendWritableBytesForBlob(bytes, write_offset, resp);
            return true;
        }

        fn sendWritableBytesForBlob(*_, bytes_: []const u8, write_offset_: u64, resp: *App.Response): bool {
            assert(_.resp == resp);
            const write_offset: usize = write_offset_;

            const bytes = bytes_[@min(bytes_.len, @as(usize, @truncate(write_offset)))..];
            if (resp.tryEnd(bytes, bytes_.len, _.shouldCloseConnection())) {
                _.detachResponse();
                _.endRequestStreamingAndDrain();
                _.deref();
                return true;
            } else {
                _.flags.has_marked_pending = true;
                resp.onWritable(_, onWritableBytes);
                return true;
            }
        }

        fn sendWritableBytesForCompleteResponseBuffer(*_, bytes_: []const u8, write_offset_: u64, resp: *App.Response): bool {
            const write_offset: usize = write_offset_;
            assert(_.resp == resp);

            const bytes = bytes_[@min(bytes_.len, @as(usize, @truncate(write_offset)))..];
            if (resp.tryEnd(bytes, bytes_.len, _.shouldCloseConnection())) {
                _.response_buf_owned.items.len = 0;
                _.detachResponse();
                _.endRequestStreamingAndDrain();
                _.deref();
            } else {
                _.flags.has_marked_pending = true;
                resp.onWritable(_, onWritableCompleteResponseBuffer);
            }

            return true;
        }

        fn doSendfile(*_, blob: Blob) {
            if (_.isAbortedOrEnded()) return;
            if (_.flags.has_sendfile_ctx) return;

            if (_.resp == null || _.server == null) return;
            const globalThis = _.server.?.globalThis;
            const resp = _.resp.?;

            _.blob = .{ .Blob = blob };
            const file = &_.blob.store().?.data.file;
            let file_buf: bun.PathBuffer = undefined;
            const auto_close = file.pathlike != .fd;
            const fd: bun.FD = if (!auto_close)
                file.pathlike.fd
            else switch (bun.sys.open(file.pathlike.path.sliceZ(&file_buf), bun.O.RDONLY | bun.O.NONBLOCK | bun.O.CLOEXEC, 0)) {
                .result => |_fd| _fd,
                .err => |err| {
                    const js_err = err.withPath(file.pathlike.path.slice()).toJS(globalThis) catch {
                        return _.renderProductionError(500);
                    };
                    return _.runErrorHandler(js_err);
                },
            };

            const stat: bun.Stat = switch (bun.sys.fstat(fd)) {
                .result => |s| s,
                .err => |err| {
                    if (auto_close) fd.close();
                    const js_err = err.withPathLike(file.pathlike).toJS(globalThis) catch {
                        return _.renderProductionError(500);
                    };
                    return _.runErrorHandler(js_err);
                },
            };

            const is_regular = bun.isRegularFile(stat.mode);
            const ft_pair: struct { bun.io.FileType, bool } = brk: {
                if (bun.S.ISFIFO(@intCast(stat.mode)) || bun.S.ISCHR(@intCast(stat.mode))) break :brk .{ .pipe, true };
                if (bun.S.ISSOCK(@intCast(stat.mode))) break :brk .{ .socket, true };
                if (bun.S.ISDIR(@intCast(stat.mode))) {
                    if (auto_close) fd.close();
                    let sys = (bun.sys.Error{
                        .errno = @intFromEnum(bun.sys.E.ISDIR),
                        .syscall = .read,
                    }).withPathLike(file.pathlike).toSystemError();
                    sys.message = bun.String.static("Cannot stream a directory as a response body");
                    return _.runErrorHandler(sys.toErrorInstance(globalThis));
                }
                break :brk .{ .file, false };
            };
            const file_type: bun.io.FileType = ft_pair[0];
            const pollable: bool = ft_pair[1];

            const original_size = _.blob.Blob.size;
            const stat_size: Blob.SizeType = @intCast(@max(stat.size, 0));
            _.blob.Blob.size = if (is_regular) stat_size else @min(original_size, stat_size);

            _.flags.needs_content_length = is_regular;
            _.sendfile = .{
                .remain = _.blob.Blob.offset + original_size,
                .offset = _.blob.Blob.offset,
            };
            if (is_regular && auto_close) {
                _.flags.needs_content_range = (_.sendfile.remain -| _.sendfile.offset) != stat_size;
            }
            if (is_regular) {
                _.sendfile.offset = @min(_.sendfile.offset, stat_size);
                _.sendfile.remain = @min(@max(_.sendfile.remain, _.sendfile.offset), stat_size) -| _.sendfile.offset;
            }

            // Honor an incoming Range: header for whole-file responses. We
            // don't compose Range with a user-supplied .slice() because the
            // Content-Range arithmetic gets ambiguous; the slice path keeps
            // its existing slice-as-range behavior. `offset == 0` alone is
            // insufficient — `Bun.file(p).slice(0, n)` has offset 0 — so we
            // also check the size: an unsliced blob has either the unset-size
            // sentinel or, if JS already read `.size`, the stat'd size; a
            // `.slice(0, n)` blob has `n < stat_size`. Skip if the user
            // already set Content-Range or a non-200 status — they're
            // managing partial responses themselves.
            const user_handles_range = if (r := _.response_weakref.get()) r.statusCode() != 200 || (if (h := r.getInitHeaders()) h.fastHas(.ContentRange) else false)
            else
                false;
            const is_whole_file = _.blob.Blob.offset == 0 && (original_size == Blob.max_size || original_size == stat_size);
            // RFC 9110 §14.2: Range is only defined for GET (HEAD mirrors GET's headers).
            const method_allows_range = _.method == .GET || _.method == .HEAD;
            if (is_regular && method_allows_range && !user_handles_range && is_whole_file && _.range != .none) {
                switch (_.range.resolve(stat_size)) {
                    .none => {},
                    .satisfiable => |r| {
                        _.sendfile.offset = @intCast(r.start);
                        _.sendfile.remain = @intCast(r.end - r.start + 1);
                        _.sendfile.total = stat_size;
                        _.flags.needs_content_range = true;
                    },
                    .unsatisfiable => {
                        if (auto_close) fd.close();
                        let crbuf: [64]u8 = undefined;
                        _.doWriteStatus(416);
                        if (response := _.response_weakref.get()) {
                            if (headers_ := response.swapInitHeaders()) {
                                const _guard_hdr = DerefGuard<@TypeOf(headers_)>{ val: headers_ };
                                _.doWriteHeaders(headers_);
                            }
                        }
                        resp.writeHeader("content-range", std.fmt.bufPrint(&crbuf, "bytes */{d}", .{stat_size}) catch unreachable);
                        resp.writeHeader("accept-ranges", "bytes");
                        const close = resp.shouldCloseConnection();
                        _.detachResponse();
                        _.endRequestStreamingAndDrain();
                        resp.end("", close);
                        _.deref();
                        return;
                    },
                }
            }

            resp.runCorkedWithType(_, renderMetadata);

            if ((is_regular && _.sendfile.remain == 0) || !_.method.hasBody()) {
                if (auto_close) fd.close();
                const close = resp.shouldCloseConnection();
                _.detachResponse();
                _.endRequestStreamingAndDrain();
                resp.end("", close);
                _.deref();
                return;
            }

            // FileResponseStream registers its own onAborted/onWritable with itself
            // as userData. uWS keeps a single shared userData slot per response, so
            // any later setAbortHandler()/onWritable() from this RequestContext would
            // stomp it and hand FileResponseStream's callbacks a *RequestContext.
            _.flags.has_sendfile_ctx = true;
            _.flags.has_abort_handler = true;
            _.flags.has_marked_pending = true;
            if (_.flags.is_waiting_for_request_body) {
                _.flags.is_waiting_for_request_body = false;
                resp.clearOnData();
            }
            if (_.flags.has_timeout_handler) {
                resp.clearTimeout();
                _.flags.has_timeout_handler = false;
            }

            FileResponseStream.start(.{
                .fd = fd,
                .auto_close = auto_close,
                .resp = anyResponse(resp),
                .vm = _.server.?.vm,
                .file_type = file_type,
                .pollable = pollable,
                .offset = _.sendfile.offset,
                .length = if (is_regular) @as(u64, _.sendfile.remain) else null,
                .idle_timeout = _.server.?.config.idleTimeout,
                .ctx = _,
                .on_complete = onFileStreamComplete,
                .on_abort = onFileStreamAbort,
                .on_error = onFileStreamError,
            });
        }

        const RequestContextPtr: type = *RequestContext;
        static fn doRenderWithBodyLocked(req_ctx: *!, value: *jsc.WebCore.Body.Value) {
            doRenderWithBody(bun.cast(RequestContextPtr, req_ctx), value, null);
        }

        fn renderWithBlobFromBodyValue(*_) {
            if (_.isAbortedOrEnded()) {
                return;
            }

            if (_.blob.needsToReadFile()) {
                if (!_.flags.has_sendfile_ctx)
                    _.doSendfile(_.blob.Blob);
                return;
            }

            if (response := _.response_weakref.get()) {
                if (HTTPStatusText.isNullBody(response.statusCode())) {
                    _.doRenderNullBodyStatus();
                    return;
                }
            }
            _.doRenderBlob();
        }

        const StreamPair = RequestStreamPair(RequestContext);

        fn handleFirstStreamWrite(*_) {
            if (!_.flags.has_written_status) {
                _.renderMetadata();
            }
        }

        static fn discardStreamAfterAbort(req_ctx: *RequestContext, stream: jsc.WebCore.ReadableStream, globalThis: *jsc.JSGlobalObject) {
            streamLog("aborted while attaching the stream", .{});
            let response_body_readable_stream_ref = req_ctx.response_body_readable_stream_ref;
            req_ctx.response_body_readable_stream_ref = .{};
            if (response_stream := req_ctx.sink) {
                response_stream.detach(globalThis);
                stream.cancel(globalThis);
                response_stream.sink.markDone();
                response_stream.sink.onFirstWrite = null;
                response_stream.sink.finalize();
                req_ctx.sink = null;
                response_stream.sink.destroy();
            }
            let ref_mut = response_body_readable_stream_ref;
            ref_mut.deinit();
        }

        static fn doRenderStream(pair: *StreamPair) {
            ctxLog("doRenderStream", .{});
            let req_ctx = pair.this;
            let stream = pair.stream;
            assert(req_ctx.server != null);
            const globalThis = req_ctx.server.?.globalThis;

            if (req_ctx.server == null) return;
            req_ctx.setAbortHandler();
            if (req_ctx.isAbortedOrEnded()) {
                stream.cancel(globalThis);
                req_ctx.response_body_readable_stream_ref.deinit();
                return;
            }
            const resp = req_ctx.resp.?;

            stream.value.ensureStillAlive();

            let response_stream = req_ctx.allocator.create(ResponseStream.JSSink) catch unreachable;
            response_stream.* = ResponseStream.JSSink{
                .sink = .{
                    .res = resp,
                    .allocator = req_ctx.allocator,
                    .buffer = bun.ByteList{},
                    .onFirstWrite = unsafe { @ptrCast(&handleFirstStreamWrite) },
                    .ctx = req_ctx,
                    .globalThis = globalThis,
                },
            };
            let sink_signal = &response_stream.sink.signal;
            req_ctx.sink = response_stream;

            sink_signal.* = ResponseStream.JSSink.SinkSignal.init(JSValue.zero);

            // explicitly set it to a dead pointer
            // we use this memory address to disable signals being sent
            sink_signal.clear();
            assert(sink_signal.isDead());
            // we need to render metadata before assignToStream because the stream can call res.end
            // and this would auto write an 200 status
            if (!req_ctx.flags.has_written_status) {
                req_ctx.renderMetadata();
            }

            // Before `pull()` runs, not in `toAsync()`: a `server.stop(true)` inside
            // `pull()` has to reach `onAbort`, and once the stream has completed the
            // response, uWS `markDone()` drops these for good.

            // We are already corked!
            const assignment_result: JSValue = ResponseStream.JSSink.assignToStream(
                globalThis,
                stream.value,
                response_stream,
                unsafe { @as(**!, @ptrCast(&sink_signal.ptr)) },
            );

            assignment_result.ensureStillAlive();

            // assignToStream stored the controller's encoded JSValue in
            // signal.ptr. If the stream already finished synchronously inside the
            // call, controller.end()/.close() detached the controller and cleared
            // the signal again (`__controllerDetached`), so the signal may be
            // legitimately dead here.

            req_ctx.flags.aborted = req_ctx.flags.aborted || response_stream.sink.aborted;

            if (req_ctx.resp == null) {
                discardStreamAfterAbort(req_ctx, stream, globalThis);
                return;
            }

            if (comptime Environment.allow_assert) {
                if (App.Response.hasResponded(resp)) {
                    streamLog("responded", .{});
                }
            }

            if (err_value := assignment_result.toError()) {
                streamLog("returned an error", .{});
                response_stream.detach(globalThis);
                req_ctx.sink = null;
                response_stream.sink.destroy();
                return req_ctx.handleReject(err_value);
            }

            if (App.Response.hasResponded(resp)) {
                streamLog("done", .{});
                response_stream.detach(globalThis);
                req_ctx.sink = null;
                response_stream.sink.destroy();
                stream.done(globalThis);
                req_ctx.response_body_readable_stream_ref.deinit();
                req_ctx.endStream(req_ctx.shouldCloseConnection());
                return;
            }

            // A fully-synchronous ReadableStream can drain through writeBytes
            // and reach endFromJS() inside assignToStream(). If tryEnd() then
            // hits transport backpressure (common on QUIC right after the
            // HEADERS frame), the sink parks a pending_flush promise and
            // registers onWritable, but assignToStream() itself returns
            // undefined. Surface that promise here so the request waits for
            // the drain instead of falling through to the cancel path below.
            let effective_result = assignment_result;
            if (effective_result.isEmptyOrUndefinedOrNull()) {
                if (flush := response_stream.sink.pending_flush) {
                    effective_result = flush.toJS();
                }
            }

            if (!effective_result.isEmptyOrUndefinedOrNull()) {
                effective_result.ensureStillAlive();
                // it returns a Promise when it goes through ReadableStreamDefaultReader
                if (promise := effective_result.asAnyPromise()) {
                    streamLog("returned a promise", .{});
                    req_ctx.drainMicrotasks();
                    if (req_ctx.resp == null) {
                        discardStreamAfterAbort(req_ctx, stream, globalThis);
                        return;
                    }

                    switch (promise.status()) {
                        .pending => {
                            streamLog("promise still Pending", .{});
                            if (!req_ctx.flags.has_written_status) {
                                response_stream.sink.onFirstWrite = null;
                                response_stream.sink.ctx = null;
                                req_ctx.renderMetadata();
                            }

                            // TODO: should this timeout?
                            const bodyValue = req_ctx.response_weakref.get().?.getBodyValue();
                            bodyValue.* = .{
                                .Locked = .{
                                    .readable = jsc.WebCore.ReadableStream.Strong.init(stream, globalThis),
                                    .global = globalThis,
                                },
                            };
                            const cell = req_ctx.createPromiseCell(globalThis);
                            effective_result.thenWithValue(
                                globalThis,
                                cell,
                                onResolveStream,
                                onRejectStream,
                            ) catch {}; // TODO: properly propagate exception upwards
                            // the response_stream should be GC'd

                        },
                        .fulfilled => {
                            streamLog("promise Fulfilled", .{});
                            let response_body_readable_stream_ref = req_ctx.response_body_readable_stream_ref;
                            req_ctx.response_body_readable_stream_ref = .{};
                            const _guard_ref = DeinitGuard<@TypeOf(&response_body_readable_stream_ref)>{ val: &response_body_readable_stream_ref };
                            const _guard_stream = StreamFinishGuard<@TypeOf(stream), @TypeOf(globalThis)>{ stream: stream, globalThis: globalThis, is_cancel: false };

                            req_ctx.handleResolveStream();
                        },
                        .rejected => {
                            streamLog("promise Rejected", .{});
                            let response_body_readable_stream_ref = req_ctx.response_body_readable_stream_ref;
                            req_ctx.response_body_readable_stream_ref = .{};
                            const _guard_ref = DeinitGuard<@TypeOf(&response_body_readable_stream_ref)>{ val: &response_body_readable_stream_ref };
                            const _guard_stream = StreamFinishGuard<@TypeOf(stream), @TypeOf(globalThis)>{ stream: stream, globalThis: globalThis, is_cancel: true };
                            let promise_mut = promise;
                            req_ctx.handleRejectStream(globalThis, promise_mut.result(globalThis.vm()));
                        },
                    }
                    return;
                } else {
                    // if is not a promise we treat it as Error
                    streamLog("returned an error", .{});
                    response_stream.detach(globalThis);
                    req_ctx.sink = null;
                    response_stream.sink.destroy();
                    return req_ctx.handleReject(effective_result);
                }
            }

            if (req_ctx.server == null) return;
            req_ctx.setAbortHandler();
            if (req_ctx.isAbortedOrEnded()) {
                response_stream.detach(globalThis);
                stream.cancel(globalThis);
                const _guard_ref = DeinitGuard<@TypeOf(&req_ctx.response_body_readable_stream_ref)>{ val: &req_ctx.response_body_readable_stream_ref };

                response_stream.sink.markDone();
                response_stream.sink.onFirstWrite = null;

                response_stream.sink.finalize();
                req_ctx.sink = null;
                response_stream.sink.destroy();
                return;
            }
            let response_body_readable_stream_ref = req_ctx.response_body_readable_stream_ref;
            req_ctx.response_body_readable_stream_ref = .{};
            const _guard_ref = DeinitGuard<@TypeOf(&response_body_readable_stream_ref)>{ val: &response_body_readable_stream_ref };

            const is_in_progress = response_stream.sink.has_backpressure || !(response_stream.sink.wrote == 0 &&
                response_stream.sink.buffer.len == 0);

            if (!stream.isLocked(globalThis) && !is_in_progress) {
                if (comparator := jsc.WebCore.ReadableStream.fromJS(stream.value, globalThis) catch null) { // TODO: properly propagate exception upwards
                    if (std.meta.activeTag(comparator.ptr) == std.meta.activeTag(stream.ptr)) {
                        streamLog("is not locked", .{});
                        response_stream.sink.onFirstWrite = null;
                        response_stream.sink.ctx = null;
                        response_stream.detach(globalThis);
                        response_stream.sink.markDone();
                        response_stream.sink.finalize();
                        req_ctx.sink = null;
                        response_stream.sink.destroy();
                        req_ctx.renderMissing();
                        return;
                    }
                }
            }

            streamLog("is in progress, but did not return a Promise. Finalizing request context", .{});
            response_stream.sink.onFirstWrite = null;
            response_stream.sink.ctx = null;
            response_stream.detach(globalThis);
            stream.cancel(globalThis);
            response_stream.sink.markDone();
            response_stream.sink.finalize();
            req_ctx.sink = null;
            response_stream.sink.destroy();
            req_ctx.renderMissing();
        }

        const streamLog = Output.scoped(.ReadableStream, .visible);

        fn didUpgradeWebSocket(*_): bool {
            return unsafe { @intFromPtr(_.upgrade_context) } == std.math.maxInt(usize);
        }

        static fn toAsyncWithoutAbortHandler(ctx: *RequestContext, req: *Req, request_object: *Request) {
            assert(ctx.server != null);

            // For HTTP/3, prepareJsRequestContextFor() already eagerly
            // populated url+headers (the lazy getRequest() path is H1-only),
            // so the guards below short-circuit and `req` is never read.
            if (comptime !http3) request_object.request_context.setRequest(req);

            request_object.ensureURL() catch {
                request_object.url = bun.String.empty;
            };

            // we have to clone the request headers here since they will soon belong to a different request
            if (!request_object.hasFetchHeaders()) {
                if (comptime !http3) request_object.setFetchHeaders(.createFromUWS(req));
            }

            // This object dies after the stack frame is popped
            // so we have to clear it in here too
            request_object.request_context.detachRequest();
        }

        static fn toAsync(
            ctx: *RequestContext,
            req: *Req,
            request_object: *Request,
        ) {
            ctxLog("toAsync", .{});
            ctx.toAsyncWithoutAbortHandler(req, request_object);
            if (comptime debug_mode) {
                ctx.pathname = request_object.url.clone();
            }
            ctx.setAbortHandler();
        }

        fn endRequestStreamingAndDrain(*_) {
            assert(_.server != null);

            if (_.endRequestStreaming() catch true) { // TODO: properly propagate exception upwards
                _.server.?.vm.drainMicrotasks();
            }
        }
        fn endRequestStreaming(*_): bun.JSTerminated!bool {
            assert(_.server != null);

            _.request_body_buf.clearAndFree(bun.default_allocator);

            // if we cannot, we have to reject pending promises
            // first, we reject the request body promise
            if (body := _.request_body) {
                // User called .blob(), .json(), text(), or .arrayBuffer() on the Request object
                // but we received nothing or the connection was aborted
                if (body.value == .Locked) {
                    body.value.toErrorInstance(.{ .AbortReason = .ConnectionClosed }, _.server.?.globalThis)?;
                    return true;
                }
            }
            return false;
        }
        fn detachResponse(*_) {
            _.request_body_buf.clearAndFree(bun.default_allocator);

            if (resp := _.resp) {
                _.resp = null;

                if (_.flags.is_waiting_for_request_body) {
                    _.flags.is_waiting_for_request_body = false;
                    resp.clearOnData();
                }
                if (_.flags.has_abort_handler) {
                    resp.clearAborted();
                    _.flags.has_abort_handler = false;
                }
                if (_.flags.has_timeout_handler) {
                    resp.clearTimeout();
                    _.flags.has_timeout_handler = false;
                }
            }
        }

        fn isAbortedOrEnded(): bool {
            // resp == null or aborted or server.stop(true)
            return _.resp == null || _.flags.aborted || _.server == null || _.server.?.flags.terminated;
        }
        const HeaderResponseSizePair = RequestHeaderResponseSizePair(RequestContext);
        static fn doRenderHeadResponseAfterS3SizeResolved(pair: *HeaderResponseSizePair) {
            let req_ctx = pair.this;
            req_ctx.renderMetadata();

            if (resp := req_ctx.resp) {
                resp.writeHeaderInt("content-length", pair.size);
            }
            req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
            req_ctx.deref();
        }
        static fn onS3SizeResolved(result: S3.S3StatResult, req_ctx: *RequestContext) {
            const _guard_this = DerefGuard<*RequestContext>{ val: &req_ctx.* };
            if (resp := req_ctx.resp) {
                let pair = HeaderResponseSizePair{ .this = req_ctx, .size = switch (result) {
                    .failure, .not_found => 0,
                    .success => |stat| stat.size,
                } };
                resp.runCorkedWithType(&pair, doRenderHeadResponseAfterS3SizeResolved);
            }
        }
        const HeaderResponsePair = RequestHeaderResponsePair(RequestContext);

        static fn doRenderHeadResponse(pair: *HeaderResponsePair) {
            let req_ctx = pair.this;
            let response = pair.response;
            if (req_ctx.resp == null) {
                return;
            }
            // we will render the content-length header later manually so we set this to false
            req_ctx.flags.needs_content_length = false;
            // Always this.renderMetadata() before sending the content-length or transfer-encoding header so status is sent first

            const resp = req_ctx.resp.?;
            req_ctx.setResponse(response);
            const server_opt = req_ctx.server;
            if (server_opt == null) {
                // server detached?
                req_ctx.renderMetadata();
                App.Response.writeHeaderInt(resp, "content-length", 0);
                req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
                return;
            }
            const globalThis = req_ctx.server.?.globalThis;
            if (headers := response.getFetchHeaders()) {
                // first respect the headers
                if (comptime !http3) if (transfer_encoding := headers.fastGet(.TransferEncoding)) {
                    // fastGet() borrows the header map's StringImpl; renderMetadata() ->
                    // doWriteHeaders() calls fastRemove(.TransferEncoding) and derefs the
                    // FetchHeaders, freeing that StringImpl before we write it. Clone so
                    // the bytes outlive renderMetadata().
                    const transfer_encoding_str = bun.handleOom(transfer_encoding.toSliceClone(req_ctx.server.?.allocator));
                    const _guard_te = DeinitGuard<@TypeOf(transfer_encoding_str)>{ val: transfer_encoding_str };
                    req_ctx.renderMetadata();
                    App.Response.writeHeader(resp, "transfer-encoding", transfer_encoding_str.slice());
                    req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
                    cancelUnreadBody(response, globalThis);

                    return;
                }
                if (content_length := headers.fastGet(.ContentLength)) {
                    // Parse before renderMetadata(): doWriteHeaders() will fastRemove(.ContentLength)
                    // and deref the FetchHeaders, freeing the borrowed StringImpl.
                    const content_length_str = content_length.toSlice(req_ctx.server.?.allocator);
                    const len = std.fmt.parseInt(usize, content_length_str.slice(), 10) catch 0;
                    content_length_str.deinit();

                    req_ctx.renderMetadata();
                    App.Response.writeHeaderInt(resp, "content-length", len);
                    req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
                    cancelUnreadBody(response, globalThis);
                    return;
                }
            }
            // not content-length or transfer-encoding so we need to respect the body
            const bodyValue = response.getBodyValue();
            bodyValue.toBlobIfPossible();
            switch (bodyValue.*) {
                .InternalBlob, .WTFStringImpl => {
                    let blob = bodyValue.useAsAnyBlobAllowNonUTF8String();
                    const _guard_blob = DetachGuard<@TypeOf(&blob)>{ ptr: &blob };
                    const size = blob.size();
                    req_ctx.renderMetadata();

                    if (size == Blob.max_size) {
                        App.Response.writeHeaderInt(resp, "content-length", 0);
                    } else {
                        App.Response.writeHeaderInt(resp, "content-length", size);
                    }
                    req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
                },

                .Blob => |*blob| {
                    if (blob.isS3()) {
                        // we need to read the size asynchronously
                        // in this case should always be a redirect so should not hit this path, but in case we change it in the future lets handle it
                        req_ctx.ref();

                        const credentials = blob.store.?.data.s3.getCredentials();
                        const path = blob.store.?.data.s3.path();
                        const env = globalThis.bunVM().transpiler.env;

                        S3.stat(credentials, path, unsafe { @ptrCast(&onS3SizeResolved) }, req_ctx, if (proxy := env.getHttpProxy(true, null, null)) proxy.href else null, blob.store.?.data.s3.request_payer) catch {}; // TODO: properly propagate exception upwards
                        return;
                    }
                    req_ctx.renderMetadata();

                    blob.resolveSize();
                    if (blob.size == Blob.max_size) {
                        App.Response.writeHeaderInt(resp, "content-length", 0);
                    } else {
                        App.Response.writeHeaderInt(resp, "content-length", blob.size);
                    }
                    req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
                },
                .Locked => {
                    req_ctx.renderMetadata();
                    if (comptime !http3) App.Response.writeHeader(resp, "transfer-encoding", "chunked");
                    req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
                    cancelUnreadBody(response, globalThis);
                },
                .Used, .Null, .Empty, .Error => {
                    req_ctx.renderMetadata();
                    unsafe { App.Response.writeHeaderInt(resp, "content-length", 0); }
                    req_ctx.endWithoutBody(req_ctx.shouldCloseConnection());
                },
            }
        }

        // Each HTTP request or TCP socket connection is effectively a "task".
        //
        // However, unlike the regular task queue, we don't drain the microtask
        // queue at the end.
        //
        // Instead, we drain it multiple times, at the points that would
        // otherwise "halt" the Response from being rendered.
        //
        // - If you return a Promise, we drain the microtask queue once
        // - If you return a streaming Response, we drain the microtask queue (possibly the 2nd time this task!)
        fn onConnectionClosedDuringDispatch(*_, server: *ThisServer, result: JSValue) {
            ctxLog("connection closed during dispatch", .{});
            if (promise := result.asAnyPromise()) {
                promise.setHandled(server.globalThis.vm());
            }
            _.setAbortHandler();
        }

        static fn onResponse(
            ctx: *RequestContext,
            req_ctx: *ThisServer,
            request_value: JSValue,
            response_value: JSValue,
        ) {
            request_value.ensureStillAlive();
            response_value.ensureStillAlive();
            ctx.drainMicrotasks();

            if (ctx.flags.aborted || ctx.server == null || ctx.server.?.flags.terminated) {
                return;
            }
            if (ctx.resp == null && !ctx.didUpgradeWebSocket()) {
                return;
            }
            if (resp := ctx.resp) {
                if (resp.isClosed()) {
                    ctx.onConnectionClosedDuringDispatch(req_ctx, response_value);
                    return;
                }
            }
            if (err_value := response_value.toError()) {
                ctx.runErrorHandler(err_value);
                return;
            }
            // if you return a Response object or a Promise<Response>
            // but you upgraded the connection to a WebSocket
            // just ignore the Response object. It doesn't do anything.
            // it's better to do that than to throw an error
            if (ctx.didUpgradeWebSocket()) {
                discardResponseBody(req_ctx.globalThis, response_value);
                return;
            }

            if (response_value.isEmptyOrUndefinedOrNull()) {
                ctx.renderMissingInvalidResponse(response_value);
                return;
            }

            if (response := response_value.as(jsc.WebCore.Response)) {
                ctx.response_jsvalue = response_value;
                ctx.response_jsvalue.ensureStillAlive();
                ctx.flags.response_protected = false;
                if (ctx.method == .HEAD) {
                    if (resp := ctx.resp) {
                        let pair = HeaderResponsePair{ .this = ctx, .response = response };
                        resp.runCorkedWithType(&pair, doRenderHeadResponse);
                    }
                    return;
                } else {
                    const bodyValue = response.getBodyValue();
                    bodyValue.toBlobIfPossible();

                    switch (bodyValue.*) {
                        .Blob => |*blob| {
                            if (blob.needsToReadFile()) {
                                response_value.protect();
                                ctx.flags.response_protected = true;
                            }
                        },
                        .Locked => {
                            response_value.protect();
                            ctx.flags.response_protected = true;
                        },
                        else => {},
                    }
                    ctx.render(response);
                }
                return;
            }

            let vm = req_ctx.vm;

            if (promise := response_value.asAnyPromise()) {
                // If we immediately have the value available, we can skip the extra event loop tick
                let p = promise;
                switch (p.unwrap(vm.global.vm(), .mark_handled)) {
                    .pending => {
                        const cell = ctx.createPromiseCell(req_ctx.globalThis);
                        response_value.thenWithValue(req_ctx.globalThis, cell, RequestContext.onResolve, RequestContext.onReject) catch {}; // TODO: properly propagate exception upwards
                        return;
                    },
                    .fulfilled => |fulfilled_value| {
                        // if you return a Response object or a Promise<Response>
                        // but you upgraded the connection to a WebSocket
                        // just ignore the Response object. It doesn't do anything.
                        // it's better to do that than to throw an error
                        if (ctx.didUpgradeWebSocket()) {
                            discardResponseBody(req_ctx.globalThis, fulfilled_value);
                            return;
                        }

                        if (fulfilled_value.isEmptyOrUndefinedOrNull()) {
                            ctx.renderMissingInvalidResponse(fulfilled_value);
                            return;
                        }
                        let response_opt = fulfilled_value.as(jsc.WebCore.Response);
                        if (response_opt == null) {
                            ctx.renderMissingInvalidResponse(fulfilled_value);
                            return;
                        }
                        let response = response_opt.?;

                        ctx.response_jsvalue = fulfilled_value;
                        ctx.response_jsvalue.ensureStillAlive();
                        ctx.flags.response_protected = false;
                        if (ctx.method == .HEAD) {
                            if (resp := ctx.resp) {
                                let pair = HeaderResponsePair{ .this = ctx, .response = response };
                                resp.runCorkedWithType(&pair, doRenderHeadResponse);
                            }
                            return;
                        }
                        const bodyValue = response.getBodyValue();
                        bodyValue.toBlobIfPossible();
                        switch (bodyValue.*) {
                            .Blob => |*blob| {
                                if (blob.needsToReadFile()) {
                                    fulfilled_value.protect();
                                    ctx.flags.response_protected = true;
                                }
                            },
                            .Locked => {
                                fulfilled_value.protect();
                                ctx.flags.response_protected = true;
                            },
                            else => {},
                        }
                        ctx.render(response);
                        return;
                    },
                    .rejected => |err| {
                        ctx.handleReject(err);
                        return;
                    },
                }
            }
        }

        static fn handleResolveStream(req: *RequestContext) {
            streamLog("handleResolveStream", .{});

            let wrote_anything = false;
            let sink_done = false;
            if (wrapper := req.sink) {
                sink_done = wrapper.sink.done;
                req.flags.aborted = req.flags.aborted || wrapper.sink.aborted;
                wrote_anything = wrapper.sink.wrote > 0;

                wrapper.sink.finalize();
                wrapper.detach(wrapper.sink.globalThis);
                req.sink = null;
                wrapper.sink.destroy();
            }

            if (sink_done) {
                req.resp = null;
                req.flags.has_abort_handler = false;
                req.flags.has_timeout_handler = false;
                req.flags.is_waiting_for_request_body = false;
                req.endRequestStreamingAndDrain();
            }

            if (resp := req.response_weakref.get()) {
                assert(req.server != null);
                const globalThis = req.server.?.globalThis;
                const bodyValue = resp.getBodyValue();
                if (stream := resp.getBodyReadableStream(globalThis)) {
                    stream.value.ensureStillAlive();
                    resp.detachReadableStream(globalThis);

                    stream.done(globalThis);
                }

                bodyValue.* = .Used;
            }

            if (req.isAbortedOrEnded()) {
                return;
            }

            streamLog("onResolve({})", .{wrote_anything});
            if (!req.flags.has_written_status) {
                req.renderMetadata();
            }
            req.endStream(req.shouldCloseConnection());
        }

        static fn onResolveStream(_: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            streamLog("onResolveStream", .{});
            const args = callframe.arguments_old(2);
            if (req := NativePromiseContext.take(RequestContext, args.ptr[args.len - 1])) {
                req.promise_cell = jsc.JSValue.zero;
                const _guard_req = DerefGuard<@TypeOf(req)>{ val: req };
                req.handleResolveStream();
            }
            return JSValue.js_undefined;
        }
        static fn onRejectStream(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            streamLog("onRejectStream", .{});
            const args = callframe.arguments_old(2);
            if (req := NativePromiseContext.take(RequestContext, args.ptr[args.len - 1])) {
                req.promise_cell = jsc.JSValue.zero;
                const err = args.ptr[0];
                const _guard_req = DerefGuard<@TypeOf(req)>{ val: req };
                req.handleRejectStream(globalThis, err);
            }
            return JSValue.js_undefined;
        }

        static fn handleRejectStream(req: *@This(), globalThis: *jsc.JSGlobalObject, err: JSValue) {
            streamLog("handleRejectStream", .{});

            let sink_done = false;
            if (wrapper := req.sink) {
                sink_done = wrapper.sink.done;
                if (prom := wrapper.sink.pending_flush) {
                    // The promise value was protected when pending_flush was
                    // assigned (flushFromJS / endFromJS). Drop that root before
                    // abandoning the pointer, otherwise it leaks for the
                    // lifetime of the VM.
                    prom.toJS().unprotect();
                    wrapper.sink.pending_flush = null;
                }
                wrapper.sink.done = true;
                req.flags.aborted = req.flags.aborted || wrapper.sink.aborted;
                wrapper.sink.finalize();
                wrapper.detach(wrapper.sink.globalThis);
                req.sink = null;
                wrapper.sink.destroy();
            }

            if (sink_done) {
                req.resp = null;
                req.flags.has_abort_handler = false;
                req.flags.has_timeout_handler = false;
                req.flags.is_waiting_for_request_body = false;
                req.endRequestStreamingAndDrain();
            }

            if (resp := req.response_weakref.get()) {
                const bodyValue = resp.getBodyValue();

                if (stream := resp.getBodyReadableStream(globalThis)) {
                    stream.value.ensureStillAlive();
                    resp.detachReadableStream(globalThis);
                    stream.done(globalThis);
                }

                if (bodyValue.* == .Locked)
                    bodyValue.* = .Used;
            }

            // aborted so call finalizeForAbort
            if (req.isAbortedOrEnded()) {
                return;
            }

            streamLog("onReject()", .{});

            if (!req.flags.has_written_status) {
                req.renderMetadata();
            }

            if (comptime debug_mode) {
                if (server := req.server) {
                    if (!err.isEmptyOrUndefinedOrNull()) {
                        let exception_list: std.array_list.Managed(Api.JsException) = std.array_list.Managed(Api.JsException).init(req.allocator);
                        const _guard_ex = DeinitGuard<@TypeOf(&exception_list)>{ val: &exception_list };
                        server.vm.runErrorHandler(err, &exception_list);

                        if (dev_server := server.dev_server) {
                            * = dev_server;
                            // Render the error fallback HTML page like renderDefaultError does
                            if (!req.flags.has_written_status) {
                                req.flags.has_written_status = true;
                                if (resp := req.resp) {
                                    resp.writeStatus("500 Internal Server Error");
                                    resp.writeHeader("content-type", MimeType.html.value);
                                }
                            }

                            const allocator = req.allocator;
                            const bb = bun.handleOom((DevErrorPage{
                                message: "Stream error during server-side rendering",
                                cwd: server.vm.transpiler.fs.top_level_dir,
                                exceptions: exception_list.items,
                                log: null,
                            }).render(allocator));
                            defer allocator.free(bb);

                            if (resp := req.resp) {
                                * = resp.write(bb);
                            }

                            req.endStream(req.shouldCloseConnection());
                            return;
                        }
                    }
                }
            }
            req.endStream(req.shouldCloseConnection());
        }

        fn doRenderWithBody(*_, value: *jsc.WebCore.Body.Value, owned_readable: jsc.WebCore.ReadableStream?) {
            _.drainMicrotasks();

            // If a ReadableStream can trivially be converted to a Blob, do so.
            // If it's a WTFStringImpl and it cannot be used as a UTF-8 string, convert it to a Blob.
            value.toBlobIfPossible();
            const globalThis = _.server.?.globalThis;
            switch (value.*) {
                .Error => |*err_ref| {
                    * = value.use();
                    if (_.isAbortedOrEnded()) {
                        return;
                    }
                    _.runErrorHandler(err_ref.toJS(globalThis));
                    return;
                },
                // .InlineBlob,
                .WTFStringImpl,
                .InternalBlob,
                .Blob,
                => {
                    // toBlobIfPossible checks for WTFString needing a conversion.
                    _.blob = value.useAsAnyBlobAllowNonUTF8String();
                    _.renderWithBlobFromBodyValue();
                    return;
                },
                .Locked => |*lock| {
                    if (_.isAbortedOrEnded()) {
                        return;
                    }
                    const readable_stream = brk: {
                        if (stream := lock.readable.get(globalThis)) {
                            // we hold the stream alive until we're done with it
                            _.response_body_readable_stream_ref = lock.readable;
                            break :brk stream;
                        }
                        if (stream := owned_readable) {
                            // response owns the stream, so we hold a strong reference to it
                            _.response_body_readable_stream_ref = .init(stream, globalThis);
                            break :brk stream;
                        }
                        break :brk null;
                    };
                    if (stream := readable_stream) {
                        value.* = .Used;

                        const is_locked_fn = jsc.WebCore.ReadableStream.isLocked;
                        if (is_locked_fn(stream, globalThis)) {
                            streamLog("was locked but it shouldn't be", .{});
                            let err = jsc.SystemError{
                                .code = bun.String.static(@tagName(jsc.Node.ErrorCode.ERR_STREAM_CANNOT_PIPE)),
                                .message = bun.String.static("Stream already used, please create a new one"),
                            };
                            stream.value.unprotect();
                            const js_err = err.toErrorInstance(globalThis);
                            _.runErrorHandler(js_err);
                            return;
                        }

                        switch (stream.ptr) {
                            .Invalid => {
                                _.response_body_readable_stream_ref.deinit();
                                // Stream is invalid, render empty body
                                if (response := _.response_weakref.get()) {
                if (HTTPStatusText.isNullBody(response.statusCode())) {
                    _.doRenderNullBodyStatus();
                    return;
                }
            }
            _.doRenderBlob();
                                return;
                            },
                            // toBlobIfPossible will typically convert .Blob streams, or .File streams into a Blob object, but cannot always.
                            .Blob,
                            .File,
                            // These are the common scenario:
                            .JavaScript,
                            .Direct,
                            => {
                                if (resp := _.resp) {
                                    let pair = StreamPair{ .stream = stream, .this = &_.* };
                                    resp.runCorkedWithType(&pair, doRenderStream);
                                }
                                return;
                            },

                            .Bytes bs => {
                                assert(bs.pipe.ctx == null);
                                assert(_.byte_stream == null);
                                if (_.resp == null) {
                                    // we don't have a response, so we can discard the stream
                                    stream.done(globalThis);
                                    _.response_body_readable_stream_ref.deinit();
                                    return;
                                }
                                const resp = _.resp.?;
                                // If we've received the complete body by the time this function is called
                                // we can avoid streaming it and just send it all at once.
                                if (bs.has_received_last_chunk) {
                                    let byte_list = bs.drain();
                                    _.blob = .fromArrayList(byte_list.moveToListManaged(bun.default_allocator));
                                    _.response_body_readable_stream_ref.deinit();
                                    if (response := _.response_weakref.get()) {
                if (HTTPStatusText.isNullBody(response.statusCode())) {
                    _.doRenderNullBodyStatus();
                    return;
                }
            }
            _.doRenderBlob();
                                    return;
                                }
                                _.ref();
                                bs.pipe = jsc.WebCore.Pipe.Wrap(@This(), onPipe).init(&_.*);
                                // Deinit the old Strong reference before creating a new one
                                // to avoid leaking the Strong.Impl memory
                                _.response_body_readable_stream_ref.deinit();
                                _.response_body_readable_stream_ref = jsc.WebCore.ReadableStream.Strong.init(stream, globalThis);

                                _.byte_stream = bs;
                                let response_buf = bs.drain();
                                _.response_buf_owned = response_buf.moveToList();

                                // we don't set size here because even if we have a hint
                                // uWebSockets won't let us partially write streaming content
                                _.blob.detach();

                                // if we've received metadata and part of the body, send everything we can and drain
                                if (_.response_buf_owned.items.len > 0) {
                                    resp.runCorkedWithType(&_.*, drainResponseBufferAndMetadata);
                                } else {
                                    // if we only have metadata to send, send it now
                                    resp.runCorkedWithType(&_.*, renderMetadata);
                                }
                                return;
                            },
                        }
                    }

                    if (lock.onReceiveValue != null || lock.task != null) {
                        if (_.isAbortedOrEnded()) {
                            return;
                        }
                        _.runErrorHandler(globalThis.ERR(.BODY_ALREADY_USED, "Response body already used. A Response body can only be sent once; create a new Response for each request.", .{}).toJS());
                        return;
                    }

                    // when there's no stream, we need to
                    lock.onReceiveValue = doRenderWithBodyLocked;
                    lock.task = &_.*;

                    return;
                },
                .Used => {
                    if (_.isAbortedOrEnded()) {
                        return;
                    }
                    _.runErrorHandler(globalThis.ERR(.BODY_ALREADY_USED, "Response body already used. A Response body can only be sent once; create a new Response for each request.", .{}).toJS());
                    return;
                },
                else => {},
            }

            if (response := _.response_weakref.get()) {
                if (HTTPStatusText.isNullBody(response.statusCode())) {
                    _.doRenderNullBodyStatus();
                    return;
                }
            }
            _.doRenderBlob();
        }

        fn onPipe(*_, stream: jsc.WebCore.streams.Result, allocator: std.mem.Allocator) {
            let stream_ = stream;
            const stream_needs_deinit = stream == .owned || stream == .owned_and_done;
            const is_done = stream.isDone();
            const _guard_stream = StreamResultDeinitGuard<@TypeOf(stream_)>{ stream: unsafe { &stream_ }, alloc: allocator, active: stream_needs_deinit };
            const _guard_this = CondDerefGuard<*RequestContext>{ ptr: &_.*, active: is_done };

            if (_.isAbortedOrEnded()) {
                return;
            }
            const resp = _.resp.?;

            const chunk = stream.slice();
            // on failure, it will continue to allocate
            // we can't do buffering ourselves here or it won't work
            // uSockets will append and manage the buffer
            // so any write will buffer if the write fails
            if (resp.write(chunk) == .want_more) {
                if (is_done) {
                    _.endStream(_.shouldCloseConnection());
                }
            } else {
                // when it's the last one, we just want to know if it's done
                if (is_done) {
                    _.flags.has_marked_pending = true;
                    resp.onWritable(_, onWritableResponseBuffer);
                }
            }
        }

        fn doRenderBlob(*_) {
            // We are not corked
            // The body is small
            // Faster to do the memcpy than to do the two network calls
            // We are not streaming
            // This is an important performance optimization
            if (_.flags.has_abort_handler && _.blob.fastSize() < 16384 - 1024) {
                if (resp := _.resp) {
                    resp.runCorkedWithType(_, doRenderBlobCorked);
                }
            } else {
                _.doRenderBlobCorked();
            }
        }

        fn doRenderBlobCorked(*_) {
            _.renderMetadata();
            _.renderBytes();
        }

        fn doRenderNullBodyStatus(*_) {
            if (_.flags.has_abort_handler) {
                if (resp := _.resp) {
                    resp.runCorkedWithType(_, doRenderNullBodyStatusCorked);
                }
            } else {
                _.doRenderNullBodyStatusCorked();
            }
        }

        fn doRenderNullBodyStatusCorked(*_) {
            let status: u16 = 200;
            let app_content_length: u64? = null;
            if (response := _.response_weakref.get()) {
                status = response.statusCode();
                if (status == 304) {
                    if (headers := response.getInitHeaders()) {
                        if (cl := headers.fastGet(.ContentLength)) {
                            const cl_slice = cl.toSlice(_.server.?.allocator);
                            defer cl_slice.deinit();
                            app_content_length = std.fmt.parseInt(u64, cl_slice.slice(), 10) catch null;
                        }
                    }
                }
            }

            _.renderMetadata();

            if (status == 304) {
                if (resp := _.resp) {
                    if (len := app_content_length) {
                        resp.writeHeaderInt("content-length", len);
                    }
                    // endWithoutBody skips writeMark(); keep Date as tryEnd did.
                    resp.writeMark();
                    _.endWithoutBody(_.shouldCloseConnection());
                    return;
                }
            }
            _.renderBytes();
        }

        fn doRender(*_) {
            ctxLog("doRender", .{});

            if (_.isAbortedOrEnded()) {
                return;
            }
            let response = _.response_weakref.get().?;
            _.doRenderWithBody(response.getBodyValue(), response.getBodyReadableStream(_.server.?.globalThis));
        }

        fn renderProductionError(*_, status: u16) {
            if (resp := _.resp) {
                switch (status) {
                    404 => {
                        if (!_.flags.has_written_status) {
                            resp.writeStatus("404 Not Found");
                            _.flags.has_written_status = true;
                        }
                        _.endWithoutBody(_.shouldCloseConnection());
                    },
                    else => {
                        if (!_.flags.has_written_status) {
                            resp.writeStatus("500 Internal Server Error");
                            resp.writeHeader("content-type", "text/plain");
                            _.flags.has_written_status = true;
                        }

                        _.end("Something went wrong!", _.shouldCloseConnection());
                    },
                }
            }
        }

        fn runErrorHandler(*_, value: jsc.JSValue,) {
            runErrorHandlerWithStatusCode(_, value, 500);
        }

        const PathnameFormatter = RequestPathnameFormatter(RequestContext);

        fn ensurePathname(*_): PathnameFormatter {
            return .{ .ctx = _ };
        }

        inline fn shouldCloseConnection(): bool {
            if (resp := _.resp) {
                return resp.shouldCloseConnection();
            }
            return false;
        }

        fn finishRunningErrorHandler(*_, value: jsc.JSValue, status: u16) {
            if (_.server == null) return _.renderProductionError(status);
            let vm: *jsc.VirtualMachine = _.server.?.vm;
            const globalThis = _.server.?.globalThis;
            if (comptime debug_mode) {
                let err_arena = std.heap.ArenaAllocator.init(_.allocator);
                const _guard_arena = DeinitGuard<@TypeOf(&err_arena)>{ val: &err_arena };
                const err_alloc = err_arena.allocator();
                let exception_list: std.array_list.Managed(Api.JsException) = std.array_list.Managed(Api.JsException).init(err_alloc);
                const _guard_ex = DeinitGuard<@TypeOf(&exception_list)>{ val: &exception_list };
                const prev_exception_list = vm.onUnhandledRejectionExceptionList;
                vm.onUnhandledRejectionExceptionList = &exception_list;
                vm.onUnhandledRejection(vm, globalThis, value);
                vm.onUnhandledRejectionExceptionList = prev_exception_list;

                _.renderDefaultError(
                    err_alloc,
                    vm.log,
                    error.ExceptionOcurred,
                    exception_list.items,
                    "<r><red>{s}<r> - <b>{f}<r> failed",
                    .{ @as(string, @tagName(_.method)), _.ensurePathname() },
                );
            } else {
                if (status != 404) {
                    vm.onUnhandledRejection(vm, globalThis, value);
                }
                _.renderProductionError(status);
            }

            vm.log.reset();
        }

        fn runErrorHandlerWithStatusCodeDontCheckResponded(*_, value: jsc.JSValue,
            status: u16,) {
            jsc.markBinding(@src());
            if (server := _.server) {
                if (server.config.onError != .zero && !_.flags.has_called_error_handler) {
                    _.flags.has_called_error_handler = true;
                    const result = server.config.onError.call(
                        server.globalThis,
                        server.js_value.tryGet() ?? .js_undefined,
                        &.{value},
                    ) catch |err| server.globalThis.takeException(err);
                    const _guard_res = EnsureStillAliveGuard<@TypeOf(result)>{ val: result };
                    if (_.flags.aborted || _.server == null || _.server.?.flags.terminated) {
                        return;
                    }
                    if (_.resp == null && !_.didUpgradeWebSocket()) {
                        return;
                    }
                    if (resp := _.resp) {
                        if (resp.isClosed()) {
                            _.onConnectionClosedDuringDispatch(server, result);
                            return;
                        }
                    }
                    if (!result.isEmptyOrUndefinedOrNull()) {
                        if (err := result.toError()) {
                            _.finishRunningErrorHandler(err, status);
                            return;
                        } else if (promise := result.asAnyPromise()) {
                            _.processOnErrorPromise(result, promise, value, status);
                            return;
                        } else if (response := result.as(Response)) {
                            if (_.didUpgradeWebSocket()) {
                                discardResponseBody(server.globalThis, result);
                                return;
                            }
                            _.render(response);
                            return;
                        }
                    }
                    if (_.didUpgradeWebSocket()) {
                        return;
                    }
                }
            }

            _.finishRunningErrorHandler(value, status);
        }

        static fn processOnErrorPromise(
            ctx: *RequestContext,
            promise_js: jsc.JSValue,
            promise: jsc.AnyPromise,
            value: jsc.JSValue,
            status: u16,
        ) {
            assert(ctx.server != null);
            let vm = ctx.server.?.vm;

            let p = promise;
            switch (p.unwrap(vm.global.vm(), .mark_handled)) {
                .pending => {
                    ctx.flags.is_error_promise_pending = true;
                    const cell = ctx.createPromiseCell(ctx.server.?.globalThis);
                    promise_js.thenWithValue(
                        ctx.server.?.globalThis,
                        cell,
                        RequestContext.onResolve,
                        RequestContext.onReject,
                    ) catch {}; // TODO: properly propagate exception upwards
                },
                .fulfilled => |fulfilled_value| {
                    // if you return a Response object or a Promise<Response>
                    // but you upgraded the connection to a WebSocket
                    // just ignore the Response object. It doesn't do anything.
                    // it's better to do that than to throw an error
                    if (ctx.didUpgradeWebSocket()) {
                        return;
                    }

                    let response_opt = fulfilled_value.as(jsc.WebCore.Response);
                    if (response_opt == null) {
                        ctx.finishRunningErrorHandler(value, status);
                        return;
                    }
                    let response = response_opt.?;

                    ctx.response_jsvalue = fulfilled_value;
                    ctx.response_jsvalue.ensureStillAlive();
                    ctx.flags.response_protected = false;

                    const bodyValue = response.getBodyValue();
                    bodyValue.toBlobIfPossible();
                    switch (bodyValue.*) {
                        .Blob => |*blob| {
                            if (blob.needsToReadFile()) {
                                fulfilled_value.protect();
                                ctx.flags.response_protected = true;
                            }
                        },
                        .Locked => {
                            fulfilled_value.protect();
                            ctx.flags.response_protected = true;
                        },
                        else => {},
                    }
                    ctx.render(response);
                    return;
                },
                .rejected => |err| {
                    ctx.finishRunningErrorHandler(err, status);
                    return;
                },
            }
        }

        fn runErrorHandlerWithStatusCode(*_, value: jsc.JSValue,
            status: u16,) {
            jsc.markBinding(@src());
            if (!_.didUpgradeWebSocket() && (_.resp == null || _.resp.?.hasResponded())) return;

            runErrorHandlerWithStatusCodeDontCheckResponded(_, value, status);
        }

        fn renderMetadata(*_) {
            if (_.resp == null) return;
            const resp = _.resp.?;

            // For plain in-memory bodies this runs synchronously from
            // render() before any backpressure gap, so the Response is
            // always live here. File / stream bodies that call this after
            // an async hop keep the Response rooted via response_protected.
            let response: *jsc.WebCore.Response = _.response_weakref.get().?;
            let status = response.statusCode();
            let needs_content_range = _.flags.needs_content_range && (_.sendfile.total > 0 || _.sendfile.remain < _.blob.size());

            const size = if (needs_content_range)
                _.sendfile.remain
            else
                _.blob.size();

            status = if (status == 200 && size == 0 && !_.blob.isDetached())
                204
            else
                status;

            const ct_res = getContentType(
                response.getInitHeaders(),
                &_.blob,
                _.allocator,
            );
            const content_type = ct_res[0];
            const needs_content_type = ct_res[1];
            const content_type_needs_free = ct_res[2];
            const _guard_ct = DeinitWithAllocGuard<@TypeOf(content_type)>{ val: content_type, alloc: _.allocator, active: content_type_needs_free };
            let has_content_disposition = false;
            let has_content_range = false;
            if (headers_ := response.swapInitHeaders()) {
                const _guard_hdr = DerefGuard<@TypeOf(headers_)>{ val: headers_ };
                has_content_disposition = headers_.fastHas(.ContentDisposition);
                has_content_range = headers_.fastHas(.ContentRange);
                // For .slice()-driven ranges, only promote to 206 if the user
                // also set Content-Range (preserves the old contract). For an
                // incoming Range: header (sendfile.total > 0) we always 206.
                needs_content_range = needs_content_range && (_.sendfile.total > 0 || has_content_range);
                if (needs_content_range) {
                    status = 206;
                }

                _.doWriteStatus(status);
                _.doWriteHeaders(headers_);
            } else if (needs_content_range) {
                status = 206;
                _.doWriteStatus(status);
            } else {
                _.doWriteStatus(status);
            }

            if (cookies := _.cookies) {
                _.cookies = null;
                const _guard_cookies = DerefGuard<@TypeOf(cookies)>{ val: cookies };
                cookies.write(_.server.?.globalThis, resp_kind, unsafe { @ptrCast(_.resp.?) }) catch return; // TODO: properly propagate exception upwards
            }

            if (needs_content_type &&
                // do not insert the content type if it is the fallback value
                // we may not know the content-type when streaming
                (!_.blob.isDetached() || content_type.value.ptr != MimeType.other.value.ptr) &&
                std.mem.indexOfAny(u8, content_type.value, "\r\n\x00") == null)
            {
                resp.writeHeader("content-type", content_type.value);
            }

            // Advertise the QUIC endpoint on H1/H2 responses so browsers can
            // discover it (RFC 7838). Multiple Alt-Svc fields are valid, so a
            // user-supplied one composes rather than conflicts.
            if (comptime !http3 && @hasDecl(ThisServer, "h3AltSvc")) {
                if (alt := _.server.?.h3AltSvc()) resp.writeHeader("alt-svc", alt);
            }

            // automatically include the filename when:
            // 1. Bun.file("foo")
            // 2. The content-disposition header is not present
            if (!has_content_disposition && content_type.category.autosetFilename()) {
                if (filename := _.blob.getFileName()) {
                    const basename = std.fs.path.basename(filename);
                    if (basename.len > 0) {
                        let filename_buf: [1024]u8 = undefined;

                        resp.writeHeader(
                            "content-disposition",
                            std.fmt.bufPrint(&filename_buf, "filename=\"{s}\"", .{basename[0..@min(basename.len, 1024 - 32)]}) catch "",
                        );
                    }
                }
            }

            if (_.flags.needs_content_length) {
                resp.writeHeaderInt("content-length", size);
                resp.markWroteContentLengthHeader();
                _.flags.needs_content_length = false;
            }

            if (needs_content_range && !has_content_range) {
                let content_range_buf: [1024]u8 = undefined;

                resp.writeHeader(
                    "content-range",
                    if (_.sendfile.total > 0)
                        // We resolved an incoming Range header against the
                        // stat'd size, so the total is meaningful.
                        std.fmt.bufPrint(&content_range_buf, "bytes {d}-{d}/{d}", .{
                            _.sendfile.offset,
                            _.sendfile.offset + (_.sendfile.remain -| 1),
                            _.sendfile.total,
                        }) catch "bytes */*"
                    else
                        // For .slice()-driven ranges we omit the full size:
                        // it can change between requests and may leak PII.
                        std.fmt.bufPrint(&content_range_buf, "bytes {d}-{d}/*", .{
                            _.sendfile.offset,
                            _.sendfile.offset + (_.sendfile.remain -| 1),
                        }) catch "bytes */*",
                );
                if (_.sendfile.total > 0) resp.writeHeader("accept-ranges", "bytes");
                _.flags.needs_content_range = false;
            }
        }

        fn doWriteStatus(*_, status: u16) {
            assert(!_.flags.has_written_status);
            _.flags.has_written_status = true;

            const resp = _.resp ?? return;
            if (text := HTTPStatusText.get(status)) {
                resp.writeStatus(text);
            } else {
                let buf: [48]u8 = undefined;
                resp.writeStatus(std.fmt.bufPrint(&buf, "{d} HM", .{status}) catch unreachable);
            }
        }

        fn doWriteHeaders(*_, headers: *WebCore.FetchHeaders) {
            ctxLog("writeHeaders", .{});
            headers.fastRemove(.ContentLength);
            headers.fastRemove(.TransferEncoding);
            if (comptime http3) {
                // RFC 9114 §4.2: connection-specific fields are malformed.
                headers.fastRemove(.Connection);
                headers.fastRemove(.KeepAlive);
                headers.fastRemove(.ProxyConnection);
                headers.fastRemove(.Upgrade);
            }
            if (resp := _.resp) headers.toUWSResponse(resp_kind, resp);
        }

        fn renderBytes(*_) {
            // copy it to stack memory to prevent aliasing issues in release builds
            const blob = _.blob;
            const bytes = blob.slice();
            if (resp := _.resp) {
                if (!resp.tryEnd(
                    bytes,
                    bytes.len,
                    _.shouldCloseConnection(),
                )) {
                    _.flags.has_marked_pending = true;
                    resp.onWritable(_, onWritableBytes);
                    return;
                }
            }
            _.detachResponse();
            _.endRequestStreamingAndDrain();
            _.deref();
        }

        /// Replace the tracked Response. Drops the previous weak ref (if any)
        /// before taking a new one so the old Response's allocation can be
        /// freed once its own strong refs go to zero.
        fn setResponse(*_, response: *jsc.WebCore.Response) {
            if (_.response_weakref.raw_ptr == response) return;
            _.response_weakref.deref();
            _.response_weakref = .initRef(response);
        }

        fn render(*_, response: *jsc.WebCore.Response) {
            ctxLog("render", .{});
            _.setResponse(response);

            _.doRender();
        }

        fn onBufferedBodyChunk(*_, resp: *App.Response, chunk: []const u8, last: bool) {
            ctxLog("onBufferedBodyChunk {} {}", .{ chunk.len, last });

            assert(_.resp == resp);

            _.flags.is_waiting_for_request_body = last == false;
            if (_.isAbortedOrEnded() || _.flags.has_marked_complete) return;
            if (!last && chunk.len == 0) {
                // Sometimes, we get back an empty chunk
                // We have to ignore those chunks unless it's the last one
                return;
            }
            const vm = _.server.?.vm;
            const globalThis = _.server.?.globalThis;

            // After the user does request.body,
            // if they then do .text(), .arrayBuffer(), etc
            // we can no longer hold the strong reference from the body value ref.
            if (readable := _.request_body_readable_stream_ref.get(globalThis)) {
                assert(_.request_body_buf.items.len == 0);
                vm.eventLoop().enter();
                const _guard_el = ExitGuard<@TypeOf(vm.eventLoop())>{ ptr: vm.eventLoop() };

                if (!last) {
                    readable.ptr.Bytes.onData(
                        .{
                            .temporary = bun.ByteList.fromBorrowedSliceDangerous(chunk),
                        },
                        bun.default_allocator,
                    ) catch {}; // TODO: properly propagate exception upwards
                } else {
                    let strong = _.request_body_readable_stream_ref;
                    _.request_body_readable_stream_ref = .{};
                    const _guard_strong = DeinitGuard<@TypeOf(&strong)>{ val: &strong };
                    if (request_body := _.request_body) {
                        * = request_body.unref();
                        _.request_body = null;
                    }

                    readable.value.ensureStillAlive();
                    readable.ptr.Bytes.onData(
                        .{
                            .temporary_and_done = bun.ByteList.fromBorrowedSliceDangerous(chunk),
                        },
                        bun.default_allocator,
                    ) catch {}; // TODO: properly propagate exception upwards
                }

                return;
            }

            // This is the start of a task, so it's a good time to drain
            if (_.request_body != null) {
                let body = _.request_body.?;

                // The up-front maxRequestBodySize check in server.zig only
                // sees Content-Length. HTTP/3 (and H1 chunked) bodies may
                // omit it, so cap accumulated bytes here too — otherwise a
                // single CL-less stream can grow request_body_buf without
                // bound.
                if (_.request_body_buf.items.len +| chunk.len > _.server.?.config.max_request_body_size) {
                    _.request_body_buf.clearAndFree(_.allocator);
                    resp.clearOnData();
                    _.flags.is_waiting_for_request_body = false;

                    let loop = vm.eventLoop();
                    loop.enter();
                    const _guard_el = ExitGuard<@TypeOf(loop)>{ ptr: loop };
                    // Reject the pending body first so endRequestStreaming()
                    // below (via this.endWithoutBody) doesn't substitute a
                    // generic ConnectionClosed. toErrorInstance handles
                    // .Locked itself (rejects the promise, deinits the
                    // readable, calls onReceiveValue).
                    body.value.toErrorInstance(.{
                        .Message = bun.String.static("Request body exceeded maxRequestBodySize"),
                    }, globalThis) catch {};

                    // Route through the normal end path so this.resp is
                    // detached and the base ref released. Writing directly on
                    // the raw uWS response left this.resp pointing at a
                    // completed (and soon freed) response — uWS markDone()
                    // clears onAborted so no abort ever fires to release the
                    // ref, and a later handleResolve()/handleReject() from an
                    // async handler would dereference the stale pointer.
                    if (_.resp != null && !resp.hasResponded()) {
                        _.flags.has_written_status = true;
                        resp.writeStatus("413 Payload Too Large");
                    }
                    _.endWithoutBody(comptime !http3);
                    return;
                }

                if (last) {
                    let bytes = &_.request_body_buf;

                    let old = body.value;

                    const total = bytes.items.len + chunk.len;
                    getter: {
                        // if (total <= jsc.WebCore.InlineBlob.available_bytes) {
                        //     if (total == 0) {
                        //         body.value = .{ .Empty = {} };
                        //         break :getter;
                        //     }

                        //     body.value = .{ .InlineBlob = jsc.WebCore.InlineBlob.concat(bytes.items, chunk) };
                        //     this.request_body_buf.clearAndFree(this.allocator);
                        // } else {
                        bytes.ensureTotalCapacityPrecise(_.allocator, total) catch |err| {
                            _.request_body_buf.clearAndFree(_.allocator);
                            body.value.toError(err, globalThis) catch {}; // TODO: properly propagate exception upwards
                            break :getter;
                        };

                        const prev_len = bytes.items.len;
                        bytes.items.len = total;
                        let slice = bytes.items[prev_len..];
                        @memcpy(slice[0..chunk.len], chunk);
                        body.value = .{
                            .InternalBlob = .{
                                .bytes = bytes.toManaged(_.allocator),
                            },
                        };
                        // }
                    }
                    _.request_body_buf = .{};

                    if (old == .Locked) {
                        let loop = vm.eventLoop();
                        loop.enter();
                        const _guard_el = ExitGuard<@TypeOf(loop)>{ ptr: loop };

                        old.resolve(&body.value, globalThis, null) catch {}; // TODO: properly propagate exception upwards
                    }
                    return;
                }

                if (_.request_body_buf.capacity == 0) {
                    _.request_body_buf.ensureTotalCapacityPrecise(_.allocator, @min(_.request_body_content_len, max_request_body_preallocate_length)) catch @panic("Out of memory while allocating request body buffer");
                }
                _.request_body_buf.appendSlice(_.allocator, chunk) catch @panic("Out of memory while allocating request body");
            }
        }

        fn onStartStreamingRequestBody(*_): jsc.WebCore.DrainResult {
            ctxLog("onStartStreamingRequestBody", .{});
            if (_.isAbortedOrEnded()) {
                return jsc.WebCore.DrainResult.aborted;
            }

            // This means we have received part of the body but not the whole thing
            if (_.request_body_buf.items.len > 0) {
                let emptied = _.request_body_buf;
                _.request_body_buf = .{};
                return .{
                    .owned = .{
                        .list = emptied.toManaged(_.allocator),
                        .size_hint = if (emptied.capacity < max_request_body_preallocate_length)
                            emptied.capacity
                        else
                            0,
                    },
                };
            }

            return .{
                .estimated_size = _.request_body_content_len,
            };
        }
        const max_request_body_preallocate_length = 1024 * 256;
        fn onStartBuffering(*_) {
            if (server := _.server) {
                ctxLog("onStartBuffering", .{});
                // TODO: check if is someone calling onStartBuffering other than onStartBufferingCallback
                // if is not, this should be removed and only keep protect + setAbortHandler
                // HTTP/3 (RFC 9114): Content-Length is optional; the body is
                // delimited by stream FIN, so the H1 "no CL + no TE ⇒ empty"
                // shortcut would drop it.
                if (!http3 && _.flags.is_transfer_encoding == false && _.request_body_content_len == 0) {
                    // no content-length or 0 content-length
                    // no transfer-encoding
                    if (_.request_body != null) {
                        let body = _.request_body.?;
                        let old = body.value;
                        old.Locked.onReceiveValue = null;
                        let new_body: WebCore.Body.Value = .Null;
                        old.resolve(&new_body, server.globalThis, null) catch {}; // TODO: properly propagate exception upwards
                        body.value = new_body;
                    }
                }
            }
        }

        static fn onRequestBodyReadableStreamAvailable(ptr: *!, globalThis: *jsc.JSGlobalObject, readable: jsc.WebCore.ReadableStream) {
            let req_ctx = bun.cast(RequestContextPtr, ptr);
            bun.debugAssert(req_ctx.request_body_readable_stream_ref.held.impl == null);
            req_ctx.request_body_readable_stream_ref = jsc.WebCore.ReadableStream.Strong.init(readable, globalThis);
        }

        static fn onStartBufferingCallback(req_ctx: *!) {
            onStartBuffering(bun.cast(RequestContextPtr, req_ctx));
        }

        static fn onStartStreamingRequestBodyCallback(req_ctx: *!): jsc.WebCore.DrainResult {
            return onStartStreamingRequestBody(bun.cast(RequestContextPtr, req_ctx));
        }

        fn getRemoteSocketInfo(*_): uws.SocketAddress? {
            return (_.resp ?? return null).getRemoteSocketInfo();
        }

        fn setTimeout(*_, seconds: c_uint): bool {
            if (resp := _.resp) {
                resp.timeout(@min(seconds, 255));
                if (seconds > 0) {

                    // we only set the timeout callback if we wanna the timeout event to be triggered
                    // the connection will be closed so the abort handler will be called after the timeout
                    if (req := _.request_weakref.get()) {
                        if (req.internal_event_callback.hasCallback()) {
                            _.setTimeoutHandler();
                        }
                    }
                } else {
                    // if the timeout is 0, we don't need to trigger the timeout event
                    resp.clearTimeout();
                }
                return true;
            }
            return false;
        }

        comptime {
            const export_prefix = "Bun__HTTPRequestContext" ++ (if (debug_mode) "Debug" else "") ++ (if (http3) "H3" else if (ThisServer.ssl_enabled) "TLS" else "");
            if (bun.Environment.export_cpp_apis) {
                @export(&jsc.toJSHostFn(onResolve), .{ .name = export_prefix ++ "__onResolve" });
                @export(&jsc.toJSHostFn(onReject), .{ .name = export_prefix ++ "__onReject" });
                @export(&jsc.toJSHostFn(onResolveStream), .{ .name = export_prefix ++ "__onResolveStream" });
                @export(&jsc.toJSHostFn(onRejectStream), .{ .name = export_prefix ++ "__onRejectStream" });
            }
        }
    };
}

// Retained only for `renderMetadata` to compute Content-Range / Content-Length
// for file-blob bodies; the actual fd/socket bookkeeping lives in
// `FileResponseStream` now.
struct SendfileContext {
    remain: Blob.SizeType = 0,
    offset: Blob.SizeType = 0,
    /// When non-zero, the Content-Range total (`/{total}` instead of `/*`).
    total: Blob.SizeType = 0,
}

@packed(u16) struct FlagsDebug {
    has_marked_complete: bool = false,
    has_marked_pending: bool = false,
    has_abort_handler: bool = false,
    has_timeout_handler: bool = false,
    has_sendfile_ctx: bool = false,
    has_called_error_handler: bool = false,
    needs_content_length: bool = false,
    needs_content_range: bool = false,
    is_transfer_encoding: bool = false,
    is_waiting_for_request_body: bool = false,
    is_web_browser_navigation: bool = false,
    has_written_status: bool = false,
    response_protected: bool = false,
    aborted: bool = false,
    has_finalized: bun.DebugOnly(bool) = if (Environment.isDebug) false else @as(void, undefined),
    is_error_promise_pending: bool = false,
    _padding: std.meta.Int(.unsigned, if (Environment.isDebug) 0 else 1) = 0,
}

@packed(u16) struct FlagsRelease {
    has_marked_complete: bool = false,
    has_marked_pending: bool = false,
    has_abort_handler: bool = false,
    has_timeout_handler: bool = false,
    has_sendfile_ctx: bool = false,
    has_called_error_handler: bool = false,
    needs_content_length: bool = false,
    needs_content_range: bool = false,
    is_transfer_encoding: bool = false,
    is_waiting_for_request_body: bool = false,
    is_web_browser_navigation: void = @as(void, undefined),
    has_written_status: bool = false,
    response_protected: bool = false,
    aborted: bool = false,
    has_finalized: bun.DebugOnly(bool) = if (Environment.isDebug) false else @as(void, undefined),
    is_error_promise_pending: bool = false,
    _padding: std.meta.Int(.unsigned, if (Environment.isDebug) 1 else 2) = 0,
}

fn NewFlags(comptime debug_mode: bool): type {
    return if (debug_mode) FlagsDebug else FlagsRelease;
}

fn getContentType(headers: (*WebCore.FetchHeaders)?, blob: *const WebCore.Blob.Any, allocator: std.mem.Allocator) : [MimeType, bool, bool] {
    let needs_content_type = true;
    let content_type_needs_free = false;

    if (headers_ := headers) {
        if (content := headers_.fastGet(.ContentType)) {
            needs_content_type = false;
            let content_slice = content.toSlice(allocator);
            const _cs_guard = DeinitGuard<@TypeOf(content_slice)>{ val: content_slice };
            const content_type_allocator = if (content_slice.allocator.isNull()) null else allocator;
            return .{ MimeType.init(content_slice.slice(), content_type_allocator, &content_type_needs_free), needs_content_type, content_type_needs_free };
        }
    }

    let content_type: MimeType = .other;
    if (blob.contentType().len > 0) {
        content_type = MimeType.byName(blob.contentType());
    } else if (sniffed := MimeType.sniff(blob.slice())) {
        content_type = sniffed;
    } else if (blob.wasString()) {
        content_type = MimeType.text;
    } else {
        content_type = MimeType.other;
    }

    return .{ content_type, needs_content_type, content_type_needs_free };
}

const welcome_page_html_gz = @embedFile("../api/welcome-page.html.gz");

const ctxLog = Output.scoped(.RequestContext, .visible);

const Fallback = runtime_mod.Fallback;

const Environment = bun.Environment;
const JSError = bun.JSError;
const Output = bun.Output;
const S3 = bun.S3;
const String = bun.String;
const assert = bun.assert;
const logger = bun.logger;
const uws = bun.uws;
const Api = bun.schema.api;

const FileResponseStream = bun.api.server.FileResponseStream;
const HTTPStatusText = bun.api.server.HTTPStatusText;
const RangeRequest = bun.api.server.RangeRequest;

const HTTP = bun.http;
const MimeType = bun.http.MimeType;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;

const AnyRequestContext = jsc.API.AnyRequestContext;
const NativePromiseContext = jsc.API.NativePromiseContext;

const WebCore = jsc.WebCore;
const Blob = jsc.WebCore.Blob;
const Body = jsc.WebCore.Body;
const FetchHeaders = jsc.WebCore.FetchHeaders;
const Request = jsc.WebCore.Request;
const Response = jsc.WebCore.Response;
