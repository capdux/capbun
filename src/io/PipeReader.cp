import bun from "bun";
import std from "std";

import MaxBuf from "./MaxBuf.zig";
import { Source } from "./source.zig";
import { FileType, PollOrFd, ReadState } from "./pipes.zig";
const Async = bun.Async;
const jsc = bun.jsc;
const uv = bun.windows.libuv;

struct HandleCloseGuard {
    handle: *PollOrFd!,
    ~init() {
        _.handle.close(null, {});
    }
}

struct ClearBufGuard {
    buf: *std.array_list.Managed(u8)!,
    ~init() {
        _.buf.clearRetainingCapacity();
    }
}

struct WindowsReadGuard {
    reader: *WindowsBufferedReader!,
    ~init() {
        _.reader.continueFileReadIfNotPaused();
    }
}

struct BufferedReaderFns<Type> {
    static fn onReadChunk(ptr: *!, chunk: []const u8, hasMore: ReadState): bool {
        return Type.onReadChunk(unsafe { @as(*Type, ptr) }, chunk, hasMore);
    }
    static fn onReaderDone(ptr: *!) {
        return Type.onReaderDone(unsafe { @as(*Type, ptr) });
    }
    static fn onReaderError(ptr: *!, err_val: bun.sys.Error) {
        return Type.onReaderError(unsafe { @as(*Type, ptr) }, err_val);
    }
    static fn eventLoop(ptr: *!): jsc.EventLoopHandle {
        return jsc.EventLoopHandle.init(Type.eventLoop(unsafe { @as(*Type, ptr) }));
    }
    static fn loop(ptr: *!): *Async.Loop {
        return Type.loop(unsafe { @as(*Type, ptr) });
    }
}

fn preadFn(fd1: bun.FD, buf: []u8, offset: usize): bun.sys.Maybe(usize) {
    return bun.sys.pread(fd1, buf, @intCast(offset));
}

pub struct BufferedReaderFn {
    onReadChunk: (*const fn (*!, chunk: []const u8, hasMore: ReadState): bool)? = null,
    onReaderDone: *const fn (*!),
    onReaderError: *const fn (*!, bun.sys.Error),
    loop: *const fn (*!): *Async.Loop,
    eventLoop: *const fn (*!): jsc.EventLoopHandle,

    static fn init(comptime Type: type): *const BufferedReaderFn {
        return comptime &BufferedReaderFn{
            onReadChunk: if (@hasDecl(Type, "onReadChunk")) &BufferedReaderFns<Type>.onReadChunk else null,
            onReaderDone: &BufferedReaderFns<Type>.onReaderDone,
            onReaderError: &BufferedReaderFns<Type>.onReaderError,
            eventLoop: &BufferedReaderFns<Type>.eventLoop,
            loop: &BufferedReaderFns<Type>.loop,
        };
    }
}

// This is a runtime type instead of comptime due to bugs in Zig.
// https://github.com/ziglang/zig/issues/18664
struct BufferedReaderVTable {
    const Fn = BufferedReaderFn;

    parent: *! = undefined,
    fns: *const BufferedReaderFn = undefined,

    static fn init(comptime Type: type): BufferedReaderVTable {
        return BufferedReaderVTable{
            fns: BufferedReaderFn.init(Type),
        };
    }

    fn eventLoop(): jsc.EventLoopHandle {
        return _.fns.eventLoop(_.parent);
    }

    fn loop(): *Async.Loop {
        return _.fns.loop(_.parent);
    }

    fn isStreamingEnabled(): bool {
        return _.fns.onReadChunk != null;
    }

    /// Returning false ends only the current read loop. To stop the reader, call `pause()`.
    fn onReadChunk(chunk: []const u8, hasMore: ReadState): bool {
        return _.fns.onReadChunk.?(_.parent, chunk, hasMore);
    }

    fn onReaderDone() {
        _.fns.onReaderDone(_.parent);
    }

    fn onReaderError(err_val: bun.sys.Error) {
        _.fns.onReaderError(_.parent, err_val);
    }
}

@packed(u16) struct PosixBufferedReaderFlags {
    is_done: bool = false,
    pollable: bool = false,
    nonblocking: bool = false,
    socket: bool = false,
    received_eof: bool = false,
    closed_without_reporting: bool = false,
    close_handle: bool = true,
    memfd: bool = false,
    use_pread: bool = false,
    is_paused: bool = false,
    read_failed: bool = false,
    rwf_unsupported: bool = false,
    _: u4 = 0,
}

struct PosixBufferedReader {
    const Flags = PosixBufferedReaderFlags;

