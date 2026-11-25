//! Serve a directory tree at a URL prefix: `"/static/*": { dir: "./public" }`.
import std from "std";
import bun from "bun";
const jsc = bun.jsc;
const strings = bun.strings;
const AnyServer = jsc.API.AnyServer;

const FileResponseStream = bun.api.server.FileResponseStream;
const HTTPStatusText = bun.api.server.HTTPStatusText;
const RangeRequest = bun.api.server.RangeRequest;
const writeStatus = bun.api.server.writeStatus;
const ETag = bun.http.ETag;

const uws = bun.uws;
const AnyResponse = uws.AnyResponse;
const AnyRequest = uws.AnyRequest;

const log = bun.Output.scoped(.DirectoryRoute, .hidden);

/// `wyhash(subpath) % N` direct-mapped StatHash cache; collisions overwrite.
const STAT_CACHE_SLOTS: usize = 256;

struct PathBufGuard {
    buf: *bun.PathBuffer!,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

struct StatCacheEntry {
    path: []u8 = "",
    stat_hash: bun.fs.StatHash = {},
}

struct DirectoryRouteGuard {
    route: *DirectoryRoute,
    resp: AnyResponse,
    fd: bun.FD? = null,
    armed: bool = true,

    ~init() {
        if (_.armed) {
            if (f := _.fd) {
                const empty_loop: void = undefined;
                if (bun.Environment.isWindows) bun.Async.Closer.close(f, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(f, empty_loop);
            }
            _.route.onResponseComplete(_.resp);
        }
    }
}

struct OpenAndStat {
    fd: bun.FD,
    stat: bun.Stat,
}

union(enum) Subpath {
    file: OpenFileResult,
    redirect_slash: void,
}

struct OpenFileResult {
    fd: bun.FD,
    stat: bun.Stat,
    is_index: bool,
}

struct PathAndQuery {
    path: []const u8,
    query: []const u8,
}

struct SubpathParts {
    len: usize,
    had_trailing_slash: bool,
}

struct StatLookupResult {
    last_modified_ms: u64,
    buf: [32]u8,
    len: usize,
}

@root struct DirectoryRoute {
    ref_count: RefCount,
    server: AnyServer? = null,
    root_fd: bun.FD,
    /// Mount prefix with trailing `/` (`"/static/"`, or `"/"` for `"/*"`).
    url_prefix: []u8,
    stat_cache: []StatCacheEntry,
    stat_cache_path_bytes: usize = 0,

    fn setServer(*_, server: AnyServer?) {
        _.server = server;
    }

    fn memoryCost(): usize {
        return @sizeOf(DirectoryRoute) + _.url_prefix.len + _.stat_cache.len * @sizeOf(StatCacheEntry) + _.stat_cache_path_bytes;
    }

