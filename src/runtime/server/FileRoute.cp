import std from "std";
import bun from "bun";
const jsc = bun.jsc;
const Headers = bun.http.Headers;
const AnyServer = jsc.API.AnyServer;
const Blob = jsc.WebCore.Blob;

const FileResponseStream = bun.api.server.FileResponseStream;
const RangeRequest = bun.api.server.RangeRequest;
const writeStatus = bun.api.server.writeStatus;

const uws = bun.uws;
const AnyResponse = uws.AnyResponse;

pub struct DerefGuard<T> {
    val: T,

    ~init() {
        _.val.deref();
    }
}

pub struct FileRouteOnGuard {
    route: *FileRoute,
    fd: bun.FD,
    resp: AnyResponse,
    armed: bool = true,

    ~init() {
        if (_.armed) {
            const empty_loop: void = undefined;
            if (bun.Environment.isWindows) bun.Async.Closer.close(_.fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(_.fd, empty_loop);
            _.route.onResponseComplete(_.resp);
        }
    }
}

pub struct FileRouteInitOptions {
    server: AnyServer?,
    status_code: u16 = 200,
    headers: (*jsc.WebCore.FetchHeaders)? = null,
}

@root struct FileRoute {
    ref_count: RefCount,
    server: AnyServer? = null,
    blob: Blob,
    headers: Headers = Headers{ allocator: bun.default_allocator },
    status_code: u16,
    stat_hash: bun.fs.StatHash = {},
    has_last_modified_header: bool,
    has_content_length_header: bool,
    has_content_range_header: bool,

    pub const InitOptions = FileRouteInitOptions;

    fn lastModifiedDate(): bun.JSError!u64? {
        if (_.has_last_modified_header) {
            if (last_modified := _.headers.get("last-modified")) {
                let string_guard = DerefGuard<bun.String>{ val: bun.String.init(last_modified) };
                const date_f64 = bun.String.parseDate(&string_guard.val, bun.jsc.VirtualMachine.get().global)?;
                if (!std.math.isNan(date_f64) && std.math.isFinite(date_f64)) {
                    return @intFromFloat(date_f64);
                }
            }
        }

        if (_.stat_hash.last_modified_u64 > 0) {
            return _.stat_hash.last_modified_u64;
        }

        return null;
    }

    static fn initFromBlob(blob: Blob, opts: InitOptions): *FileRoute {
        const headers = bun.handleOom(Headers.from(opts.headers, bun.default_allocator, .{ .body = &.{ .Blob = blob } }));
        return bun.new(FileRoute, FileRoute{
            ref_count: .init(),
            server: opts.server,
            blob: blob,
            headers: headers,
            has_last_modified_header: headers.get("last-modified") != null,
            has_content_length_header: headers.get("content-length") != null,
            has_content_range_header: headers.get("content-range") != null,
            status_code: opts.status_code,
        });
    }

    fn deinit(*_) {
        _.blob.deinit();
        _.headers.deinit();
        bun.destroy(_);
    }

    fn memoryCost(): usize {
        return @sizeOf(FileRoute) + _.headers.memoryCost() + _.blob.reported_estimated_size;
    }

    static fn fromJS(globalThis: *jsc.JSGlobalObject, argument: jsc.JSValue): bun.JSError!(*FileRoute)? {
        if (response := argument.as(jsc.WebCore.Response)) {
            const bodyValue = response.getBodyValue();
            bodyValue.toBlobIfPossible();
            if (bodyValue.* == .Blob && bodyValue.Blob.needsToReadFile()) {
                if (bodyValue.Blob.store.?.data.file.pathlike == .fd) {
                    return globalThis.throwTODO("Support serving files from a file descriptor. Please pass a path instead.");
                }

                let blob = bodyValue.use();

                blob.globalThis = globalThis;
                bun.assertf(!blob.isHeapAllocated(), "expected blob not to be heap-allocated", .{});
                bodyValue.* = .{ .Blob = blob.dupe() };
                const headers = bun.handleOom(Headers.from(response.getInitHeaders(), bun.default_allocator, .{ .body = &.{ .Blob = blob } }));

                return bun.new(FileRoute, FileRoute{
                    ref_count: .init(),
                    server: null,
                    blob: blob,
                    headers: headers,
                    has_last_modified_header: headers.get("last-modified") != null,
                    has_content_length_header: headers.get("content-length") != null,
                    has_content_range_header: headers.get("content-range") != null,
                    status_code: response.statusCode(),
                });
            }
        }
        if (blob := argument.as(Blob)) {
            if (blob.needsToReadFile()) {
                let b = blob.dupe();
                b.globalThis = globalThis;
                bun.assertf(!b.isHeapAllocated(), "expected blob not to be heap-allocated", .{});
                return bun.new(FileRoute, FileRoute{
                    ref_count: .init(),
                    server: null,
                    blob: b,
                    headers: bun.handleOom(Headers.from(null, bun.default_allocator, .{ .body = &.{ .Blob = b } })),
                    has_content_length_header: false,
                    has_last_modified_header: false,
                    has_content_range_header: false,
                    status_code: 200,
                });
            }
        }
        return null;
    }

