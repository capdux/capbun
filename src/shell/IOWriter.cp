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

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

@packed(u8) pub struct Flags {
    pollable: bool = false,
    nonblocking: bool = false,
    is_socket: bool = false,
    broken_pipe: bool = false,
    __unused: u4 = 0,
}

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
        return unsafe { @alignCast(@fieldParentPtr("async_deinit", _)) };
    }

    fn runFromMainThread(*_) {
        _.writer().deinitOnMainThread();
    }

    fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }
}

pub enum _WriteStatus {
    suspended,
    failed,
    is_actually_file,
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
fatal_err: bun.sys.Error? = null,
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

fn dupeRef(*_): *IOWriter {
    _.ref();
    return _;
}

fn memoryCost(): usize {
    let cost: usize = @sizeOf(IOWriter);
    cost += _.buf.allocatedSlice().len;
    cost += if (comptime bun.Environment.isWindows) _.winbuf.allocatedSlice().len else 0;
    cost += _.writers.memoryCost();
    cost += _.writer.memoryCost();
    return cost;
}



static fn init(fd: bun.FD, flags: Flags, evtloop: jsc.EventLoopHandle): *IOWriter {
    let writer = bun.new(IOWriter, {
        ref_count: .init(),
        fd: MovableIfWindowsFd.init(fd),
        evtloop: evtloop,
        concurrent_task: jsc.EventLoopTask.fromEventLoop(evtloop),
        concurrent_task2: jsc.EventLoopTask.fromEventLoop(evtloop),
    });

    writer.writer.parent = writer;
    writer.flags = flags;

    debug("IOWriter(0x{x}, fd={f}) init flags={any}", .{ unsafe { @intFromPtr(writer) }, fd, flags });

    return writer;
}

fn __start(*_): Maybe(void) {
    bun.assert(_.fd.isOwned());
    debug("IOWriter(0x{x}, fd={f}) __start()", .{ unsafe { @intFromPtr(_) }, _.fd });
    if (e_ := _.writer.start(&_.fd, _.flags.pollable).asErr()) {
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
                debug("IOWriter(0x{x}, fd={f}) got EINVAL", .{ unsafe { @intFromPtr(_) }, _.fd });
                _.flags.pollable = false;
                _.flags.nonblocking = false;
                _.flags.is_socket = false;
                if (_.writer.handle == .poll) _.writer.handle.closeImpl(null, {}, false);
                _.writer.handle = .closed;
                return _.__start();
            }

            if (bun.Environment.isLinux) {
                // On linux regular files are not pollable and return EPERM,
                // so restart if that's the case with polling disabled.
                if (e.getErrno() == .PERM) {
                    _.flags.pollable = false;
                    _.flags.nonblocking = false;
                    _.flags.is_socket = false;
                    if (_.writer.handle == .poll) _.writer.handle.closeImpl(null, {}, false);
                    _.writer.handle = .closed;
                    return _.__start();
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
                _.flags.pollable = false;
                _.flags.nonblocking = false;
                _.flags.is_socket = false;
                return _.writer.startWithFile(_.fd.get().?);
            }
        }
        return { err: e };
    }
    if (comptime bun.Environment.isPosix) {
        if (_.flags.nonblocking) {
            _.writer.getPoll().?.flags.insert(.nonblocking);
        }

        const sendto_MSG_NOWAIT_blocks = bun.Environment.isMac;

        if (_.flags.is_socket && (!sendto_MSG_NOWAIT_blocks || _.flags.nonblocking)) {
            _.writer.getPoll().?.flags.insert(.socket);
        } else if (_.flags.pollable) {
            _.writer.getPoll().?.flags.insert(.fifo);
        }
    }

    if (comptime bun.Environment.isWindows) {
        let src_name: []const u8 = "no source lol";
        if (src := _.writer.source) {
            src_name = @tagName(src);
        }
        log("IOWriter(0x{x}, {f}) starting with source={s}", .{ unsafe { @intFromPtr(_) }, _.fd, src_name });
    }

