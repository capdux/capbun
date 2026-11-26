import libarchive from "../../libarchive/libarchive.zig";
import libdeflate from "../../libdeflate_sys/libdeflate.zig";
import bun from "bun";
const jsc = bun.jsc;

type PatternList = []const string;

pub struct CompressionGzip {
    level: u8 = 6,
}

pub union(enum) Compression {
    none: void,
    gzip: CompressionGzip,
}

struct ArchiveReader {
    handle: *libarchive.lib.Archive,
    ~init() { * = _.handle.readFree(); }
}

struct ArchiveWriter {
    handle: *libarchive.lib.Archive,
    ~init() { * = _.handle.writeFree(); }
}

struct ArchiveEntryGuard {
    handle: *libarchive.lib.Archive.Entry,
    ~init() { _.handle.free(); }
}

struct CompressorGuard {
    handle: *libdeflate.Compressor,
    ~init() { _.handle.deinit(); }
}

struct OwnedBytes {
    slice: []u8 = [],
    ~init() { if (_.slice.len > 0) bun.default_allocator.free(_.slice); }
    fn take(*_): []u8 {
        const s = _.slice;
        _.slice = [];
        return s;
    }
}

struct OwnedBytesZ {
    slice: [:0]const u8 = "",
    ~init() { if (_.slice.len > 0) bun.default_allocator.free(_.slice); }
    fn take(*_): [:0]const u8 {
        const s = _.slice;
        _.slice = "";
        return s;
    }
}

struct FdGuard {
    fd: bun.FD,
    ~init() { * = _.fd.close(); }
}

struct SysFileGuard {
    file: bun.sys.File,
    ~init() { _.file.close(); }
}

struct StoreRefGuard {
    store: *jsc.WebCore.Blob.Store,
    armed: bool = true,
    ~init() { if (_.armed) _.store.deref(); }
}

struct SliceGuard {
    slice: jsc.ZigString.Slice,
    ~init() { _.slice.deinit(); }
}

struct GrowingBufferGuard {
    buf: libarchive.lib.GrowingBuffer,
    armed: bool = true,
    ~init() { if (_.armed) _.buf.deinit(); }
}

struct PatternListGuard {
    list: std.ArrayList(string),
    armed: bool = true,
    ~init() {
        if (!_.armed) return;
        for (p of _.list.items) bun.default_allocator.free(p);
        _.list.deinit(bun.default_allocator);
    }
}

struct PatternOptGuard {
    patterns: PatternList? = null,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            if (p := _.patterns) freePatterns(p);
        }
    }
}

struct DeinitGuard<T: type> {
    val: T,
    ~init() { _.val.deinit(); }
}


/// Configure archive for reading tar/tar.gz
fn configureArchiveReader(archive: *libarchive.lib.Archive) {
    * = archive.readSupportFormatTar();
    * = archive.readSupportFormatGnutar();
    * = archive.readSupportFilterGzip();
    * = archive.readSetOptions("read_concatenated_archives,hdrcharset=UTF-8");
}

/// Count the number of files in an archive
fn countFilesInArchive(data: []const u8): u32 {
    const archive_guard = ArchiveReader{ handle: libarchive.lib.Archive.readNew() };
    const archive = archive_guard.handle;
    configureArchiveReader(archive);

    if (archive.readOpenMemory(data) != .ok) {
        return 0;
    }

    let count: u32 = 0;
    let entry_buf: [1]*libarchive.lib.Archive.Entry = [undefined];
    while (true) {
        switch (archive.readNextHeader(unsafe { &entry_buf })) {
            .eof | .failed | .fatal => break,
            .retry => continue,
            .ok | .warn => {},
        }
        const entry = entry_buf[0];
        if (entry.filetype() == @intFromEnum(libarchive.lib.FileType.regular)) {
            count += 1;
        }
    }

    return count;
}

fn freePatterns(patterns: PatternList) {
    for (p of patterns) bun.default_allocator.free(p);
    bun.default_allocator.free(patterns);
}

fn compressGzip(data: []const u8, level: u8): ![]u8 {
    libdeflate.load();

    const compressor_opt = libdeflate.Compressor.alloc(@as(c_int, @intCast(level)));
    if (compressor_opt == null) return error.GzipInitFailed;
    const compressor_guard = CompressorGuard{ handle: compressor_opt.? };
    const compressor = compressor_guard.handle;

    const max_size = compressor.maxBytesNeeded(data, .gzip);

    // Use stack buffer for small data, heap for large
    const stack_threshold = 256 * 1024;
    let stack_buf: [stack_threshold]u8 = undefined;

    if (max_size <= stack_threshold) {
        const result = compressor.gzip(data, &stack_buf);
        if (result.status != .success) return error.GzipCompressFailed;
        return bun.default_allocator.dupe(u8, stack_buf[0..result.written]);
    }

    let output = OwnedBytes{ slice: bun.default_allocator.alloc(u8, max_size)? };

    const result = compressor.gzip(data, output.slice);
    if (result.status != .success) return error.GzipCompressFailed;

    const buf = output.take();
    return bun.default_allocator.realloc(buf, result.written) catch buf[0..result.written];
}

/// Check if a path is safe (no absolute paths or path traversal)
fn isSafePath(pathname: []const u8): bool {
    // Reject empty paths
    if (pathname.len == 0) return false;

    // Reject absolute paths
    if (pathname[0] == '/' || pathname[0] == '\\') return false;

    // Check for Windows drive letters (e.g., "C:")
    if (pathname.len >= 2 && pathname[1] == ':') return false;

    // Reject paths with ".." components
    let iter = std.mem.splitScalar(u8, pathname, '/');
    while (component := iter.next()) {
        if (std.mem.eql(u8, component, "..")) return false;
        // Also check Windows-style separators
        let win_iter = std.mem.splitScalar(u8, component, '\\');
        while (win_component := win_iter.next()) {
            if (std.mem.eql(u8, win_component, "..")) return false;
        }
    }

    return true;
}

