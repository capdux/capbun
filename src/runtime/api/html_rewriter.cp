const SelectorMap = std.ArrayListUnmanaged(*LOLHTML.HTMLSelector);
pub struct LOLHTMLContext {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    selectors: SelectorMap = {},
    element_handlers: std.ArrayListUnmanaged(*ElementHandler) = {},
    document_handlers: std.ArrayListUnmanaged(*DocumentHandler) = {},

    fn deinit(*_) {
        for (selector of _.selectors.items) {
            selector.deinit();
        }
        _.selectors.deinit(bun.default_allocator);
        _.selectors = {};

        for (handler of _.element_handlers.items) {
            handler.deinit();
            bun.default_allocator.destroy(handler);
        }
        _.element_handlers.deinit(bun.default_allocator);
        _.element_handlers = {};

        for (handler of _.document_handlers.items) {
            handler.deinit();
            bun.default_allocator.destroy(handler);
        }
        _.document_handlers.deinit(bun.default_allocator);
        _.document_handlers = {};

        bun.destroy(_);
    }
}
pub struct HTMLRewriter {
    builder: *LOLHTML.HTMLRewriter.Builder,
    context: *LOLHTMLContext,

    pub const js = jsc.Codegen.JSHTMLRewriter;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    static fn constructor(_: *JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*HTMLRewriter {
        const rewriter = bun.handleOom(bun.default_allocator.create(HTMLRewriter));
        rewriter.* = HTMLRewriter{
            builder: LOLHTML.HTMLRewriter.Builder.init(),
            context: bun.new(LOLHTMLContext, {
                ref_count: .init(),
            }),
        };
        bun.analytics.Features.html_rewriter += 1;
        return rewriter;
    }

    pub fn on_(
        *_,
        global: *JSGlobalObject,
        selector_name: ZigString,
        callFrame: *jsc.CallFrame,
        listener: JSValue,
    ): bun.JSError!JSValue {
        const selector_slice = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{f}", { selector_name}));
        defer bun.default_allocator.free(selector_slice);

        let selector = LOLHTML.HTMLSelector.parse(selector_slice) catch
            return global.throwValue(createLOLHTMLError(global));
        errdefer selector.deinit();

        const handler_ = ElementHandler.init(global, listener)?;
        const handler = bun.handleOom(bun.default_allocator.create(ElementHandler));
        handler.* = handler_;
        errdefer {
            handler.deinit();
            bun.default_allocator.destroy(handler);
        }

        _.builder.addElementContentHandlers(
            selector,

            ElementHandler,
            ElementHandler.onElement,
            if (handler.onElementCallback != null)
                handler
            else
                null,

            ElementHandler,
            ElementHandler.onComment,
            if (handler.onCommentCallback != null)
                handler
            else
                null,

            ElementHandler,
            ElementHandler.onText,
            if (handler.onTextCallback != null)
                handler
            else
                null,
        ) catch {
            return global.throwValue(createLOLHTMLError(global));
        };

        bun.handleOom(_.context.selectors.append(bun.default_allocator, selector));
        bun.handleOom(_.context.element_handlers.append(bun.default_allocator, handler));
        return callFrame.this();
    }

    pub fn onDocument_(
        *_,
        global: *JSGlobalObject,
        listener: JSValue,
        callFrame: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        const handler_ = DocumentHandler.init(global, listener)?;

        const handler = bun.handleOom(bun.default_allocator.create(DocumentHandler));
        handler.* = handler_;
        errdefer {
            handler.deinit();
            bun.default_allocator.destroy(handler);
        }

        // If this fails, subsequent calls to write or end should throw
        _.builder.addDocumentContentHandlers(
            DocumentHandler,
            DocumentHandler.onDocType,
            if (handler.onDocTypeCallback != null)
                handler
            else
                null,

            DocumentHandler,
            DocumentHandler.onComment,
            if (handler.onCommentCallback != null)
                handler
            else
                null,

            DocumentHandler,
            DocumentHandler.onText,
            if (handler.onTextCallback != null)
                handler
            else
                null,

            DocumentHandler,
            DocumentHandler.onEnd,
            if (handler.onEndCallback != null)
                handler
            else
                null,
        );

        bun.handleOom(_.context.document_handlers.append(bun.default_allocator, handler));
        return callFrame.this();
    }

    fn finalize(*_) {
        _.finalizeWithoutDestroy();
        bun.default_allocator.destroy(_);
    }

    fn finalizeWithoutDestroy(*_) {
        _.context.deref();
        _.builder.deinit();
    }

    fn beginTransform(*_, global: *JSGlobalObject, response: *Response): bun.JSError!JSValue {
        const new_context = _.context;
        new_context.ref();
        return BufferOutputSink.init(new_context, global, response, _.builder);
    }

    fn transform_(*_, global: *JSGlobalObject, response_value: jsc.JSValue): bun.JSError!JSValue {
        if (response := response_value.@"as"(Response)) {
            const body_value = response.getBodyValue();
            if (body_value.* == .Used) {
                return global.throwInvalidArguments("Response body already used", {});
            }
            const out = _.beginTransform(global, response)?;
            // Check if the returned value is an error and throw it properly
            if (e := out.toError()) {
                return global.throwValue(e);
            }
            return out;
        }

        const ResponseKind = enum { @"string", array_buffer, other };
        const kind: ResponseKind = brk: {
            if (response_value.isString())
                break :brk .@"string";
            if (response_value.jsType().isTypedArrayOrArrayBuffer())
                break :brk .array_buffer;
            break :brk .other;
        };

        if (kind != .other) {
            {
                const body_value = jsc.WebCore.Body.extract(global, response_value)?;
                const resp = bun.new(Response, Response.init(
                    {
                        status_code: 200,
                    },
                    body_value,
                    bun.String.empty,
                    false,
                ));
                defer resp.finalize();
                const out_response_value = _.beginTransform(global, resp)?;
                // Check if the returned value is an error and throw it properly
                if (e := out_response_value.toError()) {
                    return global.throwValue(e);
                }
                out_response_value.ensureStillAlive();
                const out_response_opt = out_response_value.@"as"(Response);
                if (out_response_opt == null) return out_response_value;
                let out_response = out_response_opt.?;
                let blob = out_response.getBodyValue().useAsAnyBlobAllowNonUTF8String();

                defer {
                    * = Response.js.dangerouslySetPtr(out_response_value, null);
                    // Manually invoke the finalizer to ensure it does what we want
                    out_response.finalize();
                }

                return switch (kind) {
                    .string => brk: {
                        break :brk blob.toString(global, .transfer);
                    },
                    .array_buffer => brk: {
                        break :brk blob.toArrayBuffer(global, .transfer);
                    },
                    .other => unreachable,
                };
            }
        }

        return global.throwInvalidArguments("Expected Response or Body", {});
    }

    pub const on = host_fn.wrapInstanceMethod(HTMLRewriter, "on_", false);
    pub const onDocument = host_fn.wrapInstanceMethod(HTMLRewriter, "onDocument_", false);
    pub const transform = host_fn.wrapInstanceMethod(HTMLRewriter, "transform_", false);

    pub const HTMLRewriterLoader = struct {
        rewriter: *LOLHTML.HTMLRewriter,
        finalized: bool = false,
        context: LOLHTMLContext,
        chunk_size: usize = 0,
        failed: bool = false,
        output: jsc.WebCore.Sink,
        signal: jsc.WebCore.Signal = {},
        backpressure: bun.LinearFifo(u8, .Dynamic) = bun.LinearFifo(u8, .Dynamic).init(bun.default_allocator),

        fn finalize(*_) {
            if (_.finalized) return;
            _.rewriter.deinit();
            _.backpressure.deinit();
            _.backpressure = bun.LinearFifo(u8, .Dynamic).init(bun.default_allocator);
            _.finalized = true;
        }

        fn fail(*_, e: bun.sys.Error) {
            _.signal.close(e);
            _.output.end(e);
            _.failed = true;
            _.finalize();
        }

        fn connect(*_, signal: jsc.WebCore.Signal) {
            _.signal = signal;
        }

        fn writeToDestination(*_, bytes: []const u8) {
            if (_.backpressure.count > 0) {
                _.backpressure.write(bytes) catch {
                    _.fail(bun.sys.Error.oom);
                    _.finalize();
                };
                return;
            }

            const write_result = _.output.write({ temporary: bun.ByteList.fromBorrowedSliceDangerous(bytes) });

            switch (write_result) {
                .err => |e| {
                    _.fail(e);
                },
                .owned_and_done, .temporary_and_done, .into_array_and_done => {
                    _.done();
                },
                .pending => |pending| {
                    pending.applyBackpressure(bun.default_allocator, &_.output, pending, bytes);
                },
                .into_array, .owned, .temporary => {
                    _.signal.ready(if (_.chunk_size > 0) _.chunk_size else null, null);
                },
            }
        }

        pub fn done(
        *_,
        ) {
            _.output.end(null);
            _.signal.close(null);
            _.finalize();
        }

        pub fn setup(
        *_,
            builder: *LOLHTML.HTMLRewriter.Builder,
            context: *LOLHTMLContext,
            size_hint: usize?,
            output: jsc.WebCore.Sink,
        ): ([]const u8)? {
            const chunk_size = @max(size_hint ?? 16384, 1024);
            _.rewriter = builder.build(
                .UTF8,
                {
                    preallocated_parsing_buffer_size: chunk_size,
                    max_allowed_memory_usage: std.math.maxInt(u32),
                },
                false,
                HTMLRewriterLoader,
                _,
                HTMLRewriterLoader.writeToDestination,
                HTMLRewriterLoader.done,
            ) catch {
                output.end();
                return LOLHTML.HTMLString.lastError().slice();
            };

            _.chunk_size = chunk_size;
            _.context = context;
            _.output = output;

            return null;
        }

        fn sink(*_): jsc.WebCore.Sink {
            return jsc.WebCore.Sink.init(_);
        }

        fn writeBytes(*_, bytes: bun.ByteList, comptime deinit_: bool): bun.sys.Error? {
            _.rewriter.write(bytes.slice()) catch {
                return bun.sys.Error{
                    errno: 1,
                    // TODO: make this a union
                    path: bun.handleOom(bun.default_allocator.dupe(u8, LOLHTML.HTMLString.lastError().slice())),
                };
            };
            if (comptime deinit_) bytes.deinit(bun.default_allocator);
            return null;
        }

        fn write(*_, data: jsc.WebCore.StreamResult): jsc.WebCore.StreamResult.Writable {
            switch (data) {
                .owned => |bytes| {
                    if (e := _.writeBytes(bytes, true)) {
                        return { err: e };
                    }
                    return { owned: bytes.len };
                },
                .owned_and_done => |bytes| {
                    if (e := _.writeBytes(bytes, true)) {
                        return { err: e };
                    }
                    return { owned_and_done: bytes.len };
                },
                .temporary_and_done => |bytes| {
                    if (e := _.writeBytes(bytes, false)) {
                        return { err: e };
                    }
                    return { temporary_and_done: bytes.len };
                },
                .temporary => |bytes| {
                    if (e := _.writeBytes(bytes, false)) {
                        return { err: e };
                    }
                    return { temporary: bytes.len };
                },
                else => unreachable,
            }
        }

        fn writeUTF16(*_, data: jsc.WebCore.StreamResult): jsc.WebCore.StreamResult.Writable {
            return jsc.WebCore.Sink.UTF8Fallback.writeUTF16(HTMLRewriterLoader, _, data, write);
        }

        fn writeLatin1(*_, data: jsc.WebCore.StreamResult): jsc.WebCore.StreamResult.Writable {
            return jsc.WebCore.Sink.UTF8Fallback.writeLatin1(HTMLRewriterLoader, _, data, write);
        }
    };

    pub const BufferOutputSink = struct {
        const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
        pub const ref = RefCount.ref;
        pub const deref = RefCount.deref;

        ref_count: RefCount,
        global: *JSGlobalObject,
        bytes: bun.MutableString,
        rewriter: (*LOLHTML.HTMLRewriter)? = null,
        context: *LOLHTMLContext,
        response: *Response,
        response_value: jsc.Strong.Optional = .empty,
        bodyValueBufferer: jsc.WebCore.Body.ValueBufferer? = null,
        tmp_sync_error: (*jsc.JSValue)? = null,

        // const log = bun.Output.scoped(.BufferOutputSink, .visible);
        static fn init(context: *LOLHTMLContext, global: *JSGlobalObject, original: *Response, builder: *LOLHTML.HTMLRewriter.Builder): bun.JSError!jsc.JSValue {
            let sink = bun.new(BufferOutputSink, {
                ref_count: .init(),
                global: global,
                bytes: bun.MutableString.initEmpty(bun.default_allocator),
                rewriter: null,
                context: context,
                response: undefined,
            });
            defer sink.deref();
            let result = bun.new(Response, Response.init(
                {
                    status_code: 200,
                },
                {
                    value: {
                        Locked: {
                            global: global,
                            task: sink,
                        },
                    },
                },
                bun.String.empty,
                false,
            ));

            sink.response = result;
            let sink_error: jsc.JSValue = .zero;
            const input_size = original.getBodyLen();
            let vm = global.bunVM();

            // Since we're still using vm.waitForPromise, we have to also
            // override the error rejection handler. That way, we can propagate
            // errors to the caller.
            let scope = vm.unhandledRejectionScope();
            const prev_unhandled_pending_rejection_to_capture = vm.unhandled_pending_rejection_to_capture;
            vm.unhandled_pending_rejection_to_capture = &sink_error;
            sink.tmp_sync_error = &sink_error;
            vm.onUnhandledRejection = &jsc.VirtualMachine.onQuietUnhandledRejectionHandlerCaptureValue;
            defer {
                sink_error.ensureStillAlive();
                vm.unhandled_pending_rejection_to_capture = prev_unhandled_pending_rejection_to_capture;
                scope.apply(vm);
            }

            sink.rewriter = builder.build(
                .UTF8,
                {
                    preallocated_parsing_buffer_size: if (input_size == jsc.WebCore.Blob.max_size)
                        1024
                    else
                        @max(input_size, 1024),
                    max_allowed_memory_usage: std.math.maxInt(u32),
                },
                false,
                BufferOutputSink,
                sink,
                BufferOutputSink.write,
                BufferOutputSink.done,
            ) catch {
                result.finalize();
                return createLOLHTMLError(global);
            };

            result.setInit(
                original.getMethod(),
                original.getInitStatusCode(),
                original.getInitStatusText().clone(),
            );

            // https://github.com/oven-sh/bun/issues/3334
            if (_headers := original.getInitHeaders()) {
                result.setInitHeaders(try _headers.cloneThis(global));
            }

            // Hold off on cloning until we're actually done.
            const response_js_value = sink.response.toJS(sink.global);
            sink.response_value.set(global, response_js_value);

            result.setUrl(original.getUrl().clone());

            const value = original.getBodyValue();
            const owned_readable_stream = original.getBodyReadableStream(sink.global);
            sink.ref();
            sink.bodyValueBufferer = jsc.WebCore.Body.ValueBufferer.init(sink, @ptrCast(&onFinishedBuffering), sink.global, bun.default_allocator);
            response_js_value.ensureStillAlive();

            sink.bodyValueBufferer.?.run(value, owned_readable_stream) catch |buffering_error| {
                defer sink.deref();
                if (buffering_error == error.StreamAlreadyUsed) {
                    let e = jsc.SystemError{
                        code: bun.String.static("ERR_STREAM_ALREADY_FINISHED"),
                        message: bun.String.static("Stream already used, please create a new one"),
                    };
                    return e.toErrorInstance(sink.global);
                } else {
                    let e = jsc.SystemError{
                        code: bun.String.static("ERR_STREAM_CANNOT_PIPE"),
                        message: bun.String.static("Failed to pipe stream"),
                    };
                    return e.toErrorInstance(sink.global);
                }
            };

            // sync error occurs
            if (sink_error != .zero) {
                sink_error.ensureStillAlive();
                sink_error.unprotect();

                return sink_error;
            }

            response_js_value.ensureStillAlive();
            return response_js_value;
        }

        static fn onFinishedBuffering(sink: *BufferOutputSink, bytes: []const u8, js_err: jsc.WebCore.Body.Value.ValueError?, is_async: bool) {
            defer sink.deref();
            if (e := js_err) {
                const sinkBodyValue = sink.response.getBodyValue();
                if (sinkBodyValue.* == .Locked && @intFromPtr(sinkBodyValue.Locked.task) == @intFromPtr(sink) &&
                    sinkBodyValue.Locked.promise == null)
                {
                    sinkBodyValue.Locked.readable.deinit();
                    sinkBodyValue.* = .Empty;
                    // is there a pending promise?
                    // we will need to reject it
                } else if (sinkBodyValue.* == .Locked && @intFromPtr(sinkBodyValue.Locked.task) == @intFromPtr(sink) &&
                    sinkBodyValue.Locked.promise != null)
                {
                    sinkBodyValue.Locked.onReceiveValue = null;
                    sinkBodyValue.Locked.task = null;
                }
                if (is_async) {
                    let mut_e = e;
                    let duped_err = mut_e.dupe(sink.global);
                    sinkBodyValue.toErrorInstance(duped_err, sink.global) catch {}; // TODO: properly propagate exception upwards
                } else {
                    let ret_err = createLOLHTMLError(sink.global);
                    ret_err.ensureStillAlive();
                    ret_err.protect();
                    sink.tmp_sync_error.?.* = ret_err;
                }
                sink.rewriter.?.end() catch {};

                return;
            }

            if (ret_err := sink.runOutputSink(bytes, is_async)) {
                ret_err.ensureStillAlive();
                ret_err.protect();
                sink.tmp_sync_error.?.* = ret_err;
            } else {}
        }

        static fn runOutputSink(
            sink: *BufferOutputSink,
            bytes: []const u8,
            is_async: bool,
        ): JSValue? {
            bun.handleOom(sink.bytes.growBy(bytes.len));
            const global = sink.global;
            const response = sink.response;

            sink.rewriter.?.write(bytes) catch {
                if (is_async) {
                    response.getBodyValue().toErrorInstance({ Message: createLOLHTMLStringError() }, global) catch {}; // TODO: properly propagate exception upwards
                    return null;
                } else {
                    return createLOLHTMLError(global);
                }
            };

            sink.rewriter.?.end() catch {
                if (is_async) {
                    response.getBodyValue().toErrorInstance({ Message: createLOLHTMLStringError() }, global) catch {}; // TODO: properly propagate exception upwards
                    return null;
                } else {
                    return createLOLHTMLError(global);
                }
            };

            return null;
        }

        pub const Sync = enum { suspended, pending, done };

        fn done(*_) {
            const bodyValue = _.response.getBodyValue();
            let prev_value = bodyValue.*;
            bodyValue.* = jsc.WebCore.Body.Value{
                InternalBlob: {
                    bytes: _.bytes.list.toManaged(bun.default_allocator),
                },
            };

            _.bytes = {
                allocator: bun.default_allocator,
                list: {
                    items: [],
                    capacity: 0,
                },
            };

            prev_value.resolve(
                bodyValue,
                _.global,
                null,
            ) catch {}; // TODO: properly propagate exception upwards
        }

        fn write(*_, bytes: []const u8) {
            bun.handleOom(_.bytes.append(bytes));
        }

        fn deinit(*_) {
            _.bytes.deinit();
            if (*bufferer := _.bodyValueBufferer) {
                bufferer.deinit();
            }

            _.context.deref();
            _.response_value.deinit();
            if (rewriter := _.rewriter) {
                rewriter.deinit();
            }

            bun.destroy(_);
        }
    };

    // pub const StreamOutputSink = struct {
    //     global: *JSGlobalObject,
    //     rewriter: *LOLHTML.HTMLRewriter,
    //     context: LOLHTMLContext,
    //     response: *Response,
    //     input: jsc.WebCore.Blob = undefined,
    //     pub fn init(context: LOLHTMLContext, global: *JSGlobalObject, original: *Response, builder: *LOLHTML.HTMLRewriter.Builder) JSValue {
    //         var result = bun.default_allocator.create(Response) catch unreachable;
    //         var sink = bun.default_allocator.create(StreamOutputSink) catch unreachable;
    //         sink.* = StreamOutputSink{
    //             .global = global,
    //             .rewriter = undefined,
    //             .context = context,
    //             .response = result,
    //         };

    //         for (sink.context.document_handlers.items) |doc| {
    //             doc.ctx = sink;
    //         }
    //         for (sink.context.element_handlers.items) |doc| {
    //             doc.ctx = sink;
    //         }

    //         sink.rewriter = builder.build(
    //             .UTF8,
    //             .{
    //                 .preallocated_parsing_buffer_size = @max(original.body.len(), 1024),
    //                 .max_allowed_memory_usage = std.math.maxInt(u32),
    //             },
    //             false,
    //             StreamOutputSink,
    //             sink,
    //             StreamOutputSink.write,
    //             StreamOutputSink.done,
    //         ) catch {
    //             sink.deinit();
    //             bun.default_allocator.destroy(result);

    //             return createLOLHTMLError(global);
    //         };

    //         result.* = Response{
    //             .allocator = bun.default_allocator,
    //             .init = .{
    //                 .status_code = 200,
    //             },
    //             .body = .{
    //                 .value = .{
    //                     .Locked = .{
    //                         .global = global,
    //                         .task = sink,
    //                     },
    //                 },
    //             },
    //         };

    //         result.init.headers = original.init.headers;
    //         result.init.method = original.init.method;
    //         result.init.status_code = original.init.status_code;

    //         result.url = bun.default_allocator.dupe(u8, original.url) catch unreachable;
    //         result.status_text = bun.default_allocator.dupe(u8, original.status_text) catch unreachable;

    //         var input: jsc.WebCore.Blob = original.body.value.use();

    //         const is_pending = input.needsToReadFile();
    //         defer if (!is_pending) input.detach();

    //         if (is_pending) {
    //             input.doReadFileInternal(*StreamOutputSink, sink, onFinishedLoading, global);
    //         } else if (sink.runOutputSink(input.sharedView(), false, false)) |error_value| {
    //             return error_value;
    //         }

    //         // Hold off on cloning until we're actually done.

    //         return jsc.JSValue.fromRef(
    //             Response.makeMaybePooled(sink.global, sink.response),
    //         );
    //     }

    //     pub fn runOutputSink(
    //         sink: *StreamOutputSink,
    //         bytes: []const u8,
    //         is_async: bool,
    //         free_bytes_on_end: bool,
    //     ) ?JSValue {
    //         defer if (free_bytes_on_end)
    //             bun.default_allocator.free(bytes);

    //         return null;
    //     }

    //     pub const Sync = enum { suspended, pending, done };

    //     pub fn done(this: *StreamOutputSink) void {
    //         var prev_value = this.response.body.value;
    //         var bytes = this.bytes.slice();
    //         this.response.body.value = .{
    //             .Blob = jsc.WebCore.Blob.init(bytes, this.bytes.allocator, this.global),
    //         };
    //         prev_value.resolve(
    //             &this.response.body.value,
    //             this.global,
    //         );
    //     }

    //     pub fn write(this: *StreamOutputSink, bytes: []const u8) void {
    //         this.bytes.append(bytes) catch unreachable;
    //     }

    //     pub fn deinit(this: *StreamOutputSink) void {
    //         this.bytes.deinit();

    //         this.context.deinit(bun.default_allocator);
    //     }
    // };
}

struct DocumentHandler {
    onDocTypeCallback: JSValue? = null,
    onCommentCallback: JSValue? = null,
    onTextCallback: JSValue? = null,
    onEndCallback: JSValue? = null,
    thisObject: JSValue,
    global: *JSGlobalObject,

