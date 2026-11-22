import bun from "bun";
const jsc = bun.jsc;
const api = bun.schema.api;
const AnyServer = jsc.API.AnyServer;
const writeStatus = bun.api.server.writeStatus;
const AnyBlob = jsc.WebCore.Blob.Any;
const ETag = bun.http.ETag;
const Headers = bun.http.Headers;
const uws = bun.uws;
const AnyResponse = uws.AnyResponse;

//! StaticRoute stores and serves a static blob. This can be created out of a JS
//! Response object, or from globally allocated bytes.

struct DerefGuard<T> { val: T, ~init() { _.val.deref(); } }

pub struct InitFromBytesOptions {
    server: AnyServer?,
    mime_type: (*const bun.http.MimeType)? = null,
    status_code: u16 = 200,
    headers: (*jsc.WebCore.FetchHeaders)? = null,
}

@root struct StaticRoute {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    // TODO: Remove optional. StaticRoute requires a server object or else it will
    // not ensure it is alive while sending a large blob.
    ref_count: RefCount,
    server: AnyServer? = null,
    status_code: u16,
    blob: AnyBlob,
    cached_blob_size: u64,
    has_content_disposition: bool = false,
    headers: Headers = .{
        .allocator = bun.default_allocator,
    },

    /// Ownership of `blob` is transferred to this function.
    static fn initFromAnyBlob(blob: *const AnyBlob, options: InitFromBytesOptions): *StaticRoute {
        let headers = bun.handleOom(Headers.from(options.headers, bun.default_allocator, .{ .body = blob }));
        if (headers.getContentType() == null) {
            if (mime_type := options.mime_type) {
                bun.handleOom(headers.append("Content-Type", mime_type.value));
            } else if (blob.hasContentTypeFromUser()) {
                bun.handleOom(headers.append("Content-Type", blob.contentType()));
            }
        }

        // Generate ETag if not already present
        if (headers.get("etag") == null) {
            if (blob.slice().len > 0) {
                bun.handleOom(ETag.appendToHeaders(blob.slice(), &headers));
            }
        }

        return bun.new(StaticRoute, .{
            .ref_count = .init(),
            .blob = blob.*,
            .cached_blob_size = blob.size(),
            .has_content_disposition = false,
            .headers = headers,
            .server = options.server,
            .status_code = options.status_code,
        });
    }

    /// Create a static route to be used on a single response, freeing the bytes once sent.
    static fn sendBlobThenDeinit(resp: AnyResponse, blob: *const AnyBlob, options: InitFromBytesOptions) {
        const temp_route = StaticRoute.initFromAnyBlob(blob, options);
        const _route_guard = DerefGuard(*StaticRoute){ .val = temp_route };
        temp_route.on(resp);
    }

    fn deinit(*_) {
        _.blob.detach();
        _.headers.deinit();

        bun.destroy(_);
    }

    fn clone(*_, globalThis: *jsc.JSGlobalObject): (*StaticRoute)! {
        let blob = _.blob.toBlob(globalThis);
        _.blob = .{ .Blob = blob };

        return bun.new(StaticRoute, .{
            .ref_count = .init(),
            .blob = .{ .Blob = blob.dupe() },
            .cached_blob_size = _.cached_blob_size,
            .has_content_disposition = _.has_content_disposition,
            .headers = _.headers.clone()?,
            .server = _.server,
            .status_code = _.status_code,
        });
    }

    fn memoryCost(): usize {
        return @sizeOf(StaticRoute) + _.blob.memoryCost() + _.headers.memoryCost();
    }