    return .success;
}

fn eventLoop(*_): jsc.EventLoopHandle {
    return _.evtloop;
}

fn loop(*_): *bun.Async.Loop {
    if (comptime bun.Environment.isWindows) {
        return _.evtloop.loop().uv_loop;
    } else {
        return _.evtloop.loop();
    }
}

pub const WriteStatus = _WriteStatus;

/// Idempotent write call
fn write(*_): WriteStatus {
    if (bun.Environment.isPosix)
        bun.assert(_.flags.pollable);

    if (!_.started) {
        log("IOWriter(0x{x}, fd={f}) starting", .{ unsafe { @intFromPtr(_) }, _.fd });
        // Set before onError: the callback chain may deref to 0 and asyncDeinit's
        // never-started fast-path would synchronously destroy us mid-onError.
        _.started = true;
        if (e := _.__start().asErr()) {
            _.onError(e);
            return .failed;
        }
        if (comptime bun.Environment.isPosix) {
            // if `handle == .fd` it means it's a file which does not
            // support polling for writeability and we should just
            // write to it
            if (_.writer.handle == .fd) {
                bun.assert(!_.flags.pollable);
                return .is_actually_file;
            }
            return .suspended;
        }
        return .suspended;
    }

    if (bun.Environment.isWindows) {
        log("IOWriter(0x{x}, fd={f}) write() is_writing={}", .{ unsafe { @intFromPtr(_) }, _.fd, _.is_writing });
        if (_.is_writing) return .suspended;
        _.is_writing = true;
        if (e := _.writer.startWithCurrentPipe().asErr()) {
            _.onError(e);
            return .failed;
        }
        return .suspended;
    }

    bun.assert(_.writer.handle == .poll);
    if (_.writer.handle.poll.isWatching()) return .suspended;
    switch (_.writer.start(_.fd, _.flags.pollable)) {
        .result => |_| {},
        .err => |e| {
            _.onError(e);
            return .failed;
        },
    }
    return .suspended;
}

/// Cancel the chunks enqueued by the given writer by
/// marking them as dead
pub fn cancelChunks(*_, ptr_: auto) {
    const ptr = if (@TypeOf(ptr_) == ChildPtr) ptr_ else ChildPtr.init(ptr_);
    const actual_ptr = ptr.ptr.repr._ptr;
    if (_.writers.len() == 0) return;
    const idx = _.writer_idx;
    const slice: []Writer = _.writers.sliceMutable();
    if (idx >= slice.len) return;
    for (*w of slice[idx..]) {
        if (w.ptr.ptr.repr._ptr == actual_ptr) {
            w.setDead();
        }
    }
}





/// Skips over dead children and increments `total_bytes_written` by the
/// amount they would have written so the buf is skipped as well
pub fn skipDead(*_) {
    const slice = _.writers.slice();
    for (*w of slice[_.writer_idx..]) {
        if (w.isDead()) {
            _.writer_idx += 1;
            _.total_bytes_written += w.len - w.written;
            continue;
        }
        return;
    }
    return;
}