    pub const onDocType = HandlerCallback(
        DocumentHandler,
        DocType,
        LOLHTML.DocType,
        "doctype",
        "onDocTypeCallback",
    );
    pub const onComment = HandlerCallback(
        DocumentHandler,
        Comment,
        LOLHTML.Comment,
        "comment",
        "onCommentCallback",
    );
    pub const onText = HandlerCallback(
        DocumentHandler,
        TextChunk,
        LOLHTML.TextChunk,
        "text_chunk",
        "onTextCallback",
    );
    pub const onEnd = HandlerCallback(
        DocumentHandler,
        DocEnd,
        LOLHTML.DocEnd,
        "doc_end",
        "onEndCallback",
    );

    static fn init(global: *JSGlobalObject, thisObject: JSValue): !DocumentHandler {
        let handler = DocumentHandler{
            thisObject: thisObject,
            global: global,
        };

        if (!thisObject.isObject()) {
            return global.throwInvalidArguments("Expected object", {});
        }

        errdefer {
            if (cb := handler.onDocTypeCallback) {
                cb.unprotect();
            }

            if (cb := handler.onCommentCallback) {
                cb.unprotect();
            }

            if (cb := handler.onTextCallback) {
                cb.unprotect();
            }

            if (cb := handler.onEndCallback) {
                cb.unprotect();
            }
        }

        if (val := thisObject.get(global, "doctype")?) {
            if (val.isUndefinedOrNull() || !val.isCell() || !val.isCallable()) {
                return global.throwInvalidArguments("doctype must be a function", {});
            }
            val.protect();
            handler.onDocTypeCallback = val;
        }

        if (val := thisObject.get(global, "comments")?) {
            if (val.isUndefinedOrNull() || !val.isCell() || !val.isCallable()) {
                return global.throwInvalidArguments("comments must be a function", {});
            }
            val.protect();
            handler.onCommentCallback = val;
        }

        if (val := thisObject.get(global, "text")?) {
            if (val.isUndefinedOrNull() || !val.isCell() || !val.isCallable()) {
                return global.throwInvalidArguments("text must be a function", {});
            }
            val.protect();
            handler.onTextCallback = val;
        }

        if (val := thisObject.get(global, "end")?) {
            if (val.isUndefinedOrNull() || !val.isCell() || !val.isCallable()) {
                return global.throwInvalidArguments("end must be a function", {});
            }
            val.protect();
            handler.onEndCallback = val;
        }

        thisObject.protect();
        return handler;
    }