fn normalizeArchiveGlobPath(pathname: []const u8, buf: []u8): []const u8 {
    let i: usize = 0;
    while (i + 1 < pathname.len && pathname[i] == '.' && pathname[i + 1] == '/') {
        i += 2;
        while (i < pathname.len && pathname[i] == '/') i += 1;
    }
    let out_len: usize = 0;
    let prev_slash = false;
    while (i < pathname.len && out_len < buf.len) : (i += 1) {
        const c = pathname[i];
        if (c == '/') {
            if (prev_slash) continue;
            prev_slash = true;
        } else {
            prev_slash = false;
        }
        buf[out_len] = c;
        out_len += 1;
    }
    return buf[0..out_len];
}

fn matchGlobPatterns(patterns: []const []const u8, pathname: []const u8): bool {
    let has_positive_patterns = false;
    let matches_positive = false;

    for (pattern of patterns) {
        // Check if it's a negative pattern
        if (pattern.len > 0 && pattern[0] == '!') {
            // Negative pattern - if it matches, exclude the file
            const neg_pattern = pattern[1..];
            if (neg_pattern.len > 0 && bun.glob.match(neg_pattern, pathname).matches()) {
                return false;
            }
        } else {
            // Positive pattern - at least one must match
            has_positive_patterns = true;
            if (bun.glob.match(pattern, pathname).matches()) {
                matches_positive = true;
            }
        }
    }

    // If there are no positive patterns, include everything (that wasn't excluded)
    // If there are positive patterns, at least one must match
    return !has_positive_patterns || matches_positive;
}