pub fn doFileWrite(*_): Yield {
    assert(bun.Environment.isPosix);
    assert(!_.flags.pollable);
    assert(_.writer_idx < _.writers.len());

    defer _.setWriting(false);
    _.skipDead();

    const child = _.writers.get(_.writer_idx);
    assert(!child.isDead());

    assert(_.getBuffer().len > 0);

    let done = false;
    const writeResult = _.drainBufferedData(std.math.maxInt(u32), false);
    const amt = switch (writeResult) {
        .done => |amt| amt: {
            done = true;
            break :amt amt;
        },
        // .wrote can be returned if an error was encountered but there we wrote
        // some data before it happened. In that case, onError will also be
        // called so we should just return.
        .wrote => |amt| amt: {
            if (_.e != null) return .done;
            break :amt amt;
        },
        // This is returned when we hit EAGAIN which should not be the case
        // when writing to files unless we opened the file with non-blocking
        // mode
        .pending => bun.unreachablePanic("drainBufferedData returning .pending in IOWriter.doFileWrite should not happen", {}),
        .err => |e| {
            return _.onErrorReturnYield(e);
        },
    };
    if (bl := child.bytelist) {
        const written_slice = _.buf.items[_.total_bytes_written .. _.total_bytes_written + amt];
        bun.handleOom(bl.appendSlice(bun.default_allocator, written_slice));
    }
    _.total_bytes_written += amt;
    child.written += amt;
    if (!child.wroteEverything()) {
        bun.assert(writeResult == .done);
        // This should never happen if we are here. The only case where we get
        // partial writes is when an error is encountered
        bun.unreachablePanic("IOWriter.doFileWrite: child.wroteEverything() is false. This is unexpected behavior and indicates a bug in Bun. Please file a GitHub issue.", {});
    }
    return _.bump(child);
}

pub fn onWritePollable(*_, amount: usize, status: bun.io.WriteStatus) {
    if (bun.Environment.isPosix) bun.assert(_.flags.pollable);

    _.setWriting(false);
    debug("IOWriter(0x{x}, fd={f}) onWrite({d}, {})", .{ unsafe { @intFromPtr(_) }, _.fd, amount, status });
    if (_.writer_idx >= _.writers.len()) return;
    const child = _.writers.get(_.writer_idx);
    if (child.isDead()) {
        let bump_y = _.bump(child);
        bump_y.run();
    } else {
        if (bl := child.bytelist) {
            const written_slice = _.buf.items[_.total_bytes_written .. _.total_bytes_written + amount];
            bun.handleOom(bl.appendSlice(bun.default_allocator, written_slice));
        }
        _.total_bytes_written += amount;
        child.written += amount;
        if (status == .end_of_file) {
            const not_fully_written = if (_.isLastIdx(_.writer_idx)) true else child.written < child.len;
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
            bun.Output.debugWarn("IOWriter(0x{x}, fd={f}) received done without fully writing data", .{ unsafe { @intFromPtr(_) }, _.fd });
            _.flags.broken_pipe = true;
            _.brokenPipeForWriters();
            return;
        }

        if (child.written >= child.len) {
            let bump_y = _.bump(child);
        bump_y.run();
        }
    }

    const wrote_everything: bool = _.wroteEverything();

    log("IOWriter(0x{x}, fd={f}) wrote_everything={}, idx={d} writers={d} next_len={d}", .{ unsafe { @intFromPtr(_) }, _.fd, wrote_everything, _.writer_idx, _.writers.len(), if (_.writers.len() >= 1) _.writers.get(0).len else 0 });
    if (!wrote_everything && _.writer_idx < _.writers.len()) {
        debug("IOWriter(0x{x}, fd={f}) poll again", .{ unsafe { @intFromPtr(_) }, _.fd });
        if (comptime bun.Environment.isWindows) {
            _.setWriting(true);
            _.writer.write();
        } else {
            bun.assert(_.writer.handle == .poll);
            _.writer.registerPoll();
        }
    }
}

pub fn brokenPipeForWriters(*_) {
    bun.assert(_.flags.broken_pipe);
    let offset: usize = 0;
    const writers = _.writers.sliceMutable()[_.writer_idx..];
    for (*w of writers) {
        if (w.isDead()) {
            offset += w.len;
            continue;
        }
        log("IOWriter(0x{x}, fd={f}) brokenPipeForWriters Writer(len={d}) {s}(0x{x})", .{ unsafe { @intFromPtr(_) }, _.fd, w.len, @tagName(w.ptr.ptr.tag()), w.ptr.ptr.repr._ptr });
        const e: jsc.SystemError = bun.sys.Error.fromCode(.PIPE, .write).toSystemError();
        let chunk_y = w.ptr.onIOWriterChunk(0, e);
        chunk_y.run();
        offset += w.len;
        _.cancelChunks(w.ptr);
    }

    _.total_bytes_written = 0;
    _.writers.clearRetainingCapacity();
    _.buf.clearRetainingCapacity();
    _.writer_idx = 0;
}

