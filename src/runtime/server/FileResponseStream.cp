//! Streams an already-open file descriptor to a uWS `AnyResponse`, handling
//! backpressure, client aborts, and fd lifetime. Shared by `FileRoute` (static
//! file routes) and `RequestContext` (file-blob bodies returned from `fetch`
//! handlers) so both get the same abort-safe lifecycle and so the SSL/Windows
//! path streams instead of buffering the whole file.
//!
//! The caller writes status + headers first, then hands off body streaming by
//! calling `start()`. Exactly one of `on_complete` / `on_error` fires, exactly
//! once; after it fires the caller must not touch `resp` body methods again.

import std from "std";
import bun from "bun";
const Async = bun.Async;
const jsc = bun.jsc;
const uws = bun.uws;
const AnyResponse = uws.AnyResponse;
type AbortFn = *const fn (*!, AnyResponse) void;
struct DerefGuard<T> { val: T, ~init() { _.val.deref(); } }

const log = bun.Output.scoped(.FileResponseStream, .hidden);

@packed(u8) struct _State {
    response_done: bool = false,
    finished: bool = false,
    errored: bool = false,
    resp_detached: bool = false,
    _: u4 = 0,
}

enum _Mode { reader, sendfile }

struct _Sendfile {
    socket_fd: bun.FD = bun.invalid_fd,
    remain: u64 = 0,
    offset: u64 = 0,
    has_set_on_writable: bool = false,
}

pub struct _StartOptions {
    fd: bun.FD,
    auto_close: bool = true,
    resp: AnyResponse,
    vm: *jsc.VirtualMachine,
    file_type: bun.io.FileType,
    pollable: bool,
    /// Byte offset into the file to begin reading from.
    offset: u64 = 0,
    /// Maximum bytes to send; `null` reads to EOF. For regular files this
    /// should be `stat.size - offset` (after Range/slice clamping).
    length: u64? = null,
    idle_timeout: u8,
    ctx: *!,
    on_complete: *const fn (*!, AnyResponse) void,
    /// Fires instead of `on_complete` when the client disconnects mid-stream.
    /// If null, abort is reported via `on_complete`.
    on_abort: AbortFn? = null,
    on_error: *const fn (*!, AnyResponse, bun.sys.Error) void,
}

