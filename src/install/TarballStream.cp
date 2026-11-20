import install from "./install.zig";
import bun from "bun";
import integrity_mod from "./integrity.zig";

type string = []const u8;

enum Phase {
    want_header,
    want_data,
    done,
}

@root struct TarballStream {
//! Resumable, non-blocking tarball extractor for `bun install`.
//!
//! The HTTP thread hands each body chunk to `onChunk`, which appends to a
//! small pending buffer and (if not already running) schedules
//! `drain_task` on `PackageManager.thread_pool`. The drain task calls into
//! libarchive to gunzip and untar whatever is available, writing files as
//! their data arrives, until libarchive asks for more compressed bytes
//! than are currently buffered. At that point the read callback returns
//! `ARCHIVE_RETRY`, libarchive propagates it (see the BUN PATCHes in
//! `vendor/libarchive`), and the drain task returns — the worker is
//! released. The next HTTP chunk reschedules the drain task, which calls
//! back into libarchive and resumes exactly where it left off because the
//! `struct archive *`, the gzip inflate state, the partially-read tar
//! header and the open output `bun.FD` all live on the heap in this
//! struct.
//!
//! This lets `bun install` overlap download and extraction on the normal
//! resolve thread pool without ever parking a worker on a condvar, and
//! without holding the full compressed or decompressed tarball in memory.


// ---------------------------------------------------------------------
// Cross-thread producer state (HTTP → worker)
// ---------------------------------------------------------------------

mutex: Mutex = {},

/// Compressed .tgz bytes that have arrived from the HTTP thread but have
/// not yet been consumed by libarchive.
pending: std.ArrayListUnmanaged(u8) = {},

/// True once the HTTP thread has delivered the final chunk (or an error).
closed: bool = false,

/// Non-null if the HTTP request failed mid-stream; surfaced to the user
/// instead of whatever libarchive would otherwise report.
http_err: anyerror? = null,

/// Cached response status (metadata only arrives on the first callback).
status_code: u32 = 0,

/// True while a drain task is either queued on the thread pool or
/// running. `onChunk` sets it before scheduling; `drain` clears it when
/// it runs out of input and decides to yield.
draining: std.atomic.Value(bool) = .init(false),

// ---------------------------------------------------------------------
// Drain-side state (touched only by one drain task at a time)
// ---------------------------------------------------------------------

/// Bytes currently being consumed by libarchive. Populated by swapping
/// with `pending` under the mutex so the HTTP thread can keep appending
/// while libarchive decompresses without the lock held. libarchive's
/// read callback hands out `reading.items[read_pos..]` and advances
/// `read_pos`; the slice must remain valid until the next callback, so
/// we only recycle this buffer on the *following* swap.
reading: std.ArrayListUnmanaged(u8) = {},
read_pos: usize = 0,

archive: (*lib.Archive)? = null,

/// Where we are in the per-entry state machine between drain
/// invocations. libarchive preserves everything else (filter buffers,
/// zlib stream, tar header progress) on its own heap.
phase: Phase = .want_header,

/// Output file for the entry currently being written. `null` while
/// between entries or when the current entry is being skipped.
out_fd: bun.FD? = null,
use_pwrite: bool = Environment.isPosix,
use_lseek: bool = true,
/// Per-entry write cursors, carried across `writeDataBlock` calls so
/// the sparse-file handling in `closeOutputFile` matches
/// `Archive.readDataIntoFd` exactly (which tracks these across its own
/// block loop). Reset in `beginEntry` when a new output file is opened.
entry_actual_offset: i64 = 0,
entry_final_offset: i64 = 0,

/// Temp directory files are written into before being renamed into the
/// cache. Lazily opened on the first drain so the HTTP thread never
/// touches the filesystem.
dest: bun.FD? = null,
/// Owned copy of the temp-directory name; freed in `deinit()`.
tmpname: []const u8 = "",

/// Incremental SHA over the *compressed* bytes, matching
/// `Integrity.verify` / `Integrity.forBytes` in the buffered path.
hasher: Integrity.Streaming,

/// Resolved first-directory name for GitHub tarballs (written to
/// `.bun-tag` and used for the cache folder name).
resolved_github_dirname: []const u8 = "",
want_first_dirname: bool = false,
npm_mode: bool = true,

bytes_received: usize = 0,
entry_count: u32 = 0,
fail: anyerror? = null,

allocator: std.mem.Allocator,

/// Thread-pool task that runs `drain`. Re-enqueued whenever new data
/// arrives and no drain is currently in flight.
drain_task: ThreadPool.Task = { callback: &drainCallback },

/// Completion task that carries the final result back to the main
/// thread. Populated by `finish()` and pushed onto `resolve_tasks` there.
extract_task: *Task,
network_task: *NetworkTask,
package_manager: *PackageManager,

pub const new = bun.TrivialNew(@This());

const log = Output.scoped(.TarballStream, .hidden);

/// Minimum Content-Length for which the streaming path is used. Below
/// this the whole body is buffered as before; the resumable libarchive
/// state machine is only worth its per-chunk overhead for tarballs that
/// would otherwise consume a noticeable amount of memory.
pub fn minSize(): usize {
    return i32(bun.env_var.BUN_INSTALL_STREAMING_MIN_SIZE.get());
}

pub fn init(
    allocator: std.mem.Allocator,
    extract_task: *Task,
    network_task: *NetworkTask,
    manager: *PackageManager,
): *TarballStream {
    const tarball = &extract_task.request.extract.tarball;

    // For GitHub/URL/local tarballs we need a SHA-512 to record in the
    // lockfile even when there is no expected value to verify against,
    // matching `ExtractTarball.run`.
    const compute_if_missing = switch (tarball.resolution.tag) {
        .github, .remote_tarball, .local_tarball => true,
        else => false,
    };

    return TarballStream.new({
        allocator: allocator,
        extract_task: extract_task,
        network_task: network_task,
        package_manager: manager,
        npm_mode: tarball.resolution.tag != .github,
        want_first_dirname: tarball.resolution.tag == .github,
        hasher: Integrity.Streaming.init(
            if (tarball.skip_verify) {} else tarball.integrity,
            compute_if_missing,
        ),
    });
}

pub fn deinit(client: *TarballStream) {
    if (fd := client.out_fd) fd.close();
    if (d := client.dest) d.close();
    if (a := client.archive) {
        * = a.readClose();
        * = a.readFree();
    }
    if (client.tmpname.len > 0) client.allocator.free(client.tmpname);
    client.pending.deinit(client.allocator);
    client.reading.deinit(client.allocator);
    bun.destroy(client);
}

/// Called from the HTTP thread for each response-body chunk. Returns
/// without touching the filesystem or libarchive; actual processing is
/// deferred to `drain` on a worker so the HTTP event loop stays
/// responsive.
pub fn onChunk(client: *TarballStream, chunk: []const u8, is_last: bool, e: anyerror?) {
    client.mutex.lock();
    if (chunk.len > 0) {
        bun.handleOom(client.pending.appendSlice(client.allocator, chunk));
        client.bytes_received += chunk.len;
    }
    if (is_last) client.closed = true;
    if (err_val := e) client.http_err = err_val;
    client.mutex.unlock();

    client.scheduleDrain();
}

fn scheduleDrain(client: *TarballStream) {
    if (client.draining.swap(true, .acq_rel)) return;
    client.package_manager.thread_pool.schedule(ThreadPool.Batch.@"from"(&client.drain_task));
}

fn drainCallback(task: *ThreadPool.Task) {
    const stream: *TarballStream = @fieldParentPtr("drain_task", task);
    stream.drain();
}

/// Pull whatever compressed bytes are available into libarchive, writing
/// entries to disk, until libarchive reports `ARCHIVE_RETRY` (out of
/// input — yield) or a terminal state (EOF / error — finish).
fn drain(client: *TarballStream) {
    Output.Source.configureThread();

    while (true) {
        if (client.fail == null && client.phase != .done) {
            // Only pull bytes into `reading` while libarchive is still
            // going to consume them. After EOF/failure `step()` is
            // never called again, so appending here would let
            // `reading` grow by one HTTP chunk per wakeup for the
            // remainder of the download.
            const more = client.takePending();

            client.step() catch |e| {
                client.fail = e;
                client.closeOutputFile();
            };

            if (client.fail == null && client.phase != .done) {
                if (more) continue;
                // libarchive consumed everything we had. Yield the
                // worker until the HTTP thread delivers the next
                // chunk.
                client.draining.store(false, .release);
                // Close the race between clearing `draining` and a
                // chunk arriving: if `pending` is non-empty now, try
                // to reclaim the flag ourselves instead of waiting
                // for the next schedule.
                client.mutex.lock();
                const again = client.pending.items.len > 0 || client.closed;
                client.mutex.unlock();
                if (again && !client.draining.swap(true, .acq_rel)) continue;
                return;
            }
        }

        // Terminal: archive finished or extraction failed. libarchive
        // will not be called again, so `reading` is dead — drop it
        // now rather than carrying its capacity until `finish()`.
        // `reading` is drain-local (only the read callback touches
        // it, and that runs inside `step()`), so this needs no lock.
        client.reading.clearAndFree(client.allocator);
        client.read_pos = 0;

        client.mutex.lock();
        // Hash any bytes that arrived after libarchive hit
        // end-of-archive so the integrity digest covers the full
        // response (tar zero-padding, gzip footer). Skip this once
        // an error is recorded — the digest won't be checked anyway.
        if (client.fail == null && client.pending.items.len > 0) {
            client.hasher.update(client.pending.items);
        }
        // After EOF/failure we stop feeding libarchive but must keep
        // consuming (and discarding) chunks until the HTTP thread
        // closes the stream; freeing ourselves earlier would let the
        // next `notify` dereference a dead pointer.
        client.pending.clearRetainingCapacity();
        const closed = client.closed;
        const http_err = client.http_err;
        client.mutex.unlock();
        // A transport error that arrives *after* libarchive reached
        // EOF (e.g. the server RSTs the connection once the last
        // byte is on the wire) must not override a successful
        // extraction; the integrity check in `populateResult()` is
        // the sole arbiter of correctness once `.done` is reached.
        if (e := http_err) {
            if (client.fail == null && client.phase != .done) {
                client.fail = e;
            }
        }
        if (closed) {
            client.finish();
            return;
        }

        // Archive is done (or failed) but the HTTP response has not
        // finished yet. Yield; the next `onChunk` will reschedule us
        // to discard the new bytes and eventually observe `closed`.
        client.draining.store(false, .release);
        client.mutex.lock();
        const again = client.pending.items.len > 0 || client.closed;
        client.mutex.unlock();
        if (again && !client.draining.swap(true, .acq_rel)) continue;
        return;
    }
}

/// Move any bytes still sitting in `pending` into `reading` so the read
/// callback can hand them to libarchive. Returns true if new bytes were
/// added or the stream is now closed.
fn takePending(client: *TarballStream): bool {
    client.mutex.lock();
    defer client.mutex.unlock();

    if (client.pending.items.len == 0) return client.closed;

    // Hash before libarchive sees the bytes so integrity covers exactly
    // what came off the socket.
    client.hasher.update(client.pending.items);

    if (client.reading.items.len == client.read_pos) {
        // Previous buffer fully consumed — swap so the HTTP thread can
        // reuse its capacity without reallocating.
        client.reading.clearRetainingCapacity();
        std.mem.swap(std.ArrayListUnmanaged(u8), &client.reading, &client.pending);
        client.read_pos = 0;
    } else {
        // libarchive still holds a slice into `reading` (the read
        // callback contract keeps the last-returned buffer valid until
        // the next call). Appending would realloc and invalidate that
        // slice, so instead shift the unconsumed tail down and append
        // in place — the callback is not running concurrently with us
        // (single drain at a time) and will be re-primed with the new
        // base on its next invocation.
        const remaining = client.reading.items.len - client.read_pos;
        std.mem.copyForwards(u8, client.reading.items[0..remaining], client.reading.items[client.read_pos..]);
        client.reading.items.len = remaining;
        client.read_pos = 0;
        bun.handleOom(client.reading.appendSlice(client.allocator, client.pending.items));
        client.pending.clearRetainingCapacity();
    }
    return true;
}

/// Run libarchive until it needs more input (`.retry`) or hits a
/// terminal state. All libarchive state persists on the heap, so
/// returning from here and re-entering later is safe.
fn step(client: *TarballStream)! {
    if (client.archive == null) client.openArchive()?;
    if (client.dest == null) client.openDestination()?;

    const archive = client.archive.?;

    while (true) {
        switch (client.phase) {
            .done => { return; },
            .want_header => {
                let entry: *lib.Archive.Entry = undefined;
                switch (archive.readNextHeader(&entry)) {
                    .retry => { return; },
                    .eof => {
                        client.phase = .done;
                        return;
                    },
                    .ok, .warn => client.beginEntry(entry)?,
                    .failed, .fatal => {
                        const hdr_err = .{ archive.errorString() };
                        log("readNextHeader: {s}", hdr_err);
                        return error.Fail;
                    },
                }
            },
            .want_data => {
                let offset: i64 = 0;
                if (block := archive.next(&offset)) {
                    switch (block.result) {
                        .retry => { return; },
                        .ok, .warn => {
                            if (fd := client.out_fd) {
                                client.writeDataBlock(fd, block)?;
                            }
                        },
                        else => {
                            const db_err = .{ archive.errorString() };
                            log("read_data_block: {s}", db_err);
                            return error.Fail;
                        },
                    }
                } else {
                    // End of this entry's data.
                    client.closeOutputFile();
                    client.phase = .want_header;
                    continue;
                }
            },
        }
    }
}

fn openArchive(client: *TarballStream)! {
    const ar = lib.Archive.readNew();
    errdefer {
        * = ar.readClose();
        * = ar.readFree();
    }
    // Bypass bidding entirely: the stream is always gzip → tar, and
    // bidding would try to read-ahead before any bytes have arrived.
    // ARCHIVE_FILTER_GZIP = 1, ARCHIVE_FORMAT_TAR = 0x30000.
    if (lib.archive_read_append_filter(@ptrCast(ar), 1) != 0) return error.Fail;
    if (lib.archive_read_set_format(@ptrCast(ar), 0x30000) != 0) return error.Fail;
    * = ar.readSetOptions("read_concatenated_archives");

    switch (@as(lib.Archive.Result, @enumFromInt(lib.archive_read_open(
        @ptrCast(ar),
        client,
        null,
        archiveReadCallback,
        null,
    )))) {
        .ok, .warn => {},
        .retry => {
            // open() runs the filter bidder which we bypassed, but the
            // client open path may still probe; treat as transient.
            client.archive = ar;
            return;
        },
        else => {
            const err_args = .{ ar.errorString() };
            log("archive_read_open: {s}", err_args);
            return error.Fail;
        },
    }
    client.archive = ar;
}

fn openDestination(client: *TarballStream)! {
    const tarball = &client.extract_task.request.extract.tarball;
    const name_pair = tarball.nameAndBasename();
    const basename = name_pair[1];
    let buf: bun.PathBuffer = undefined;
    const tmpname = FileSystem.tmpname(
        basename[0..@min(basename.len, 32)],
        buf[0..],
        bun.fastRandom(),
    )?;
    client.tmpname = client.allocator.dupeZ(u8, tmpname)?;

    client.dest = .fromStdDir(bun.MakePath.makeOpenPath(tarball.temp_dir, client.tmpname, {})?);
}

fn closeOutputFile(client: *TarballStream) {
    if (fd := client.out_fd) {
        // Same trailing-hole handling as `Archive.readDataIntoFd`:
        // extend the file to cover the furthest block we were asked
        // to write even if the pwrite/lseek fallback path left
        // `actual_offset` behind.
        if (client.entry_final_offset > client.entry_actual_offset) {
            * = bun.sys.ftruncate(fd, client.entry_final_offset);
        }
        fd.close();
        client.out_fd = null;
    }
}

/// libarchive client read callback. Returns whatever compressed bytes
/// are currently buffered in `reading`; if none, returns `ARCHIVE_RETRY`
/// (when more data is still expected) so libarchive unwinds with a
/// resumable status, or `0` (EOF) once the HTTP response is complete.
@c fn archiveReadCallback(
    _: *lib.struct_archive,
    ctx: *!,
    out_buffer: [*c]*const !,
): lib.la_ssize_t {
    const stream: *TarballStream = @ptrCast(@alignCast(ctx));

    const remaining = stream.reading.items[stream.read_pos..];
    if (remaining.len > 0) {
        out_buffer.* = remaining.ptr;
        stream.read_pos = stream.reading.items.len;
        return i32(remaining.len);
    }

    // No data left in `reading`. Check for more under the lock —
    // libarchive may have called us more than once for a single
    // `step()` (e.g. gzip header + first deflate block), and `onChunk`
    // might have landed a fresh chunk in the meantime.
    this.mutex.lock();
    const has_pending = this.pending.items.len > 0;
    const closed = this.closed;
    this.mutex.unlock();

    if (has_pending) {
        // Pull the new bytes into `reading` and retry the read. We are
        // the only consumer of `reading`/`read_pos`, and `takePending`
        // only touches producer state under the same mutex.
        * = this.takePending();
        const again = this.reading.items[this.read_pos..];
        if (again.len > 0) {
            out_buffer.* = again.ptr;
            this.read_pos = this.reading.items.len;
            return i32(again.len);
        }
    }

    if (closed) {
        out_buffer.* = @ptrCast(@alignCast(this)); // unused when len==0
        return 0;
    }

    // Tell libarchive to unwind with a resumable status. The BUN PATCHes
    // in vendor/libarchive make every layer (filter_ahead → gzip → tar)
    // preserve its state and propagate ARCHIVE_RETRY to our `step()`
    // loop, which then returns so this worker can be reused.
    return @intFromEnum(lib.Archive.Result.retry);
}

/// Process one entry header returned by `readNextHeader`. Opens the
/// output file (or creates the directory/symlink) and transitions to
/// `.want_data` so the next `step()` iteration starts pulling its body.
fn beginEntry(client: *TarballStream, entry: *lib.Archive.Entry)! {
    let pathname: bun.OSPathSliceZ = if (comptime Environment.isWindows)
        entry.pathnameW()
    else
        entry.pathname();

    if (client.want_first_dirname) {
        client.want_first_dirname = false;
        // GitHub's archive API always emits an explicit `repo-sha/`
        // directory entry first, which is what the buffered path
        // relies on. Take only the leading component so a tarball
        // whose first member is `repo-sha/file` (no directory entry)
        // still yields the correct cache-folder name.
        let root_it = std.mem.tokenizeScalar(bun.OSPathChar, pathname, '/');
        const root = root_it.next() ?? pathname[0..0];
        if (comptime Environment.isWindows) {
            const list = std.array_list.Managed(u8).init(bun.default_allocator);
            let result = strings.toUTF8ListWithType(list, root)?;
            defer result.deinit();
            client.resolved_github_dirname = FileSystem.DirnameStore.instance.append(
                string,
                result.items,
            ) catch unreachable;
        } else {
            client.resolved_github_dirname = FileSystem.DirnameStore.instance.append(
                string,
                bun.asByteSlice(root),
            ) catch unreachable;
        }
    }

    const kind = bun.sys.kindFromMode(entry.filetype());

    if (client.npm_mode && kind != .file) {
        // npm tarballs only contain files; matching the libarchive path
        // in Archiver.extractToDir we skip everything else.
        client.phase = .want_data;
        client.out_fd = null;
        return;
    }

    // Strip the leading `package/` (or `<repo>-<sha>/` for GitHub) and
    // normalise. Same transformation as Archiver.extractToDir so both
    // paths produce identical on-disk layouts.
    let tokenizer = std.mem.tokenizeScalar(bun.OSPathChar, pathname, '/');
    if (tokenizer.next() == null) {
        client.phase = .want_data;
        client.out_fd = null;
        return;
    }
    const rest = tokenizer.rest();
    const stripped_pathname = rest.ptr[0..rest.len :0];

    let norm_buf: bun.OSPathBuffer = undefined;
    const normalized = bun.path.normalizeBufT(bun.OSPathChar, stripped_pathname, &norm_buf, .auto);
    norm_buf[normalized.len] = 0;
    const path = norm_buf[0..normalized.len :0];
    if (path.len == 0 || (path.len == 1 && path[0] == '.')) {
        client.phase = .want_data;
        client.out_fd = null;
        return;
    }
    // `normalizeBufT` collapses interior `..` but leaves a leading `..`
    // on a relative input. Reject those so `openat(dest_fd, ...)` can
    // never escape the temp extraction root. `Archiver.extractToDir`
    // sees the same normalised path; this check is belt-and-braces on
    // top of the integrity gate.
    if (path.len >= 2 && path[0] == '.' && path[1] == '.' &&
        (path.len == 2 || path[2] == std.fs.path.sep))
    {
        client.phase = .want_data;
        client.out_fd = null;
        return;
    }
    if (comptime Environment.isWindows) {
        if (std.fs.path.isAbsoluteWindowsWTF16(path)) {
            client.phase = .want_data;
            client.out_fd = null;
            return;
        }
        if (client.npm_mode) applyWindowsNpmPathEscapes(path);
    }

    const path_slice = path[0..path.len];
    const dest = client.dest.?;

    switch (kind) {
        .directory => {
            makeDirectory(entry, dest, path, path_slice);
            client.phase = .want_data;
            client.out_fd = null;
        },
        .sym_link => {
            if (Environment.isPosix) makeSymlink(entry, dest, path, path_slice);
            client.phase = .want_data;
            client.out_fd = null;
        },
        .file => {
            const mode = if (comptime Environment.isWindows) @as(bun.Mode, 0) else @as(bun.Mode, @intCast(entry.perm() | 0o666));
            const fd = openOutputFile(dest, path, path_slice, mode)?;
            client.entry_count += 1;

            if (comptime Environment.isLinux) {
                const size: usize = @intCast(@max(entry.size(), 0));
                if (size > 1_000_000) {
                    bun.sys.preallocate_file(fd.cast(), 0, i32(size)) catch {};
                }
            }

            client.out_fd = fd;
            client.entry_actual_offset = 0;
            client.entry_final_offset = 0;
            client.phase = .want_data;
        },
        else => {
            client.phase = .want_data;
            client.out_fd = null;
        },
    }
}

fn openOutputFile(
    dest_fd: bun.FD,
    path: [:0]bun.OSPathChar,
    path_slice: bun.OSPathSlice,
    mode: bun.Mode,
): !bun.FD {
    const flags = bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC;
    if (comptime Environment.isWindows) {
        return switch (bun.sys.openatWindows(dest_fd, path, flags, 0)) {
            .result => |fd| fd,
            .err => |e| switch (e.errno) {
                @intFromEnum(bun.sys.E.PERM), @intFromEnum(bun.sys.E.NOENT) => brk: {
                    dest_fd.makePath(u16, bun.Dirname.dirname(u16, path_slice) ?? return bun.errnoToZigErr(e.errno)) catch {};
                    break :brk try bun.sys.openatWindows(dest_fd, path, flags, 0).unwrap();
                },
                else => return bun.errnoToZigErr(e.errno),
            },
        };
    }
    return switch (bun.sys.openat(dest_fd, path, flags, mode)) {
        .result => |fd| fd,
        .err => |e| switch (e.getErrno()) {
            .ACCES, .NOENT => brk: {
                dest_fd.makePath(u8, std.fs.path.dirname(path_slice) ?? return bun.errnoToZigErr(e.errno)) catch {};
                break :brk try bun.sys.openat(dest_fd, path, flags, mode).unwrap();
            },
            else => return bun.errnoToZigErr(e.errno),
        },
    };
}

fn makeDirectory(
    entry: *lib.Archive.Entry,
    dest_fd: bun.FD,
    path: [:0]bun.OSPathChar,
    path_slice: bun.OSPathSlice,
) {
    let mode = @as(i32, i32(entry.perm()));
    // if dirs are readable, then they should be listable
    // https://github.com/npm/node-tar/blob/main/lib/mode-fix.js
    if ((mode & 0o400) != 0) mode |= 0o100;
    if ((mode & 0o40) != 0) mode |= 0o10;
    if ((mode & 0o4) != 0) mode |= 0o1;
    if (comptime Environment.isWindows) {
        dest_fd.makePath(u16, path) catch {};
    } else {
        switch (bun.sys.mkdiratZ(dest_fd, path, i32(mode))) {
            .result => {},
            .err => |e| switch (e.getErrno()) {
                .EXIST, .NOTDIR => {},
                else => {
                    dest_fd.makePath(u8, std.fs.path.dirname(path_slice) ?? return) catch {};
                    * = bun.sys.mkdiratZ(dest_fd, path, 0o777);
                },
            },
        }
    }
}

fn makeSymlink(
    entry: *lib.Archive.Entry,
    dest_fd: bun.FD,
    path: [:0]bun.OSPathChar,
    path_slice: bun.OSPathSlice,
) {
    const target = entry.symlink();
    // Same safety rule as `isSymlinkTargetSafe` in the buffered path:
    // reject absolute targets and anything that escapes via `..`.
    if (target.len == 0 || target[0] == '/') return;
    {
        const symlink_dir = std.fs.path.dirname(path_slice) ?? "";
        let join_buf: bun.PathBuffer = undefined;
        const resolved = bun.path.joinAbsStringBuf("/packages/", &join_buf, &{ symlink_dir, target }, .posix);
        if (!strings.hasPrefix(resolved, "/packages/")) return;
    }
    bun.sys.symlinkat(target, dest_fd, path).unwrap() catch |e| switch (e) {
        error.EPERM, error.ENOENT => {
            dest_fd.makePath(u8, std.fs.path.dirname(path_slice) ?? return) catch {};
            bun.sys.symlinkat(target, dest_fd, path).unwrap() catch {};
        },
        else => {},
    };
}

fn applyWindowsNpmPathEscapes(path: [:0]bun.OSPathChar) {
    // Same transformation as Archiver.extractToDir: encode characters
    // Windows rejects in filenames into the 0xf000 private-use range so
    // the extraction round-trips with node-tar.
    let remain: []bun.OSPathChar = path;
    if (strings.startsWithWindowsDriveLetterT(bun.OSPathChar, remain)) remain = remain[2..];
    for (*char of remain) switch (char.*) {
        '|', '<', '>', '?', ':' => char.* += @as(bun.OSPathChar, 0xf000),
        else => {},
    };
}

/// Write one data block from `archive_read_data_block`. Mirrors the
/// sparse/pwrite handling in `Archive.readDataIntoFd` but operates on a
/// single block so it can be interleaved with ARCHIVE_RETRY yields.
/// `entry_actual_offset` / `entry_final_offset` persist across calls so
/// `closeOutputFile` can perform the same trailing `ftruncate` the
/// buffered path does after its block loop.
fn writeDataBlock(client: *TarballStream, fd: bun.FD, block: lib.Archive.Block)! {
    const file = bun.sys.File{ handle: fd };
    const data = block.bytes;
    if (data.len == 0) return;

    client.entry_final_offset = @max(
        client.entry_final_offset,
        block.offset + @as(i64, i32(data.len)),
    );

    if (comptime Environment.isPosix) {
        if (client.use_pwrite) {
            switch (file.pwriteAll(data, block.offset)) {
                .result => {
                    client.entry_actual_offset = @max(
                        client.entry_actual_offset,
                        block.offset + @as(i64, i32(data.len)),
                    );
                    return;
                },
                .err => client.use_pwrite = false,
            }
        }
    }

    if (block.offset != client.entry_actual_offset) seek: {
        if (client.use_lseek) {
            switch (bun.sys.setFileOffset(fd, i32(block.offset))) {
                .result => {
                    client.entry_actual_offset = block.offset;
                    break :seek;
                },
                .err => client.use_lseek = false,
            }
        }
        if (block.offset > client.entry_actual_offset) {
            const zero_count: usize = @intCast(block.offset - client.entry_actual_offset);
            switch (lib.Archive.writeZerosToFile(file, zero_count)) {
                .ok => client.entry_actual_offset = block.offset,
                else => return error.Fail,
            }
        } else {
            return error.Fail;
        }
    }

    switch (file.writeAll(data)) {
        .result => client.entry_actual_offset += i32(data.len),
        .err => |e| return bun.errnoToZigErr(e.errno),
    }
}

fn finish(client: *TarballStream) {
    const task = client.extract_task;
    const network = client.network_task;
    const manager = client.package_manager;

    client.closeOutputFile();

    // The HTTP thread has delivered the final `has_more=false` chunk
    // (that's the only way `closed` gets set) and `notify()` does not
    // touch `response_buffer` again after that hand-off, so we own it
    // now. The main thread reads only `streaming_committed` when it
    // later processes the NetworkTask, so freeing the buffer here is
    // safe and matches the `defer buffer.deinit()` in the buffered
    // `.extract` arm of `Task.callback`.
    network.response_buffer.deinit();

    client.populateResult(task);

    // Temp-dir cleanup must happen before we release the stream or
    // publish the task: both `this.tmpname` and
    // `task.request.extract.tarball.temp_dir` become invalid once
    // `this.deinit()` runs / the main thread recycles the Task.
    if (task.status != .success && client.tmpname.len > 0) {
        // `populateResult` closes `dest` on the success path before the
        // rename; the early-return failure paths leave it open, so close
        // it here first — Windows can't remove an open directory.
        // `deinit()` null-checks so this is not a double-close.
        if (d := client.dest) {
            d.close();
            client.dest = null;
        }
        task.request.extract.tarball.temp_dir.deleteTree(client.tmpname) catch {};
    }

    client.deinit();

    // `task.apply_patch_task` is intentionally not touched: the
    // buffered `.extract` path (`enqueueExtractNPMPackage` →
    // `Task.callback`) never populates it for npm tarballs either —
    // patching is handled later by the install phase.
    //
    // Publish last: once the task is on `resolve_tasks` the main
    // thread may immediately recycle it *and* the NetworkTask it
    // references, so nothing below this line may touch either.
    manager.resolve_tasks.push(task);
    manager.wake();
}

fn populateResult(client: *TarballStream, task: *Task) {
    const tarball = &task.request.extract.tarball;
    task.data = { extract: {} };

    if (e := client.fail) {
        const fail_args = .{ @errorName(e), tarball.name.slice() };
        task.log.addErrorFmt(
            null,
            logger.Loc.Empty,
            client.allocator,
            "{s} extracting tarball for \"{s}\"",
            fail_args,
        ) catch unreachable;
        task.err = e;
        task.status = .fail;
        return;
    }

    if (!tarball.skip_verify && tarball.integrity.tag.isSupported()) {
        if (!client.hasher.verify()) {
            const int_args = .{ tarball.name.slice() };
            task.log.addErrorFmt(
                null,
                logger.Loc.Empty,
                client.allocator,
                "Integrity check failed<r> for tarball: {s}",
                int_args,
            ) catch unreachable;
            task.err = error.IntegrityCheckFailed;
            task.status = .fail;
            return;
        }
    }

    if (tarball.resolution.tag == .github) {
        if (client.resolved_github_dirname.len > 0) insert_tag: {
            const gh_tag = bun.sys.openat(
                client.dest.?,
                ".bun-tag",
                bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC,
                0o644,
            ).unwrap() catch break :insert_tag;
            defer gh_tag.close();
            const gh_file = bun.sys.File{ handle: gh_tag };
            gh_file.writeAll(client.resolved_github_dirname).unwrap() catch {
                * = bun.sys.unlinkat(client.dest.?, ".bun-tag");
            };
        }
    }

    // Close the temp dir handle before renaming so Windows can move it.
    if (d := client.dest) {
        d.close();
        client.dest = null;
    }

    const nb_pair = tarball.nameAndBasename();
    const name = nb_pair[0];
    const basename = nb_pair[1];

    let result = tarball.moveToCacheDirectory(
        &task.log,
        client.tmpname,
        name,
        basename,
        client.resolved_github_dirname,
    ) catch |e| {
        task.err = e;
        task.status = .fail;
        return;
    };

    switch (tarball.resolution.tag) {
        .github, .remote_tarball, .local_tarball => {
            if (tarball.integrity.tag.isSupported()) {
                result.integrity = tarball.integrity;
            } else {
                result.integrity = client.hasher.final();
            }
        },
        else => {},
    }

    if (PackageManager.verbose_install) {
        const verb_args = .{
            name,
            bun.fmt.size(client.bytes_received, {}),
            client.entry_count,
        };
        Output.prettyErrorln("[{s}] Streamed {f} tarball → {d} entries<r>", verb_args);
        Output.flush();
    }

    task.data = { extract: result };
    task.status = .success;
    return;
}

/// Prepare this stream for another HTTP attempt after a failed request
/// that never scheduled a drain.
pub fn resetForRetry(client: *TarballStream) {
    client.mutex.lock();
    client.pending.clearRetainingCapacity();
    client.closed = false;
    client.http_err = null;
    client.status_code = 0;
    client.bytes_received = 0;
    client.mutex.unlock();
}

const Integrity = integrity_mod.Integrity;

const NetworkTask = install.NetworkTask;
const PackageManager = install.PackageManager;
const Task = install.Task;

const Environment = bun.Environment;
const Output = bun.Output;
const ThreadPool = bun.ThreadPool;
const logger = bun.logger;
const strings = bun.strings;
const FileSystem = bun.fs.FileSystem;
const Mutex = bun.threading.Mutex;
const lib = bun.libarchive.lib;

}