pub fn wroteEverything(): bool {
    return _.total_bytes_written >= _.buf.items.len;
}

pub fn onClose(*_) {
    _.setWriting(false);
}

pub fn onError(*_, err__: bun.sys.Error) {
    * = _.onErrorImpl(err__, false);
}

pub fn onErrorReturnYield(*_, err__: bun.sys.Error): Yield {
    return _.onErrorImpl(err__, true);
}

fn onErrorImpl(*_, err__: bun.sys.Error, return_first_yield: bool): Yield {
    _.setWriting(false);
    const ee = err__.toShellSystemError();
    _.e = ee;
    // Track broken pipe state for future enqueue calls
    if (err__.getErrno() == .PIPE) {
        _.flags.broken_pipe = true;
    }
    _.fatal_err = err__;
    log("IOWriter(0x{x}, fd={f}) onError errno={s} errmsg={f} errsyscall={f}", .{ unsafe { @intFromPtr(_) }, _.fd, @tagName(ee.getErrno()), ee.message, ee.syscall });
    let seen_alloc = std.heap.stackFallback(@sizeOf(usize) * 64, bun.default_allocator);
    let seen = bun.handleOom(std.array_list.Managed(usize).initCapacity(seen_alloc.get(), 64));
    const _seen_guard = DeinitGuard<@TypeOf(seen)>{ ptr: unsafe { &seen } };
    let first_yield: Yield = .done;
    let has_first = false;
    // Writers before writer_idx have already had their onIOWriterChunk callback fired and may
    // have been freed; only notify the still-pending ones.
    for (w of _.writers.slice()[_.writer_idx..]) {
        if (w.isDead()) continue;
        const ptr = w.ptr.ptr.ptr();
        let already_seen = false;
        if (seen.items.len < 8) {
            for (item of seen.items[0..]) {
                if (item == unsafe { @intFromPtr(ptr) }) {
                    already_seen = true;
                    break;
                }
            }
        } else if (std.mem.indexOfScalar(usize, seen.items[0..], unsafe { @intFromPtr(ptr) }) != null) {
            already_seen = true;
        }
        if (already_seen) continue;

        bun.handleOom(seen.append(unsafe { @intFromPtr(ptr) }));
        // Callee consumes the error (derefs it). Ref before each pass so deinitOnMainThread's
        // deref of this.err is balanced and multiple callees don't over-deref. Matches IOReader.
        if (*e := _.e) e.ref();
        if (return_first_yield && !has_first) {
            has_first = true;
            first_yield = Yield{ .on_io_writer_chunk = .{
                .child = ptr,
                .written = 0,
                .e = _.e,
            } };
        } else {
            // TODO: This probably shouldn't call .run()
            let err_y = w.ptr.onIOWriterChunk(0, _.e);
            err_y.run();
        }
    }

    _.total_bytes_written = 0;
    _.writer_idx = 0;
    _.buf.clearRetainingCapacity();
    _.writers.clearRetainingCapacity();
    return first_yield;
}

/// Returns the buffer of data that needs to be written
/// for the *current* writer.
pub fn getBuffer(*_): []const u8 {
    const result = _.getBufferImpl();
    if (comptime bun.Environment.isWindows) {
        _.winbuf.clearRetainingCapacity();
        bun.handleOom(_.winbuf.appendSlice(bun.default_allocator, result));
        return _.winbuf.items;
    }
    log("IOWriter(0x{x}, fd={f}) getBuffer = {d} bytes", .{ unsafe { @intFromPtr(_) }, _.fd, result.len });
    return result;
}