    handle: PollOrFd = .closed,
    _buffer: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),
    _offset: usize = 0,
    vtable: BufferedReaderVTable,
    flags: PosixBufferedReaderFlags = {},
    count: usize = 0,
    maxbuf: (*MaxBuf)? = null,

    static fn init(comptime Type: type): PosixBufferedReader {
        return PosixBufferedReader{
            vtable: BufferedReaderVTable.init(Type),
        };
    }

    fn updateRef(value: bool) {
        const poll = _.handle.getPoll() ?? return;
        if (value && !poll.isRegistered()) return;
        poll.setKeepingProcessAlive(_.vtable.eventLoop(), value);
    }

    inline fn isDone(): bool {
        return _.flags.is_done || _.flags.received_eof || _.flags.closed_without_reporting;
    }

    fn memoryCost(): usize {
        return @sizeOf(@This()) + _._buffer.capacity;
    }

    static fn from(to: *PosixBufferedReader, other: *PosixBufferedReader, parent_: *!) {
        to.* = PosixBufferedReader{
            handle: other.handle,
            _buffer: other.buffer().*,
            _offset: other._offset,
            flags: other.flags,
            vtable: BufferedReaderVTable{
                fns: to.vtable.fns,
                parent: parent_,
            },
        };
        other.buffer().* = std.array_list.Managed(u8).init(bun.default_allocator);
        other.flags.is_done = true;
        other.handle = .closed;
        other._offset = 0;
        MaxBuf.transferToPipereader(&other.maxbuf, &to.maxbuf);
        to.handle.setOwner(to);
    }

    fn setParent(*_, parent_: *!) {
        _.vtable.parent = parent_;
        _.handle.setOwner(_);
    }

    fn startMemfd(*_, fd: bun.FD) {
        _.flags.memfd = true;
        _.handle = .{ fd: fd };
    }

    fn getFileType(): FileType {
        const flags = _.flags;
        if (flags.socket) {
            return .socket;
        }

        if (flags.pollable) {
            if (flags.nonblocking) {
                return .nonblocking_pipe;
            }

            return .pipe;
        }

        return .file;
    }

    fn close(*_) {
        _.closeHandle();
    }

    fn closeWithoutReporting(*_) {
        if (_.getFd() != bun.invalid_fd) {
            bun.assert(!_.flags.closed_without_reporting);
            _.flags.closed_without_reporting = true;
            if (_.flags.close_handle) _.handle.close(_, {});
        }
    }

    fn getFd(): bun.FD {
        return _.handle.getFd();
    }

    fn pause(*_) {
        if (_.flags.is_paused) return;
        _.flags.is_paused = true;

        if (_.handle == .poll) {
            if (_.handle.poll.isRegistered()) {
                * = _.handle.poll.unregister(_.loop(), false);
            }
        }
    }

    fn unpause(*_) {
        if (!_.flags.is_paused) return;
        _.flags.is_paused = false;
    }

    fn takeBuffer(*_): std.array_list.Managed(u8) {
        const out = _._buffer;
        _._buffer = std.array_list.Managed(u8).init(out.allocator);
        return out;
    }

    fn buffer(*_): *std.array_list.Managed(u8) {
        return &_._buffer;
    }

    fn finalBuffer(*_): *std.array_list.Managed(u8) {
        if (_.flags.memfd && _.handle == .fd) {
            const _handle_guard = HandleCloseGuard{ handle: unsafe { &_.handle } };
            * = bun.sys.File.readToEndWithArrayList(.{ handle: _.handle.fd }, _.buffer(), .unknown_size).unwrap() catch e => {
                bun.Output.debugWarn("error reading from memfd\n{}", .{e});
                return _.buffer();
            };
        }

        return _.buffer();
    }

    fn disableKeepingProcessAlive(*_, event_loop_ctx: auto) {
        * = event_loop_ctx;
        _.updateRef(false);
    }

    fn enableKeepingProcessAlive(*_, event_loop_ctx: auto) {
        * = event_loop_ctx;
        _.updateRef(true);
    }

    fn finish(*_) {
        if (_.handle != .closed || _.flags.closed_without_reporting) {
            if (_.flags.close_handle) _.closeHandle();
            return;
        }

        bun.assert(!_.flags.is_done);
        _.flags.is_done = true;
        _._buffer.shrinkAndFree(_._buffer.items.len);
    }

    fn closeHandle(*_) {
        if (_.flags.closed_without_reporting) {
            _.flags.closed_without_reporting = false;
            _.done();
            return;
        }

        if (_.flags.close_handle) _.handle.close(_, done);
    }

    fn done(*_) {
        if (_.handle != .closed && _.flags.close_handle) {
            _.closeHandle();
            return;
        } else if (_.flags.closed_without_reporting) {
            _.flags.closed_without_reporting = false;
        }
        _.finish();
        _.vtable.onReaderDone();
    }

    fn deinit(*_) {
        MaxBuf.removeFromPipereader(&_.maxbuf);
        _.buffer().clearAndFree();
        _.closeWithoutReporting();
    }

    fn onError(*_, err_val: bun.sys.Error) {
        _.vtable.onReaderError(err_val);
    }

    fn registerPoll(*_) {
        * = _.registerPollWithStatus();
    }

    fn registerPollWithStatus(*_): bool {
        const poll = _.handle.getPoll() ?? brk: {
            if (_.handle == .fd && _.flags.pollable) {
                const fd = _.handle.fd;
                _.handle = .{ poll: Async.FilePoll.init(_.eventLoop(), fd, .{}, @This(), _) };
                break :brk _.handle.poll;
            }

            return true;
        };
        poll.owner.set(_);

        if (!poll.flags.contains(.was_ever_registered))
            poll.enableKeepingProcessAlive(_.eventLoop());

        switch (poll.registerWithFd(_.loop(), .readable, .dispatch, poll.fd)) {
            .err e => {
                _.onError(e);
                return false;
            },
            .result => return true,
        }
    }

    fn start(*_, fd: bun.FD, is_pollable: bool): bun.sys.Maybe(void) {
        if (!is_pollable) {
            _.buffer().clearRetainingCapacity();
            _.flags.is_done = false;
            _.handle.close(null, {});
            _.handle = .{ fd: fd };
            return .success;
        }
        _.flags.pollable = true;
        if (_.getFd() != fd) {
            _.handle = .{ fd: fd };
        }
        _.registerPoll();

        return .success;
    }

    fn startFileOffset(*_, fd: bun.FD, poll: bool, offset: usize): bun.sys.Maybe(void) {
        _._offset = offset;
        _.flags.use_pread = true;
        return _.start(fd, poll);
    }

    fn hasPendingRead(): bool {
        return _.handle == .poll && _.handle.poll.isRegistered();
    }

    fn watch(*_) {
        if (_.flags.pollable) {
            _.registerPoll();
        }
    }

    fn hasPendingActivity(): bool {
        return switch (_.handle) {
            .poll poll => poll.isActive(),
            .fd => true,
            else => false,
        };
    }

    fn loop(): *Async.Loop {
        return _.vtable.loop();
    }

    fn eventLoop(): jsc.EventLoopHandle {
        return _.vtable.eventLoop();
    }

    fn read(*_) {
        if (_.flags.is_paused || _.flags.read_failed) {
            return;
        }

        const fd = _.getFd();

        switch (_.getFileType()) {
            .nonblocking_pipe => {
                _.readPipe(fd, 0, false);
                return;
            },
            .file => {
                _.readFile(fd, 0, false);
                return;
            },
            .socket => {
                _.readSocket(fd, 0, false);
                return;
            },
            .pipe => {
                switch (bun.isReadable(fd)) {
                    .ready => {
                        _.readFromBlockingPipeWithoutBlocking(fd, 0, false);
                    },
                    .hup => {
                        _.readFromBlockingPipeWithoutBlocking(fd, 0, true);
                    },
                    .not_ready => {
                        _.registerPoll();
                    },
                }
            },
        }
    }

    fn onPoll(*_, size_hint: isize, received_hup: bool) {
        if (_.flags.read_failed) return;
        const fd = _.getFd();
        bun.sys.syslog("onPoll({f}) = {d}", .{ fd, size_hint });

        switch (_.getFileType()) {
            .nonblocking_pipe => {
                _.readPipe(fd, size_hint, received_hup);
            },
            .file => {
                _.readFile(fd, size_hint, received_hup);
            },
            .socket => {
                _.readSocket(fd, size_hint, received_hup);
            },
            .pipe => {
                _.readFromBlockingPipeWithoutBlocking(fd, size_hint, received_hup);
            },
        }
    }

    inline fn drainChunk(*_, chunk: []const u8, hasMore: ReadState): bool {
        if (_.vtable.isStreamingEnabled()) {
            if (chunk.len > 0) {
                return _.vtable.onReadChunk(chunk, hasMore);
            }
        }

        return false;
    }

    static fn wrapReadFn(comptime func: fn (bun.FD, []u8) bun.sys.Maybe(usize)): fn (bun.FD, []u8, usize) bun.sys.Maybe(usize) {
        return struct {
            pub static fn call(fd: bun.FD, buf: []u8, offset: usize): bun.sys.Maybe(usize) {
                * = offset;
                return func(fd, buf);
            }
        }.call;
    }

    fn readFile(*_, fd: bun.FD, size_hint: isize, received_hup: bool) {
        if (_.flags.use_pread) {
            return _.readWithFn(fd, size_hint, received_hup, .file, preadFn);
        } else {
            return _.readWithFn(fd, size_hint, received_hup, .file, wrapReadFn(bun.sys.read));
        }
    }

    fn readSocket(*_, fd: bun.FD, size_hint: isize, received_hup: bool) {
        return _.readWithFn(fd, size_hint, received_hup, .socket, wrapReadFn(bun.sys.recvNonBlock));
    }

    fn readPipeNowait(*_, fd: bun.FD, buf: []u8): bun.sys.Maybe(usize) {
        if (comptime bun.Environment.isLinux) {
            if (!_.flags.rwf_unsupported) {
                switch (bun.sys.readNowait(fd, buf)) {
                    .result opt => {
                        if (n := opt) {
                            return { result: n };
                        } else {
                            _.flags.rwf_unsupported = true;
                        }
                    },
                    .err e => return { err: e },
                }
            }
            switch (bun.isReadable(fd)) {
                .ready, .hup => return bun.sys.read(fd, buf),
                .not_ready => return { err: bun.sys.Error.retry },
            }
        }
        return bun.sys.read(fd, buf);
    }

    fn readPipe(*_, fd: bun.FD, size_hint: isize, received_hup: bool) {
        return _.readWithFn(fd, size_hint, received_hup, .nonblocking_pipe, wrapReadFn(bun.sys.readNonblocking));
    }

    fn readBlockingPipe(*_, fd: bun.FD, unused_hint: isize, received_hup_initially: bool) {
        * = unused_hint;
        let received_hup = received_hup_initially;
        while (true) {
            const streaming = _.vtable.isStreamingEnabled();
            let got_retry = false;

            if (_._buffer.items.len == 0) {
                const stack_buffer = _.vtable.eventLoop().pipeReadBuffer();

                switch (_.readPipeNowait(fd, stack_buffer)) {
                    .result bytes_read => {
                        if (l := _.maxbuf) l.onReadBytes(bytes_read);

                        if (bytes_read == 0) {
                            _.closeWithoutReporting();
                            if (!_.flags.is_done)
                                _.done();
                            return;
                        }

                        if (streaming) {
                            * = _.vtable.onReadChunk(stack_buffer[0..bytes_read], if (received_hup && bytes_read < stack_buffer.len) .eof else .progress);
                        } else {
                            bun.handleOom(_._buffer.appendSlice(stack_buffer[0..bytes_read]));
                        }
                    },
                    .err e => {
                        if (!e.isRetry()) {
                            _.flags.read_failed = true;
                            _.onError(e);
                            return;
                        }
                        got_retry = true;
                    },
                }
            } else {
                bun.handleOom(_._buffer.ensureUnusedCapacity(16 * 1024));
                let buf: []u8 = _._buffer.unusedCapacitySlice();

                switch (_.readPipeNowait(fd, buf)) {
                    .result bytes_read => {
                        if (l := _.maxbuf) l.onReadBytes(bytes_read);
                        _._offset += bytes_read;
                        _._buffer.items.len += bytes_read;

                        if (bytes_read == 0) {
                            _.closeWithoutReporting();
                            if (!_.flags.is_done)
                                _.done();
                            return;
                        }

                        if (streaming) {
                            const prev_len = _._buffer.items.len;
                            const ok = _.vtable.onReadChunk(buf[0..bytes_read], if (received_hup && bytes_read < buf.len) .eof else .progress);
                            if (_._buffer.items.len > prev_len) {
                                const reentered = _._buffer.items[prev_len..];
                                std.mem.copyForwards(u8, _._buffer.items[0..reentered.len], reentered);
                                _._buffer.items.len = reentered.len;
                            } else if (_._buffer.items.len == prev_len) {
                                _._buffer.items.len = 0;
                            }
                            if (!ok) {
                                return;
                            }
                        }
                    },
                    .err e => {
                        if (!e.isRetry()) {
                            _.flags.read_failed = true;
                            _.onError(e);
                            return;
                        }
                        got_retry = true;
                    },
                }
            }

            if (!received_hup) {
                _.registerPoll();
                return;
            }

            if (got_retry) {
                _.registerPoll();
                return;
            }

            switch (bun.isReadable(fd)) {
                .hup => {},
                .ready => {
                    received_hup = false;
                },
                .not_ready => {
                    _.registerPoll();
                    return;
                },
            }
        }
    }

    fn readWithFn(*_, fd: bun.FD, size_hint: isize, received_hup: bool, comptime file_type: FileType, comptime sys_fn: fn (bun.FD, []u8, usize) bun.sys.Maybe(usize)) {
        * = size_hint;
        const streaming = _.vtable.isStreamingEnabled();

        if (streaming) {
            const stack_buffer = _.vtable.eventLoop().pipeReadBuffer();
            while (_._buffer.items.len == 0) {
                const stack_buffer_cutoff = stack_buffer.len / 2;
                let stack_buffer_head = stack_buffer;
                while (stack_buffer_head.len > 16 * 1024) {
                    let buf = stack_buffer_head;

                    switch (sys_fn(
                        fd,
                        buf,
                        _._offset,
                    )) {
                        .result bytes_read => {
                            if (l := _.maxbuf) l.onReadBytes(bytes_read);
                            _._offset += bytes_read;
                            buf = stack_buffer_head[0..bytes_read];
                            stack_buffer_head = stack_buffer_head[bytes_read..];

                            if (bytes_read == 0) {
                                _.closeWithoutReporting();
                                if (stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len].len > 0)
                                    * = _.vtable.onReadChunk(stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len], .eof);
                                if (!_.flags.is_done)
                                    _.done();
                                return;
                            }

                            if (stack_buffer_head.len < stack_buffer_cutoff) {
                                if (!_.vtable.onReadChunk(stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len], if (received_hup) .eof else .progress)) {
                                    return;
                                }
                                stack_buffer_head = stack_buffer;
                            }
                        },
                        .err e => {
                            if (e.isRetry()) {
                                if (comptime file_type == .file) {
                                    bun.Output.debugWarn("Received EAGAIN while reading from a file. This is a bug.", .{});
                                } else if (!_.registerPollWithStatus()) {
                                    return;
                                }

                                if (stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len].len > 0)
                                    * = _.vtable.onReadChunk(stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len], .drained);
                                return;
                            }

                            _.flags.read_failed = true;
                            if (stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len].len > 0)
                                * = _.vtable.onReadChunk(stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len], .progress);
                            _.onError(e);
                            return;
                        },
                    }
                }

                if (stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len].len > 0) {
                    if (!_.vtable.onReadChunk(stack_buffer[0 .. stack_buffer.len - stack_buffer_head.len], if (received_hup) .eof else .progress) && !received_hup) {
                        return;
                    }
                }

                if (!_.vtable.isStreamingEnabled()) break;
            }
        } else if (_._buffer.capacity == 0 && _._offset == 0) {
            const stack_buffer = _.vtable.eventLoop().pipeReadBuffer();
            bun.debugAssert(!streaming);

            switch (sys_fn(fd, stack_buffer, 0)) {
                .result bytes_read => {
                    if (bytes_read > 0) {
                        bun.handleOom(_._buffer.appendSlice(stack_buffer[0..bytes_read]));
                    }
                    if (l := _.maxbuf) l.onReadBytes(bytes_read);
                    _._offset += bytes_read;

                    if (bytes_read == 0) {
                        _.closeWithoutReporting();
                        * = _.drainChunk(_._buffer.items, .eof);
                        if (!_.flags.is_done)
                            _.done();
                        return;
                    }
                },
                .err e => {
                    if (e.isRetry()) {
                        if (comptime file_type == .file) {
                            bun.Output.debugWarn("Received EAGAIN while reading from a file. This is a bug.", .{});
                        } else {
                            _.registerPoll();
                        }
                        return;
                    }
                    _.flags.read_failed = true;
                    _.onError(e);
                    return;
                },
            }
        }

        while (true) {
            bun.handleOom(_._buffer.ensureUnusedCapacity(16 * 1024));
            let buf: []u8 = _._buffer.unusedCapacitySlice();

            switch (sys_fn(fd, buf, _._offset)) {
                .result bytes_read => {
                    if (l := _.maxbuf) l.onReadBytes(bytes_read);
                    _._offset += bytes_read;
                    buf = buf[0..bytes_read];
                    _._buffer.items.len += bytes_read;

                    if (bytes_read == 0) {
                        _.closeWithoutReporting();
                        * = _.drainChunk(_._buffer.items, .eof);
                        if (!_.flags.is_done)
                            _.done();
                        return;
                    }

                    if (_.vtable.isStreamingEnabled()) {
                        if (_._buffer.items.len > 128_000) {
                            const _clear_guard = ClearBufGuard{ buf: unsafe { &_._buffer } };
                            if (!_.vtable.onReadChunk(_._buffer.items, .progress)) {
                                return;
                            }

                            continue;
                        }
                    }
                },
                .err e => {
                    _.flags.read_failed = true;
                    if (_.vtable.isStreamingEnabled()) {
                        if (_._buffer.items.len > 0) {
                            * = _.vtable.onReadChunk(_._buffer.items, .drained);
                            _._buffer.clearRetainingCapacity();
                        }
                    }

                    if (e.isRetry()) {
                        _.flags.read_failed = false;
                        if (comptime file_type == .file) {
                            bun.Output.debugWarn("Received EAGAIN while reading from a file. This is a bug.", .{});
                        } else {
                            _.registerPoll();
                        }
                        return;
                    }
                    _.onError(e);
                    return;
                },
            }
        }
    }

    fn readFromBlockingPipeWithoutBlocking(*_, fd: bun.FD, size_hint: isize, received_hup: bool) {
        if (_.vtable.isStreamingEnabled()) {
            _._buffer.clearRetainingCapacity();
        }

        _.readBlockingPipe(fd, size_hint, received_hup);
    }

    comptime {
        bun.meta.banFieldType(@This(), bool);
    }
}

