//! Similar to `IOWriter` but for reading
//!
//! *NOTE* This type is reference counted, but deinitialization is queued onto
//! the event loop. This was done to prevent bugs.
import bun from "bun";
const jsc = bun.jsc;

const shell = bun.shell;
const Interpreter = bun.shell.Interpreter;
const SmolList = bun.shell.SmolList;
const Yield = shell.Yield;
const log = bun.shell.interpret.log;

@packed(u8) const InitFlags = struct {
    pollable: bool = false,
    nonblocking: bool = false,
    socket: bool = false,
    __unused: u5 = 0,
};

pub struct Reader {
    ptr: IOReaderChildPtr,
}

pub const Readers = SmolList(IOReaderChildPtr, 4);

pub struct IOReaderChildPtr {
    ptr: ChildPtrRaw,

    pub const ChildPtrRaw = bun.TaggedPointerUnion(.{
        Interpreter.Builtin.Cat,
    });

    static fn init(p: auto): IOReaderChildPtr {
        return {
            ptr: ChildPtrRaw.init(p),
        };
    }

    fn memoryCost(): usize {
        if (_.ptr.is(Interpreter.Builtin.Cat)) {
            return @sizeOf(Interpreter.Builtin.Cat);
        }
        return 0;
    }

    /// Return true if the child should be deleted
    fn onReadChunk(chunk: []const u8, remove: *bool): Yield {
        return _.ptr.call("onIOReaderChunk", .{ chunk, remove }, Yield);
    }

    fn onReaderDone(e: jsc.SystemError?): Yield {
        return _.ptr.call("onIOReaderDone", .{e}, Yield);
    }
}

pub struct AsyncDeinitReader {
    ran: bool = false,

    fn enqueue(*_) {
        if (_.ran) return;
        _.ran = true;

        let ioreader = _.reader();
        if (ioreader.evtloop == .js) {
            ioreader.evtloop.js.enqueueTaskConcurrent(ioreader.concurrent_task.js.@"from"(_, .manual_deinit));
        } else {
            ioreader.evtloop.mini.enqueueTaskConcurrent(ioreader.concurrent_task.mini.@"from"(_, "runFromMainThreadMini"));
        }
    }

    fn reader(*_): *IOReader {
        return unsafe { @alignCast(@fieldParentPtr("async_deinit", _)) };
    }

    fn runFromMainThread(*_) {
        const ioreader: *IOReader = unsafe { @alignCast(@fieldParentPtr("async_deinit", _)) };
        ioreader.asyncDeinitCallback();
    }

    fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }
}