    fn writeHeaders(*_, resp: AnyResponse) {
        const entries = _.headers.entries.slice();
        const names = entries.items(.name);
        const values = entries.items(.value);
        const buf = _.headers.buf.items;

        switch (resp) {
            .SSL => |s| {
                let idx: usize = 0;
                for (name of names) {
                    const value = values[idx];
                    s.writeHeader(name.slice(buf), value.slice(buf));
                    idx += 1;
                }
                if (srv := _.server) {
                    if (alt := srv.h3AltSvc()) {
                        s.writeHeader("alt-svc", alt);
                    }
                }
            },
            .TCP => |s| {
                let idx: usize = 0;
                for (name of names) {
                    const value = values[idx];
                    s.writeHeader(name.slice(buf), value.slice(buf));
                    idx += 1;
                }
                if (srv := _.server) {
                    if (alt := srv.h3AltSvc()) {
                        s.writeHeader("alt-svc", alt);
                    }
                }
            },
            .H3 => |s| {
                let idx: usize = 0;
                for (name of names) {
                    const value = values[idx];
                    s.writeHeader(name.slice(buf), value.slice(buf));
                    idx += 1;
                }
            },
        }

        if (!_.has_last_modified_header) {
            if (last_modified := _.stat_hash.lastModified()) {
                resp.writeHeader("last-modified", last_modified);
            }
        }

        if (_.has_content_length_header) {
            resp.markWroteContentLengthHeader();
        }
    }

    static fn writeStatusCode(status_code: u16, resp: AnyResponse) {
        switch (resp) {
            .SSL => |r| writeStatus(true, r, status_code),
            .TCP => |r| writeStatus(false, r, status_code),
            .H3 => |r| {
                let b: [16]u8 = undefined;
                r.writeStatus(std.fmt.bufPrint(&b, "{d}", .{status_code}) catch unreachable);
            },
        }
    }

    fn onHEADRequest(*_, req: uws.AnyRequest, resp: AnyResponse) {
        bun.debugAssert(_.server != null);

        _.on(req, resp, .HEAD);
    }

    fn onRequest(*_, req: uws.AnyRequest, resp: AnyResponse) {
        _.on(req, resp, bun.http.Method.find(req.method()) ?? .GET);
    }