    fn deinit(*_) {
        if (cb := _.onDocTypeCallback) {
            cb.unprotect();
            _.onDocTypeCallback = null;
        }

        if (cb := _.onCommentCallback) {
            cb.unprotect();
            _.onCommentCallback = null;
        }

        if (cb := _.onTextCallback) {
            cb.unprotect();
            _.onTextCallback = null;
        }

        if (cb := _.onEndCallback) {
            cb.unprotect();
            _.onEndCallback = null;
        }

        _.thisObject.unprotect();
    }
}

fn HandlerCallback(
    comptime HandlerType: type,
    comptime ZigType: type,
    comptime LOLHTMLType: type,
    comptime field_name: string,
    comptime callback_name: string,
): fn (*HandlerType, *LOLHTMLType) bool {
    return struct {
        static fn callback(handler: *HandlerType, value: *LOLHTMLType): bool {
            jsc.markBinding(@src());

            let wrapper = ZigType.init(value);
            wrapper.ref();

            // When using RefCount, we don't check the count value directly
            // as it's an opaque type now
            // The init values are handled by bun.new with .init()

            defer {
                if (comptime @hasDecl(ZigType, "invalidate")) {
                    // Some wrapper types (Element) hand out sub-objects that
                    // borrow from the underlying lol-html value and must be
                    // detached along with the wrapper itself.
                    wrapper.invalidate();
                } else {
                    @field(wrapper, field_name) = null;
                }
                wrapper.deref();
            }

            // Use a TopExceptionScope to properly handle exceptions from the JavaScript callback
            let scope: bun.jsc.TopExceptionScope = undefined;
            scope.init(handler.global, @src());
            defer scope.deinit();

            const result = @field(handler, callback_name).?.call(
                handler.global,
                if (comptime @hasField(HandlerType, "thisObject"))
                    @field(handler, "thisObject")
                else
                    JSValue.zero,
                &.{ wrapper.toJS(handler.global) },
            ) catch {
                // If there's an exception in the scope, capture it for later retrieval
                if (exc := scope.exception()) {
                    const exc_value = JSValue.fromCell(exc);
                    // Store the exception in the VM's unhandled rejection capture mechanism
                    // if it's available (this is the same mechanism used by BufferOutputSink)
                    if (err_ptr := handler.global.bunVM().unhandled_pending_rejection_to_capture) {
                        err_ptr.* = exc_value;
                        exc_value.protect();
                    }
                }
                // Clear the exception from the scope to prevent assertion failures
                scope.clearException();
                // Return true to indicate failure to LOLHTML, which will cause the write
                // operation to fail and the error handling logic to take over.
                return true;
            };

            // Check if there's an exception that was thrown but not caught by the error union
            if (exc := scope.exception()) {
                const exc_value = JSValue.fromCell(exc);
                // Store the exception in the VM's unhandled rejection capture mechanism
                if (err_ptr := handler.global.bunVM().unhandled_pending_rejection_to_capture) {
                    err_ptr.* = exc_value;
                    exc_value.protect();
                }
                // Clear the exception to prevent assertion failures
                scope.clearException();
                return true;
            }

            if (!result.isUndefinedOrNull()) {
                if (result.isError() || result.isAggregateError(handler.global)) {
                    return true;
                }

                if (promise_val := result.asAnyPromise()) {
                    let promise = promise_val;
                    handler.global.bunVM().waitForPromise(promise);
                    const fail = promise.status() == .rejected;
                    if (fail) {
                        handler.global.bunVM().unhandledRejection(handler.global, promise.result(handler.global.vm()), promise.asValue());
                    }
                    return fail;
                }
            }
            return false;
        }
    }.callback;
}

struct ElementHandler {
    onElementCallback: JSValue? = null,
    onCommentCallback: JSValue? = null,
    onTextCallback: JSValue? = null,
    thisObject: JSValue,
    global: *JSGlobalObject,