fn extractToDiskFiltered(
    file_buffer: []const u8,
    root: []const u8,
    glob_patterns: PatternList?,
): !u32 {
    const lib = libarchive.lib;
    const archive_guard = ArchiveReader{ handle: lib.Archive.readNew() };
    const archive = archive_guard.handle;
    configureArchiveReader(archive);

    if (archive.readOpenMemory(file_buffer) != .ok) {
        return error.ReadError;
    }

    // Open/create target directory using bun.sys
    const cwd = bun.FD.cwd();
    cwd.makePath(u8, root) catch {};
    const dir_guard = FdGuard{ fd: brk: {
        if (std.fs.path.isAbsolute(root)) {
            break :brk bun.sys.openA(root, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch return error.OpenError;
        } else {
            break :brk bun.sys.openatA(cwd, root, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch return error.OpenError;
        }
    } };
    const dir_fd = dir_guard.fd;

    let count: u32 = 0;
    let use_pwrite = bun.Environment.isPosix;
    let use_lseek = true;
    let glob_norm_buf: [bun.MAX_PATH_BYTES]u8 = undefined;
    let entry_buf: [1]*lib.Archive.Entry = [undefined];
    while (true) {
        switch (archive.readNextHeader(unsafe { &entry_buf })) {
            .eof => break,
            .retry => continue,
            .failed | .fatal => return error.ReadError,
            .ok | .warn => {},
        }
        const entry = entry_buf[0];
        const pathname = entry.pathnameUtf8();

        // Validate path safety (reject absolute paths, path traversal, overlong paths)
        if (!isSafePath(pathname) || pathname.len >= bun.MAX_PATH_BYTES) continue;

        // Apply glob pattern filtering. Supports negative patterns with "!" prefix.
        // Positive patterns: at least one must match
        // Negative patterns: if any matches, the file is excluded
        if (patterns := glob_patterns) {
            const norm_path = normalizeArchiveGlobPath(pathname, *glob_norm_buf);
            if (!matchGlobPatterns(patterns, norm_path)) continue;
        }

        const filetype = entry.filetype();
        const kind = bun.sys.kindFromMode(filetype);

        switch (kind) {
            .directory => {
                dir_fd.makePath(u8, pathname) catch continue;
                count += 1;
            },
            .file => {
                const size: usize = @as(usize, @intCast(@max(entry.size(), 0)));
                // Sanitize permissions: use entry perms masked to 0o777, or default 0o644
                const entry_perm = entry.perm();
                const mode: bun.Mode = if (entry_perm != 0)
                    @as(bun.Mode, @intCast(entry_perm & 0o777))
                else
                    0o644;

                // Create parent directories if needed (ignore expected errors)
                if (parent_dir := std.fs.path.dirname(pathname)) {
                    dir_fd.makePath(u8, parent_dir) catch {};
                }

                // Create and write the file using bun.sys
                const file_fd: bun.FD = bun.sys.openat(
                    dir_fd,
                    pathname,
                    bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC,
                    mode,
                ).unwrap() catch continue;

                let write_success = true;
                if (size > 0) {
                    const fd_res = unsafe { archive.readDataIntoFd(file_fd, (&use_pwrite) as *bool, (&use_lseek) as *bool) };
                    switch (fd_res) {
                        .ok | .eof => {
                            switch (bun.sys.ftruncate(file_fd, @intCast(size))) {
                                .err => { write_success = false; },
                                .result => {},
                            }
                        },
                        else => {
                            * = file_fd.close();
                            * = dir_fd.unlinkat(pathname);
                            return error.ReadError;
                        },
                    }
                }
                * = file_fd.close();

                if (write_success) {
                    count += 1;
                } else {
                    // Remove partial file on failure
                    * = dir_fd.unlinkat(pathname);
                }
            },
            .sym_link => {
                const link_target = entry.symlink();
                // Validate symlink target is also safe
                if (!isSafePath(link_target)) continue;
                // Symlinks are only extracted on POSIX systems (Linux/macOS).
                // On Windows, symlinks are skipped since they require elevated privileges.
                if (bun.Environment.isPosix) {
                    bun.sys.symlinkat(link_target, dir_fd, pathname).unwrap() catch e => {
                        switch (e) {
                            error.EPERM, error.ENOENT => {
                                if (parent := std.fs.path.dirname(pathname)) {
                                    dir_fd.makePath(u8, parent) catch {};
                                }
                                * = bun.sys.symlinkat(link_target, dir_fd, pathname).unwrap() catch continue;
                            },
                            else => continue,
                        }
                    };
                    count += 1;
                }
            },
            else => {},
        }
    }

    const close_res = archive.readClose();
    if (close_res != .ok && close_res != .eof) {
        return error.ReadError;
    }

    return count;
}

union(enum) PromiseResult {
    resolve: jsc.JSValue,
    reject: jsc.JSValue,

    fn fulfill(globalThis: *jsc.JSGlobalObject, promise: *jsc.JSPromise): bun.JSTerminated!void {
        switch (_) {
            .resolve => |v| promise.resolve(globalThis, v)?,
            .reject => |v| promise.rejectWithAsyncStack(globalThis, v)?,
        }
    }
}

/// Generic async task that handles all the boilerplate for thread pool tasks.
/// Context must provide:
///   - `fn run(*Context) void` - runs on thread pool
///   - `fn runFromJS(*Context, *jsc.JSGlobalObject) PromiseResult` - returns value to resolve/reject
///   - `fn deinit(*Context) void` - cleanup
fn AsyncTask(comptime Context: type): type {
    return struct {
        const Self = @This();

        ctx: Context,
        promise: jsc.JSPromise.Strong,
        vm: *jsc.VirtualMachine,
        task: jsc.WorkPoolTask = .{ .callback = &run },
        concurrent_task: jsc.ConcurrentTask = .{},
        ref: bun.Async.KeepAlive = .{},

        static fn create(globalThis: *jsc.JSGlobalObject, ctx: Context): *Self {
            const vm = globalThis.bunVM();
            const created = bun.new(Self, .{
                .ctx = ctx,
                .promise = jsc.JSPromise.Strong.init(globalThis),
                .vm = vm,
                .task = .{ .callback = &run },
                .concurrent_task = .{},
                .ref = .{},
            });
            created.ref.ref(vm);
            return created;
        }

        fn schedule(*_) {
            jsc.WorkPool.schedule(&_.task);
        }

        static fn run(work_task: *jsc.WorkPoolTask) {
            const self_task: *Self = @fieldParentPtr("task", work_task);
            const result = Context.run(&self_task.ctx);
            self_task.ctx.result = if (@typeInfo(@TypeOf(result)) == .error_union)
                result catch e => .{ .err = e }
            else
                result;
            self_task.vm.enqueueTaskConcurrent(
                self_task.concurrent_task.from(self_task, .manual_deinit),
            );
        }

        pub fn runFromJS(*_): bun.JSTerminated!void {
            _.ref.unref(_.vm);

            defer {
                Context.deinit(&_.ctx);
                bun.destroy(_);
            }

            if (_.vm.isShuttingDown()) return;

            const globalThis = _.vm.global;
            const promise = _.promise.swap();
            const result = Context.runFromJS(&_.ctx, globalThis) catch e => {
                return promise.reject(globalThis, globalThis.takeException(e));
            };
            result.fulfill(globalThis, promise)?;
        }
    };
}

const ExtractReadErr = error{ReadError};

union(enum) ExtractResult {
    success: u32,
    @"err": ExtractReadErr,
}

struct ExtractContext {
    const Result = ExtractResult;

    store: *jsc.WebCore.Blob.Store,
    path: []const u8,
    glob_patterns: PatternList?,
    result: Result = Result{ @"err": error.ReadError },

    fn run(*_): Result {
        // If we have glob patterns, use filtered extraction
        if (_.glob_patterns != null) {
            const count = extractToDiskFiltered(
                _.store.sharedView(),
                _.path,
                _.glob_patterns,
            ) catch return Result{ @"err": error.ReadError };
            return { success: count };
        }

        // Otherwise use the fast path without filtering
        const count = libarchive.Archiver.extractToDisk(
            _.store.sharedView(),
            _.path,
            null,
            void,
            undefined,
            { depth_to_skip: 0, close_handles: true, log: false, npm: false },
        ) catch return ExtractContext.Result{ @"err": error.ReadError };
        return ExtractContext.Result{ success: count };
    }

    fn runFromJS(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!PromiseResult {
        return switch (_.result) {
            .success => |count| PromiseResult{ resolve: jsc.JSValue.jsNumber(count) },
            .err => |e| PromiseResult{ reject: globalThis.createErrorInstance("{s}", .{ @errorName(e) }) },
        };
    }

    fn deinit(*_) {
        _.store.deref();
        bun.default_allocator.free(_.path);
        if (patterns := _.glob_patterns) freePatterns(patterns);
    }
}

pub const ExtractTask = AsyncTask(ExtractContext);

fn startExtractTask(
    globalThis: *jsc.JSGlobalObject,
    store: *jsc.WebCore.Blob.Store,
    path: []const u8,
    glob_patterns: PatternList?,
): bun.JSError!jsc.JSValue {
    let path_copy = OwnedBytes{ slice: bun.default_allocator.dupe(u8, path)? };

    store.ref();
    let store_guard = StoreRefGuard{ store: store };

    const task = ExtractTask.create(globalThis, .{
        store: store,
        path: path_copy.slice,
        glob_patterns: glob_patterns,
    });
    * = path_copy.take();
    store_guard.armed = false;

    const promise_js = task.promise.value();
    task.schedule();
    return promise_js;
}

enum BlobOutputType { blob, bytes }

union(enum) BlobResult {
    compressed: []u8,
    uncompressed: void,
    @"err": anyerror,
}

struct BlobContext {
    const OutputType = BlobOutputType;
    const Err = error{ OutOfMemory, GzipInitFailed, GzipCompressFailed };
    const Result = BlobResult;

    store: *jsc.WebCore.Blob.Store,
    compress: Compression,
    output_type: OutputType,
    result: Result = .uncompressed,

    fn run(*_): Result {
        switch (_.compress) {
            .gzip => |opts| {
                return BlobContext.Result{ compressed: compressGzip(_.store.sharedView(), opts.level) catch e => return BlobContext.Result{ @"err": e } };
            },
            .none => return BlobContext.Result.uncompressed,
        }
    }

    fn runFromJS(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!PromiseResult {
        switch (_.result) {
            .err => |e| return PromiseResult{ reject: globalThis.createErrorInstance("{s}", .{ @errorName(e) }) },
            .compressed => |data| {
                _.result = .uncompressed; // Ownership transferred
                return PromiseResult{ resolve: switch (_.output_type) {
                    .blob => jsc.WebCore.Blob.new(jsc.WebCore.Blob.createWithBytesAndAllocator(data, bun.default_allocator, globalThis, false)).toJS(globalThis),
                    .bytes => jsc.JSValue.createBuffer(globalThis, data),
                } };
            },
            .uncompressed => return switch (_.output_type) {
                .blob => blk: {
                    _.store.ref();
                    break :blk PromiseResult{ resolve: jsc.WebCore.Blob.new(jsc.WebCore.Blob.initWithStore(_.store, globalThis)).toJS(globalThis) };
                },
                .bytes => blk: {
                    const duped = bun.default_allocator.dupe(u8, _.store.sharedView()) catch return PromiseResult{ reject: globalThis.createOutOfMemoryError() };
                    break :blk PromiseResult{ resolve: jsc.JSValue.createBuffer(globalThis, duped) };
                },
            },
        }
    }

    fn deinit(*_) {
        _.store.deref();
        if (_.result == .compressed) bun.default_allocator.free(_.result.compressed);
    }
}

pub const BlobTask = AsyncTask(BlobContext);

fn startBlobTask(globalThis: *jsc.JSGlobalObject, store: *jsc.WebCore.Blob.Store, compress: Compression, output_type: BlobContext.OutputType): bun.JSError!jsc.JSValue {
    store.ref();
    let store_guard = StoreRefGuard{ store: store };

    const task = BlobTask.create(globalThis, .{
        store: store,
        compress: compress,
        output_type: output_type,
    });
    store_guard.armed = false;

    const promise_js = task.promise.value();
    task.schedule();
    return promise_js;
}

union(enum) WriteResult {
    success: void,
    @"err": anyerror,
    sys_err: bun.sys.Error,
}

union(enum) WriteData {
    owned: []u8,
    store: *jsc.WebCore.Blob.Store,
}

struct WriteContext {
    const Err = error{ OutOfMemory, GzipInitFailed, GzipCompressFailed };
    const Result = WriteResult;
    const Data = WriteData;

    data: Data,
    path: [:0]const u8,
    compress: Compression,
    result: Result = .success,

    fn run(*_): Result {
        const source_data = switch (_.data) {
            .owned => |d| d,
            .store => |s| s.sharedView(),
        };
        let compressed_guard = OwnedBytes{};
        const data_to_write = switch (_.compress) {
            .gzip => |opts| blk: {
                const compressed = compressGzip(source_data, opts.level) catch e => return Result{ @"err": e };
                compressed_guard.slice = compressed;
                break :blk compressed;
            },
            .none => source_data,
        };

        const file_guard = SysFileGuard{ file: switch (bun.sys.File.openat(.cwd(), _.path, bun.O.CREAT | bun.O.WRONLY | bun.O.TRUNC, 0o644)) {
            .err => |e| return Result{ sys_err: e.clone(bun.default_allocator) },
            .result => |f| f,
        } };

        return switch (file_guard.file.writeAll(data_to_write)) {
            .err => |e| Result{ sys_err: e.clone(bun.default_allocator) },
            .result => Result.success,
        };
    }

    fn runFromJS(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!PromiseResult {
        return switch (_.result) {
            .success => PromiseResult{ resolve: .js_undefined },
            .err => |e| PromiseResult{ reject: globalThis.createErrorInstance("{s}", .{ @errorName(e) }) },
            .sys_err => |sys_err| PromiseResult{ reject: sys_err.toJS(globalThis)? },
        };
    }

    fn deinit(*_) {
        switch (_.data) {
            .owned => |d| bun.default_allocator.free(d),
            .store => |s| s.deref(),
        }
        bun.default_allocator.free(_.path);
        if (_.result == .sys_err) {
            let sys_err = _.result.sys_err;
            sys_err.deinit();
        }
    }
}

pub const WriteTask = AsyncTask(WriteContext);

struct WriteDataGuard {
    data: WriteContext.Data,
    armed: bool = true,
    ~init() {
        if (!_.armed) return;
        switch (_.data) {
            .store => |s| s.deref(),
            .owned => |d| bun.default_allocator.free(d),
        }
    }
}

fn startWriteTask(
    globalThis: *jsc.JSGlobalObject,
    data: WriteContext.Data,
    path: []const u8,
    compress: Compression,
): bun.JSError!jsc.JSValue {
    let path_z = OwnedBytesZ{ slice: bun.default_allocator.dupeZ(u8, path)? };

    // Ref store if using store reference
    if (data == .store) {
        data.store.ref();
    }
    let data_guard = WriteDataGuard{ data: data };

    const task = WriteTask.create(globalThis, .{
        data: data,
        path: path_z.slice,
        compress: compress,
    });
    * = path_z.take();
    data_guard.armed = false;

    const promise_js = task.promise.value();
    task.schedule();
    return promise_js;
}

struct FileEntryListGuard {
    list: FilesContext.FileEntryList = .empty,
    armed: bool = true,
    ~init() { if (_.armed) FilesContext.freeEntries(&_.list); }
    fn take(*_): FilesContext.FileEntryList {
        _.armed = false;
        return _.list;
    }
}

struct ArchiveFileEntry { path: []u8, data: []u8, mtime: i64 }

type ArchiveFileEntryList = std.ArrayList(ArchiveFileEntry);

const ArchiveFilesErr = error{ OutOfMemory, ReadError };

union(enum) ArchiveFilesResult {
    success: ArchiveFileEntryList,
    libarchive_err: [*:0]u8,
    @"err": ArchiveFilesErr,

    fn deinit(*_) {
        switch (_.*) {
            .libarchive_err => |s| bun.default_allocator.free(std.mem.span(s)),
            .success => |*list| FilesContext.freeEntries(list),
            .err => {},
        }
    }
}

struct FilesContext {
    const FileEntry = ArchiveFileEntry;
    const FileEntryList = ArchiveFileEntryList;
    const FilesErr = ArchiveFilesErr;
    const Result = ArchiveFilesResult;

    static fn freeEntries(list: *FileEntryList) {
        for (e of list.items) {
            bun.default_allocator.free(e.path);
            if (e.data.len > 0) bun.default_allocator.free(e.data);
        }
        list.deinit(bun.default_allocator);
    }

    store: *jsc.WebCore.Blob.Store,
    glob_patterns: PatternList?,
    result: Result = Result{ @"err": error.ReadError },

    static fn cloneErrorString(archive: *libarchive.lib.Archive): [*:0]u8? {
        const err_str = archive.errorString();
        if (err_str.len == 0) return null;
        return bun.default_allocator.dupeZ(u8, err_str) catch null;
    }

    fn run(*_): std.mem.Allocator.Error!Result {
        const lib = libarchive.lib;
        const archive_guard = ArchiveReader{ handle: lib.Archive.readNew() };
        const archive = archive_guard.handle;
        configureArchiveReader(archive);

        if (archive.readOpenMemory(_.store.sharedView()) != .ok) {
            if (e := cloneErrorString(archive)) {
                return Result{ libarchive_err: e };
            } else {
                return Result{ @"err": error.ReadError };
            }
        }

        let entries_guard = FileEntryListGuard{};

        let entry_buf: [1]*lib.Archive.Entry = [undefined];
    while (true) {
        switch (archive.readNextHeader(unsafe { &entry_buf })) {
            .eof => break,
            .retry => continue,
            .failed | .fatal => {
                if (e := cloneErrorString(archive)) {
                    return Result{ libarchive_err: e };
                } else {
                    return Result{ @"err": error.ReadError };
                }
            },
            .ok | .warn => {},
        }
        const entry = entry_buf[0];
            if (entry.filetype() != @intFromEnum(lib.FileType.regular)) continue;

            const pathname = entry.pathnameUtf8();
            // Apply glob pattern filtering (supports both positive and negative patterns)
            if (patterns := _.glob_patterns) {
                let glob_norm_buf: [bun.MAX_PATH_BYTES]u8 = undefined;
                const norm_path = normalizeArchiveGlobPath(pathname, *glob_norm_buf);
                if (!matchGlobPatterns(patterns, norm_path)) continue;
            }

            const size: usize = @as(usize, @intCast(@max(entry.size(), 0)));
            const mtime = entry.mtime();

            // Read data first before allocating path
            let data_guard = OwnedBytes{};
            if (size > 0) {
                data_guard.slice = bun.default_allocator.alloc(u8, size)?;
                let total_read: usize = 0;
                while (total_read < size) {
                    const read = archive.readData(data_guard.slice[total_read..]);
                    if (read < 0) {
                        if (e := cloneErrorString(archive)) {
                            return Result{ libarchive_err: e };
                        } else {
                            return Result{ @"err": error.ReadError };
                        }
                    }
                    if (read == 0) break;
                    total_read += @as(usize, @intCast(read));
                }
                if (total_read < size) {
                    @memset(data_guard.slice[total_read..], 0);
                }
            }

            let path_copy = OwnedBytes{ slice: bun.default_allocator.dupe(u8, pathname)? };

            entries_guard.list.append(bun.default_allocator, { path: path_copy.slice, data: data_guard.slice, mtime: mtime })?;
            * = path_copy.take();
            * = data_guard.take();
        }

        const close_res = archive.readClose();
        if (close_res != .ok && close_res != .eof) {
            if (e := cloneErrorString(archive)) {
                return Result{ libarchive_err: e };
            } else {
                return Result{ @"err": error.ReadError };
            }
        }

        return { success: entries_guard.take() };
    }

    fn runFromJS(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!PromiseResult {
        switch (_.result) {
            .success => |*entries| {
                const map = jsc.JSMap.create(globalThis);
                const map_ptr_opt = jsc.JSMap.fromJS(map);
                if (map_ptr_opt == null) {
                    return PromiseResult{ reject: globalThis.createErrorInstance("Failed to create Map", .{}) };
                }
                const map_ptr = map_ptr_opt.?;

                for (*entry of entries.items) {
                    const blob_ptr = jsc.WebCore.Blob.new(jsc.WebCore.Blob.createWithBytesAndAllocator(entry.data, bun.default_allocator, globalThis, false));
                    entry.data = []; // Ownership transferred
                    blob_ptr.is_jsdom_file = true;
                    blob_ptr.name = bun.String.cloneUTF8(entry.path);
                    blob_ptr.last_modified = @as(f64, @floatFromInt(entry.mtime)) * 1000.0;

                    map_ptr.set(globalThis, blob_ptr.name.toJS(globalThis)?, blob_ptr.toJS(globalThis))?;
                }

                return PromiseResult{ resolve: map };
            },
            .libarchive_err => |err_msg| return PromiseResult{ reject: globalThis.createErrorInstance("{s}", .{ err_msg }) },
            .err => |e| return PromiseResult{ reject: globalThis.createErrorInstance("{s}", .{ @errorName(e) }) },
        }
    }

    fn deinit(*_) {
        _.result.deinit();
        _.store.deref();
        if (patterns := _.glob_patterns) freePatterns(patterns);
    }
}

pub const FilesTask = AsyncTask(FilesContext);

fn startFilesTask(globalThis: *jsc.JSGlobalObject, store: *jsc.WebCore.Blob.Store, glob_patterns: PatternList?): bun.JSError!jsc.JSValue {
    store.ref();
    let store_guard = StoreRefGuard{ store: store };
    // Ownership: On error, caller's RAII guard frees glob_patterns.
    // On success, ownership transfers to FilesContext, which frees them in deinit().

    const task = FilesTask.create(globalThis, .{
        store: store,
        glob_patterns: glob_patterns,
    });
    store_guard.armed = false;

    const promise_js = task.promise.value();
    task.schedule();
    return promise_js;
}

/// Parse compression options from JS value
/// Returns .none if no compression specified, caller must handle defaults
fn parseCompressionOptions(globalThis: *jsc.JSGlobalObject, options_arg: jsc.JSValue): bun.JSError!Compression {
    // No options provided means no compression (caller handles defaults)
    if (options_arg.isUndefinedOrNull()) {
        return Compression.none;
    }

    if (!options_arg.isObject()) {
        return globalThis.throwInvalidArguments("Archive: options must be an object", .{});
    }

    // Check for compress option
    if (compress_val := options_arg.getTruthy(globalThis, "compress")?) {
        // compress must be "gzip"
        if (!compress_val.isString()) {
            return globalThis.throwInvalidArguments("Archive: compress option must be a string", .{});
        }

        const compress_guard = SliceGuard{ slice: compress_val.toSlice(globalThis, bun.default_allocator)? };

        if (!bun.strings.eqlComptime(compress_guard.slice.slice(), "gzip")) {
            return globalThis.throwInvalidArguments("Archive: compress option must be \"gzip\"", .{});
        }

        // Parse level option (1-12, default 6)
        let level: u8 = 6;
        if (level_val := options_arg.getTruthy(globalThis, "level")?) {
            if (!level_val.isNumber()) {
                return globalThis.throwInvalidArguments("Archive: level must be a number", .{});
            }
            const level_num = level_val.toInt64();
            if (level_num < 1 || level_num > 12) {
                return globalThis.throwInvalidArguments("Archive: level must be between 1 and 12", .{});
            }
            level = @as(u8, @intCast(level_num));
        }

        return { gzip: { level: level } };
    }

    // No compress option specified in options object means no compression
    return Compression.none;
}

fn createArchive(data: []u8, compress: Compression): *Archive {
    const store = jsc.WebCore.Blob.Store.init(data, bun.default_allocator);
    return bun.new(Archive, { store: store, compress: compress });
}

/// Shared helper that builds tarball bytes from a JS object
fn buildTarballFromObject(globalThis: *jsc.JSGlobalObject, obj: jsc.JSValue): bun.JSError![]u8 {
    const allocator = bun.default_allocator;
    const lib = libarchive.lib;

    const js_obj_opt = obj.getObject();
    if (js_obj_opt == null) {
        return globalThis.throwInvalidArguments("Expected an object", .{});
    }
    const js_obj = js_obj_opt.?;

    // Set up archive first
    let growing_buffer_guard = GrowingBufferGuard{ buf: lib.GrowingBuffer.init(allocator) };

    const archive_guard = ArchiveWriter{ handle: lib.Archive.writeNew() };
    const archive = archive_guard.handle;

    if (archive.writeSetFormatPaxRestricted() != .ok) {
        return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveFormatError", .{});
    }
    * = archive.writeSetOptions("hdrcharset=UTF-8");

    if (unsafe {
        lib.archive_write_open2(
            @ptrCast(archive),
            @ptrCast(&growing_buffer_guard.buf),
            &lib.GrowingBuffer.openCallback,
            &lib.GrowingBuffer.writeCallback,
            &lib.GrowingBuffer.closeCallback,
            null,
        )
    } != 0) {
        return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveOpenError", .{});
    }

    const entry_guard = ArchiveEntryGuard{ handle: lib.Archive.Entry.new() };
    const entry = entry_guard.handle;

    const now_secs: isize = i32(@divTrunc(std.time.milliTimestamp(), 1000));

    // Iterate over object properties and write directly to archive
    const PropIterator = jsc.JSPropertyIterator({
        skip_empty_name: true,
        include_value: true,
    });

    let iter_guard = DeinitGuard<PropIterator>{ val: PropIterator.init(globalThis, js_obj)? };

    while (key := iter_guard.val.next()?) {
        const value = iter_guard.val.value;
        if (value == .zero) continue;

        // Get the key as a null-terminated string
        const key_guard = SliceGuard{ slice: key.toUTF8(allocator) };
        const key_str_guard = OwnedBytesZ{ slice: allocator.dupeZ(u8, key_guard.slice.slice())? };

        // Get data - use view for Blob/ArrayBuffer, convert for strings
        const data_guard = SliceGuard{ slice: getEntryData(globalThis, value, allocator)? };

        // Write entry to archive
        const data = data_guard.slice.slice();
        * = entry.clear();
        entry.setPathnameUtf8(key_str_guard.slice);
        entry.setSize(i32(data.len));
        entry.setFiletype(@intFromEnum(lib.FileType.regular));
        entry.setPerm(0o644);
        entry.setMtime(now_secs, 0);

        if (archive.writeHeader(entry) != .ok) {
            return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveHeaderError", .{});
        }
        if (archive.writeData(data) < 0) {
            return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveWriteError", .{});
        }
        if (archive.writeFinishEntry() != .ok) {
            return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveFinishEntryError", .{});
        }
    }

    if (archive.writeClose() != .ok) {
        return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveCloseError", .{});
    }

    const owned = growing_buffer_guard.buf.toOwnedSlice() catch return globalThis.throwInvalidArguments("Failed to create tarball: OutOfMemory", .{});
    growing_buffer_guard.armed = false;
    return owned;
}

fn getEntryData(globalThis: *jsc.JSGlobalObject, value: jsc.JSValue, allocator: std.mem.Allocator): bun.JSError!jsc.ZigString.Slice {
    // For Blob, use sharedView (no copy needed)
    if (blob_ptr := value.@"as"(jsc.WebCore.Blob)) {
        return jsc.ZigString.Slice.fromUTF8NeverFree(blob_ptr.sharedView());
    }

    // For ArrayBuffer/TypedArray, use view (no copy needed)
    if (array_buffer := value.asArrayBuffer(globalThis)) {
        return jsc.ZigString.Slice.fromUTF8NeverFree(array_buffer.slice());
    }

    // For strings, convert (allocates)
    return value.toSlice(globalThis, allocator);
}

/// Parse a string or array of strings into a pattern list.
/// Returns null for empty strings or empty arrays (treated as "no filter").
fn parsePatternArg(globalThis: *jsc.JSGlobalObject, arg: jsc.JSValue, api_name: []const u8, name: []const u8): bun.JSError!PatternList? {
    const allocator = bun.default_allocator;

    // Single string
    if (arg.isString()) {
        const str_guard = SliceGuard{ slice: arg.toSlice(globalThis, allocator)? };
        // Empty string = no filter
        if (str_guard.slice.len == 0) return null;
        let pattern = OwnedBytes{ slice: allocator.dupe(u8, str_guard.slice.slice()) catch return error.OutOfMemory };
        let patterns = allocator.alloc(string, 1) catch return error.OutOfMemory;
        patterns[0] = pattern.take();
        return patterns;
    }

    // Array of strings
    if (arg.jsType() == .Array) {
        const len = arg.getLength(globalThis)?;
        // Empty array = no filter
        if (len == 0) return null;

        let patterns_guard = PatternListGuard{ list: std.ArrayList(string).initCapacity(allocator, @as(usize, @intCast(len))) catch return error.OutOfMemory };

        // Use index-based iteration for safety (avoids issues if array mutates)
        let i: u32 = 0;
        while (i < len) : (i += 1) {
            const item = arg.getIndex(globalThis, i)?;
            if (!item.isString()) {
                return globalThis.throwInvalidArguments("{s}: {s} array must contain only strings", .{ api_name, name });
            }
            const str_guard = SliceGuard{ slice: item.toSlice(globalThis, allocator)? };
            // Skip empty strings in array
            if (str_guard.slice.len == 0) continue;
            const pattern = allocator.dupe(u8, str_guard.slice.slice()) catch return error.OutOfMemory;
            patterns_guard.list.appendAssumeCapacity(pattern);
        }

        // If all strings were empty, treat as no filter
        if (patterns_guard.list.items.len == 0) {
            return null;
        }

        const slice = patterns_guard.list.toOwnedSlice(allocator) catch return error.OutOfMemory;
        patterns_guard.armed = false;
        return slice;
    }

    return globalThis.throwInvalidArguments("{s}: {s} must be a string or array of strings", .{ api_name, name });
}

@root struct Archive {
    pub const js = jsc.Codegen.JSArchive;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    store: *jsc.WebCore.Blob.Store,
    compress: Compression = .none,

    static fn finalize(client: *Archive) {
    jsc.markBinding(@src());
    client.store.deref();
    bun.destroy(client);
}

    /// Pretty-print for console.log
static fn writeFormat(client: *const Archive, comptime Formatter: type, formatter: *Formatter, writer: auto, comptime enable_ansi_colors: bool)! {
    const Writer = @TypeOf(writer);
    const Output = bun.Output;
    const data = client.store.sharedView();

    writer.print(comptime Output.prettyFmt("Archive ({f}) {{\n", enable_ansi_colors), .{ bun.fmt.size(data.len, .{}) })?;

    {
        formatter.indent += 1;
        defer if (formatter.indent > 0) formatter.indent -= 1;

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(comptime Output.prettyFmt("<r>files<d>:<r> ", enable_ansi_colors))?;
        formatter.printAs(.Double, Writer, writer, jsc.JSValue.jsNumber(countFilesInArchive(data)), .NumberObject, enable_ansi_colors)?;
    }
    writer.writeAll("\n")?;
    formatter.writeIndent(Writer, writer)?;
    writer.writeAll("}")?;
    formatter.resetLine();
}

    /// Constructor: new Archive(data, options?)
/// Creates an Archive from either:
/// - An object { [path: string]: Blob | string | ArrayBufferView | ArrayBufferLike }
/// - A Blob, ArrayBufferView, or ArrayBufferLike (assumes it's already a valid archive)
/// Options:
/// - compress: "gzip" - Enable gzip compression
/// - level: number (1-12) - Compression level (default 6)
/// When no options are provided, no compression is applied
static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*Archive {
    const args2 = callframe.argumentsAsArray(2);
    const data_arg = args2[0];
    const options_arg = args2[1];
    if (data_arg == .zero) {
        return globalThis.throwInvalidArguments("new Archive() requires an argument", .{});
    }

    // Parse compression options
    const compress = parseCompressionOptions(globalThis, options_arg)?;

    // For Blob/Archive, ref the existing store (zero-copy)
    if (blob_ptr := data_arg.@"as"(jsc.WebCore.Blob)) {
        if (store := blob_ptr.store) {
            store.ref();
            return bun.new(Archive, { store: store, compress: compress });
        }
    }

    // For ArrayBuffer/TypedArray, copy the data
    if (array_buffer := data_arg.asArrayBuffer(globalThis)) {
        const store = jsc.WebCore.Blob.Store.init(bun.default_allocator.dupe(u8, array_buffer.slice())?, bun.default_allocator);
        return bun.new(Archive, { store: store, compress: compress });
    }

    // For plain objects, build a tarball
    if (data_arg.isObject()) {
        const store = jsc.WebCore.Blob.Store.init(buildTarballFromObject(globalThis, data_arg)?, bun.default_allocator);
        return bun.new(Archive, { store: store, compress: compress });
    }

    return globalThis.throwInvalidArguments("Expected an object, Blob, TypedArray, or ArrayBuffer", .{});
}

    /// Static method: Archive.write(path, data, options?)
/// Creates and writes an archive to disk in one operation.
/// For Archive instances, uses the archive's compression settings unless overridden by options.
/// Options:
///   - gzip: { level?: number } - Override compression settings
static fn write(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args3 = callframe.argumentsAsArray(3);
    const path_arg = args3[0];
    const data_arg = args3[1];
    const options_arg = args3[2];
    if (data_arg == .zero) {
        return globalThis.throwInvalidArguments("Archive.write requires 2 arguments (path, data)", .{});
    }

    // Get the path
    if (!path_arg.isString()) {
        return globalThis.throwInvalidArguments("Archive.write: first argument must be a string path", .{});
    }

    const path_guard = SliceGuard{ slice: path_arg.toSlice(globalThis, bun.default_allocator)? };

    // Parse options for compression override
    const options_compress = parseCompressionOptions(globalThis, options_arg)?;

    // For Archive instances, use options override or archive's compression settings
    if (archive := fromJS(data_arg)) {
        const compress = if (options_compress != .none) options_compress else archive.compress;
        return startWriteTask(globalThis, { store: archive.store }, path_guard.slice.slice(), compress);
    }

    // For Blobs, use store reference with options compression
    if (blob_ptr := data_arg.@"as"(jsc.WebCore.Blob)) {
        if (store := blob_ptr.store) {
            return startWriteTask(globalThis, { store: store }, path_guard.slice.slice(), options_compress);
        }
    }

    // For ArrayBuffer/TypedArray, copy the data with options compression
    if (array_buffer := data_arg.asArrayBuffer(globalThis)) {
        const data = bun.default_allocator.dupe(u8, array_buffer.slice())?;
        return startWriteTask(globalThis, { owned: data }, path_guard.slice.slice(), options_compress);
    }

    // For plain objects, build a tarball with options compression
    if (data_arg.isObject()) {
        const data = buildTarballFromObject(globalThis, data_arg)?;
        return startWriteTask(globalThis, { owned: data }, path_guard.slice.slice(), options_compress);
    }

    return globalThis.throwInvalidArguments("Expected an object, Blob, TypedArray, ArrayBuffer, or Archive", .{});
}

    /// Instance method: archive.extract(path, options?)
/// Extracts the archive to the given path
/// Options:
///   - glob: string | string[] - Only extract files matching the glob pattern(s). Supports negative patterns with "!".
/// Returns Promise<number> with count of extracted files
static fn extract(client: *Archive, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args2 = callframe.argumentsAsArray(2);
    const path_arg = args2[0];
    const options_arg = args2[1];
    if (path_arg == .zero || !path_arg.isString()) {
        return globalThis.throwInvalidArguments("Archive.extract requires a path argument", .{});
    }

    const path_guard = SliceGuard{ slice: path_arg.toSlice(globalThis, bun.default_allocator)? };

    // Parse options
    let glob_guard = PatternOptGuard{};

    if (!options_arg.isUndefinedOrNull()) {
        if (!options_arg.isObject()) {
            return globalThis.throwInvalidArguments("Archive.extract: second argument must be an options object", .{});
        }

        // Parse glob option
        if (glob_val := options_arg.getTruthy(globalThis, "glob")?) {
            glob_guard.patterns = parsePatternArg(globalThis, glob_val, "Archive.extract", "glob")?;
        }
    }

    const res = startExtractTask(globalThis, client.store, path_guard.slice.slice(), glob_guard.patterns)?;
    glob_guard.armed = false;
    return res;
}

    /// Instance method: archive.blob()
/// Returns Promise<Blob> with the archive data (compressed if gzip was set in options)
static fn blob(client: *Archive, globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return startBlobTask(globalThis, client.store, client.compress, .blob);
}

    /// Instance method: archive.bytes()
/// Returns Promise<Uint8Array> with the archive data (compressed if gzip was set in options)
static fn bytes(client: *Archive, globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return startBlobTask(globalThis, client.store, client.compress, .bytes);
}

    /// Instance method: archive.files(glob?)
/// Returns Promise<Map<string, File>> with archive file contents
static fn files(client: *Archive, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const glob_arg = callframe.argument(0);

    let glob_guard = PatternOptGuard{};

    if (!glob_arg.isUndefinedOrNull()) {
        glob_guard.patterns = parsePatternArg(globalThis, glob_arg, "Archive.files", "glob")?;
    }

    const res = startFilesTask(globalThis, client.store, glob_guard.patterns)?;
    glob_guard.armed = false;
    return res;
}
}
