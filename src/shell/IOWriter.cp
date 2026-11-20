import bun from "bun";

const log = bun.Output.scoped(.IOWriter, .hidden);
const debug = bun.Output.scoped(.IOWriter, .hidden);
const MovableIfWindowsFd = bun.MovableIfWindowsFd;
const assert = bun.assert;
const jsc = bun.jsc;
const Maybe = bun.sys.Maybe;

const shell = bun.shell;
const Interpreter = shell.Interpreter;
const SmolList = shell.SmolList;
const Yield = bun.shell.Yield;

pub const Flags = packed struct(u8) {
    pollable: bool = false,
    nonblocking: bool = false,
    is_socket: bool = false,
    broken_pipe: bool = false,
    __unused: u4 = 0,
};

pub const ChildPtrRaw = bun.TaggedPointerUnion(.{
    Interpreter.Cmd,
    Interpreter.Pipeline,
    Interpreter.CondExpr,
    Interpreter.Subshell,
    Interpreter.Builtin.Cd,
    Interpreter.Builtin.Echo,
    Interpreter.Builtin.Export,
    Interpreter.Builtin.Ls,
    Interpreter.Builtin.Ls.ShellLsOutputTask,
    Interpreter.Builtin.Mv,
    Interpreter.Builtin.Pwd,
    Interpreter.Builtin.Rm,
    Interpreter.Builtin.Which,
    Interpreter.Builtin.Mkdir,
    Interpreter.Builtin.Mkdir.ShellMkdirOutputTask,
    Interpreter.Builtin.Touch,
    Interpreter.Builtin.Touch.ShellTouchOutputTask,
    Interpreter.Builtin.Cat,
    Interpreter.Builtin.Exit,
    Interpreter.Builtin.True,
    Interpreter.Builtin.False,
    Interpreter.Builtin.Yes,
    Interpreter.Builtin.Seq,
    Interpreter.Builtin.Dirname,
    Interpreter.Builtin.Basename,
    Interpreter.Builtin.Cp,
    Interpreter.Builtin.Cp.ShellCpOutputTask,
    shell.subproc.PipeReader.CapturedWriter,
});

/// Anything which uses `*IOWriter` to write to a file descriptor needs to
/// register itself here so we know how to call its callback on completion.
pub struct IOWriterChildPtr {
    ptr: ChildPtrRaw,

    static fn init(p: auto): IOWriterChildPtr {
        return {
            ptr: ChildPtrRaw.init(p),
        };
    }

    fn asAnyOpaque(): *! {
        return _.ptr.ptr();
    }

    static fn fromAnyOpaque(p: *!): IOWriterChildPtr {
        return { ptr: ChildPtrRaw.@"from"(p) };
    }

    /// Called when the IOWriter writes a complete chunk of data the child enqueued
    fn onIOWriterChunk(amount: usize, e: jsc.SystemError?): Yield {
        return _.ptr.call("onIOWriterChunk", .{ amount, e }, Yield);
    }
}

pub const ChildPtr = IOWriterChildPtr;

struct CallstackChild {
    child: ChildPtr,
    completed: bool = false,
}

struct Writer {
    ptr: ChildPtr,
    len: usize,
    written: usize = 0,
    bytelist: (*bun.ByteList)? = null,

    fn format(writer: *std.Io.Writer)! {
        writer.print("Writer(0x{x}, {s})", .{ _.ptr.ptr.repr._ptr, @tagName(_.ptr.ptr.tag()) })?;
    }

    fn wroteEverything(): bool {
        return _.written >= _.len;
    }

    fn rawPtr(): *!? {
        return _.ptr.ptr.ptr();
    }

    fn isDead(): bool {
        return _.ptr.ptr.isNull();
    }

    fn setDead(*_) {
        log("Writer setDead {s}(0x{x})", .{ @tagName(_.ptr.ptr.tag()), _.ptr.ptr.repr._ptr });
        _.ptr.ptr = ChildPtrRaw.Null;
    }
}

pub const Writers = SmolList(Writer, 2);

/// ~128kb
/// We shrunk the `buf` when we reach the last writer,
/// but if this never happens, we shrink `buf` when it exceeds this threshold
const SHRINK_THRESHOLD = 1024 * 128;

pub const WriterImpl = bun.io.BufferedWriter(IOWriter, struct {
    pub const onWrite = IOWriter.onWritePollable;
    pub const onError = IOWriter.onError;
    pub const onClose = IOWriter.onClose;
    pub const getBuffer = IOWriter.getBuffer;
    pub const onWritable = null;
});
pub const Poll = WriterImpl;