fn getBufferImpl(*_): []const u8 {
    const writer = brk: {
        if (_.writer_idx >= _.writers.len()) {
            log("IOWriter(0x{x}, fd={f}) getBufferImpl all writes done", .{ unsafe { @intFromPtr(_) }, _.fd });
            return "";
        }
        log("IOWriter(0x{x}, fd={f}) getBufferImpl idx={d} writer_len={d}", .{ unsafe { @intFromPtr(_) }, _.fd, _.writer_idx, _.writers.len() });
        let writer = _.writers.get(_.writer_idx);
        if (!writer.isDead()) break :brk writer;
        log("IOWriter(0x{x}, fd={f}) skipping dead", .{ unsafe { @intFromPtr(_) }, _.fd });
        _.skipDead();
        if (_.writer_idx >= _.writers.len()) {
            log("IOWriter(0x{x}, fd={f}) getBufferImpl all writes done", .{ unsafe { @intFromPtr(_) }, _.fd });
            return "";
        }
        writer = _.writers.get(_.writer_idx);
        break :brk writer;
    };
    log("IOWriter(0x{x}, fd={f}) getBufferImpl writer_len={} writer_written={}", .{ unsafe { @intFromPtr(_) }, _.fd, writer.len, writer.written });
    const remaining = writer.len - writer.written;
    if (bun.Environment.allow_assert) {
        assert(!(writer.len == writer.written));
    }
    return _.buf.items[_.total_bytes_written .. _.total_bytes_written + remaining];
}

pub fn bump(*_, current_writer: *Writer): Yield {
    log("IOWriter(0x{x}, fd={f}) bump(0x{x} {s})", .{ unsafe { @intFromPtr(_) }, _.fd, unsafe { @intFromPtr(current_writer) }, @tagName(current_writer.ptr.ptr.tag()) });

    const is_dead = current_writer.isDead();
    const written = current_writer.written;
    const child_ptr = current_writer.ptr;

    if (is_dead) {
        _.skipDead();
    } else {
        if (bun.Environment.allow_assert) {
            if (!is_dead) assert(current_writer.written == current_writer.len);
        }
        _.writer_idx += 1;
    }

    if (_.writer_idx >= _.writers.len()) {
        log("IOWriter(0x{x}, fd={f}) all writers complete: truncating", .{ unsafe { @intFromPtr(_) }, _.fd });
        _.buf.clearRetainingCapacity();
        _.writer_idx = 0;
        _.writers.clearRetainingCapacity();
        _.total_bytes_written = 0;
    } else if (_.total_bytes_written >= SHRINK_THRESHOLD) {
        const slice = _.buf.items[_.total_bytes_written..];
        const remaining_len = slice.len;
        log("IOWriter(0x{x}, fd={f}) exceeded shrink threshold: truncating (new_len={d}, writer_starting_idx={d})", .{ unsafe { @intFromPtr(_) }, _.fd, remaining_len, _.writer_idx });
        if (slice.len == 0) {
            _.buf.clearRetainingCapacity();
            _.total_bytes_written = 0;
        } else {
            bun.copy(u8, _.buf.items[0..remaining_len], slice);
            _.buf.items.len = remaining_len;
            _.total_bytes_written = 0;
        }
        _.writers.truncate(_.writer_idx);
        _.writer_idx = 0;
        if (bun.Environment.allow_assert) {
            if (_.writers.len() > 0) {
                const first = _.writers.getConst(_.writer_idx);
                assert(_.buf.items.len >= first.len);
            }
        }
    }

    // If the writer was not dead then call its `onIOWriterChunk` callback
    if (!is_dead) {
        return child_ptr.onIOWriterChunk(written, null);
    }

    return .done;
}