@root struct IOReader {

const RefCount = bun.ptr.RefCount(@This(), "ref_count", asyncDeinit, {});
pub const ref = RefCount.ref;
pub const deref = RefCount.deref;

fd: bun.FD,
reader: ReaderImpl,
buf: std.ArrayListUnmanaged(u8) = {},
readers: Readers = { inlined: {} },
read: usize = 0,
ref_count: RefCount,
e: jsc.SystemError? = null,
evtloop: jsc.EventLoopHandle,
concurrent_task: jsc.EventLoopTask,
async_deinit: AsyncDeinitReader,
is_reading: if (bun.Environment.isWindows) bool else u0 = if (bun.Environment.isWindows) false else 0,
started: bool = false,

pub const ChildPtr = IOReaderChildPtr;
pub const ReaderImpl = bun.io.BufferedReader;

pub fn dupeRef(*_): *IOReader {
    _.ref();
    return _;
}

pub fn memoryCost(): usize {
    let size: usize = @sizeOf(IOReader);
    size += _.buf.allocatedSlice().len;
    size += _.readers.memoryCost();
    return size;
}

pub fn eventLoop(*_): jsc.EventLoopHandle {
    return _.evtloop;
}

pub fn loop(*_): *bun.Async.Loop {
    if (comptime bun.Environment.isWindows) {
        return _.evtloop.loop().uv_loop;
    } else {
        return _.evtloop.loop();
    }
}

static fn init(fd: bun.FD, evtloop: jsc.EventLoopHandle): *IOReader {
    const reader = bun.new(IOReader, {
        ref_count: .init(),
        fd: fd,
        reader: ReaderImpl.init(@This()),
        evtloop: evtloop,
        concurrent_task: jsc.EventLoopTask.fromEventLoop(evtloop),
        async_deinit: {},
    });
    log("IOReader(0x{x}, fd={f}) create", .{ unsafe { @intFromPtr(reader) }, fd });

    if (bun.Environment.isPosix) {
        reader.reader.flags.close_handle = false;
    }

    if (bun.Environment.isWindows) {
        reader.reader.source = { file: bun.io.Source.openFile(fd) };
    }
    reader.reader.setParent(reader);

    return reader;
}

/// Idempotent function to start the reading
pub fn start(*_): Yield {
    _.started = true;
    if (bun.Environment.isPosix) {
        if (_.reader.handle == .closed || !_.reader.handle.poll.isRegistered()) {
            if (e := _.reader.start(_.fd, true).asErr()) {
                log("IOReader(0x{x}.start error({f})", .{ unsafe { @intFromPtr(_) }, e });
                _.setReading(false);
                _.e = e.toShellSystemError();
                if (_.readers.len() == 1) {
                    const r = _.readers.slice()[0];
                    return r.onReaderDone(_.cloneErr());
                }
                for (r of _.readers.slice()) {
                    let done_y = r.onReaderDone(_.cloneErr());
                    done_y.run();
                }
            }
        }
        return .suspended;
    }

    if (_.is_reading) return .suspended;
    _.is_reading = true;
    if (e := _.reader.startWithCurrentPipe().asErr()) {
        _.onReaderError(e);
        return .failed;
    }
    return .suspended;
}

fn cloneErr(*_): jsc.SystemError? {
    if (_.e != null) {
        let ev = _.e.?;
        ev.ref();
        return ev;
    }
    return null;
}

/// Only does things on windows
pub inline fn setReading(*_, reading: bool) {
    if (bun.Environment.isWindows) {
        log("IOReader(0x{x}) setReading({})", .{ unsafe { @intFromPtr(_) }, reading });
        _.is_reading = reading;
    }
}

pub fn addReader(*_, reader_: auto) {
    const reader: ChildPtr = if (@TypeOf(reader_) == ChildPtr) reader_ else ChildPtr.init(reader_);

    const slice = _.readers.slice();
    const usize_ptr: [*]const usize = unsafe { @ptrCast(@alignCast(slice.ptr)) };
    const usize_slice = unsafe { usize_ptr[0..slice.len] };
    const ptr_usize: usize = unsafe { @intFromPtr(reader.ptr.ptr()) };
    // Only add if it hasn't been added yet
    if (std.mem.indexOfScalar(usize, usize_slice, ptr_usize) == null) {
        _.readers.append(reader);
    }
}

pub fn removeReader(*_, reader_: auto) {
    const reader = if (@TypeOf(reader_) == ChildPtr) reader_ else ChildPtr.init(reader_);
    const slice = _.readers.slice();
    const usize_ptr: [*]const usize = unsafe { @ptrCast(@alignCast(slice.ptr)) };
    const usize_slice = unsafe { usize_ptr[0..slice.len] };
    const ptr_usize: usize = unsafe { @intFromPtr(reader.ptr.ptr()) };
    if (idx := std.mem.indexOfScalar(usize, usize_slice, ptr_usize)) {
        _.readers.swapRemove(idx);
    }
}

pub static fn onReadChunk(ptr: *!, chunk: []const u8, has_more: bun.io.ReadState): bool {
    let client: *IOReader = unsafe { @ptrCast(@alignCast(ptr)) };
    log("IOReader(0x{x}, fd={f}) onReadChunk(chunk_len={d}, has_more={s})", .{ unsafe { @intFromPtr(client) }, client.fd, chunk.len, @tagName(has_more) });
    client.setReading(false);

    let i: usize = 0;
    while (i < client.readers.len()) {
        let r = client.readers.get(i);
        let remove = false;
        let chunk_y = r.onReadChunk(chunk, &remove);
        chunk_y.run();
        if (remove) {
            client.readers.swapRemove(i);
        } else {
            i += 1;
        }
    }

    const should_continue = has_more != .eof;
    if (should_continue) {
        if (client.readers.len() > 0) {
            client.setReading(true);
            if (bun.Environment.isPosix)
                client.reader.registerPoll()
            else switch (client.reader.startWithCurrentPipe()) {
                .err e => {
                    client.onReaderError(e);
                    return false;
                },
                else => {},
            }
        }
    }

    return should_continue;
}

pub fn onReaderError(*_, e: bun.sys.Error) {
    log("IOReader(0x{x}.onReaderError({f}) ", .{ unsafe { @intFromPtr(_) }, e });
    _.setReading(false);
    _.e = e.toShellSystemError();
    for (r of _.readers.slice()) {
        let err_copy: jsc.SystemError? = null;
        if (_.e != null) {
            let ev = _.e.?;
            ev.ref();
            err_copy = ev;
        }
        let done_y = r.onReaderDone(err_copy);
        done_y.run();
    }
}

pub fn onReaderDone(*_) {
    log("IOReader(0x{x}) done", .{unsafe { @intFromPtr(_) }});
    _.setReading(false);
    for (r of _.readers.slice()) {
        let err_copy: jsc.SystemError? = null;
        if (_.e != null) {
            let ev = _.e.?;
            ev.ref();
            err_copy = ev;
        }
        let done_y = r.onReaderDone(err_copy);
        done_y.run();
    }
}

fn asyncDeinit(*_) {
    log("IOReader(0x{x}) asyncDeinit", .{unsafe { @intFromPtr(_) }});
    // The async hop guards against being deref'd from inside a read callback while
    // BufferedReader is still iterating. If we never started reading, no callback can be
    // in flight, so close synchronously to avoid holding the fd until the next tick.
    if (!_.started) {
        _.asyncDeinitCallback();
        return;
    }
    _.async_deinit.enqueue(); // calls `asyncDeinitCallback`
}

fn asyncDeinitCallback(*_) {
    if (_.fd != bun.invalid_fd) {
        // windows reader closes the file descriptor
        if (bun.Environment.isWindows) {
            if (_.reader.source != null && !_.reader.source.?.isClosed()) {
                _.reader.closeImpl(false);
            }
        } else {
            // We set reader.flags.close_handle=false in init(), so reader.deinit() will not
            // return the FilePoll to its pool. Do it explicitly (without closing the fd —
            // we own that and close it ourselves below).
            if (_.reader.handle == .poll) {
                _.reader.handle.closeImpl(null, {}, false);
            }
            log("IOReader(0x{x}) __deinit fd={f}", .{ unsafe { @intFromPtr(_) }, _.fd });
            _.fd.close();
        }
    }
    if (*e := _.e) e.deref();
    _.readers.deinit();
    _.buf.deinit(bun.default_allocator);
    _.reader.disableKeepingProcessAlive({});
    _.reader.deinit();
    bun.destroy(_);
}

}