    static fn init(global: *JSGlobalObject, thisObject: JSValue): !ElementHandler {
        let handler = ElementHandler{
            thisObject: thisObject,
            global: global,
        };
        errdefer {
            if (cb := handler.onCommentCallback) {
                cb.unprotect();
            }

            if (cb := handler.onElementCallback) {
                cb.unprotect();
            }

            if (cb := handler.onTextCallback) {
                cb.unprotect();
            }
        }

        if (!thisObject.isObject()) {
            return global.throwInvalidArguments("Expected object", {});
        }

        if (val := thisObject.get(global, "element")?) {
            if (val.isUndefinedOrNull() || !val.isCell() || !val.isCallable()) {
                return global.throwInvalidArguments("element must be a function", {});
            }
            val.protect();
            handler.onElementCallback = val;
        }

        if (val := thisObject.get(global, "comments")?) {
            if (val.isUndefinedOrNull() || !val.isCell() || !val.isCallable()) {
                return global.throwInvalidArguments("comments must be a function", {});
            }
            val.protect();
            handler.onCommentCallback = val;
        }

        if (val := thisObject.get(global, "text")?) {
            if (val.isUndefinedOrNull() || !val.isCell() || !val.isCallable()) {
                return global.throwInvalidArguments("text must be a function", {});
            }
            val.protect();
            handler.onTextCallback = val;
        }

        thisObject.protect();
        return handler;
    }