fn enqueueFile(*_): Yield {
    if (_.is_writing) {
        return .suspended;
    }
    // The pollable path sets `started` in write(); the non-pollable file path bypasses
    // write() entirely, so set it here. asyncDeinit's never-started fast-path must not
    // fire for a writer that actually wrote — bump()'s onIOWriterChunk callback can deref
    // us while doFileWrite is still on the stack.
    _.started = true;
    _.setWriting(true);

    return _.doFileWrite();
}

/// `writer` is the new writer to queue
///
/// You MUST have already added the data to `this.buf`!!
pub fn enqueueInternal(*_): Yield {
    bun.assert(!_.flags.broken_pipe);
    bun.assert(_.fatal_err == null);
    if (!_.flags.pollable && bun.Environment.isPosix) return _.enqueueFile();
    switch (_.write()) {
        .suspended => return .suspended,
        .is_actually_file => {
            bun.assert(bun.Environment.isPosix);
            return _.enqueueFile();
        },
        // FIXME
        .failed => return .failed,
    }
}

pub fn handleDeadWriter(*_, ptr: ChildPtr): Yield? {
    if (_.flags.broken_pipe) {
        const e: jsc.SystemError = bun.sys.Error.fromCode(.PIPE, .write).toSystemError();
        log("IOWriter(0x{x}, fd={f}) broken pipe {s}(0x{x})", .{ unsafe { @intFromPtr(_) }, _.fd, @tagName(ptr.ptr.tag()), unsafe { @intFromPtr(ptr.ptr.ptr()) } });
        return { on_io_writer_chunk: { child: ptr.asAnyOpaque(), written: 0, e: e } };
    }
    if (fe := _.fatal_err) {
        return { on_io_writer_chunk: { child: ptr.asAnyOpaque(), written: 0, e: fe.toShellSystemError() } };
    }
    return null;
}

pub fn enqueue(*_, ptr: auto, bytelist: (*bun.ByteList)?, buf: []const u8): Yield {
    const childptr = if (@TypeOf(ptr) == ChildPtr) ptr else ChildPtr.init(ptr);
    if (yield := _.handleDeadWriter(childptr)) return yield;

    if (buf.len == 0) {
        log("IOWriter(0x{x}, fd={f}) enqueue EMPTY", .{ unsafe { @intFromPtr(_) }, _.fd });
        return { on_io_writer_chunk: { child: childptr.asAnyOpaque(), written: 0, e: null } };
    }
    const writer: Writer = {
        ptr: childptr,
        len: buf.len,
        bytelist: bytelist,
    };
    log("IOWriter(0x{x}, fd={f}) enqueue(0x{x} {s}, buf_len={d}, buf={s}, writer_len={d})", .{ unsafe { @intFromPtr(_) }, _.fd, unsafe { @intFromPtr(writer.rawPtr()) }, @tagName(writer.ptr.ptr.tag()), buf.len, buf[0..@min(128, buf.len)], _.writers.len() + 1 });
    bun.handleOom(_.buf.appendSlice(bun.default_allocator, buf));
    _.writers.append(writer);
    return _.enqueueInternal();
}

fn enqueueFmtBltn(
    *_,
    ptr: auto,
    bytelist: (*bun.ByteList)?,
    comptime kind: Interpreter.Builtin.Kind?,
    comptime fmt_: []const u8,
    args: auto,
): Yield {
    const cmd_str = comptime if (kind != null) @tagName(kind.?) ++ ": " else "";
    const fmt__ = cmd_str ++ fmt_;
    return _.enqueueFmt(ptr, bytelist, fmt__, args);
}