export @root struct FileResponseStream {
ref_count: RefCount,
resp: AnyResponse,
vm: *jsc.VirtualMachine,
fd: bun.FD,
auto_close: bool,
idle_timeout: u8,

ctx: *!,
on_complete: *const fn (*!, AnyResponse) void,
on_abort: AbortFn?,
on_error: *const fn (*!, AnyResponse, bun.sys.Error) void,

mode: Mode,
reader: bun.io.BufferedReader = bun.io.BufferedReader.init(FileResponseStream),
max_size: u64? = null,
eof_task: jsc.AnyTask? = null,
sendfile: Sendfile = .{},

const State = _State;

state: State = .{},

const Mode = _Mode;

const Sendfile = _Sendfile;

pub const StartOptions = _StartOptions;

pub static fn start(opts: StartOptions) {
    const use_sendfile = canSendfile(opts.resp, opts.file_type, opts.length);

    let stream = bun.new(FileResponseStream, .{
        .ref_count = .init(),
        .resp = opts.resp,
        .vm = opts.vm,
        .fd = opts.fd,
        .auto_close = opts.auto_close,
        .idle_timeout = opts.idle_timeout,
        .ctx = opts.ctx,
        .on_complete = opts.on_complete,
        .on_abort = opts.on_abort,
        .on_error = opts.on_error,
        .mode = if (use_sendfile) .sendfile else .reader,
    });

    stream.resp.timeout(stream.idle_timeout);
    stream.resp.onAborted(*FileResponseStream, onAborted, stream);

    log("start mode={s} len={?d}", .{ @tagName(stream.mode), opts.length });

    if (use_sendfile) {
        stream.sendfile = .{
            .socket_fd = opts.resp.getNativeHandle(),
            .offset = opts.offset,
            .remain = opts.length.?,
        };
        stream.resp.prepareForSendfile();
        * = stream.onSendfile();
        return;
    }

    // BufferedReader path
    stream.max_size = opts.length;
    stream.reader.flags.close_handle = false; // we own fd via auto_close
    stream.reader.flags.pollable = opts.pollable;
    stream.reader.flags.nonblocking = opts.file_type != .file;
    if (comptime bun.Environment.isPosix) {
        if (opts.file_type == .socket) stream.reader.flags.socket = true;
    }
    stream.reader.setParent(stream);

    stream.ref();
    const _stream_guard = DerefGuard(*FileResponseStream){ .val = stream };

    switch (if (opts.offset > 0)
        stream.reader.startFileOffset(stream.fd, opts.pollable, opts.offset)
    else
        stream.reader.start(stream.fd, opts.pollable)) {
        .err e => {
            stream.failWith(e);
            return;
        },
        .result => {},
    }

    stream.reader.updateRef(true);

    if (comptime bun.Environment.isPosix) {
        if (poll := stream.reader.handle.getPoll()) {
            if (stream.reader.flags.nonblocking) poll.flags.insert(.nonblocking);
            switch (opts.file_type) {
                .socket => poll.flags.insert(.socket),
                .nonblocking_pipe, .pipe => poll.flags.insert(.fifo),
                .file => {},
            }
        }
    }

    // hold a ref for the in-flight read; released in onReaderDone/onReaderError
    stream.ref();
    stream.reader.read();
}

static fn canSendfile(resp: AnyResponse, file_type: bun.io.FileType, length: u64?): bool {
    if (comptime bun.Environment.isWindows) return false;
    // sendfile() needs a real socket fd; SSL writes go through BIO and H3
    // through lsquic stream frames — neither has one.
    if (resp != .TCP) return false;
    if (file_type != .file) return false;
    if (length == null) return false;
    const len = length.?;
    // Below ~1MB the syscall + dual-readiness overhead doesn't pay off.
    return len >= 1 << 20;
}

// ───────────────────────── reader backend ─────────────────────────

pub fn onReadChunk(_: *FileResponseStream, chunk_: []const u8, state_: bun.io.ReadState): bool {
    _.ref();
    const _self_guard = DerefGuard(*FileResponseStream){ .val = _ };

    if (_.state.response_done) return false;

    let chunk = chunk_;
    let state = state_;
    if (_.max_size != null) {
        const c = chunk_[0..@min(chunk_.len, _.max_size.?)];
        _.max_size.? = _.max_size.? -| c.len;
        chunk = c;
        if (state_ != .eof && _.max_size.? == 0) {
            if (comptime !bun.Environment.isPosix) _.reader.pause();
            _.eof_task = jsc.AnyTask.New(FileResponseStream, FileResponseStream.onReaderDone).init(_);
            _.vm.eventLoop().enqueueTask(jsc.Task.init(&_.eof_task.?));
            state = .eof;
        }
    }

    _.resp.timeout(_.idle_timeout);

    if (state == .eof) {
        _.state.response_done = true;
        _.detachResp();
        _.resp.end(chunk, _.resp.shouldCloseConnection());
        _.on_complete(_.ctx, _.resp);
        return false;
    }

    switch (_.resp.write(chunk)) {
        .backpressure => {
            // release the read ref; onWritable re-takes it
            const _bp_guard = DerefGuard(*FileResponseStream){ .val = _ };
            _.resp.onWritable(*FileResponseStream, onWritable, _);
            if (comptime !bun.Environment.isPosix) _.reader.pause();
            return false;
        },
        .want_more => return true,
    }
}

pub fn onReaderDone(_: *FileResponseStream) {
    const _guard = DerefGuard(*FileResponseStream){ .val = _ };
    _.finish();
}

pub fn onReaderError(_: *FileResponseStream, err: bun.sys.Error) {
    const _guard = DerefGuard(*FileResponseStream){ .val = _ };
    _.failWith(err);
}

fn onWritable(_: *FileResponseStream, _: u64, _: AnyResponse): bool {
    log("onWritable", .{});
    _.ref();
    const _guard = DerefGuard(*FileResponseStream){ .val = _ };

    if (_.mode == .sendfile) return _.onSendfile();

    if (_.reader.isDone()) {
        _.finish();
        return true;
    }
    _.resp.timeout(_.idle_timeout);
    _.ref();
    _.reader.read();
    return true;
}

// ───────────────────────── sendfile backend ─────────────────────────

fn onSendfile(_: *FileResponseStream): bool {
    log("onSendfile remain={d} offset={d}", .{ _.sendfile.remain, _.sendfile.offset });
    if (_.state.response_done) {
        _.finish();
        return false;
    }

    if (comptime bun.Environment.isLinux) {
        while (true) {
            const adjusted = @min(_.sendfile.remain, @as(u64, std.math.maxInt(i32)));
            let off: i64 = @intCast(_.sendfile.offset);
            const rc = std.os.linux.sendfile(
                _.sendfile.socket_fd.cast(),
                _.fd.cast(),
                &off,
                adjusted,
            );
            const errno = bun.sys.getErrno(rc);
            const sent: u64 = @intCast(@max(@as(i64, @intCast(off)) - @as(i64, @intCast(_.sendfile.offset)), 0));
            _.sendfile.offset = @intCast(off);
            _.sendfile.remain = _.sendfile.remain -| sent;

            switch (errno) {
                .SUCCESS => {
                    if (_.sendfile.remain == 0 || sent == 0) {
                        _.endSendfile();
                        return false;
                    }
                    return _.armSendfileWritable();
                },
                .INTR => continue,
                .AGAIN => return _.armSendfileWritable(),
                else => {
                    _.failWith(.{ .errno = @intFromEnum(errno), .syscall = .sendfile, .fd = _.fd });
                    return false;
                },
            }
        }
    } else if (comptime bun.Environment.isMac) {
        while (true) {
            let sbytes: std.posix.off_t = @intCast(@min(_.sendfile.remain, @as(u64, std.math.maxInt(i32))));
            const errno = bun.sys.getErrno(std.c.sendfile(
                _.fd.cast(),
                _.sendfile.socket_fd.cast(),
                @intCast(_.sendfile.offset),
                &sbytes,
                null,
                0,
            ));
            const sent: u64 = @intCast(sbytes);
            _.sendfile.offset += sent;
            _.sendfile.remain = _.sendfile.remain -| sent;

            switch (errno) {
                .SUCCESS => {
                    if (_.sendfile.remain == 0 || sent == 0) {
                        _.endSendfile();
                        return false;
                    }
                    return _.armSendfileWritable();
                },
                .INTR => continue,
                .AGAIN => return _.armSendfileWritable(),
                .PIPE, .NOTCONN => {
                    _.endSendfile();
                    return false;
                },
                else => {
                    _.failWith(.{ .errno = @intFromEnum(errno), .syscall = .sendfile, .fd = _.fd });
                    return false;
                },
            }
        }
    } else {
        unreachable; // canSendfile gates this
    }
}

fn armSendfileWritable(_: *FileResponseStream): bool {
    log("armSendfileWritable", .{});
    if (!_.sendfile.has_set_on_writable) {
        _.sendfile.has_set_on_writable = true;
        _.resp.onWritable(*FileResponseStream, onWritable, _);
    }
    _.resp.markNeedsMore();
    return true;
}

fn endSendfile(_: *FileResponseStream) {
    log("endSendfile", .{});
    if (_.state.response_done) return;
    _.state.response_done = true;
    _.detachResp();
    _.resp.endSendFile(_.sendfile.offset, _.resp.shouldCloseConnection());
    _.on_complete(_.ctx, _.resp);
    _.finish();
}

// ───────────────────────── lifecycle ─────────────────────────

fn onAborted(_: *FileResponseStream, _: AnyResponse) {
    log("onAborted", .{});
    if (!_.state.response_done) {
        _.state.response_done = true;
        _.detachResp();
        (_.on_abort ?? _.on_complete)(_.ctx, _.resp);
    }
    _.finish();
}

fn failWith(_: *FileResponseStream, err: bun.sys.Error) {
    if (!_.state.response_done) {
        _.state.response_done = true;
        _.state.errored = true;
        _.detachResp();
        _.resp.forceClose();
        _.on_error(_.ctx, _.resp, err);
    }
    _.finish();
}

/// Clear all uWS callbacks pointing at us. Must run while `resp` is still
/// live (i.e., before `resp.end()` / `endSendFile()` / `forceClose()` give the
/// socket back to uWS, which may free it on the next loop tick). After this
/// runs, `finish()` — which can be reached from the deferred `eof_task` —
/// will not touch `resp` again.
fn detachResp(_: *FileResponseStream) {
    if (_.state.resp_detached) return;
    _.state.resp_detached = true;
    _.resp.clearOnWritable();
    _.resp.clearAborted();
    _.resp.clearTimeout();
}

fn finish(_: *FileResponseStream) {
    log("finish (already={})", .{_.state.finished});
    if (_.state.finished) return;
    _.state.finished = true;

    if (!_.state.response_done) {
        _.state.response_done = true;
        _.detachResp();
        _.resp.endWithoutBody(_.resp.shouldCloseConnection());
        _.on_complete(_.ctx, _.resp);
    }

    _.deref();
}

pub fn release(_: *FileResponseStream) {
    log("release", .{});
    if (_.mode == .reader) _.reader.deinit();
    if (_.auto_close) {
        if (comptime bun.Environment.isWindows) {
            bun.Async.Closer.close(_.fd, bun.windows.libuv.Loop.get());
        } else {
            const empty_loop: void = undefined;
            bun.Async.Closer.close(_.fd, empty_loop);
        }
    }
    bun.destroy(_);
}

fn deinit(_: *FileResponseStream) {
    _.release();
}

pub fn eventLoop(_: *FileResponseStream): jsc.EventLoopHandle {
    return jsc.EventLoopHandle.init(_.vm.eventLoop());
}

pub fn loop(_: *FileResponseStream): *Async.Loop {
    if (comptime bun.Environment.isWindows) {
        return _.eventLoop().loop().uv_loop;
    }
    return _.eventLoop().loop();
}

const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
pub const ref = RefCount.ref;
pub const deref = RefCount.deref;
}