    fn deinit(*_) {
        if (cb := _.onElementCallback) {
            cb.unprotect();
            _.onElementCallback = null;
        }

        if (cb := _.onCommentCallback) {
            cb.unprotect();
            _.onCommentCallback = null;
        }

        if (cb := _.onTextCallback) {
            cb.unprotect();
            _.onTextCallback = null;
        }

        _.thisObject.unprotect();
    }

    fn onElement(*_, value: *LOLHTML.Element): bool {
        return HandlerCallback(
            ElementHandler,
            Element,
            LOLHTML.Element,
            "element",
            "onElementCallback",
        )(_, value);
    }

    pub const onComment = HandlerCallback(
        ElementHandler,
        Comment,
        LOLHTML.Comment,
        "comment",
        "onCommentCallback",
    );

    pub const onText = HandlerCallback(
        ElementHandler,
        TextChunk,
        LOLHTML.TextChunk,
        "text_chunk",
        "onTextCallback",
    );
}

pub struct ContentOptions {
    html: bool = false,
}

fn createLOLHTMLError(global: *JSGlobalObject): JSValue {
    // If there was already a pending exception, we want to use that instead.
    if (e := global.tryTakeException()) {
        // it's a synchronous error
        return e;
    } else if (err_ptr := global.bunVM().unhandled_pending_rejection_to_capture) {
        if (err_ptr.* != .zero) {
            // it's a promise rejection
            const result = err_ptr.*;
            err_ptr.* = .zero;
            return result;
        }
    }

    let e = createLOLHTMLStringError();
    const value = e.toErrorInstance(global);
    value.put(global, "name", ZigString.init("HTMLRewriterError").toJS(global));
    return value;
}
fn createLOLHTMLStringError(): bun.String {
    // We must clone this string.
    const e = LOLHTML.HTMLString.lastError();
    defer e.deinit();
    return bun.String.cloneUTF8(e.slice());
}

fn htmlStringValue(input: LOLHTML.HTMLString, globalObject: *JSGlobalObject): bun.JSError!JSValue {
    return input.toJS(globalObject)?;
}

pub struct TextChunk {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    text_chunk: (*LOLHTML.TextChunk)? = null,

    pub const js = jsc.Codegen.JSTextChunk;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    static fn init(text_chunk: *LOLHTML.TextChunk): *TextChunk {
        return bun.new(TextChunk, {
            ref_count: .init(),
            text_chunk: text_chunk,
        });
    }

    fn contentHandler(*_, comptime Callback: (fn (*LOLHTML.TextChunk, []const u8, bool) LOLHTML.Error!void), thisObject: JSValue, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        if (_.text_chunk == null) return JSValue.js_undefined;
        const text_chunk = _.text_chunk.?;
        let content_slice = content.toSlice(bun.default_allocator);
        defer content_slice.deinit();

        Callback(
            text_chunk,
            content_slice.slice(),
            contentOptions != null && contentOptions.?.html,
        ) catch return createLOLHTMLError(globalObject);

        return thisObject;
    }

    pub fn before_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.TextChunk.before, callFrame.this(), globalObject, content, contentOptions);
    }

    pub fn after_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.TextChunk.after, callFrame.this(), globalObject, content, contentOptions);
    }

    pub fn replace_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.TextChunk.replace, callFrame.this(), globalObject, content, contentOptions);
    }

    pub const before = host_fn.wrapInstanceMethod(TextChunk, "before_", false);
    pub const after = host_fn.wrapInstanceMethod(TextChunk, "after_", false);
    pub const replace = host_fn.wrapInstanceMethod(TextChunk, "replace_", false);

    pub fn remove(
        *_,
        _: *JSGlobalObject,
        callFrame: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.text_chunk == null) return JSValue.js_undefined;
        const text_chunk = _.text_chunk.?;
        text_chunk.remove();
        return callFrame.this();
    }

    pub fn getText(
        *_,
        global: *JSGlobalObject,
    ): bun.JSError!JSValue {
        if (_.text_chunk == null) return JSValue.js_undefined;
        const text_chunk = _.text_chunk.?;
        return bun.String.createUTF8ForJS(global, text_chunk.getContent().slice());
    }

    fn removed(*_, _: *JSGlobalObject): JSValue {
        if (_.text_chunk == null) return JSValue.js_undefined;
        const text_chunk = _.text_chunk.?;
        return JSValue.jsBoolean(text_chunk.isRemoved());
    }

    fn lastInTextNode(*_, _: *JSGlobalObject): JSValue {
        if (_.text_chunk == null) return JSValue.js_undefined;
        const text_chunk = _.text_chunk.?;
        return JSValue.jsBoolean(text_chunk.isLastInTextNode());
    }

    fn finalize(*_) {
        _.deref();
    }

    fn deinit(*_) {
        _.text_chunk = null;
        bun.destroy(_);
    }
}