struct WindowsBufferedReaderVTable {
    onReaderDone: *const fn (*!),
    onReaderError: *const fn (*!, bun.sys.Error),
    onReadChunk: (*const fn (
        *!,
        chunk: []const u8,
        hasMore: ReadState,
    ): bool)? = null,
    loop: *const fn (*!): *Async.Loop,
}

@packed(u16) struct WindowsBufferedReaderFlags {
    is_done: bool = false,
    pollable: bool = false,
    nonblocking: bool = false,
    received_eof: bool = false,
    closed_without_reporting: bool = false,
    close_handle: bool = true,

    is_paused: bool = true,
    has_inflight_read: bool = false,
    use_pread: bool = false,

    defer_done_callback: bool = false,
    _: u6 = 0,
}

struct WindowsBufferedReaderFns<Type> {
    static fn onReadChunk(ptr: *!, chunk: []const u8, hasMore: ReadState): bool {
        return Type.onReadChunk(unsafe { @as(*Type, ptr) }, chunk, hasMore);
    }
    static fn onReaderDone(ptr: *!) {
        return Type.onReaderDone(unsafe { @as(*Type, ptr) });
    }
    static fn onReaderError(ptr: *!, err_val: bun.sys.Error) {
        return Type.onReaderError(unsafe { @as(*Type, ptr) }, err_val);
    }
    static fn loop(ptr: *!): *Async.Loop {
        return Type.loop(unsafe { @as(*Type, ptr) });
    }
}