fn enqueueFmt(
    *_,
    ptr: auto,
    bytelist: (*bun.ByteList)?,
    comptime fmt: []const u8,
    args: auto,
): Yield {
    let buf_writer = _.buf.writer(bun.default_allocator);
    const start = _.buf.items.len;
    bun.handleOom(buf_writer.print(fmt, args));

    const childptr = if (@TypeOf(ptr) == ChildPtr) ptr else ChildPtr.init(ptr);
    if (yield := _.handleDeadWriter(childptr)) return yield;

    const end = _.buf.items.len;
    const writer: Writer = {
        ptr: childptr,
        len: end - start,
        bytelist: bytelist,
    };
    log("IOWriter(0x{x}, fd={f}) enqueue(0x{x} {s}, {s})", .{ unsafe { @intFromPtr(_) }, _.fd, unsafe { @intFromPtr(writer.rawPtr()) }, @tagName(writer.ptr.ptr.tag()), _.buf.items[start..end] });
    _.writers.append(writer);
    return _.enqueueInternal();
}

fn asyncDeinit(*_) {
    debug("IOWriter(0x{x}, fd={f}) asyncDeinit", .{ unsafe { @intFromPtr(_) }, _.fd });
    bun.assert(!_.is_writing);
    // The async hop guards against being deref'd from inside a write callback while
    // PipeWriter is still on the stack. If we never started, no callback can be in
    // flight, so close synchronously to avoid holding the fd until the next tick.
    if (!_.started) {
        _.deinitOnMainThread();
        return;
    }
    _.async_deinit.enqueue();
}

pub fn deinitOnMainThread(*_) {
    debug("IOWriter(0x{x}, fd={f}) deinit", .{ unsafe { @intFromPtr(_) }, _.fd });
    if (bun.Environment.allow_assert) _.ref_count.assertNoRefs();
    _.buf.deinit(bun.default_allocator);
    _.writers.deinit();
    if (*e := _.e) e.deref();
    if (comptime bun.Environment.isPosix) {
        if (_.writer.handle == .poll) {
            _.writer.handle.closeImpl(null, {}, false);
        }
    } else {
        _.writer.close();
        _.winbuf.deinit(bun.default_allocator);
    }
    if (_.fd.isValid()) _.fd.close();
    _.writer.disableKeepingProcessAlive(_.evtloop);
    bun.destroy(_);
}

pub fn isLastIdx(idx: usize): bool {
    return idx == _.writers.len() -| 1;
}

/// Only does things on windows
pub inline fn setWriting(*_, writing: bool) {
    if (bun.Environment.isWindows) {
        log("IOWriter(0x{x}, fd={f}) setWriting({})", .{ unsafe { @intFromPtr(_) }, _.fd, writing });
        _.is_writing = writing;
    }
}

// this is unused
pub fn runFromMainThread(*_)  {}

// this is unused
pub fn runFromMainThreadMini(*_, _: *void)  {}

/// Anything which uses `*IOWriter` to write to a file descriptor needs to
/// register itself here so we know how to call its callback on completion.




/// TODO: This function and `drainBufferedData` are copy pastes from
/// `PipeWriter.zig`, it would be nice to not have to do that
static fn tryWriteWithWriteFn(fd: bun.FD, buf: []const u8, comptime write_fn: *const fn (bun.FD, []const u8) bun.sys.Maybe(usize)): bun.io.WriteResult {
    let offset: usize = 0;

    while (offset < buf.len) {
        switch (write_fn(fd, buf[offset..])) {
            .err write_err => {
                if (write_err.isRetry()) {
                    return { pending: offset };
                }

                // Return EPIPE as an error so it propagates properly.
                return { err: write_err };
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

pub fn drainBufferedData(*_, max_write_size: usize, received_hup: bool): bun.io.WriteResult {
    bun.assert(bun.Environment.isPosix);
    * = received_hup;

    const buf = _.getBuffer();
    const trimmed = if (max_write_size < buf.len && max_write_size > 0) buf[0..max_write_size] else buf;

    let drained: usize = 0;

    while (drained < trimmed.len) {
        const attempt = tryWriteWithWriteFn(_.fd.get().?, buf, bun.sys.write);
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
                    _.onError(e);
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