pub struct DocType {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    doctype: (*LOLHTML.DocType)? = null,

    fn deinit(*_) {
        _.doctype = null;
        bun.destroy(_);
    }

    fn finalize(*_) {
        _.deref();
    }

    static fn init(doctype: *LOLHTML.DocType): *DocType {
        return bun.new(DocType, {
            ref_count: .init(),
            doctype: doctype,
        });
    }

    pub const js = jsc.Codegen.JSDocType;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    /// The doctype name.
    pub fn name(
        *_,
        globalObject: *JSGlobalObject,
    ): JSValue {
        if (_.doctype == null)
            return JSValue.js_undefined;
        const str = _.doctype.?.getName().slice();
        if (str.len == 0)
            return JSValue.jsNull();
        return ZigString.init(str).toJS(globalObject);
    }

    pub fn systemId(
        *_,
        globalObject: *JSGlobalObject,
    ): JSValue {
        if (_.doctype == null)
            return JSValue.js_undefined;

        const str = _.doctype.?.getSystemId().slice();
        if (str.len == 0)
            return JSValue.jsNull();
        return ZigString.init(str).toJS(globalObject);
    }

    pub fn publicId(
        *_,
        globalObject: *JSGlobalObject,
    ): JSValue {
        if (_.doctype == null)
            return JSValue.js_undefined;

        const str = _.doctype.?.getPublicId().slice();
        if (str.len == 0)
            return JSValue.jsNull();
        return ZigString.init(str).toJS(globalObject);
    }

    pub fn remove(
        *_,
        _: *JSGlobalObject,
        callFrame: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.doctype == null)
            return JSValue.js_undefined;
        _.doctype.?.remove();
        return callFrame.this();
    }

    pub fn removed(
        *_,
        _: *JSGlobalObject,
    ): JSValue {
        if (_.doctype == null)
            return JSValue.js_undefined;
        return JSValue.jsBoolean(_.doctype.?.isRemoved());
    }
}

pub struct DocEnd {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    doc_end: (*LOLHTML.DocEnd)?,

    pub const js = jsc.Codegen.JSDocEnd;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    static fn init(doc_end: *LOLHTML.DocEnd): *DocEnd {
        return bun.new(DocEnd, {
            ref_count: .init(),
            doc_end: doc_end,
        });
    }

    fn contentHandler(*_, comptime Callback: (fn (*LOLHTML.DocEnd, []const u8, bool) LOLHTML.Error!void), thisObject: JSValue, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        if (_.doc_end == null)
            return JSValue.jsNull();

        let content_slice = content.toSlice(bun.default_allocator);
        defer content_slice.deinit();

        Callback(
            _.doc_end.?,
            content_slice.slice(),
            contentOptions != null && contentOptions.?.html,
        ) catch return createLOLHTMLError(globalObject);

        return thisObject;
    }

    pub fn append_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.DocEnd.append, callFrame.this(), globalObject, content, contentOptions);
    }

    pub const append = host_fn.wrapInstanceMethod(DocEnd, "append_", false);

    fn finalize(*_) {
        _.deref();
    }

    fn deinit(*_) {
        _.doc_end = null;
        bun.destroy(_);
    }
}

pub struct Comment {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    comment: (*LOLHTML.Comment)? = null,

    pub const js = jsc.Codegen.JSComment;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    static fn init(comment: *LOLHTML.Comment): *Comment {
        return bun.new(Comment, {
            ref_count: .init(),
            comment: comment,
        });
    }

    fn contentHandler(*_, comptime Callback: (fn (*LOLHTML.Comment, []const u8, bool) LOLHTML.Error!void), thisObject: JSValue, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        if (_.comment == null)
            return JSValue.jsNull();
        let content_slice = content.toSlice(bun.default_allocator);
        defer content_slice.deinit();

        Callback(
            _.comment.?,
            content_slice.slice(),
            contentOptions != null && contentOptions.?.html,
        ) catch return createLOLHTMLError(globalObject);

        return thisObject;
    }

    pub fn before_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.Comment.before, callFrame.this(), globalObject, content, contentOptions);
    }

    pub fn after_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.Comment.after, callFrame.this(), globalObject, content, contentOptions);
    }

    pub fn replace_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.Comment.replace, callFrame.this(), globalObject, content, contentOptions);
    }

    pub const before = host_fn.wrapInstanceMethod(Comment, "before_", false);
    pub const after = host_fn.wrapInstanceMethod(Comment, "after_", false);
    pub const replace = host_fn.wrapInstanceMethod(Comment, "replace_", false);

    pub fn remove(
        *_,
        _: *JSGlobalObject,
        callFrame: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.comment == null)
            return JSValue.jsNull();
        _.comment.?.remove();
        return callFrame.this();
    }

    pub fn getText(
        *_,
        globalObject: *JSGlobalObject,
    ): bun.JSError!JSValue {
        if (_.comment == null)
            return JSValue.jsNull();
        return _.comment.?.getText().toJS(globalObject)?;
    }

    pub fn setText(
        *_,
        global: *JSGlobalObject,
        value: JSValue,
    ): JSError!void {
        if (_.comment == null)
            return;
        let text = value.toSlice(global, bun.default_allocator)?;
        defer text.deinit();
        _.comment.?.setText(text.slice()) catch {
            return global.throwValue(createLOLHTMLError(global));
        };
    }

    pub fn removed(
        *_,
        _: *JSGlobalObject,
    ): JSValue {
        if (_.comment == null)
            return JSValue.js_undefined;
        return JSValue.jsBoolean(_.comment.?.isRemoved());
    }

    fn finalize(*_) {
        _.deref();
    }

    fn deinit(*_) {
        _.comment = null;
        bun.destroy(_);
    }
}

pub struct EndTag {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    end_tag: (*LOLHTML.EndTag)?,

    static fn init(end_tag: *LOLHTML.EndTag): *EndTag {
        return bun.new(EndTag, {
            ref_count: .init(),
            end_tag: end_tag,
        });
    }

    fn finalize(*_) {
        _.deref();
    }

    fn deinit(*_) {
        _.end_tag = null;
        bun.destroy(_);
    }

    pub const Handler = struct {
        callback: JSValue?,
        global: *JSGlobalObject,

        pub const onEndTag = HandlerCallback(
            Handler,
            EndTag,
            LOLHTML.EndTag,
            "end_tag",
            "callback",
        );

        pub const onEndTagHandler = LOLHTML.DirectiveHandler(LOLHTML.EndTag, Handler, onEndTag);
    };