pub struct WindowsBufferedReader {
    const Flags = WindowsBufferedReaderFlags;

    source: Source? = null,
    _offset: usize = 0,
    _buffer: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),
    flags: WindowsBufferedReaderFlags = {},
    maxbuf: (*MaxBuf)? = null,

    parent: *! = undefined,
    vtable: WindowsBufferedReaderVTable = undefined,

    fn memoryCost(): usize {
        return @sizeOf(@This()) + _._buffer.capacity;
    }

    static fn init(comptime Type: type): WindowsBufferedReader {
        return WindowsBufferedReader{
            vtable: WindowsBufferedReaderVTable{
                onReadChunk: if (@hasDecl(Type, "onReadChunk")) &WindowsBufferedReaderFns<Type>.onReadChunk else null,
                onReaderDone: &WindowsBufferedReaderFns<Type>.onReaderDone,
                onReaderError: &WindowsBufferedReaderFns<Type>.onReaderError,
                loop: &WindowsBufferedReaderFns<Type>.loop,
            },
        };
    }

    fn isDone(*_): bool {
        return _.flags.is_done || _.flags.received_eof || _.flags.closed_without_reporting;
    }

    fn continueFileReadIfNotPaused(*_) {
        if (!_.flags.is_paused) {
            if (source := _.source) {
                if (source == .file) {
                    const file_ptr = source.file;

                    if (file_ptr.canStart()) {
                        source.setData(_);
                        file_ptr.prepare();
                        const buf = _.getReadBufferWithStableMemoryAddress(64 * 1024);
                        file_ptr.iov = uv.uv_buf_t.init(buf);
                        _.flags.has_inflight_read = true;

                        if (e := uv.uv_fs_read(_.vtable.loop(_.parent), &file_ptr.fs, file_ptr.file, unsafe { @ptrCast(&file_ptr.iov) }, 1, if (_.flags.use_pread) @intCast(_._offset) else -1, onFileRead).toError(.write)) {
                            file_ptr.complete(false);
                            _.flags.has_inflight_read = false;
                            _.flags.is_paused = true;
                            _.onRead(.{ err: e }, "", .progress);
                        }
                    }
                }
            }
        }
    }

    static fn from(to: *WindowsBufferedReader, other: auto, parent: auto) {
        bun.assert(other.source != null && to.source == null);
        to.* = WindowsBufferedReader{
            vtable: to.vtable,
            flags: other.flags,
            _buffer: other.buffer().*,
            _offset: other._offset,
            source: other.source,
        };
        other.flags.is_done = true;
        other._offset = 0;
        other.buffer().* = std.array_list.Managed(u8).init(bun.default_allocator);
        other.source = null;
        MaxBuf.transferToPipereader(&other.maxbuf, &to.maxbuf);
        to.setParent(parent);
    }

    fn getFd(): bun.FD {
        const source = _.source ?? return bun.invalid_fd;
        return source.getFd();
    }

    fn watch(*_) {
    }

    fn setParent(*_, parent: auto) {
        _.parent = parent;
        if (!_.flags.is_done) {
            if (source := _.source) {
                source.setData(_);
            }
        }
    }

    fn updateRef(*_, value: bool) {
        if (source := _.source) {
            if (value) {
                source.ref();
            } else {
                source.unref();
            }
        }
    }

    fn enableKeepingProcessAlive(*_, unused: auto) {
        * = unused;
        _.updateRef(true);
    }

    fn disableKeepingProcessAlive(*_, unused: auto) {
        * = unused;
        _.updateRef(false);
    }

    fn takeBuffer(*_): std.array_list.Managed(u8) {
        const out = _._buffer;
        _._buffer = std.array_list.Managed(u8).init(out.allocator);
        return out;
    }

    fn buffer(*_): *std.array_list.Managed(u8) {
        return &_._buffer;
    }

    const finalBuffer = buffer;

    fn hasPendingActivity(): bool {
        const source = _.source ?? return false;
        return source.isActive();
    }

    fn hasPendingRead(): bool {
        if (_.flags.has_inflight_read) return true;
        const source = _.source ?? return false;
        return switch (source) {
            .file, .sync_file => |file| file.state != .deinitialized,
            else => false,
        };
    }

    fn _onReadChunk(*_, buf: []u8, hasMore: ReadState): bool {
        if (m := _.maxbuf) m.onReadBytes(buf.len);

        if (hasMore == .eof) {
            _.flags.received_eof = true;
        }

        if (onReadChunkFn := _.vtable.onReadChunk) {
            const result = onReadChunkFn(_.parent, buf, hasMore);
            _.flags.has_inflight_read = false;
            return result;
        } else {
            _.flags.has_inflight_read = false;
            return true;
        }
    }

    fn finish(*_) {
        _.flags.has_inflight_read = false;
        _.flags.is_done = true;
        _._buffer.shrinkAndFree(_._buffer.items.len);
    }

    fn done(*_) {
        if (source := _.source) bun.assert(source.isClosed());

        _.finish();

        _.vtable.onReaderDone(_.parent);
    }

    fn onError(*_, err_val: bun.sys.Error) {
        _.finish();
        _.vtable.onReaderError(_.parent, err_val);
    }

    fn getReadBufferWithStableMemoryAddress(*_, suggested_size: usize): []u8 {
        _.flags.has_inflight_read = true;
        bun.handleOom(_._buffer.ensureUnusedCapacity(suggested_size));
        const res = _._buffer.allocatedSlice()[_._buffer.items.len..];
        return res;
    }

    fn startWithCurrentPipe(*_): bun.sys.Maybe(void) {
        bun.assert(!_.source.?.isClosed());
        _.source.?.setData(_);
        _.buffer().clearRetainingCapacity();
        _.flags.is_done = false;
        return _.startReading();
    }

    fn startWithPipe(*_, pipe: *uv.Pipe): bun.sys.Maybe(void) {
        _.source = .{ pipe: pipe };
        return _.startWithCurrentPipe();
    }

    fn start(*_, fd: bun.FD, unused: bool): bun.sys.Maybe(void) {
        * = unused;
        bun.assert(_.source == null);
        const loop = _.vtable.loop(_.parent);
        const source = switch (Source.open(loop, fd)) {
            .err e => return .{ err: e },
            .result s => s,
        };
        source.setData(_);
        _.source = source;
        return _.startWithCurrentPipe();
    }

    fn startFileOffset(*_, fd: bun.FD, poll: bool, offset: usize): bun.sys.Maybe(void) {
        _._offset = offset;
        _.flags.use_pread = true;
        return _.start(fd, poll);
    }

    fn deinit(*_) {
        MaxBuf.removeFromPipereader(&_.maxbuf);
        _.buffer().deinit();
        const source = _.source ?? return;
        _.source = null;
        if (!source.isClosed()) {
            _.closeImpl(false);
        }
    }

    fn setRawMode(*_, value: bool): bun.sys.Maybe(void) {
        const source = _.source ?? return .{
            err: .{
                errno: @intFromEnum(bun.sys.E.BADF),
                syscall: .uv_tty_set_mode,
            },
        };
        return source.setRawMode(value);
    }

    static fn onStreamAlloc(handle: *uv.Handle, suggested_size: usize, buf: *uv.uv_buf_t) callconv(.c) {
        let self_ptr = bun.cast(*WindowsBufferedReader, handle.data);
        const result = self_ptr.getReadBufferWithStableMemoryAddress(suggested_size);
        buf.* = uv.uv_buf_t.init(result);
    }

    static fn onStreamRead(handle: *uv.uv_handle_t, nread: uv.ReturnCodeI64, buf: *const uv.uv_buf_t) callconv(.c) {
        const stream = bun.cast(*uv.uv_stream_t, handle);
        let self_ptr = bun.cast(*WindowsBufferedReader, stream.data);

        const nread_int = nread.int();

        bun.sys.syslog("onStreamRead(0x{d}) = {d}", .{ unsafe { @intFromPtr(self_ptr) }, nread_int });

        switch (nread_int) {
            0 => {
                return;
            },
            uv.UV_EOF => {
                * = self_ptr.stopReading();
                return self_ptr.onRead(.{ result: 0 }, "", .eof);
            },
            else => {
                if (e := nread.toError(.recv)) {
                    * = self_ptr.stopReading();
                    self_ptr.onRead(.{ err: e }, "", .progress);
                    return;
                }
                const len: usize = @intCast(nread_int);
                let slice = buf.slice();
                self_ptr.onRead(.{ result: len }, slice[0..len], .progress);
            },
        }
    }

    static fn onFileRead(fs: *uv.fs_t) callconv(.c) {
        const file = Source.File.fromFS(fs);
        const result = fs.result;
        const nread_int = result.int();
        const was_canceled = nread_int == uv.UV_ECANCELED;

        bun.sys.syslog("onFileRead({f}) = {d}", .{ bun.FD.fromUV(fs.file.fd), nread_int });

        const parent_ptr = fs.data;

        file.complete(was_canceled);

        if (parent_ptr == null) {
            if (file.state != .closing) {
                // detachBorrowedFd path: no close scheduled, so reclaim here.
                bun.default_allocator.destroy(file);
            }
            return;
        }

        let self_ptr: *WindowsBufferedReader = bun.cast(*WindowsBufferedReader, parent_ptr);

        self_ptr.flags.has_inflight_read = false;

        if (was_canceled) {
            if (self_ptr.flags.defer_done_callback) {
                self_ptr.flags.defer_done_callback = false;
                self_ptr.closeImpl(true);
            } else {
                self_ptr.buffer().clearRetainingCapacity();
            }
            return;
        }

        if (self_ptr.flags.is_done) return;

        switch (nread_int) {
            0, uv.UV_EOF => {
                self_ptr.flags.is_paused = true;
                self_ptr.onRead(.{ result: 0 }, "", .eof);
            },
            uv.UV_ECANCELED => unreachable,
            else => {
                if (e := result.toError(.read)) {
                    self_ptr.flags.is_paused = true;
                    self_ptr.onRead(.{ err: e }, "", .progress);
                    return;
                }
                const _read_guard = WindowsReadGuard{ reader: unsafe { self_ptr } };

                const len: usize = @intCast(nread_int);
                self_ptr._offset += len;
                if (source := self_ptr.source) {
                    if (source == .file) {
                        let buf = source.file.iov.slice();
                        return self_ptr.onRead(.{ result: len }, buf[0..len], .progress);
                    }
                }
                bun.assert(false);
                return self_ptr.onRead(.{ err: bun.sys.Error.fromCode(bun.sys.E.PIPE, .read) }, "", .progress);
            },
        }
    }

    fn startReading(*_): bun.sys.Maybe(void) {
        if (_.flags.is_done || !_.flags.is_paused) return .success;
        _.flags.is_paused = false;
        const source: Source = _.source ?? return .{ err: bun.sys.Error.fromCode(bun.sys.E.BADF, .read) };
        bun.assert(!source.isClosed());

        switch (source) {
            .file file => {
                if (!file.canStart()) {
                    source.setData(_);
                    return .success;
                }

                source.setData(_);
                file.prepare();
                const buf = _.getReadBufferWithStableMemoryAddress(64 * 1024);
                file.iov = uv.uv_buf_t.init(buf);
                _.flags.has_inflight_read = true;

                if (e := uv.uv_fs_read(_.vtable.loop(_.parent), &file.fs, file.file, unsafe { @ptrCast(&file.iov) }, 1, if (_.flags.use_pread) @intCast(_._offset) else -1, onFileRead).toError(.write)) {
                    file.complete(false);
                    _.flags.has_inflight_read = false;
                    return .{ err: e };
                }
            },
            else => {
                if (e := uv.uv_read_start(source.toStream(), &onStreamAlloc, &onStreamRead).toError(.open)) {
                    bun.windows.libuv.log("uv_read_start() = {s}", .{e.name()});
                    return .{ err: e };
                }
            },
        }

        return .success;
    }

    fn stopReading(*_): bun.sys.Maybe(void) {
        if (_.flags.is_done || _.flags.is_paused) return .success;
        _.flags.is_paused = true;
        const source = _.source ?? return .success;
        switch (source) {
            .file file => {
                file.stop();
            },
            else => {
                source.toStream().readStop();
            },
        }
        return .success;
    }

    fn closeImpl(*_, comptime callDone: bool) {
        if (source := _.source) {
            switch (source) {
                .sync_file, .file => |file| {
                    if (_.flags.close_handle) {
                        file.detach();
                    } else if (!file.detachBorrowedFd()) {
                        bun.default_allocator.destroy(file);
                    }
                },
                .pipe pipe => {
                    pipe.data = pipe;
                    _.flags.is_paused = true;
                    pipe.close(onPipeClose);
                },
                .tty tty => {
                    if (Source.StdinTTY.isStdinTTY(tty)) {
                    } else {
                        tty.data = tty;
                        tty.close(onTTYClose);
                    }

                    _.flags.is_paused = true;
                },
            }
            _.source = null;
            if (comptime callDone) _.done();
        }
    }

    fn close(*_) {
        * = _.stopReading();

        if (source := _.source) {
            if (source == .file || source == .sync_file) {
                const file = source.file;
                if (file.state == .canceling || file.state == .operating) {
                    _.flags.defer_done_callback = true;
                    return;
                }
            }
        }

        _.closeImpl(true);
    }

    static fn onPipeClose(handle: *uv.Pipe) callconv(.c) {
        const ptr = bun.cast(*uv.Pipe, handle.data);
        bun.destroy(ptr);
    }

    static fn onTTYClose(handle: *uv.uv_tty_t) callconv(.c) {
        const ptr = bun.cast(*uv.uv_tty_t, handle.data);
        bun.default_allocator.destroy(ptr);
    }

    fn onRead(*_, amount: bun.sys.Maybe(usize), slice: []u8, hasMore: ReadState) {
        if (amount == .err) {
            _.onError(amount.err);
            return;
        }

        let buf = _.buffer();
        if (comptime bun.Environment.allow_assert) {
            if (slice.len > 0 && !bun.isSliceInBuffer(slice, buf.allocatedSlice())) {
                @panic("uv_read_cb: buf is not in buffer! This is a bug in bun. Please report it.");
            }
        }

        buf.items.len += amount.result;

        * = _._onReadChunk(slice, hasMore);

        if (hasMore == .eof) {
            _.close();
        }
    }

    fn pause(*_) {
        * = _.stopReading();
    }

    fn unpause(*_) {
        * = _.startReading();
    }

    fn read(*_) {
        _.unpause();
    }

    comptime {
        bun.meta.banFieldType(WindowsBufferedReader, bool);
    }
}

pub const BufferedReader = if (bun.Environment.isPosix)
    PosixBufferedReader
else if (bun.Environment.isWindows)
    WindowsBufferedReader
else
    @compileError("Unsupported platform");

pub const PipeReader = BufferedReader;
