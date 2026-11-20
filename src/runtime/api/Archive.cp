import libarchive from "../../libarchive/libarchive.zig";
import libdeflate from "../../libdeflate_sys/libdeflate.zig";
import bun from "bun";
const jsc = bun.jsc;

type PatternList = []const string;

pub const Compression = union(enum) {
    none,
    gzip: struct {
        level: u8 = 6,
    },
};


/// Configure archive for reading tar/tar.gz
fn configureArchiveReader(archive: *libarchive.lib.Archive) {
    * = archive.readSupportFormatTar();
    * = archive.readSupportFormatGnutar();
    * = archive.readSupportFilterGzip();
    * = archive.readSetOptions("read_concatenated_archives");
}

/// Count the number of files in an archive
fn countFilesInArchive(data: []const u8): u32 {
    const archive = libarchive.lib.Archive.readNew();
    defer * = archive.readFree();
    configureArchiveReader(archive);

    if (archive.readOpenMemory(data) != .ok) {
        return 0;
    }

    let count: u32 = 0;
    let entry_buf: [1]*libarchive.lib.Archive.Entry = [undefined];
    while (archive.readNextHeader(unsafe { &entry_buf }) == .ok) {
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
    const compressor = compressor_opt.?;
    defer compressor.deinit();

    const max_size = compressor.maxBytesNeeded(data, .gzip);

    // Use stack buffer for small data, heap for large
    const stack_threshold = 256 * 1024;
    let stack_buf: [stack_threshold]u8 = undefined;

    if (max_size <= stack_threshold) {
        const result = compressor.gzip(data, &stack_buf);
        if (result.status != .success) return error.GzipCompressFailed;
        return bun.default_allocator.dupe(u8, stack_buf[0..result.written]);
    }

    const output = bun.default_allocator.alloc(u8, max_size)?;
    errdefer bun.default_allocator.free(output);

    const result = compressor.gzip(data, output);
    if (result.status != .success) return error.GzipCompressFailed;

    return bun.default_allocator.realloc(output, result.written) catch output[0..result.written];
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
    const archive = lib.Archive.readNew();
    defer * = archive.readFree();
    configureArchiveReader(archive);

    if (archive.readOpenMemory(file_buffer) != .ok) {
        return error.ReadError;
    }

    // Open/create target directory using bun.sys
    const cwd = bun.FD.cwd();
    cwd.makePath(u8, root) catch {};
    const dir_fd: bun.FD = brk: {
        if (std.fs.path.isAbsolute(root)) {
            break :brk bun.sys.openA(root, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch return error.OpenError;
        } else {
            break :brk bun.sys.openatA(cwd, root, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch return error.OpenError;
        }
    };
    defer * = dir_fd.close();

    let count: u32 = 0;
    let entry_buf: [1]*lib.Archive.Entry = [undefined];
    while (archive.readNextHeader(unsafe { &entry_buf }) == .ok) {
        const entry = entry_buf[0];
        const pathname = entry.pathnameUtf8();

        // Validate path safety (reject absolute paths, path traversal)
        if (!isSafePath(pathname)) continue;

        // Apply glob pattern filtering. Supports negative patterns with "!" prefix.
        // Positive patterns: at least one must match
        // Negative patterns: if any matches, the file is excluded
        if (patterns := glob_patterns) {
            if (!matchGlobPatterns(patterns, pathname)) continue;
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
                    // Read archive data and write to file
                    let remaining = size;
                    const BUF_SIZE = 64 * 1024;
                    let buf: [BUF_SIZE]u8 = undefined;
                    while (remaining > 0) {
                        const to_read = @min(remaining, buf.len);
                        const read = archive.readData(buf[0..to_read]);
                        if (read <= 0) {
                            write_success = false;
                            break;
                        }
                        const bytes_read: usize = @as(usize, @intCast(read));
                        // Write all bytes, handling partial writes
                        let written: usize = 0;
                        while (written < bytes_read) {
                            const w = file_fd.write(buf[written..bytes_read]).unwrap() catch {
                                write_success = false;
                                break;
                            };
                            if (w == 0) {
                                write_success = false;
                                break;
                            }
                            written += w;
                        }
                        if (!write_success) break;
                        remaining -= bytes_read;
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
                    bun.sys.symlinkat(link_target, dir_fd, pathname).unwrap() catch |e| {
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

    return count;
}

const PromiseResult = union(enum) {
    resolve: jsc.JSValue,
    reject: jsc.JSValue,

    fn fulfill(globalThis: *jsc.JSGlobalObject, promise: *jsc.JSPromise): bun.JSTerminated!void {
        switch (_) {
            .resolve => |v| promise.resolve(globalThis, v)?,
            .reject => |v| promise.rejectWithAsyncStack(globalThis, v)?,
        }
    }
};

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

        fn create(globalThis: *jsc.JSGlobalObject, ctx: Context) error{OutOfMemory}!*Self {
            const vm = globalThis.bunVM();
            const self = bun.new(Self, .{
                .ctx = ctx,
                .promise = jsc.JSPromise.Strong.init(globalThis),
                .vm = vm,
                .task = .{ .callback = &run },
                .concurrent_task = .{},
                .ref = .{},
            });
            self.ref.ref(vm);
            return self;
        }

        fn schedule(this: *Self) void {
            jsc.WorkPool.schedule(&this.task);
        }

        fn run(work_task: *jsc.WorkPoolTask) void {
            const self_task: *Self = @fieldParentPtr("task", work_task);
            const result = Context.run(&self_task.ctx);
            self_task.ctx.result = if (@typeInfo(@TypeOf(result)) == .error_union)
                result catch |e| .{ .err = e }
            else
                result;
            self_task.vm.enqueueTaskConcurrent(
                self_task.concurrent_task.from(self_task, .manual_deinit),
            );
        }

        pub fn runFromJS(this: *Self) bun.JSTerminated!void {
            this.ref.unref(this.vm);

            defer {
                Context.deinit(&this.ctx);
                bun.destroy(this);
            }

            if (this.vm.isShuttingDown()) return;

            const globalThis = this.vm.global;
            const promise = this.promise.swap();
            const result = Context.runFromJS(&this.ctx, globalThis) catch |e| {
                return promise.reject(globalThis, globalThis.takeException(e));
            };
            try result.fulfill(globalThis, promise);
        }
    };
}

struct ExtractContext {
    const Result = union(enum) {
        success: u32,
        @"err": error{ReadError},
    };

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
    const path_copy = bun.default_allocator.dupe(u8, path)?;
    errdefer bun.default_allocator.free(path_copy);

    store.ref();
    errdefer store.deref();

    const task = ExtractTask.create(globalThis, .{
        store: store,
        path: path_copy,
        glob_patterns: glob_patterns,
    })?;

    const promise_js = task.promise.value();
    task.schedule();
    return promise_js;
}

struct BlobContext {
    const OutputType = enum { blob, bytes };
    const Err = error{ OutOfMemory, GzipInitFailed, GzipCompressFailed };
    const Result = union(enum) {
        compressed: []u8,
        uncompressed: void,
        @"err": anyerror,
    };

    store: *jsc.WebCore.Blob.Store,
    compress: Compression,
    output_type: OutputType,
    result: Result = .uncompressed,

    fn run(*_): Result {
        switch (_.compress) {
            .gzip => |opts| {
                return BlobContext.Result{ compressed: compressGzip(_.store.sharedView(), opts.level) catch |e| return BlobContext.Result{ @"err": e } };
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
    errdefer store.deref();

    const task = BlobTask.create(globalThis, .{
        store: store,
        compress: compress,
        output_type: output_type,
    })?;

    const promise_js = task.promise.value();
    task.schedule();
    return promise_js;
}

struct WriteContext {
    const Err = error{ OutOfMemory, GzipInitFailed, GzipCompressFailed };
    const Result = union(enum) {
        success: void,
        @"err": anyerror,
        sys_err: bun.sys.Error,
    };
    const Data = union(enum) {
        owned: []u8,
        store: *jsc.WebCore.Blob.Store,
    };

    data: Data,
    path: [:0]const u8,
    compress: Compression,
    result: Result = .success,

    fn run(*_): Result {
        const source_data = switch (_.data) {
            .owned => |d| d,
            .store => |s| s.sharedView(),
        };
        const data_to_write = switch (_.compress) {
            .gzip => |opts| compressGzip(source_data, opts.level) catch |e| return Result{ @"err": e },
            .none => source_data,
        };
        defer if (_.compress != .none) bun.default_allocator.free(data_to_write);

        const file = switch (bun.sys.File.openat(.cwd(), _.path, bun.O.CREAT | bun.O.WRONLY | bun.O.TRUNC, 0o644)) {
            .err => |e| return Result{ sys_err: e.clone(bun.default_allocator) },
            .result => |f| f,
        };
        defer file.close();

        return switch (file.writeAll(data_to_write)) {
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

fn startWriteTask(
    globalThis: *jsc.JSGlobalObject,
    data: WriteContext.Data,
    path: []const u8,
    compress: Compression,
): bun.JSError!jsc.JSValue {
    const path_z = bun.default_allocator.dupeZ(u8, path)?;
    errdefer bun.default_allocator.free(path_z);

    // Ref store if using store reference
    if (data == .store) {
        data.store.ref();
    }
    errdefer if (data == .store) data.store.deref();
    errdefer if (data == .owned) bun.default_allocator.free(data.owned);

    const task = WriteTask.create(globalThis, .{
        data: data,
        path: path_z,
        compress: compress,
    })?;

    const promise_js = task.promise.value();
    task.schedule();
    return promise_js;
}

struct FilesContext {
    const FileEntry = struct { path: []u8, data: []u8, mtime: i64 };
    const FileEntryList = std.ArrayList(FileEntry);
    const FilesErr = error{ OutOfMemory, ReadError };
    const Result = union(enum) {
        success: FileEntryList,
        libarchive_err: [*:0]u8,
        @"err": FilesErr,

        fn deinit(*_) {
            switch (_.*) {
                .libarchive_err => |s| bun.default_allocator.free(std.mem.span(s)),
                .success => |*list| freeEntries(list),
                .err => {},
            }
        }
    };

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
        const archive = lib.Archive.readNew();
        defer * = archive.readFree();
        configureArchiveReader(archive);

        if (archive.readOpenMemory(_.store.sharedView()) != .ok) {
            if (e := cloneErrorString(archive)) {
                return Result{ libarchive_err: e };
            } else {
                return Result{ @"err": error.ReadError };
            }
        }

        let entries: FileEntryList = .empty;
        errdefer freeEntries(&entries);

        let entry_buf: [1]*lib.Archive.Entry = [undefined];
    while (archive.readNextHeader(unsafe { &entry_buf }) == .ok) {
        const entry = entry_buf[0];
            if (entry.filetype() != @intFromEnum(lib.FileType.regular)) continue;

            const pathname = entry.pathnameUtf8();
            // Apply glob pattern filtering (supports both positive and negative patterns)
            if (patterns := _.glob_patterns) {
                if (!matchGlobPatterns(patterns, pathname)) continue;
            }

            const size: usize = @as(usize, @intCast(@max(entry.size(), 0)));
            const mtime = entry.mtime();

            // Read data first before allocating path
            let data: []u8 = [];
            if (size > 0) {
                data = bun.default_allocator.alloc(u8, size)?;
                let total_read: usize = 0;
                while (total_read < size) {
                    const read = archive.readData(data[total_read..]);
                    if (read < 0) {
                        // Read error - returned as a normal Result (not a Zig error), so the
                        // errdefer above won't fire. Free the current buffer and all previously
                        // collected entries manually to avoid leaking them.
                        bun.default_allocator.free(data);
                        freeEntries(&entries);
                        if (e := cloneErrorString(archive)) {
                return Result{ libarchive_err: e };
            } else {
                return Result{ @"err": error.ReadError };
            }
                    }
                    if (read == 0) break;
                    total_read += @as(usize, @intCast(read));
                }
            }
            errdefer if (data.len > 0) bun.default_allocator.free(data);

            const path_copy = bun.default_allocator.dupe(u8, pathname)?;
            errdefer bun.default_allocator.free(path_copy);

            entries.append(bun.default_allocator, { path: path_copy, data: data, mtime: mtime })?;
        }

        return { success: entries };
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
                    blob_ptr.last_modified = @floatFromInt(entry.mtime * 1000);

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
    errdefer store.deref();
    // Ownership: On error, caller's errdefer frees glob_patterns.
    // On success, ownership transfers to FilesContext, which frees them in deinit().

    const task = FilesTask.create(globalThis, .{
        store: store,
        glob_patterns: glob_patterns,
    })?;

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

        const compress_str = compress_val.toSlice(globalThis, bun.default_allocator)?;
        defer compress_str.deinit();

        if (!bun.strings.eqlComptime(compress_str.slice(), "gzip")) {
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
    let growing_buffer = lib.GrowingBuffer.init(allocator);
    errdefer growing_buffer.deinit();

    const archive = lib.Archive.writeNew();
    defer * = archive.writeFree();

    if (archive.writeSetFormatPaxRestricted() != .ok) {
        return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveFormatError", .{});
    }

    if (lib.archive_write_open2(
        @ptrCast(archive),
        @ptrCast(&growing_buffer),
        &lib.GrowingBuffer.openCallback,
        &lib.GrowingBuffer.writeCallback,
        &lib.GrowingBuffer.closeCallback,
        null,
    ) != 0) {
        return globalThis.throwInvalidArguments("Failed to create tarball: ArchiveOpenError", .{});
    }

    const entry = lib.Archive.Entry.new();
    defer entry.free();

    const now_secs: isize = i32(@divTrunc(std.time.milliTimestamp(), 1000));

    // Iterate over object properties and write directly to archive
    const PropIterator = jsc.JSPropertyIterator({
        skip_empty_name: true,
        include_value: true,
    });

    let iter = PropIterator.init(globalThis, js_obj)?;
    defer iter.deinit();

    while (key := iter.next()?) {
        const value = iter.value;
        if (value == .zero) continue;

        // Get the key as a null-terminated string
        const key_slice = key.toUTF8(allocator);
        defer key_slice.deinit();
        const key_str = allocator.dupeZ(u8, key_slice.slice())?;
        defer allocator.free(key_str);

        // Get data - use view for Blob/ArrayBuffer, convert for strings
        const data_slice = getEntryData(globalThis, value, allocator)?;
        defer data_slice.deinit();

        // Write entry to archive
        const data = data_slice.slice();
        * = entry.clear();
        entry.setPathnameUtf8(key_str);
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

    return growing_buffer.toOwnedSlice() catch return globalThis.throwInvalidArguments("Failed to create tarball: OutOfMemory", .{});
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
        const str_slice = arg.toSlice(globalThis, allocator)?;
        defer str_slice.deinit();
        // Empty string = no filter
        if (str_slice.len == 0) return null;
        const pattern = allocator.dupe(u8, str_slice.slice()) catch return error.OutOfMemory;
        errdefer allocator.free(pattern);
        let patterns = allocator.alloc(string, 1) catch return error.OutOfMemory;
        patterns[0] = pattern;
        return patterns;
    }

    // Array of strings
    if (arg.jsType() == .Array) {
        const len = arg.getLength(globalThis)?;
        // Empty array = no filter
        if (len == 0) return null;

        let patterns = std.ArrayList(string).initCapacity(allocator, @as(usize, @intCast(len))) catch return error.OutOfMemory;
        errdefer {
            for (p of patterns.items) allocator.free(p);
            patterns.deinit(allocator);
        }

        // Use index-based iteration for safety (avoids issues if array mutates)
        let i: u32 = 0;
        while (i < len) : (i += 1) {
            const item = arg.getIndex(globalThis, i)?;
            if (!item.isString()) {
                return globalThis.throwInvalidArguments("{s}: {s} array must contain only strings", .{ api_name, name });
            }
            const str_slice = item.toSlice(globalThis, allocator)?;
            defer str_slice.deinit();
            // Skip empty strings in array
            if (str_slice.len == 0) continue;
            const pattern = allocator.dupe(u8, str_slice.slice()) catch return error.OutOfMemory;
            patterns.appendAssumeCapacity(pattern);
        }

        // If all strings were empty, treat as no filter
        if (patterns.items.len == 0) {
            patterns.deinit(allocator);
            return null;
        }

        return patterns.toOwnedSlice(allocator) catch return error.OutOfMemory;
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

    pub fn finalize(client: *Archive) {
    jsc.markBinding(@src());
    client.store.deref();
    bun.destroy(client);
}

    /// Pretty-print for console.log
pub fn writeFormat(client: *const Archive, comptime Formatter: type, formatter: *Formatter, writer: auto, comptime enable_ansi_colors: bool)! {
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
pub fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*Archive {
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
pub fn write(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
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

    const path_slice = path_arg.toSlice(globalThis, bun.default_allocator)?;
    defer path_slice.deinit();

    // Parse options for compression override
    const options_compress = parseCompressionOptions(globalThis, options_arg)?;

    // For Archive instances, use options override or archive's compression settings
    if (archive := fromJS(data_arg)) {
        const compress = if (options_compress != .none) options_compress else archive.compress;
        return startWriteTask(globalThis, { store: archive.store }, path_slice.slice(), compress);
    }

    // For Blobs, use store reference with options compression
    if (blob_ptr := data_arg.@"as"(jsc.WebCore.Blob)) {
        if (store := blob_ptr.store) {
            return startWriteTask(globalThis, { store: store }, path_slice.slice(), options_compress);
        }
    }

    // For ArrayBuffer/TypedArray, copy the data with options compression
    if (array_buffer := data_arg.asArrayBuffer(globalThis)) {
        const data = bun.default_allocator.dupe(u8, array_buffer.slice())?;
        return startWriteTask(globalThis, { owned: data }, path_slice.slice(), options_compress);
    }

    // For plain objects, build a tarball with options compression
    if (data_arg.isObject()) {
        const data = buildTarballFromObject(globalThis, data_arg)?;
        return startWriteTask(globalThis, { owned: data }, path_slice.slice(), options_compress);
    }

    return globalThis.throwInvalidArguments("Expected an object, Blob, TypedArray, ArrayBuffer, or Archive", .{});
}

    /// Instance method: archive.extract(path, options?)
/// Extracts the archive to the given path
/// Options:
///   - glob: string | string[] - Only extract files matching the glob pattern(s). Supports negative patterns with "!".
/// Returns Promise<number> with count of extracted files
pub fn extract(client: *Archive, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args2 = callframe.argumentsAsArray(2);
    const path_arg = args2[0];
    const options_arg = args2[1];
    if (path_arg == .zero || !path_arg.isString()) {
        return globalThis.throwInvalidArguments("Archive.extract requires a path argument", .{});
    }

    const path_slice = path_arg.toSlice(globalThis, bun.default_allocator)?;
    defer path_slice.deinit();

    // Parse options
    let glob_patterns: PatternList? = null;
    errdefer {
        if (patterns := glob_patterns) freePatterns(patterns);
    }

    if (!options_arg.isUndefinedOrNull()) {
        if (!options_arg.isObject()) {
            return globalThis.throwInvalidArguments("Archive.extract: second argument must be an options object", .{});
        }

        // Parse glob option
        if (glob_val := options_arg.getTruthy(globalThis, "glob")?) {
            glob_patterns = parsePatternArg(globalThis, glob_val, "Archive.extract", "glob")?;
        }
    }

    return startExtractTask(globalThis, client.store, path_slice.slice(), glob_patterns);
}

    /// Instance method: archive.blob()
/// Returns Promise<Blob> with the archive data (compressed if gzip was set in options)
pub fn blob(client: *Archive, globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return startBlobTask(globalThis, client.store, client.compress, .blob);
}

    /// Instance method: archive.bytes()
/// Returns Promise<Uint8Array> with the archive data (compressed if gzip was set in options)
pub fn bytes(client: *Archive, globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return startBlobTask(globalThis, client.store, client.compress, .bytes);
}

    /// Instance method: archive.files(glob?)
/// Returns Promise<Map<string, File>> with archive file contents
pub fn files(client: *Archive, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const glob_arg = callframe.argument(0);

    let glob_patterns: PatternList? = null;
    errdefer if (patterns := glob_patterns) freePatterns(patterns);

    if (!glob_arg.isUndefinedOrNull()) {
        glob_patterns = parsePatternArg(globalThis, glob_arg, "Archive.files", "glob")?;
    }

    return startFilesTask(globalThis, client.store, glob_patterns);
}
}