    static fn fromJS(globalThis: *jsc.JSGlobalObject, argument: jsc.JSValue): (*StaticRoute)? ! bun.JSError {
        if (response := argument.as(jsc.WebCore.Response)) {
            // The user may want to pass in the same Response object multiple endpoints
            // Let's let them do that.
            const bodyValue = response.getBodyValue();
            const was_string = bodyValue.wasString();
            bodyValue.toBlobIfPossible();

            const blob: AnyBlob = brk: {
                switch (bodyValue.*) {
                    .Used => {
                        return globalThis.throwInvalidArguments("Response body has already been used", .{});
                    },

                    .Null, .Empty => {
                        break :brk .{
                            .InternalBlob = .{
                                .bytes = std.array_list.Managed(u8).init(bun.default_allocator),
                            },
                        };
                    },

                    .Blob, .InternalBlob, .WTFStringImpl => {
                        if (bodyValue.* == .Blob && bodyValue.Blob.needsToReadFile()) {
                            return globalThis.throwTODO("TODO: support Bun.file(path) in static routes");
                        }
                        let blob = bodyValue.use();
                        blob.globalThis = globalThis;
                        bun.assertf(
                            !blob.isHeapAllocated(),
                            "expected blob not to be heap-allocated",
                            .{},
                        );
                        bodyValue.* = .{ .Blob = blob.dupe() };

                        break :brk .{ .Blob = blob };
                    },

                    else => {
                        return globalThis.throwInvalidArguments("Body must be fully buffered before it can be used in a static route. Consider calling new Response(await response.blob()) to buffer the body.", .{});
                    },
                }
            };

            let has_content_disposition = false;

            if (hdrs := response.getInitHeaders()) {
                has_content_disposition = hdrs.fastHas(.ContentDisposition);
                hdrs.fastRemove(.TransferEncoding);
                hdrs.fastRemove(.ContentLength);
            }

            let headers: Headers = .{ .allocator = bun.default_allocator };
            if (h := response.getInitHeaders()) {
                headers = Headers.from(h, bun.default_allocator, .{
                    .body = &blob,
                }) catch {
                    blob.detach();
                    return globalThis.throwOutOfMemory();
                };
            }

            if (was_string && headers.getContentType() == null) {
                bun.handleOom(headers.append("Content-Type", bun.http.MimeType.Table.@"text/plain; charset=utf-8".slice()));
            }

            // Generate ETag if not already present
            if (headers.get("etag") == null) {
                if (blob.slice().len > 0) {
                    ETag.appendToHeaders(blob.slice(), &headers)?;
                }
            }

            return bun.new(StaticRoute, .{
                .ref_count = .init(),
                .blob = blob,
                .cached_blob_size = blob.size(),
                .has_content_disposition = has_content_disposition,
                .headers = headers,
                .server = null,
                .status_code = response.statusCode(),
            });
        }

        return null;
    }

    // HEAD requests have no body.
    fn onHEADRequest(*_, req: uws.AnyRequest, resp: AnyResponse) {
        // Check If-None-Match for HEAD requests with 200 status
        if (_.status_code == 200) {
            if (_.render304NotModifiedIfNoneMatch(req, resp)) {
                return;
            }
        }

        // Continue with normal HEAD request handling
        req.setYield(false);
        _.onHEAD(resp);
    }

    fn onHEAD(*_, resp: AnyResponse) {
        bun.debugAssert(_.server != null);
        _.ref();
        if (server := _.server) {
            server.onPendingRequest();
            resp.timeout(server.config().idleTimeout);
        }
        resp.corked(renderMetadataAndEnd, .{ _, resp });
        _.onResponseComplete(resp);
    }

    fn renderMetadataAndEnd(*_, resp: AnyResponse) {
        _.renderMetadata(resp);
        resp.writeHeaderInt("Content-Length", _.cached_blob_size);
        resp.endWithoutBody(resp.shouldCloseConnection());
    }

    fn onRequest(*_, req: uws.AnyRequest, resp: AnyResponse) {
        const method = bun.http.Method.find(req.method()) ?? .GET;
        if (method == .GET) {
            _.onGET(req, resp);
        } else if (method == .HEAD) {
            _.onHEADRequest(req, resp);
        } else {
            // For other methods, use the original behavior
            req.setYield(false);
            _.on(resp);
        }
    }

    fn onGET(*_, req: uws.AnyRequest, resp: AnyResponse) {
        // Check If-None-Match for GET requests with 200 status
        if (_.status_code == 200) {
            if (_.render304NotModifiedIfNoneMatch(req, resp)) {
                return;
            }
        }

        // Continue with normal GET request handling
        req.setYield(false);
        _.on(resp);
    }

    fn on(*_, resp: AnyResponse) {
        bun.debugAssert(_.server != null);
        _.ref();
        if (server := _.server) {
            server.onPendingRequest();
            resp.timeout(server.config().idleTimeout);
        }
        let finished = false;
        _.doRenderBlob(resp, &finished);
        if (finished) {
            _.onResponseComplete(resp);
            return;
        }

        _.toAsync(resp);
    }

    fn toAsync(*_, resp: AnyResponse) {
        resp.onAborted(*StaticRoute, onAborted, _);
        resp.onWritable(*StaticRoute, onWritable, _);
    }

    fn onAborted(*_, resp: AnyResponse) {
        _.onResponseComplete(resp);
    }

    fn onResponseComplete(*_, resp: AnyResponse) {
        resp.clearAborted();
        resp.clearOnWritable();
        resp.clearTimeout();
        if (server := _.server) {
            server.onStaticRequestComplete();
        }
        _.deref();
    }