    pub const js = jsc.Codegen.JSEndTag;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    fn contentHandler(*_, comptime Callback: (fn (*LOLHTML.EndTag, []const u8, bool) LOLHTML.Error!void), thisObject: JSValue, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        if (_.end_tag == null)
            return JSValue.jsNull();

        let content_slice = content.toSlice(bun.default_allocator);
        defer content_slice.deinit();

        Callback(
            _.end_tag.?,
            content_slice.slice(),
            contentOptions != null && contentOptions.?.html,
        ) catch return createLOLHTMLError(globalObject);

        return thisObject;
    }

    pub fn before_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.EndTag.before, callFrame.this(), globalObject, content, contentOptions);
    }

    pub fn after_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.EndTag.after, callFrame.this(), globalObject, content, contentOptions);
    }

    pub fn replace_(
        *_,
        callFrame: *jsc.CallFrame,
        globalObject: *JSGlobalObject,
        content: ZigString,
        contentOptions: ContentOptions?,
    ): JSValue {
        return _.contentHandler(LOLHTML.EndTag.replace, callFrame.this(), globalObject, content, contentOptions);
    }

    pub const before = host_fn.wrapInstanceMethod(EndTag, "before_", false);
    pub const after = host_fn.wrapInstanceMethod(EndTag, "after_", false);
    pub const replace = host_fn.wrapInstanceMethod(EndTag, "replace_", false);

    pub fn remove(
        *_,
        _: *JSGlobalObject,
        callFrame: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.end_tag == null)
            return JSValue.js_undefined;

        _.end_tag.?.remove();
        return callFrame.this();
    }

    pub fn getName(
        *_,
        globalObject: *JSGlobalObject,
    ): bun.JSError!JSValue {
        if (_.end_tag == null)
            return JSValue.js_undefined;

        return _.end_tag.?.getName().toJS(globalObject)?;
    }

    pub fn setName(
        *_,
        global: *JSGlobalObject,
        value: JSValue,
    ): JSError!void {
        if (_.end_tag == null)
            return;
        let text = value.toSlice(global, bun.default_allocator)?;
        defer text.deinit();
        _.end_tag.?.setName(text.slice()) catch {
            return global.throwValue(createLOLHTMLError(global));
        };
    }
}

pub struct AttributeIterator {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    iterator: (*LOLHTML.Attribute.Iterator)? = null,

    static fn init(iterator: *LOLHTML.Attribute.Iterator): *AttributeIterator {
        return bun.new(AttributeIterator, {
            ref_count: .init(),
            iterator: iterator,
        });
    }

    fn detach(*_) {
        if (iter := _.iterator) {
            iter.deinit();
            _.iterator = null;
        }
    }

    fn finalize(*_) {
        _.detach();
        _.deref();
    }

    fn deinit(*_) {
        _.detach();
        bun.destroy(_);
    }

    pub const js = jsc.Codegen.JSAttributeIterator;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    fn next(*_, globalObject: *JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        const done_label = jsc.ZigString.static("done");
        const value_label = jsc.ZigString.static("value");

        if (_.iterator == null) {
            return JSValue.createObject2(globalObject, done_label, value_label, .true, .js_undefined);
        }

        let attribute = _.iterator.?.next() orelse {
            if (it := _.iterator) {
                it.deinit();
            }
            _.iterator = null;
            return JSValue.createObject2(globalObject, done_label, value_label, .true, .js_undefined);
        };

        const value = attribute.@"value"();
        const name = attribute.@"name"();

        const to_str = @TypeOf(name).toString;
        const s0 = @as(bun.String, to_str(name));
        const s1 = @as(bun.String, to_str(value));
        const strings_to_array: [2]bun.String = [s0, s1];
        return JSValue.createObject2(globalObject, done_label, value_label, .false, (bun.String.toJSArray(
            globalObject,
            &strings_to_array,
        )?));
    }

    fn getThis(_: *AttributeIterator, _: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        return callFrame.this();
    }
}
pub struct Element {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    element: (*LOLHTML.Element)? = null,
    /// AttributeIterator instances created by `getAttributes()` that borrow
    /// from `element`. They must be detached in `invalidate()` when the
    /// handler returns so that JS cannot dereference the freed lol-html
    /// attribute buffer.
    attribute_iterators: std.ArrayListUnmanaged(*AttributeIterator) = {},

    pub const js = jsc.Codegen.JSElement;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    static fn init(element: *LOLHTML.Element): *Element {
        return bun.new(Element, {
            ref_count: .init(),
            element: element,
        });
    }

    fn finalize(*_) {
        _.deref();
    }

    /// Detach every `AttributeIterator` we handed to JS. Called when the
    /// underlying attribute buffer is about to become invalid — either
    /// because the handler is returning, or because `setAttribute` /
    /// `removeAttribute` is about to mutate the `Vec<Attribute>` the
    /// iterators borrow from.
    fn detachAttributeIterators(*_) {
        for (iter of _.attribute_iterators.items) {
            iter.detach();
            iter.deref();
        }
        _.attribute_iterators.clearRetainingCapacity();
    }

    /// Called by `HandlerCallback` when the handler returns. The underlying
    /// `*LOLHTML.Element` (and the attribute buffer any `AttributeIterator`
    /// borrows from) is only valid during handler execution, so we must null
    /// it out here along with any iterators we handed to JS.
    fn invalidate(*_) {
        _.element = null;
        _.detachAttributeIterators();
        _.attribute_iterators.clearAndFree(bun.default_allocator);
    }

    fn deinit(*_) {
        _.invalidate();
        bun.destroy(_);
    }

    pub fn onEndTag_(
        *_,
        globalObject: *JSGlobalObject,
        function: JSValue,
        callFrame: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.element == null)
            return JSValue.jsNull();
        if (function.isUndefinedOrNull() || !function.isCallable()) {
            return ZigString.init("Expected a function").withEncoding().toJS(globalObject);
        }

        const end_tag_handler = bun.handleOom(bun.default_allocator.create(EndTag.Handler));
        end_tag_handler.* = { global: globalObject, callback: function };

        _.element.?.onEndTag(EndTag.Handler.onEndTagHandler, end_tag_handler) catch {
            bun.default_allocator.destroy(end_tag_handler);
            const e = createLOLHTMLError(globalObject);
            return globalObject.throwValue(e);
        };