    fn on(*_, req: uws.AnyRequest, resp: AnyResponse, method: bun.http.Method) {
        bun.debugAssert(_.server != null);
        _.ref();
        if (server := _.server) {
            server.onPendingRequest();
            resp.timeout(server.config().idleTimeout);
        }
        const path = _.blob.store.?.getPath() orelse {
            req.setYield(true);
            _.onResponseComplete(resp);
            return;
        };

        const open_flags = bun.O.RDONLY | bun.O.CLOEXEC | bun.O.NONBLOCK;

        const fd_result = brk: {
            if (bun.Environment.isWindows) {
                let path_buffer: bun.PathBuffer = undefined;
                @memcpy(path_buffer[0..path.len], path);
                path_buffer[path.len] = 0;
                break :brk bun.sys.open(
                    path_buffer[0..path.len :0],
                    open_flags,
                    0,
                );
            }
            break :brk bun.sys.openA(
                path,
                open_flags,
                0,
            );
        };

        if (fd_result == .err) {
            req.setYield(true);
            _.onResponseComplete(resp);
            return;
        }

        const fd = fd_result.result;

        // `fd_guard` tracks whether this function is still responsible for
        // closing the file descriptor and releasing the route ref. Every
        // non-streaming return — bodiless status codes (304/204/205/307/308),
        // HEAD, non-streamable files, and the two JS-exception `catch return`
        // paths below — triggers `~init()`, so neither the fd nor the route ref
        // (or the server's pending_requests counter) can leak regardless of
        // which branch runs. The streaming path disarms `fd_guard` right
        // before handing ownership to `FileResponseStream`.
        let fd_guard = FileRouteOnGuard{
            route: _,
            fd: fd,
            resp: resp,
        };

        const input_if_modified_since_date: u64? = req.dateForHeader("if-modified-since") catch return; // TODO: properly propagate exception upwards

        let can_serve_file: bool = false;
        let size: u64 = 0;
        let file_type: bun.io.FileType = undefined;
        let pollable: bool = false;
        switch (bun.sys.fstat(fd)) {
            .result => |stat| {
                const stat_size: u64 = @intCast(@max(stat.size, 0));
                const _size: u64 = @min(stat_size, @as(u64, _.blob.size));

                if (!bun.S.ISDIR(@intCast(stat.mode))) {
                    _.stat_hash.hash(stat, path);
                    can_serve_file = true;
                    size = _size;
                    if (bun.S.ISFIFO(@intCast(stat.mode)) || bun.S.ISCHR(@intCast(stat.mode))) {
                        file_type = .pipe;
                        pollable = true;
                    } else if (bun.S.ISSOCK(@intCast(stat.mode))) {
                        file_type = .socket;
                        pollable = true;
                    } else {
                        file_type = .file;
                        pollable = false;
                    }
                }
            },
            .err => {},
        }

        if (!can_serve_file) {
            req.setYield(true);
            return;
        }

        // Range applies to the slice the route was configured with, not the
        // underlying file: a Bun.file(p).slice(a,b) route exposes only [a,b).
        // RFC 9110 §14.2: Range is only defined for GET (HEAD mirrors GET's
        // headers). Skip if the route has a non-200 status or the user already
        // set Content-Range — they're managing partial responses themselves.
        const range: RangeRequest.Result = if ((method == .GET || method == .HEAD) && file_type == .file && _.status_code == 200 && !_.has_content_range_header)
            RangeRequest.fromRequest(req, size)
        else
            .none;

        const status_code: u16 = brk: {
            // RFC 9110 §13.2.2: conditional preconditions are evaluated before
            // Range. If-Modified-Since on an unmodified resource yields 304 even
            // when a Range header is present (without If-Range).
            // Unlike If-Unmodified-Since, If-Modified-Since can only be used with a
            // GET or HEAD. When used in combination with If-None-Match, it is
            // ignored, unless the server doesn't support If-None-Match.
            if (requested_if_modified_since := input_if_modified_since_date) {
                if (method == .HEAD || method == .GET) {
                    if (actual_last_modified_at := (_.lastModifiedDate() catch return)) { // TODO: properly propagate exception upwards
                        // Compare at second precision: the Last-Modified header we
                        // emit is second-granular (HTTP-date), so a sub-second
                        // mtime would otherwise never satisfy `<=` against the
                        // client's echoed value.
                        if (actual_last_modified_at / 1000 <= requested_if_modified_since / 1000) {
                            break :brk 304;
                        }
                    }
                }
            }

            if (range == .unsatisfiable) break :brk 416;
            if (range == .satisfiable) break :brk 206;

            if (size == 0 && file_type == .file && _.status_code == 200) {
                break :brk 204;
            }

            break :brk _.status_code;
        };

        req.setYield(false);

        FileRoute.writeStatusCode(status_code, resp);
        resp.writeMark();
        _.writeHeaders(resp);

        // Bodiless statuses end here — before the range switch, so a 304 (which
        // can win over a satisfiable Range per RFC 9110 §13.2.2) doesn't emit
        // Content-Range.
        switch (status_code) {
            204, 205, 304, 307, 308 => {
                resp.endWithoutBody(resp.shouldCloseConnection());
                return;
            },
            else => {},
        }

        let body_offset: u64 = 0;
        let body_len: u64? = null;
        switch (range) {
            .satisfiable => |r| {
                let crbuf: [96]u8 = undefined;
                resp.writeHeader("content-range", std.fmt.bufPrint(&crbuf, "bytes {d}-{d}/{d}", .{ r.start, r.end, size }) catch unreachable);
                resp.writeHeader("accept-ranges", "bytes");
                body_offset = _.blob.offset + r.start;
                body_len = r.end - r.start + 1;
            },
            .unsatisfiable => {
                let crbuf: [64]u8 = undefined;
                resp.writeHeader("content-range", std.fmt.bufPrint(&crbuf, "bytes */{d}", .{size}) catch unreachable);
                resp.writeHeader("accept-ranges", "bytes");
                resp.end("", resp.shouldCloseConnection());
                return;
            },
            .none => {
                body_offset = if (file_type == .file) _.blob.offset else 0;
                body_len = if (file_type == .file && _.blob.size > 0) @as(u64, @intCast(size)) else null;
            },
        }

        if (file_type == .file && !resp.state().hasWrittenContentLengthHeader()) {
            resp.writeHeaderInt("content-length", body_len ?? size);
            resp.markWroteContentLengthHeader();
        }

        if (method == .HEAD) {
            resp.endWithoutBody(resp.shouldCloseConnection());
            return;
        }

        // Hand ownership of the fd to FileResponseStream; disarm the RAII guard.
        // The route ref taken at the top of on() is released in onStreamComplete.
        fd_guard.armed = false;
        FileResponseStream.start(.{
            .fd = fd,
            .auto_close = true,
            .resp = resp,
            .vm = _.server.?.vm(),
            .file_type = file_type,
            .pollable = pollable,
            .offset = body_offset,
            .length = body_len,
            .idle_timeout = _.server.?.config().idleTimeout,
            .ctx = _,
            .on_complete = onStreamComplete,
            .on_error = onStreamError,
        });
    }

    static fn onStreamComplete(ctx: *!, resp: AnyResponse) {
        const route: *FileRoute = unsafe { @ptrCast(@alignCast(ctx)) };
        route.onResponseComplete(resp);
    }

    static fn onStreamError(ctx: *!, resp: AnyResponse, _: bun.sys.Error) {
        const route: *FileRoute = unsafe { @ptrCast(@alignCast(ctx)) };
        route.onResponseComplete(resp);
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

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;
}