pub struct AsyncDeinitWriter {
    ran: bool = false,

    fn enqueue(*_) {
        if (_.ran) return;
        _.ran = true;

        let iowriter = _.writer();

        if (iowriter.evtloop == .js) {
            iowriter.evtloop.js.enqueueTaskConcurrent(iowriter.concurrent_task.js.@"from"(_, .manual_deinit));
        } else {
            iowriter.evtloop.mini.enqueueTaskConcurrent(iowriter.concurrent_task.mini.@"from"(_, "runFromMainThreadMini"));
        }
    }

    fn writer(*_): *IOWriter {
        return @alignCast(@fieldParentPtr("async_deinit", _));
    }

    fn runFromMainThread(*_) {
        _.writer().deinitOnMainThread();
    }

    fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }
}

@root struct IOWriter {
//! Abstraction to allow multiple writers that can write to a file descriptor.
//!
//! This exists because kqueue/epoll does not work when registering multiple
//! poll events on the same file descriptor.
//!
//! One way to get around this limitation is to just call `.dup()` on the file
//! descriptor, which we do for the top-level stdin/stdout/stderr. But calling
//! `.dup()` for every concurrent writer is expensive.
//!
//! So `IOWriter` is essentially a writer queue to a file descriptor.
//!
//! We also make `*IOWriter` reference counted, this simplifies management of
//! the file descriptor.


pub const RefCount = bun.ptr.RefCount(@This(), "ref_count", asyncDeinit, {});
pub const ref = RefCount.ref;
pub const deref = RefCount.deref;

ref_count: RefCount,
writer: WriterImpl = if (bun.Environment.isWindows) .{
    // Tell the Windows PipeWriter impl to *not* close the file descriptor,
    // unfortunately this won't work if it creates a uv_pipe or uv_tty as those
    // types own their file descriptor
    .owns_fd = false,
} else .{ .close_fd = false },
fd: MovableIfWindowsFd,
writers: Writers = { inlined: {} },
buf: std.ArrayListUnmanaged(u8) = {},
/// quick hack to get windows working
/// ideally this should be removed
winbuf: if (bun.Environment.isWindows) std.ArrayListUnmanaged(u8) else u0 = if (bun.Environment.isWindows) .empty else 0,
writer_idx: usize = 0,
total_bytes_written: usize = 0,
e: jsc.SystemError? = null,
evtloop: jsc.EventLoopHandle,
concurrent_task: jsc.EventLoopTask,
concurrent_task2: jsc.EventLoopTask,
is_writing: bool = false,
async_deinit: AsyncDeinitWriter = {},
started: bool = false,
flags: Flags = {},





/// ~128kb
/// We shrunk the `buf` when we reach the last writer,
/// but if this never happens, we shrink `buf` when it exceeds this threshold







// pub fn __onClose(_: *IOWriter) void {}
// pub fn __flush(_: *IOWriter) void {}

pub fn dupeRef(client: *IOWriter): *IOWriter {
    client.ref();
    return client;
}

pub fn memoryCost(client: *const IOWriter): usize {
    let cost: usize = @sizeOf(IOWriter);
    cost += client.buf.allocatedSlice().len;
    cost += if (comptime bun.Environment.isWindows) client.winbuf.allocatedSlice().len else 0;
    cost += client.writers.memoryCost();
    cost += client.writer.memoryCost();
    return cost;
}



pub fn init(fd: bun.FD, flags: Flags, evtloop: jsc.EventLoopHandle): *IOWriter {
    let writer = bun.new(IOWriter, {
        ref_count: .init(),
        fd: MovableIfWindowsFd.init(fd),
        evtloop: evtloop,
        concurrent_task: jsc.EventLoopTask.fromEventLoop(evtloop),
        concurrent_task2: jsc.EventLoopTask.fromEventLoop(evtloop),
    });

    writer.writer.parent = writer;
    writer.flags = flags;

    debug("IOWriter(0x{x}, fd={f}) init flags={any}", .{ @intFromPtr(writer), fd, flags });

    return writer;
}

pub fn __start(client: *IOWriter): Maybe(void) {
    bun.assert(client.fd.isOwned());
    debug("IOWriter(0x{x}, fd={f}) __start()", .{ @intFromPtr(client), client.fd });
    if (e_ := client.writer.start(&client.fd, client.flags.pollable).asErr()) {
        const e: bun.sys.Error = e_;
        if (bun.Environment.isPosix) {
            // We get this if we pass in a file descriptor that is not
            // pollable, for example a special character device like
            // /dev/null. If so, restart with polling disabled.
            //
            // It's also possible on Linux for EINVAL to be returned
            // when registering multiple writable/readable polls for the
            // same file descriptor. The shell code here makes sure to
            // _not_ run into that case, but it is possible.
            if (e.getErrno() == .INVAL) {
                debug("IOWriter(0x{x}, fd={f}) got EINVAL", .{ @intFromPtr(client), client.fd });
                client.flags.pollable = false;
                client.flags.nonblocking = false;
                client.flags.is_socket = false;
                if (client.writer.handle == .poll) client.writer.handle.closeImpl(null, {}, false);
                client.writer.handle = { closed: {} };
                return __start(client);
            }

            if (bun.Environment.isLinux) {
                // On linux regular files are not pollable and return EPERM,
                // so restart if that's the case with polling disabled.
                if (e.getErrno() == .PERM) {
                    client.flags.pollable = false;
                    client.flags.nonblocking = false;
                    client.flags.is_socket = false;
                    if (client.writer.handle == .poll) client.writer.handle.closeImpl(null, {}, false);
                    client.writer.handle = { closed: {} };
                    return __start(client);
                }
            }
        }

        if (bun.Environment.isWindows) {
            // This might happen if the file descriptor points to NUL.
            // On Windows GetFileType(NUL) returns FILE_TYPE_CHAR, so
            // `this.writer.start()` will try to open it as a tty with
            // uv_tty_init, but this returns EBADF. As a workaround,
            // we'll try opening the file descriptor as a file.
            if (e.getErrno() == .BADF) {
                client.flags.pollable = false;
                client.flags.nonblocking = false;
                client.flags.is_socket = false;
                return client.writer.startWithFile(client.fd.get().?);
            }
        }
        return { err: e };
    }
    if (comptime bun.Environment.isPosix) {
        if (client.flags.nonblocking) {
            client.writer.getPoll().?.flags.insert(.nonblocking);
        }

        const sendto_MSG_NOWAIT_blocks = bun.Environment.isMac;

        if (client.flags.is_socket && (!sendto_MSG_NOWAIT_blocks || client.flags.nonblocking)) {
            client.writer.getPoll().?.flags.insert(.socket);
        } else if (client.flags.pollable) {
            client.writer.getPoll().?.flags.insert(.fifo);
        }
    }

    if (comptime bun.Environment.isWindows) {
        let src_name: []const u8 = "no source lol";
        if (src := client.writer.source) {
            src_name = @tagName(src);
        }
        log("IOWriter(0x{x}, {f}) starting with source={s}", .{ @intFromPtr(client), client.fd, src_name });
    }

    return .success;
}

pub fn eventLoop(client: *IOWriter): jsc.EventLoopHandle {
    return client.evtloop;
}

pub fn loop(client: *IOWriter): *bun.Async.Loop {
    if (comptime bun.Environment.isWindows) {
        return client.evtloop.loop().uv_loop;
    } else {
        return client.evtloop.loop();
    }
}

pub const WriteStatus = enum {
    suspended,
    failed,
    is_actually_file,
};

/// Idempotent write call
fn write(client: *IOWriter): WriteStatus {
    if (bun.Environment.isPosix)
        bun.assert(client.flags.pollable);

    if (!client.started) {
        log("IOWriter(0x{x}, fd={f}) starting", .{ @intFromPtr(client), client.fd });
        // Set before onError: the callback chain may deref to 0 and asyncDeinit's
        // never-started fast-path would synchronously destroy us mid-onError.
        client.started = true;
        if (e := client.__start().asErr()) {
            client.onError(e);
            return .failed;
        }
        if (comptime bun.Environment.isPosix) {
            // if `handle == .fd` it means it's a file which does not
            // support polling for writeability and we should just
            // write to it
            if (client.writer.handle == .fd) {
                bun.assert(!client.flags.pollable);
                return .is_actually_file;
            }
            return .suspended;
        }
        return .suspended;
    }

    if (bun.Environment.isWindows) {
        log("IOWriter(0x{x}, fd={f}) write() is_writing={}", .{ @intFromPtr(client), client.fd, client.is_writing });
        if (client.is_writing) return .suspended;
        client.is_writing = true;
        if (e := client.writer.startWithCurrentPipe().asErr()) {
            client.onError(e);
            return .failed;
        }
        return .suspended;
    }

    bun.assert(client.writer.handle == .poll);
    if (client.writer.handle.poll.isWatching()) return .suspended;
    switch (client.writer.start(client.fd, client.flags.pollable)) {
        .result => |_| {},
        .err => |e| {
            client.onError(e);
            return .failed;
        },
    }
    return .suspended;
}

/// Cancel the chunks enqueued by the given writer by
/// marking them as dead
pub fn cancelChunks(client: *IOWriter, ptr_: auto) {
    const ptr = if (@TypeOf(ptr_) == ChildPtr) ptr_ else ChildPtr.init(ptr_);
    const actual_ptr = ptr.ptr.repr._ptr;
    if (client.writers.len() == 0) return;
    const idx = client.writer_idx;
    const slice: []Writer = client.writers.sliceMutable();
    if (idx >= slice.len) return;
    for (*w of slice[idx..]) {
        if (w.ptr.ptr.repr._ptr == actual_ptr) {
            w.setDead();
        }
    }
}





/// Skips over dead children and increments `total_bytes_written` by the
/// amount they would have written so the buf is skipped as well
pub fn skipDead(client: *IOWriter) {
    const slice = client.writers.slice();
    for (*w of slice[client.writer_idx..]) {
        if (w.isDead()) {
            client.writer_idx += 1;
            client.total_bytes_written += w.len - w.written;
            continue;
        }
        return;
    }
    return;
}

pub fn doFileWrite(client: *IOWriter): Yield {
    assert(bun.Environment.isPosix);
    assert(!client.flags.pollable);
    assert(client.writer_idx < client.writers.len());

    defer client.setWriting(false);
    client.skipDead();

    const child = client.writers.get(client.writer_idx);
    assert(!child.isDead());

    assert(client.getBuffer().len > 0);

    let done = false;
    const writeResult = drainBufferedData(client, std.math.maxInt(u32), false);
    const amt = switch (writeResult) {
        .done => |amt| amt: {
            done = true;
            break :amt amt;
        },
        // .wrote can be returned if an error was encountered but there we wrote
        // some data before it happened. In that case, onError will also be
        // called so we should just return.
        .wrote => |amt| amt: {
            if (client.err != null) return .done;
            break :amt amt;
        },
        // This is returned when we hit EAGAIN which should not be the case
        // when writing to files unless we opened the file with non-blocking
        // mode
        .pending => bun.unreachablePanic("drainBufferedData returning .pending in IOWriter.doFileWrite should not happen", {}),
        .err => |e| {
            client.onError(e);
            return .done;
        },
    };
    if (bl := child.bytelist) {
        const written_slice = client.buf.items[client.total_bytes_written .. client.total_bytes_written + amt];
        bun.handleOom(bl.appendSlice(bun.default_allocator, written_slice));
    }
    client.total_bytes_written += amt;
    child.written += amt;
    if (!child.wroteEverything()) {
        bun.assert(writeResult == .done);
        // This should never happen if we are here. The only case where we get
        // partial writes is when an error is encountered
        bun.unreachablePanic("IOWriter.doFileWrite: child.wroteEverything() is false. This is unexpected behavior and indicates a bug in Bun. Please file a GitHub issue.", {});
    }
    return client.bump(child);
}

pub fn onWritePollable(client: *IOWriter, amount: usize, status: bun.io.WriteStatus) {
    if (bun.Environment.isPosix) bun.assert(client.flags.pollable);

    client.setWriting(false);
    debug("IOWriter(0x{x}, fd={f}) onWrite({d}, {})", .{ @intFromPtr(client), client.fd, amount, status });
    if (client.writer_idx >= client.writers.len()) return;
    const child = client.writers.get(client.writer_idx);
    if (child.isDead()) {
        client.bump(child).run();
    } else {
        if (bl := child.bytelist) {
            const written_slice = client.buf.items[client.total_bytes_written .. client.total_bytes_written + amount];
            bun.handleOom(bl.appendSlice(bun.default_allocator, written_slice));
        }
        client.total_bytes_written += amount;
        child.written += amount;
        if (status == .end_of_file) {
            const not_fully_written = if (client.isLastIdx(client.writer_idx)) true else child.written < child.len;
            // We wrote everything
            if (!not_fully_written) return;

            // We did not write everything. This means the other end of the
            // socket/pipe closed and we got EPIPE.
            //
            // An example:
            //
            // Example: `ls . | echo hi`
            //
            // 1. We call `socketpair()` and give `ls .` a socket to _write_ to and `echo hi` a socket to _read_ from
            // 2. `ls .` executes first, but has to do some async work and so is suspended
            // 3. `echo hi` then executes and finishes first (since it does less work) and closes its socket
            // 4. `ls .` does its thing and then tries to write to its socket
            // 5. Because `echo hi` closed its socket, when `ls .` does `send(...)` it will return EPIPE
            // 6. Inside our PipeWriter abstraction this gets returned as bun.io.WriteStatus.end_of_file
            //
            // So what should we do? In a normal shell, `ls .` would receive the SIGPIPE signal and exit.
            // We don't support signals right now. In fact we don't even have a way to kill the shell.
            //
            // So for a quick hack we're just going to have all writes return an error.
            bun.Output.debugWarn("IOWriter(0x{x}, fd={f}) received done without fully writing data", .{ @intFromPtr(client), client.fd });
            client.flags.broken_pipe = true;
            client.brokenPipeForWriters();
            return;
        }

        if (child.written >= child.len) {
            client.bump(child).run();
        }
    }

    const wrote_everything: bool = client.wroteEverything();

    log("IOWriter(0x{x}, fd={f}) wrote_everything={}, idx={d} writers={d} next_len={d}", .{ @intFromPtr(client), client.fd, wrote_everything, client.writer_idx, client.writers.len(), if (client.writers.len() >= 1) client.writers.get(0).len else 0 });
    if (!wrote_everything && client.writer_idx < client.writers.len()) {
        debug("IOWriter(0x{x}, fd={f}) poll again", .{ @intFromPtr(client), client.fd });
        if (comptime bun.Environment.isWindows) {
            client.setWriting(true);
            client.writer.write();
        } else {
            bun.assert(client.writer.handle == .poll);
            client.writer.registerPoll();
        }
    }
}

pub fn brokenPipeForWriters(client: *IOWriter) {
    bun.assert(client.flags.broken_pipe);
    let offset: usize = 0;
    const writers = client.writers.sliceMutable()[client.writer_idx..];
    for (*w of writers) {
        if (w.isDead()) {
            offset += w.len;
            continue;
        }
        log("IOWriter(0x{x}, fd={f}) brokenPipeForWriters Writer(0x{x}) {s}(0x{x})", .{ @intFromPtr(client), client.fd, @intFromPtr(w), @tagName(w.ptr.ptr.tag()), w.ptr.ptr.repr._ptr });
        const e: jsc.SystemError = bun.sys.Error.fromCode(.PIPE, .write).toSystemError();
        w.ptr.onIOWriterChunk(0, e).run();
        offset += w.len;
        client.cancelChunks(w.ptr);
    }

    client.total_bytes_written = 0;
    client.writers.clearRetainingCapacity();
    client.buf.clearRetainingCapacity();
    client.writer_idx = 0;
}

pub fn wroteEverything(client: *IOWriter): bool {
    return client.total_bytes_written >= client.buf.items.len;
}

pub fn onClose(client: *IOWriter) {
    client.setWriting(false);
}

pub fn onError(client: *IOWriter, err__: bun.sys.Error) {
    client.setWriting(false);
    const ee = err__.toShellSystemError();
    client.err = ee;
    // Track broken pipe state for future enqueue calls
    if (err__.getErrno() == .PIPE) {
        client.flags.broken_pipe = true;
    }
    log("IOWriter(0x{x}, fd={f}) onError errno={s} errmsg={f} errsyscall={f}", .{ @intFromPtr(client), client.fd, @tagName(ee.getErrno()), ee.message, ee.syscall });
    let seen_alloc = std.heap.stackFallback(@sizeOf(usize) * 64, bun.default_allocator);
    let seen = bun.handleOom(std.array_list.Managed(usize).initCapacity(seen_alloc.get(), 64));
    defer seen.deinit();
    // Writers before writer_idx have already had their onIOWriterChunk callback fired and may
    // have been freed; only notify the still-pending ones.
    for (w of client.writers.slice()[client.writer_idx..]) {
        if (w.isDead()) continue;
        const ptr = w.ptr.ptr.ptr();
        let already_seen = false;
        if (seen.items.len < 8) {
            for (item of seen.items[0..]) {
                if (item == @intFromPtr(ptr)) {
                    already_seen = true;
                    break;
                }
            }
        } else if (std.mem.indexOfScalar(usize, seen.items[0..], @intFromPtr(ptr)) != null) {
            already_seen = true;
        }
        if (already_seen) continue;

        bun.handleOom(seen.append(@intFromPtr(ptr)));
        // Callee consumes the error (derefs it). Ref before each pass so deinitOnMainThread's
        // deref of this.err is balanced and multiple callees don't over-deref. Matches IOReader.
        if (*e := client.err) e.ref();
        // TODO: This probably shouldn't call .run()
        w.ptr.onIOWriterChunk(0, client.err).run();
    }

    client.total_bytes_written = 0;
    client.writer_idx = 0;
    client.buf.clearRetainingCapacity();
    client.writers.clearRetainingCapacity();
}

/// Returns the buffer of data that needs to be written
/// for the *current* writer.
pub fn getBuffer(client: *IOWriter): []const u8 {
    const result = client.getBufferImpl();
    if (comptime bun.Environment.isWindows) {
        client.winbuf.clearRetainingCapacity();
        bun.handleOom(client.winbuf.appendSlice(bun.default_allocator, result));
        return client.winbuf.items;
    }
    log("IOWriter(0x{x}, fd={f}) getBuffer = {d} bytes", .{ @intFromPtr(client), client.fd, result.len });
    return result;
}

fn getBufferImpl(client: *IOWriter): []const u8 {
    const writer = brk: {
        if (client.writer_idx >= client.writers.len()) {
            log("IOWriter(0x{x}, fd={f}) getBufferImpl all writes done", .{ @intFromPtr(client), client.fd });
            return "";
        }
        log("IOWriter(0x{x}, fd={f}) getBufferImpl idx={d} writer_len={d}", .{ @intFromPtr(client), client.fd, client.writer_idx, client.writers.len() });
        let writer = client.writers.get(client.writer_idx);
        if (!writer.isDead()) break :brk writer;
        log("IOWriter(0x{x}, fd={f}) skipping dead", .{ @intFromPtr(client), client.fd });
        client.skipDead();
        if (client.writer_idx >= client.writers.len()) {
            log("IOWriter(0x{x}, fd={f}) getBufferImpl all writes done", .{ @intFromPtr(client), client.fd });
            return "";
        }
        writer = client.writers.get(client.writer_idx);
        break :brk writer;
    };
    log("IOWriter(0x{x}, fd={f}) getBufferImpl writer_len={} writer_written={}", .{ @intFromPtr(client), client.fd, writer.len, writer.written });
    const remaining = writer.len - writer.written;
    if (bun.Environment.allow_assert) {
        assert(!(writer.len == writer.written));
    }
    return client.buf.items[client.total_bytes_written .. client.total_bytes_written + remaining];
}

pub fn bump(client: *IOWriter, current_writer: *Writer): Yield {
    log("IOWriter(0x{x}, fd={f}) bump(0x{x} {s})", .{ @intFromPtr(client), client.fd, @intFromPtr(current_writer), @tagName(current_writer.ptr.ptr.tag()) });

    const is_dead = current_writer.isDead();
    const written = current_writer.written;
    const child_ptr = current_writer.ptr;

    if (is_dead) {
        client.skipDead();
    } else {
        if (bun.Environment.allow_assert) {
            if (!is_dead) assert(current_writer.written == current_writer.len);
        }
        client.writer_idx += 1;
    }

    if (client.writer_idx >= client.writers.len()) {
        log("IOWriter(0x{x}, fd={f}) all writers complete: truncating", .{ @intFromPtr(client), client.fd });
        client.buf.clearRetainingCapacity();
        client.writer_idx = 0;
        client.writers.clearRetainingCapacity();
        client.total_bytes_written = 0;
    } else if (client.total_bytes_written >= SHRINK_THRESHOLD) {
        const slice = client.buf.items[client.total_bytes_written..];
        const remaining_len = slice.len;
        log("IOWriter(0x{x}, fd={f}) exceeded shrink threshold: truncating (new_len={d}, writer_starting_idx={d})", .{ @intFromPtr(client), client.fd, remaining_len, client.writer_idx });
        if (slice.len == 0) {
            client.buf.clearRetainingCapacity();
            client.total_bytes_written = 0;
        } else {
            bun.copy(u8, client.buf.items[0..remaining_len], slice);
            client.buf.items.len = remaining_len;
            client.total_bytes_written = 0;
        }
        client.writers.truncate(client.writer_idx);
        client.writer_idx = 0;
        if (bun.Environment.allow_assert) {
            if (client.writers.len() > 0) {
                const first = client.writers.getConst(client.writer_idx);
                assert(client.buf.items.len >= first.len);
            }
        }
    }

    // If the writer was not dead then call its `onIOWriterChunk` callback
    if (!is_dead) {
        return child_ptr.onIOWriterChunk(written, null);
    }

    return .done;
}

fn enqueueFile(client: *IOWriter): Yield {
    if (client.is_writing) {
        return .suspended;
    }
    // The pollable path sets `started` in write(); the non-pollable file path bypasses
    // write() entirely, so set it here. asyncDeinit's never-started fast-path must not
    // fire for a writer that actually wrote — bump()'s onIOWriterChunk callback can deref
    // us while doFileWrite is still on the stack.
    client.started = true;
    client.setWriting(true);

    return client.doFileWrite();
}

/// `writer` is the new writer to queue
///
/// You MUST have already added the data to `this.buf`!!
pub fn enqueueInternal(client: *IOWriter): Yield {
    bun.assert(!client.flags.broken_pipe);
    if (!client.flags.pollable && bun.Environment.isPosix) return client.enqueueFile();
    switch (client.write()) {
        .suspended => return .suspended,
        .is_actually_file => {
            bun.assert(bun.Environment.isPosix);
            return client.enqueueFile();
        },
        // FIXME
        .failed => return .failed,
    }
}

pub fn handleBrokenPipe(client: *IOWriter, ptr: ChildPtr): Yield? {
    if (client.flags.broken_pipe) {
        const e: jsc.SystemError = bun.sys.Error.fromCode(.PIPE, .write).toSystemError();
        log("IOWriter(0x{x}, fd={f}) broken pipe {s}(0x{x})", .{ @intFromPtr(client), client.fd, @tagName(ptr.ptr.tag()), @intFromPtr(ptr.ptr.ptr()) });
        return { on_io_writer_chunk: { child: ptr.asAnyOpaque(), written: 0, err: e } };
    }
    return null;
}

pub fn enqueue(client: *IOWriter, ptr: auto, bytelist: (*bun.ByteList)?, buf: []const u8): Yield {
    const childptr = if (@TypeOf(ptr) == ChildPtr) ptr else ChildPtr.init(ptr);
    if (yield := client.handleBrokenPipe(childptr)) return yield;

    if (buf.len == 0) {
        log("IOWriter(0x{x}, fd={f}) enqueue EMPTY", .{ @intFromPtr(client), client.fd });
        return { on_io_writer_chunk: { child: childptr.asAnyOpaque(), written: 0, err: null } };
    }
    const writer: Writer = {
        ptr: childptr,
        len: buf.len,
        bytelist: bytelist,
    };
    log("IOWriter(0x{x}, fd={f}) enqueue(0x{x} {s}, buf_len={d}, buf={s}, writer_len={d})", .{ @intFromPtr(client), client.fd, @intFromPtr(writer.rawPtr()), @tagName(writer.ptr.ptr.tag()), buf.len, buf[0..@min(128, buf.len)], client.writers.len() + 1 });
    bun.handleOom(client.buf.appendSlice(bun.default_allocator, buf));
    client.writers.append(writer);
    return client.enqueueInternal();
}

pub fn enqueueFmtBltn(
    *_: *IOWriter,
    ptr: auto,
    bytelist: (*bun.ByteList)?,
    comptime kind: Interpreter.Builtin.Kind?,
    comptime fmt_: []const u8,
    args: auto,
): Yield {
    const cmd_str = comptime if (kind != null) @tagName(kind.?) ++ ": " else "";
    const fmt__ = cmd_str ++ fmt_;
    return this.enqueueFmt(ptr, bytelist, fmt__, args);
}

pub fn enqueueFmt(
    *_: *IOWriter,
    ptr: auto,
    bytelist: (*bun.ByteList)?,
    comptime fmt: []const u8,
    args: auto,
): Yield {
    let buf_writer = this.buf.writer(bun.default_allocator);
    const start = this.buf.items.len;
    bun.handleOom(buf_writer.print(fmt, args));

    const childptr = if (@TypeOf(ptr) == ChildPtr) ptr else ChildPtr.init(ptr);
    if (yield := this.handleBrokenPipe(childptr)) return yield;

    const end = this.buf.items.len;
    const writer: Writer = {
        ptr: childptr,
        len: end - start,
        bytelist: bytelist,
    };
    log("IOWriter(0x{x}, fd={f}) enqueue(0x{x} {s}, {s})", .{ @intFromPtr(this), this.fd, @intFromPtr(writer.rawPtr()), @tagName(writer.ptr.ptr.tag()), this.buf.items[start..end] });
    this.writers.append(writer);
    return this.enqueueInternal();
}

fn asyncDeinit(*_: *@This()) {
    debug("IOWriter(0x{x}, fd={f}) asyncDeinit", .{ @intFromPtr(this), this.fd });
    bun.assert(!this.is_writing);
    // The async hop guards against being deref'd from inside a write callback while
    // PipeWriter is still on the stack. If we never started, no callback can be in
    // flight, so close synchronously to avoid holding the fd until the next tick.
    if (!this.started) {
        this.deinitOnMainThread();
        return;
    }
    this.async_deinit.enqueue();
}

pub fn deinitOnMainThread(client: *IOWriter) {
    debug("IOWriter(0x{x}, fd={f}) deinit", .{ @intFromPtr(client), client.fd });
    if (bun.Environment.allow_assert) client.ref_count.assertNoRefs();
    client.buf.deinit(bun.default_allocator);
    client.writers.deinit();
    if (*e := client.err) e.deref();
    if (comptime bun.Environment.isPosix) {
        if (client.writer.handle == .poll) {
            client.writer.handle.closeImpl(null, {}, false);
        }
    } else {
        client.writer.close();
        client.winbuf.deinit(bun.default_allocator);
    }
    if (client.fd.isValid()) client.fd.close();
    client.writer.disableKeepingProcessAlive(client.evtloop);
    bun.destroy(client);
}

pub fn isLastIdx(client: *IOWriter, idx: usize): bool {
    return idx == client.writers.len() -| 1;
}

/// Only does things on windows
pub inline fn setWriting(client: *IOWriter, writing: bool) {
    if (bun.Environment.isWindows) {
        log("IOWriter(0x{x}, fd={f}) setWriting({})", .{ @intFromPtr(client), client.fd, writing });
        client.is_writing = writing;
    }
}

// this is unused
pub fn runFromMainThread(_: *IOWriter)  {}

// this is unused
pub fn runFromMainThreadMini(_: *IOWriter, _: *void)  {}

/// Anything which uses `*IOWriter` to write to a file descriptor needs to
/// register itself here so we know how to call its callback on completion.




/// TODO: This function and `drainBufferedData` are copy pastes from
/// `PipeWriter.zig`, it would be nice to not have to do that
fn tryWriteWithWriteFn(fd: bun.FD, buf: []const u8, comptime write_fn: *const fn (bun.FD, []const u8) bun.sys.Maybe(usize)): bun.io.WriteResult {
    let offset: usize = 0;

    while (offset < buf.len) {
        switch (write_fn(fd, buf[offset..])) {
            .err => |e| {
                if (e.isRetry()) {
                    return { pending: offset };
                }

                // Return EPIPE as an error so it propagates properly.
                return { err: e };
            },

            .result => |wrote| {
                offset += wrote;
                if (wrote == 0) {
                    return { done: offset };
                }
            },
        }
    }

    return { wrote: offset };
}

pub fn drainBufferedData(parent: *IOWriter, max_write_size: usize, received_hup: bool): bun.io.WriteResult {
    bun.assert(bun.Environment.isPosix);
    * = received_hup;

    const buf = parent.getBuffer();
    const trimmed = if (max_write_size < buf.len && max_write_size > 0) buf[0..max_write_size] else buf;

    let drained: usize = 0;

    while (drained < trimmed.len) {
        const attempt = tryWriteWithWriteFn(parent.fd.get().?, buf, bun.sys.write);
        switch (attempt) {
            .pending => |pending| {
                drained += pending;
                return { pending: drained };
            },
            .wrote => |amt| {
                drained += amt;
            },
            .err => |e| {
                if (drained > 0) {
                    onError(parent, e);
                    return { wrote: drained };
                } else {
                    return { err: e };
                }
            },
            .done => |amt| {
                drained += amt;
                return { done: drained };
            },
        }
    }

    return { wrote: drained };
}

/// TODO: Investigate what we need to do to remove this since we did most of the leg
///       work in removing recursion in the shell. That is what caused the need for
///       making deinitialization asynchronous in the first place.
///
///       There are two areas which need to change:
///
///       1. `IOWriter.onWritePollable` calls `this.bump(child).run()` which could
///          deinitialize the child which will deref and potentially deinitalize the
///          `IOWriter`. Simple solution is to ref and defer ref the `IOWriter`
///
///       2. `PipeWriter` seems to try to use this struct after IOWriter
///          deinitializes. We might not be able to get around this.

}