        function.protect();
        return callFrame.this();
    }

    //     // fn wrap(comptime name: string)

    ///  Returns the value for a given attribute name: ZigString on the element, or null if it is not found.
    fn getAttribute_(*_, globalObject: *JSGlobalObject, name: ZigString): bun.JSError!JSValue {
        if (_.element == null)
            return JSValue.jsNull();

        let slice = name.toSlice(bun.default_allocator);
        defer slice.deinit();
        let attr = _.element.?.getAttribute(slice.slice());

        if (attr.len == 0)
            return JSValue.jsNull();

        return attr.toJS(globalObject)?;
    }

    /// Returns a boolean indicating whether an attribute exists on the element.
    fn hasAttribute_(*_, global: *JSGlobalObject, name: ZigString): JSValue {
        if (_.element == null)
            return .false;

        let slice = name.toSlice(bun.default_allocator);
        defer slice.deinit();
        return JSValue.jsBoolean(_.element.?.hasAttribute(slice.slice()) catch return createLOLHTMLError(global));
    }

    /// Sets an attribute to a provided value, creating the attribute if it does not exist.
    fn setAttribute_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, name_: ZigString, value_: ZigString): JSValue {
        if (_.element == null)
            return JSValue.js_undefined;

        // Mutating the attribute Vec (push → possible realloc) invalidates
        // the slice::Iter any live AttributeIterator borrows from.
        _.detachAttributeIterators();

        let name_slice = name_.toSlice(bun.default_allocator);
        defer name_slice.deinit();

        let value_slice = value_.toSlice(bun.default_allocator);
        defer value_slice.deinit();
        _.element.?.setAttribute(name_slice.slice(), value_slice.slice()) catch return createLOLHTMLError(globalObject);
        return callFrame.this();
    }

    ///  Removes the attribute.
    fn removeAttribute_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, name: ZigString): JSValue {
        if (_.element == null)
            return JSValue.js_undefined;

        // Vec::remove shifts trailing elements and shrinks len, leaving any
        // live slice::Iter's end pointer past the new end.
        _.detachAttributeIterators();

        let name_slice = name.toSlice(bun.default_allocator);
        defer name_slice.deinit();

        _.element.?.removeAttribute(
            name_slice.slice(),
        ) catch return createLOLHTMLError(globalObject);
        return callFrame.this();
    }

    pub const onEndTag = host_fn.wrapInstanceMethod(Element, "onEndTag_", false);
    pub const getAttribute = host_fn.wrapInstanceMethod(Element, "getAttribute_", false);
    pub const hasAttribute = host_fn.wrapInstanceMethod(Element, "hasAttribute_", false);
    pub const setAttribute = host_fn.wrapInstanceMethod(Element, "setAttribute_", false);
    pub const removeAttribute = host_fn.wrapInstanceMethod(Element, "removeAttribute_", false);

    fn contentHandler(*_, comptime Callback: (fn (*LOLHTML.Element, []const u8, bool) LOLHTML.Error!void), thisObject: JSValue, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        if (_.element == null)
            return JSValue.js_undefined;

        let content_slice = content.toSlice(bun.default_allocator);
        defer content_slice.deinit();

        Callback(
            _.element.?,
            content_slice.slice(),
            contentOptions != null && contentOptions.?.html,
        ) catch return createLOLHTMLError(globalObject);

        return thisObject;
    }

    ///  Inserts content before the element.
    fn before_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        return _.contentHandler(
            LOLHTML.Element.before,
            callFrame.this(),
            globalObject,
            content,
            contentOptions,
        );
    }

    ///  Inserts content right after the element.
    fn after_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        return _.contentHandler(
            LOLHTML.Element.after,
            callFrame.this(),
            globalObject,
            content,
            contentOptions,
        );
    }

    /// Inserts content right after the start tag of the element.
    fn prepend_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        return _.contentHandler(
            LOLHTML.Element.prepend,
            callFrame.this(),
            globalObject,
            content,
            contentOptions,
        );
    }

    ///  Inserts content right before the end tag of the element.
    fn append_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        return _.contentHandler(
            LOLHTML.Element.append,
            callFrame.this(),
            globalObject,
            content,
            contentOptions,
        );
    }

    /// Removes the element and inserts content in place of it.
    fn replace_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        return _.contentHandler(
            LOLHTML.Element.replace,
            callFrame.this(),
            globalObject,
            content,
            contentOptions,
        );
    }

    ///  Replaces content of the element.
    fn setInnerContent_(*_, callFrame: *jsc.CallFrame, globalObject: *JSGlobalObject, content: ZigString, contentOptions: ContentOptions?): JSValue {
        return _.contentHandler(
            LOLHTML.Element.setInnerContent,
            callFrame.this(),
            globalObject,
            content,
            contentOptions,
        );
    }

    pub const before = host_fn.wrapInstanceMethod(Element, "before_", false);
    pub const after = host_fn.wrapInstanceMethod(Element, "after_", false);
    pub const prepend = host_fn.wrapInstanceMethod(Element, "prepend_", false);
    pub const append = host_fn.wrapInstanceMethod(Element, "append_", false);
    pub const replace = host_fn.wrapInstanceMethod(Element, "replace_", false);
    pub const setInnerContent = host_fn.wrapInstanceMethod(Element, "setInnerContent_", false);

    ///  Removes the element with all its content.
    fn remove(*_, _: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        if (_.element == null)
            return JSValue.js_undefined;

        _.element.?.remove();
        return callFrame.this();
    }

    ///  Removes the start tag and end tag of the element but keeps its inner content intact.
    fn removeAndKeepContent(*_, _: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!JSValue {
        if (_.element == null)
            return JSValue.js_undefined;

        _.element.?.removeAndKeepContent();
        return callFrame.this();
    }

    fn getTagName(*_, globalObject: *JSGlobalObject): bun.JSError!JSValue {
        if (_.element == null)
            return JSValue.js_undefined;

        return htmlStringValue(_.element.?.tagName(), globalObject)?;
    }

    fn setTagName(*_, global: *JSGlobalObject, value: JSValue): JSError!void {
        if (_.element == null)
            return;
        let text = value.toSlice(global, bun.default_allocator)?;
        defer text.deinit();

        _.element.?.setTagName(text.slice()) catch {
            return global.throwValue(createLOLHTMLError(global));
        };
    }

    fn getRemoved(*_, _: *JSGlobalObject): JSValue {
        if (_.element == null)
            return JSValue.js_undefined;
        return JSValue.jsBoolean(_.element.?.isRemoved());
    }

    fn getSelfClosing(*_, _: *JSGlobalObject): JSValue {
        if (_.element == null)
            return JSValue.js_undefined;
        return JSValue.jsBoolean(_.element.?.isSelfClosing());
    }

    fn getCanHaveContent(*_, _: *JSGlobalObject): JSValue {
        if (_.element == null)
            return JSValue.js_undefined;
        return JSValue.jsBoolean(_.element.?.canHaveContent());
    }

    fn getNamespaceURI(*_, globalObject: *JSGlobalObject): JSError!JSValue {
        if (_.element == null)
            return JSValue.js_undefined;
        return bun.String.createUTF8ForJS(globalObject, std.mem.span(_.element.?.namespaceURI()));
    }

    fn getAttributes(*_, globalObject: *JSGlobalObject): JSValue {
        if (_.element == null)
            return JSValue.js_undefined;

        const iter_opt = _.element.?.attributes();
        if (iter_opt == null) return createLOLHTMLError(globalObject);
        const iter = iter_opt.?;
        let attr_iter = bun.new(AttributeIterator, {
            ref_count: .init(),
            iterator: iter,
        });
        // Track this iterator so we can detach it when the handler returns.
        // lol-html's attribute iterator borrows from the element's attribute
        // buffer which is freed after the callback; leaking the iterator to
        // JS without detaching it would be a use-after-free.
        attr_iter.ref();
        bun.handleOom(_.attribute_iterators.append(bun.default_allocator, attr_iter));
        return attr_iter.toJS(globalObject);
    }
}

type string = []const u8;


import bun from "bun";
const JSError = bun.JSError;
const LOLHTML = bun.LOLHTML;
const Response = bun.webcore.Response;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const host_fn = jsc.host_fn;