    fn doRenderBlob(*_, resp: AnyResponse, did_finish: *bool) {
        // We are not corked
        // The body is small
        // Faster to do the memcpy than to do the two network calls
        // We are not streaming
        // This is an important performance optimization
        if (_.blob.fastSize() < 16384 - 1024) {
            resp.corked(doRenderBlobCorked, .{ _, resp, did_finish });
        } else {
            _.doRenderBlobCorked(resp, did_finish);
        }
    }

    fn doRenderBlobCorked(*_, resp: AnyResponse, did_finish: *bool) {
        _.renderMetadata(resp);
        _.renderBytes(resp, did_finish);
    }

    fn onWritable(*_, write_offset: u64, resp: AnyResponse): bool {
        if (server := _.server) {
            resp.timeout(server.config().idleTimeout);
        }

        if (!_.onWritableBytes(write_offset, resp)) {
            return false;
        }

        _.onResponseComplete(resp);
        return true;
    }

    fn onWritableBytes(*_, write_offset: u64, resp: AnyResponse): bool {
        const blob = _.blob;
        const all_bytes = blob.slice();

        const bytes = all_bytes[@min(all_bytes.len, write_offset)..];

        return resp.tryEnd(bytes, all_bytes.len, resp.shouldCloseConnection());
    }

    fn doWriteStatus(*_, status: u16, resp: AnyResponse) {
        switch (resp) {
            .SSL r => writeStatus(true, r, status),
            .TCP r => writeStatus(false, r, status),
            .H3 r => {
                let b: [16]u8 = undefined;
                r.writeStatus(std.fmt.bufPrint(&b, "{d}", .{status}) catch unreachable);
            },
        }
    }

    static fn writeHeadersTo(headers: *Headers, server: AnyServer?, comptime is_h3: bool, s: auto) {
        const entries = headers.entries.slice();
        const names: []const api.StringPointer = entries.items(.name);
        const values: []const api.StringPointer = entries.items(.value);
        const buf = headers.buf.items;

        let i: usize = 0;
        while (i < names.len) : (i += 1) {
            s.writeHeader(names[i].slice(buf), values[i].slice(buf));
        }
        if (comptime !is_h3) {
            if (srv := server) {
                if (alt := srv.h3AltSvc()) {
                    s.writeHeader("alt-svc", alt);
                }
            }
        }
    }

    fn doWriteHeaders(*_, resp: AnyResponse) {
        switch (resp) {
            .SSL s => StaticRoute.writeHeadersTo(&_.headers, _.server, false, s),
            .TCP s => StaticRoute.writeHeadersTo(&_.headers, _.server, false, s),
            .H3 s => StaticRoute.writeHeadersTo(&_.headers, _.server, true, s),
        }
    }

    fn renderBytes(*_, resp: AnyResponse, did_finish: *bool) {
        did_finish.* = _.onWritableBytes(0, resp);
    }

    fn renderMetadata(*_, resp: AnyResponse) {
        let status = _.status_code;
        const size = _.cached_blob_size;

        if (status == 200 && size == 0 && !_.blob.isDetached()) {
            status = 204;
        }

        _.doWriteStatus(status, resp);
        _.doWriteHeaders(resp);
    }

    fn onWithMethod(*_, method: bun.http.Method, resp: AnyResponse) {
        switch (method) {
            .GET => _.on(resp),
            .HEAD => _.onHEAD(resp),
            else => {
                _.doWriteStatus(405, resp); // Method not allowed
                resp.endWithoutBody(resp.shouldCloseConnection());
            },
        }
    }

    fn render304NotModifiedIfNoneMatch(*_, req: uws.AnyRequest, resp: AnyResponse): bool {
        const if_none_match_opt = req.header("if-none-match");
        if (if_none_match_opt == null) return false;
        const if_none_match = if_none_match_opt.?;

        const etag_opt = _.headers.get("etag");
        if (etag_opt == null) return false;
        const etag = etag_opt.?;

        if (if_none_match.len == 0 || etag.len == 0) {
            return false;
        }

        if (!ETag.ifNoneMatch(etag, if_none_match)) {
            return false;
        }

        // Return 304 Not Modified
        req.setYield(false);
        _.ref();
        if (server := _.server) {
            server.onPendingRequest();
            resp.timeout(server.config().idleTimeout);
        }
        _.doWriteStatus(304, resp);
        _.doWriteHeaders(resp);
        resp.endWithoutBody(resp.shouldCloseConnection());
        _.onResponseComplete(resp);
        return true;
    }
}