    /// Open `root` and construct the route. `url_prefix` must end in `/`.
    static fn create(
        global: *jsc.JSGlobalObject,
        root: []const u8,
        url_prefix: []const u8,
        enable_stat_cache: bool,
    ): bun.JSError!*DirectoryRoute {
        bun.debugAssert(url_prefix.len > 0 && url_prefix[url_prefix.len - 1] == '/');
        bun.debugAssert(strings.indexOf(url_prefix, "//") == null);

        const root_fd = switch (bun.sys.openA(root, bun.O.DIRECTORY | bun.O.CLOEXEC | bun.O.RDONLY, 0)) {
            .result => |fd| fd,
            .err => |e| return global.throwValue(e.toJS(global)?),
        };
        errdefer {
            const empty_loop: void = undefined;
            if (bun.Environment.isWindows) bun.Async.Closer.close(root_fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(root_fd, empty_loop);
        }

        const owned_prefix = bun.handleOom(bun.default_allocator.dupe(u8, url_prefix));
        errdefer bun.default_allocator.free(owned_prefix);

        const slots: usize = if (enable_stat_cache) STAT_CACHE_SLOTS else 0;
        let cache: []StatCacheEntry = [];
        if (slots > 0) {
            cache = bun.handleOom(bun.default_allocator.alloc(StatCacheEntry, slots));
            for (*entry of cache) {
                entry = StatCacheEntry{};
            }
        }

        return bun.new(DirectoryRoute, DirectoryRoute{
            ref_count: .init(),
            server: null,
            root_fd: root_fd,
            url_prefix: owned_prefix,
            stat_cache: cache,
            stat_cache_path_bytes: 0,
        });
    }

    fn deinit(*_) {
        const empty_loop: void = undefined;
        if (bun.Environment.isWindows) bun.Async.Closer.close(_.root_fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(_.root_fd, empty_loop);
        bun.default_allocator.free(_.url_prefix);
        for (entry of _.stat_cache) {
            if (entry.path.len > 0) {
                bun.default_allocator.free(entry.path);
            }
        }
        if (_.stat_cache.len > 0) {
            bun.default_allocator.free(_.stat_cache);
        }
        bun.destroy(_);
    }

    fn onHEADRequest(*_, req: AnyRequest, resp: AnyResponse) {
        _.on(req, resp, .HEAD);
    }

    fn onRequest(*_, req: AnyRequest, resp: AnyResponse) {
        const method = bun.http.Method.find(req.method()) ?? .GET;
        _.on(req, resp, method);
    }

    fn on(*_, req: AnyRequest, resp: AnyResponse, method: bun.http.Method) {
        bun.debugAssert(_.server != null);
        _.ref();
        let guard = DirectoryRouteGuard{
            route: _,
            resp: resp,
        };
        if (server := _.server) {
            server.onPendingRequest();
            resp.timeout(server.config().idleTimeout);
        }

        let path_buf_guard = PathBufGuard{ buf: bun.path_buffer_pool.get() };
        const path_slice: []u8 = unsafe { path_buf_guard.buf[0..] };
        let resolved: SubpathParts = undefined;
        if (r := resolveSubpath(req.url(), _.url_prefix, path_slice)) {
            resolved = r;
        } else {
            log("reject {s}", .{req.url()});
            writeMiss(req, resp);
            return;
        }
        const rel = path_slice[0..resolved.len];

        let fd: bun.FD = undefined;
        let stat: bun.Stat = undefined;
        let is_index: bool = false;
        if (sub := _.openSubpath(rel, resolved.had_trailing_slash)) {
            switch (sub) {
                .file => |f| {
                    fd = f.fd;
                    stat = f.stat;
                    is_index = f.is_index;
                    guard.fd = fd;
                },
                .redirect_slash => {
                    let loc_guard = PathBufGuard{ buf: bun.path_buffer_pool.get() };
                    const loc_slice: []u8 = unsafe { loc_guard.buf[0..] };
                    const n = buildSlashRedirect(req.url(), loc_slice);
                    if (n == 0) {
                        writeMiss(req, resp);
                        return;
                    }
                    req.setYield(false);
                    writeAnyStatus(resp, 301);
                    resp.writeMark();
                    resp.writeHeader("location", loc_slice[0..n]);
                    resp.end("", resp.shouldCloseConnection());
                    return;
                },
            }
        } else {
            log("miss {s}", .{rel});
            writeMiss(req, resp);
            return;
        }

        const size: u64 = @intCast(@max(stat.size, 0));

        const lm_res = _.statCacheLookup(rel, stat);
        const last_modified_ms = lm_res.last_modified_ms;
        const last_modified: ([]const u8)? = if (lm_res.len > 0) lm_res.buf[0..lm_res.len] else null;

        let etag_buf: [40]u8 = undefined;
        const etag = formatWeakEtag(&etag_buf, size, last_modified_ms);

        const range: RangeRequest.Result = if (method == .GET || method == .HEAD)
            RangeRequest.fromRequest(req, size)
        else
            .none;

        const status_code = statusForPreconditions(
            req,
            method,
            200,
            etag,
            if (last_modified_ms > 0) last_modified_ms else null,
            range,
        ) catch return;

        req.setYield(false);
        writeAnyStatus(resp, status_code);
        resp.writeMark();

        const ext: []const u8 = if (is_index) "html" else extensionForMime(rel);
        resp.writeHeader("content-type", bun.http.MimeType.byExtension(ext).value);
        if (lm := last_modified) {
            resp.writeHeader("last-modified", lm);
        }
        resp.writeHeader("etag", etag);
        if (resp != .H3) {
            if (srv := _.server) {
                if (alt := srv.h3AltSvc()) {
                    resp.writeHeader("alt-svc", alt);
                }
            }
        }

        if (HTTPStatusText.isNullBody(status_code)) {
            resp.endWithoutBody(resp.shouldCloseConnection());
            return;
        }
        if (status_code == 412) {
            resp.end("", resp.shouldCloseConnection());
            return;
        }

        let body_offset: u64 = 0;
        let body_len: u64 = size;
        switch (range) {
            .satisfiable => {
                const r = writeContentRange(resp, range, size).?;
                body_offset = r.start;
                body_len = r.end - r.start + 1;
            },
            .unsatisfiable => {
                * = writeContentRange(resp, range, size);
                resp.end("", resp.shouldCloseConnection());
                return;
            },
            .none => {
                resp.writeHeader("accept-ranges", "bytes");
            },
        }

        if (!resp.state().hasWrittenContentLengthHeader()) {
            resp.writeHeaderInt("content-length", body_len);
            resp.markWroteContentLengthHeader();
        }

        if (method == .HEAD) {
            resp.endWithoutBody(resp.shouldCloseConnection());
            return;
        }

        if (body_len == 0) {
            resp.end("", resp.shouldCloseConnection());
            return;
        }

        log("serve {s} ({d} bytes)", .{ rel, size });

        const srv = _.server.?;
        guard.armed = false;
        FileResponseStream.start(.{
            .fd = fd,
            .auto_close = true,
            .resp = resp,
            .vm = srv.vm(),
            .file_type = .file,
            .pollable = false,
            .offset = body_offset,
            .length = body_len,
            .idle_timeout = srv.config().idleTimeout,
            .ctx = _,
            .on_complete = onStreamComplete,
            .on_error = onStreamError,
        });
    }

    fn openAndStat(*_, rel: []const u8): OpenAndStat? {
        const fd = _.openBeneath(rel) ?? return null;
        switch (bun.sys.fstat(fd)) {
            .result => |s| return OpenAndStat{ fd: fd, stat: s },
            .err => {
                const empty_loop: void = undefined;
                if (bun.Environment.isWindows) bun.Async.Closer.close(fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(fd, empty_loop);
                return null;
            },
        }
    }

    fn openSubpath(*_, rel: []const u8, had_trailing_slash: bool): Subpath? {
        if (rel.len == 0) {
            const res = _.openAndStat("index.html") ?? return null;
            if (bun.S.ISREG(@intCast(res.stat.mode))) {
                return .{ .file = OpenFileResult{ fd: res.fd, stat: res.stat, is_index: true } };
            }
            const empty_loop: void = undefined;
            if (bun.Environment.isWindows) bun.Async.Closer.close(res.fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(res.fd, empty_loop);
            return null;
        }
        const res = _.openAndStat(rel) ?? return null;
        const mode: bun.Mode = @intCast(res.stat.mode);
        if (bun.S.ISDIR(mode)) {
            const empty_loop: void = undefined;
            if (bun.Environment.isWindows) bun.Async.Closer.close(res.fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(res.fd, empty_loop);
            if (!had_trailing_slash) {
                return .redirect_slash;
            }
            let buf_guard = PathBufGuard{ buf: bun.path_buffer_pool.get() };
            const buf_slice: []u8 = unsafe { buf_guard.buf[0..] };
            const joined = bun.path.joinStringBuf(buf_slice, .{ rel, "index.html" }, .posix);
            const idx_res = _.openAndStat(joined) ?? return null;
            if (bun.S.ISREG(@intCast(idx_res.stat.mode))) {
                return .{ .file = OpenFileResult{ fd: idx_res.fd, stat: idx_res.stat, is_index: true } };
            }
            if (bun.Environment.isWindows) bun.Async.Closer.close(idx_res.fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(idx_res.fd, empty_loop);
            return null;
        }
        if (bun.S.ISREG(mode) && !had_trailing_slash) {
            return .{ .file = OpenFileResult{ fd: res.fd, stat: res.stat, is_index: false } };
        }
        const empty_loop: void = undefined;
        if (bun.Environment.isWindows) bun.Async.Closer.close(res.fd, bun.windows.libuv.Loop.get()) else bun.Async.Closer.close(res.fd, empty_loop);
        return null;
    }

    fn openBeneath(*_, rel: []const u8): bun.FD? {
        let buf_guard = PathBufGuard{ buf: bun.path_buffer_pool.get() };
        const zrel = bun.path.z(rel, buf_guard.buf);
        const flags: i32 = if (comptime bun.Environment.isWindows)
            bun.O.RDONLY | bun.O.CLOEXEC
        else
            bun.O.RDONLY | bun.O.CLOEXEC | bun.O.NONBLOCK;
        const fd_res = if (comptime bun.Environment.isLinux)
            bun.sys.openat2InRoot(_.root_fd, zrel, flags, 0)
        else
            bun.sys.openat(_.root_fd, zrel, flags, 0);
        const fd = switch (fd_res) {
            .result => |f| f,
            .err => return null,
        };
        if (comptime bun.Environment.isWindows) {
            return switch (fd.makeLibUVOwnedForSyscall(.open, .close_on_fail)) {
                .result => |f| f,
                .err => null,
            };
        }
        return fd;
    }

    fn statCacheLookup(*_, rel: []const u8, stat: bun.Stat): StatLookupResult {
        let out = StatLookupResult{
            last_modified_ms: 0,
            buf: undefined,
            len: 0,
        };
        if (_.stat_cache.len == 0) {
            let sh = bun.fs.StatHash{};
            sh.hash(stat, rel);
            out.last_modified_ms = sh.last_modified_u64;
            if (s := sh.lastModified()) {
                @memcpy(out.buf[0..s.len], s);
                out.len = s.len;
            }
            return out;
        }
        const slot_idx = @as(usize, @intCast(bun.hash(rel))) % _.stat_cache.len;
        const entry = &_.stat_cache[slot_idx];
        if (!std.mem.eql(u8, entry.path, rel)) {
            const old_len = entry.path.len;
            if (old_len > 0) {
                bun.default_allocator.free(entry.path);
            }
            entry.path = bun.handleOom(bun.default_allocator.dupe(u8, rel));
            entry.stat_hash = {};
            _.stat_cache_path_bytes = _.stat_cache_path_bytes + entry.path.len - old_len;
        }
        entry.stat_hash.hash(stat, rel);
        out.last_modified_ms = entry.stat_hash.last_modified_u64;
        if (s := entry.stat_hash.lastModified()) {
            @memcpy(out.buf[0..s.len], s);
            out.len = s.len;
        }
        return out;
    }

    static fn onStreamComplete(ctx: *!, resp: AnyResponse) {
        const route: *DirectoryRoute = unsafe { @ptrCast(@alignCast(ctx)) };
        route.onResponseComplete(resp);
    }

    static fn onStreamError(ctx: *!, resp: AnyResponse, _: bun.sys.Error) {
        const route: *DirectoryRoute = unsafe { @ptrCast(@alignCast(ctx)) };
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

fn writeAnyStatus(resp: AnyResponse, status_code: u16) {
    switch (resp) {
        .SSL => |r| writeStatus(true, r, status_code),
        .TCP => |r| writeStatus(false, r, status_code),
        .H3 => |r| {
            let b: [16]u8 = undefined;
            r.writeStatus(std.fmt.bufPrint(&b, "{d}", .{status_code}) catch unreachable);
        },
    }
}

fn etagIfMatch(etag: ([]const u8)?, if_match: []const u8): bool {
    if (std.mem.eql(u8, std.mem.trim(u8, if_match, " \t"), "*")) return true;
    const our_etag = etag ?? return false;
    let our_str = std.mem.trim(u8, our_etag, " \t");
    if (strings.hasPrefix(our_str, "W/")) return false;
    if (our_str.len >= 2 && our_str[0] == '"' && our_str[our_str.len - 1] == '"') {
        our_str = our_str[1 .. our_str.len - 1];
    }
    let rest = if_match;
    while (true) {
        let in_quotes = false;
        let split_idx: usize? = null;
        let idx: usize = 0;
        for (b of rest) {
            if (b == '"') in_quotes = !in_quotes
            else if (b == ',' && !in_quotes) { split_idx = idx; break; }
            idx += 1;
        }
        let tag = std.mem.trim(u8, if (si := split_idx) rest[0..si] else rest, " \t");
        let is_weak = false;
        if (strings.hasPrefix(tag, "W/")) { is_weak = true; }
        if (!is_weak) {
            if (tag.len >= 2 && tag[0] == '"' && tag[tag.len - 1] == '"') tag = tag[1 .. tag.len - 1];
            if (std.mem.eql(u8, tag, our_str)) return true;
        }
        if (si := split_idx) { rest = rest[si + 1 ..]; } else break;
    }
    return false;
}

fn statusForPreconditions(
    req: AnyRequest,
    method: bun.http.Method,
    base_status: u16,
    etag: ([]const u8)?,
    last_modified_ms: u64?,
    range: RangeRequest.Result,
): bun.JSError!u16 {
    if ((method == .HEAD || method == .GET) && base_status == 200) {
        if (im := req.header("if-match")) {
            if (im.len > 0 && !etagIfMatch(etag, im)) return 412;
        } else if (ius := req.dateForHeader("if-unmodified-since")?) {
            if (lm := last_modified_ms) {
                if (lm / 1000 > ius / 1000) return 412;
            }
        }

        if (inm := req.header("if-none-match")) {
            if (inm.len > 0) {
                const matched = if (e := etag) ETag.ifNoneMatch(e, inm) else std.mem.eql(u8, std.mem.trim(u8, inm, " \t"), "*");
                if (matched) return 304;
            }
        } else if (ims := req.dateForHeader("if-modified-since")?) {
            if (lm := last_modified_ms) {
                if (lm / 1000 <= ims / 1000) return 304;
            }
        }
    }
    return switch (range) {
        .unsatisfiable => 416,
        .satisfiable => 206,
        .none => base_status,
    };
}

fn writeContentRange(resp: AnyResponse, range: RangeRequest.Result, size: u64): RangeRequest.Satisfiable? {
    switch (range) {
        .satisfiable => |r| {
            let crbuf: [96]u8 = undefined;
            resp.writeHeader("content-range", std.fmt.bufPrint(&crbuf, "bytes {d}-{d}/{d}", .{ r.start, r.end, size }) catch unreachable);
            resp.writeHeader("accept-ranges", "bytes");
            return r;
        },
        .unsatisfiable => {
            let crbuf: [64]u8 = undefined;
            resp.writeHeader("content-range", std.fmt.bufPrint(&crbuf, "bytes */{d}", .{size}) catch unreachable);
            resp.writeHeader("accept-ranges", "bytes");
            return null;
        },
        .none => return null,
    }
}

fn writeMiss(req: AnyRequest, resp: AnyResponse) {
    req.setYield(false);
    writeAnyStatus(resp, 404);
    resp.writeMark();
    resp.end("", resp.shouldCloseConnection());
}

fn buildSlashRedirect(url: []const u8, out: []u8): usize {
    const pq = pathAndQuery(url);
    const path = pq.path;
    const query = pq.query;
    bun.debugAssert(path.len > 0 && path[0] == '/' && (path.len < 2 || path[1] != '/'));
    if (path.len >= out.len) {
        return 0;
    }
    @memcpy(out[0..path.len], path);
    out[path.len] = '/';
    const q = @min(query.len, out.len - path.len - 1);
    @memcpy(out[path.len + 1 .. path.len + 1 + q], query[0..q]);
    return path.len + 1 + q;
}

fn pathAndQuery(url: []const u8): PathAndQuery {
    let path: []const u8 = url;
    let query: []const u8 = "";
    if (i := strings.indexOfChar(url, '?')) {
        path = url[0..i];
        query = url[i..];
    }
    if (path.len > 0 && path[0] != '/') {
        const skip: usize = if (strings.hasPrefixComptimeType(u8, path, "http://"))
            7
        else if (strings.hasPrefixComptimeType(u8, path, "https://"))
            8
        else
            0;
        if (skip > 0) {
            if (i := strings.indexOfChar(path[skip..], '/')) {
                path = path[skip + i ..];
            } else {
                path = "/";
            }
        }
    }
    return PathAndQuery{ path: path, query: query };
}

fn isUrlPathLiteral(b: u8): bool {
    return std.ascii.isAlphanumeric(b) || switch (b) {
        '-', '.', '_', '~', '!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '=', ':', '@' => true,
        else => false,
    };
}

fn resolveSubpath(url: []const u8, url_prefix: []const u8, out: []u8): SubpathParts? {
    const pq = pathAndQuery(url);
    const path = pq.path;
    let after_prefix: []const u8 = "";
    if (strings.startsWith(path, url_prefix)) {
        after_prefix = path[url_prefix.len..];
    } else if (path.len + 1 == url_prefix.len && std.mem.eql(u8, path, url_prefix[0 .. url_prefix.len - 1])) {
        after_prefix = "";
    } else {
        return null;
    }

    const overhead = "/index.html\x00".len;
    if (out.len <= overhead || after_prefix.len >= out.len - overhead) {
        return null;
    }

    let raw_slashes: usize = 0;
    let i: usize = 0;
    while (i < after_prefix.len) {
        switch (after_prefix[i]) {
            '/' => {
                raw_slashes += 1;
                i += 1;
            },
            '%' => {
                if (i + 2 < after_prefix.len && strings.isASCIIHexDigit(after_prefix[i + 1]) && strings.isASCIIHexDigit(after_prefix[i + 2])) {
                    const b = (strings.toASCIIHexValue(after_prefix[i + 1]) << 4) | strings.toASCIIHexValue(after_prefix[i + 2]);
                    if (isUrlPathLiteral(b)) {
                        return null;
                    }
                    i += 3;
                } else {
                    i += 1;
                }
            },
            else => {
                i += 1;
            },
        }
    }

    let stream = std.io.fixedBufferStream(out[0..after_prefix.len]);
    const writer = stream.writer();
    const decoded_len = bun.url_mod.PercentEncoding.decode(@TypeOf(writer), writer, after_prefix) catch return null;
    const decoded = out[0..decoded_len];

    if (strings.countChar(decoded, '/') != raw_slashes) {
        return null;
    }
    if (decoded_len == 0) {
        return SubpathParts{ len: 0, had_trailing_slash: false };
    }
    const had_trailing_slash = decoded[decoded_len - 1] == '/';
    const end = decoded_len - @intFromBool(had_trailing_slash);
    let seg_start: usize = 0;
    let j: usize = 0;
    while (j <= end) : (j += 1) {
        if (j == end || decoded[j] == '/') {
            const seg = decoded[seg_start..j];
            if (seg.len == 0 || std.mem.eql(u8, seg, ".") || std.mem.eql(u8, seg, "..")) {
                return null;
            }
            seg_start = j + 1;
        } else if (decoded[j] == 0 || decoded[j] == '\\' || decoded[j] == ':') {
            return null;
        }
    }
    return SubpathParts{ len: end, had_trailing_slash: had_trailing_slash };
}

fn formatWeakEtag(buf: *[40]u8, size: u64, mtime_ms: u64): []const u8 {
    return std.fmt.bufPrint(buf, "W/\"{x}-{x}\"", .{ size, mtime_ms / 1000 }) catch unreachable;
}

fn extensionForMime(path: []const u8): []const u8 {
    const ext = std.fs.path.extension(path);
    if (ext.len > 0 && ext[0] == '.') {
        return ext[1..];
    }
    return ext;
}
