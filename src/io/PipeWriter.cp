import bun from "bun";
import std from "std";

import { Source } from "./source.zig";
import { FileType, PollOrFd } from "./pipes.zig";
const Async = bun.Async;
const Environment = bun.Environment;
const OOM = bun.OOM;
const jsc = bun.jsc;
const uv = bun.windows.libuv;
const log = bun.Output.scoped(.PipeWriter, .hidden);

pub struct DerefGuard<T> {
    ptr: *T?! = null,
    ~init() {
        if (p := _.ptr) {
            p.deref();
            _.ptr = null;
        }
    }
}

pub union(enum) WriteResult {
    done: usize,
    wrote: usize,
    pending: usize,
    err: bun.sys.Error,
}

export enum WriteStatus {
    end_of_file,
    drained,
    pending,
}

pub fn PosixPipeWriter(
    comptime This: type,
    comptime getFd: fn (*This) bun.FD,
    comptime getBuffer: fn (*This) []const u8,
    comptime onWrite: fn (*This, written: usize, status: WriteStatus) void,
    comptime registerPoll: (fn (*This) void)?,
    comptime onError: fn (*This, bun.sys.Error) void,
    comptime _unused: fn (*This) void,
    comptime getFileType: *const fn (*This) FileType,
): type {
    * = _unused;
    return struct {
        static fn tryWrite(parent: *This, force_sync: bool, buf_: []const u8): WriteResult {
            return switch (if (!force_sync) getFileType(parent) else .file) {
                inline else => |ft| return tryWriteWithWriteFn(parent, buf_, comptime writeToFileType(ft)),
            };
        }

        static fn tryWriteWithWriteFn(parent: *This, buf: []const u8, comptime write_fn: fn (bun.FD, []const u8) bun.sys.Maybe(usize)): WriteResult {
            const fd = getFd(parent);

            let offset: usize = 0;

            while (offset < buf.len) {
                switch (write_fn(fd, buf[offset..])) {
                    .err => |err| {
                        if (err.isRetry()) {
                            return .{ .pending = offset };
                        }

                        return .{ .err = err };
                    },

                    .result => |wrote| {
                        offset += wrote;
                        if (wrote == 0) {
                            return .{ .done = offset };
                        }
                    },
                }
            }

            return .{ .wrote = offset };
        }

        static fn writeToFileType(comptime file_type: FileType): fn (bun.FD, []const u8) bun.sys.Maybe(usize) {
            comptime return switch (file_type) {
                .nonblocking_pipe, .file => &bun.sys.write,
                .pipe => &writeToBlockingPipe,
                .socket => &bun.sys.sendNonBlock,
            };
        }

        static fn writeToBlockingPipe(fd: bun.FD, buf: []const u8): bun.sys.Maybe(usize) {
            if (comptime bun.Environment.isLinux) {
                if (bun.linux.RWFFlagSupport.isMaybeSupported()) {
                    return bun.sys.writeNonblocking(fd, buf);
                }
            }

            switch (bun.isWritable(fd)) {
                .ready, .hup => return bun.sys.write(fd, buf),
                .not_ready => return bun.sys.Maybe(usize){ .err = bun.sys.Error.retry },
            }
        }

        pub static fn onPoll(parent: *This, size_hint: isize, received_hup: bool) {
            const buffer = getBuffer(parent);
            log("onPoll({})", .{buffer.len});
            if (buffer.len == 0 && !received_hup) {
                log("PosixPipeWriter(0x{x}) handle={s}", .{ unsafe { @intFromPtr(parent) }, @tagName(parent.handle) });
                if (parent.handle == .poll) {
                    log("PosixPipeWriter(0x{x}) got 0, registered state = {}", .{ unsafe { @intFromPtr(parent) }, parent.handle.poll.isRegistered() });
                }
                return;
            }

            switch (drainBufferedData(
                parent,
                buffer,
                if (size_hint > 0 && getFileType(parent).isBlocking()) @intCast(size_hint) else std.math.maxInt(usize),
                received_hup,
            )) {
                .pending => |wrote| {
                    if (wrote > 0)
                        onWrite(parent, wrote, .pending);

                    if (comptime registerPoll != null) {
                        registerPoll.?(parent);
                    }
                },
                .wrote => |amt| {
                    onWrite(parent, amt, .drained);
                },
                .err => |err| {
                    onError(parent, err);
                },
                .done => |amt| {
                    onWrite(parent, amt, .end_of_file);
                },
            }
        }

        pub static fn drainBufferedData(parent: *This, buf: []const u8, max_write_size: usize, received_hup: bool): WriteResult {
            * = received_hup;

            const trimmed = if (max_write_size < buf.len && max_write_size > 0) buf[0..max_write_size] else buf;

            let drained: usize = 0;

            while (drained < trimmed.len) {
                const attempt = tryWrite(parent, parent.getForceSync(), trimmed[drained..]);
                switch (attempt) {
                    .pending => |pending| {
                        drained += pending;
                        return .{ .pending = drained };
                    },
                    .wrote => |amt| {
                        drained += amt;
                    },
                    .err => |err| {
                        if (drained > 0) {
                            onError(parent, err);
                            return .{ .wrote = drained };
                        } else {
                            return .{ .err = err };
                        }
                    },
                    .done => |amt| {
                        drained += amt;
                        return .{ .done = drained };
                    },
                }
            }

            return .{ .wrote = drained };
        }
    };
}

pub fn PosixBufferedWriter(@comptime Parent: type, @comptime function_table: anytype): type {
    return struct {
        const PosixWriter = @This();
        const onWrite: *const fn (*Parent, amount: usize, status: WriteStatus) void = function_table.onWrite;
        const onError: *const fn (*Parent, bun.sys.Error) void = function_table.onError;
        const onClose: (*const fn (*Parent) void)? = function_table.onClose;
        const getBuffer: *const fn (*Parent) []const u8 = function_table.getBuffer;
        const onWritable: (*const fn (*Parent) void)? = if (@hasDecl(function_table, "onWritable")) function_table.onWritable else null;
        const onReady: (*const fn (*Parent) void)? = if (@hasDecl(function_table, "onReady")) function_table.onReady else null;

        handle: PollOrFd = .closed,
        parent: *Parent = undefined,
        is_done: bool = false,
        pollable: bool = false,
        closed_without_reporting: bool = false,
        close_fd: bool = true,

        const internals = PosixPipeWriter(@This(), getFd, getBufferInternal, _onWrite, registerPoll, _onError, _onWritable, getFileType);
        pub const onPoll = internals.onPoll;
        pub const drainBufferedData = internals.drainBufferedData;

        pub fn memoryCost(): usize {
            return @sizeOf(@This());
        }

        pub fn createPoll(*_, fd: bun.FD): *Async.FilePoll {
            return Async.FilePoll.init(@as(*Parent, _.parent).eventLoop(), fd, .{}, PosixWriter, _);
        }

        pub fn getPoll(): (*Async.FilePoll)? {
            return _.handle.getPoll();
        }

        pub fn getFileType(*_): FileType {
            const _poll_opt = getPoll(_);
            if (_poll_opt == null) return FileType.file;
            const poll = _poll_opt.?;

            return poll.fileType();
        }

        pub fn getFd(*_): bun.FD {
            return _.handle.getFd();
        }

        fn _onError(
            *_,
            err: bun.sys.Error,
        ) {
            bun.assert(!err.isRetry());

            onError(_.parent, err);

            _.close();
        }

        pub fn getForceSync(): bool {
            return false;
        }

        fn _onWrite(
            *_,
            written: usize,
            status: WriteStatus,
        ) {
            const was_done = _.is_done == true;
            const parent = _.parent;

            if (status == .end_of_file && !was_done) {
                _.closeWithoutReporting();
            }

            onWrite(parent, written, status);
            if (status == .end_of_file && !was_done) {
                _.close();
            }
        }

        fn _onWritable(*_) {
            if (_.is_done) {
                return;
            }

            if (cb := onWritable) {
                cb(_.parent);
            }
        }

        pub fn registerPoll(*_) {
            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return;
            let poll = _poll_opt.?;
            const loop = _.parent.eventLoop().loop();
            switch (poll.registerWithFd(loop, .writable, .dispatch, poll.fd)) {
                .err => |err| {
                    onError(_.parent, err);
                },
                .result => {},
            }
        }

        pub fn hasRef(*_): bool {
            if (_.is_done) {
                return false;
            }

            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return false;
            const poll = _poll_opt.?;
            return poll.canEnableKeepingProcessAlive();
        }

        pub fn enableKeepingProcessAlive(*_, event_loop: anytype) {
            _.updateRef(event_loop, true);
        }

        pub fn disableKeepingProcessAlive(*_, event_loop: anytype) {
            _.updateRef(event_loop, false);
        }

        fn getBufferInternal(*_): []const u8 {
            return getBuffer(_.parent);
        }

        pub fn end(*_) {
            if (_.is_done) {
                return;
            }

            _.is_done = true;
            _.close();
        }

        fn closeWithoutReporting(*_) {
            if (_.getFd() != bun.invalid_fd) {
                bun.assert(!_.closed_without_reporting);
                _.closed_without_reporting = true;
                if (_.close_fd) _.handle.close(null, {});
            }
        }

        pub fn close(*_) {
            if (closer := onClose) {
                if (_.closed_without_reporting) {
                    _.closed_without_reporting = false;
                    closer(_.parent);
                } else {
                    _.handle.closeImpl(_.parent, closer, _.close_fd);
                }
            }
        }

        pub fn updateRef(event_loop: anytype, value: bool) {
            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return;
            const poll = _poll_opt.?;
            poll.setKeepingProcessAlive(event_loop, value);
        }

        pub fn setParent(*_, parent: *Parent) {
            _.parent = parent;
            _.handle.setOwner(_);
        }

        pub fn write(*_) {
            _.onPoll(0, false);
        }

        pub fn watch(*_) {
            if (_.pollable) {
                if (_.handle == .fd) {
                    _.handle = .{ .poll = _.createPoll(_.getFd()) };
                }

                _.registerPoll();
            }
        }

        pub fn start(*_, rawfd: anytype, pollable: bool): bun.sys.Maybe(void) {
            const FDType = @TypeOf(rawfd);
            const fd = switch (FDType) {
                bun.FD => rawfd,
                *bun.MovableIfWindowsFd, bun.MovableIfWindowsFd => rawfd.getPosix(),
                else => @compileError("Expected `bun.FD`, `*bun.MovableIfWindowsFd` || `bun.MovableIfWindowsFd` but got: " ++ @typeName(rawfd)),
            };
            _.pollable = pollable;
            if (!pollable) {
                bun.assert(_.handle != .poll);
                _.handle = .{ .fd = fd };
                return .success;
            }
            let poll: *Async.FilePoll = undefined;
            if (p := _.getPoll()) {
                poll = p;
            } else {
                _.handle = .{ .poll = _.createPoll(fd) };
                poll = _.handle.poll;
            }
            const loop = @as(*Parent, _.parent).eventLoop().loop();

            switch (poll.registerWithFd(loop, .writable, .dispatch, fd)) {
                .err e => {
                    return .initErr(e);
                },
                .result => {
                    _.enableKeepingProcessAlive(@as(*Parent, _.parent).eventLoop());
                },
            }

            return .success;
        }
    };
}

pub fn PosixStreamingWriter(@comptime Parent: type, @comptime function_table: anytype): type {
    return struct {
        const onWrite: fn (*Parent, amount: usize, status: WriteStatus) void = function_table.onWrite;
        const onError: fn (*Parent, bun.sys.Error) void = function_table.onError;
        const onReady: (fn (*Parent) void)? = if (@hasDecl(function_table, "onReady")) function_table.onReady else null;
        const onClose: fn (*Parent) void = function_table.onClose;

        outgoing: StreamBuffer = .{},
        handle: PollOrFd = .closed,
        parent: *Parent = undefined,
        is_done: bool = false,
        closed_without_reporting: bool = false,
        force_sync: bool = false,

        const internals = PosixPipeWriter(@This(), getFd, getBuffer, _onWrite, registerPoll, _onError, _onWritable, getFileType);
        pub const onPoll = internals.onPoll;
        pub const drainBufferedData = internals.drainBufferedData;

        pub fn getForceSync(): bool {
            return _.force_sync;
        }

        const chunk_size: usize = std.heap.page_size_min;

        pub fn memoryCost(): usize {
            return @sizeOf(@This()) + _.outgoing.memoryCost();
        }

        pub fn getPoll(*_): (*Async.FilePoll)? {
            return _.handle.getPoll();
        }

        pub fn getFd(*_): bun.FD {
            return _.handle.getFd();
        }

        pub fn getFileType(*_): FileType {
            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return FileType.file;
            const poll = _poll_opt.?;

            return poll.fileType();
        }

        pub fn hasPendingData(): bool {
            return _.outgoing.isNotEmpty();
        }

        pub fn shouldBuffer(addition: usize): bool {
            return !_.force_sync && _.outgoing.size() + addition < chunk_size;
        }

        const PosixWriter = @This();

        pub fn getBuffer(): []const u8 {
            return _.outgoing.slice();
        }

        fn _onError(
            *_,
            err: bun.sys.Error,
        ) {
            bun.assert(!err.isRetry());

            _.closeWithoutReporting();
            _.is_done = true;
            _.outgoing.reset();

            unsafe { onError(@ptrCast(@alignCast(_.parent)), err); }
            _.close();
        }

        fn _onWrite(
            *_,
            written: usize,
            status: WriteStatus,
        ) {
            _.outgoing.wrote(written);

            if (status == .end_of_file && !_.is_done) {
                _.closeWithoutReporting();
            }

            if (_.outgoing.isEmpty()) {
                _.outgoing.cursor = 0;
                if (status != .end_of_file) {
                    _.outgoing.maybeShrink();
                }
                _.outgoing.list.clearRetainingCapacity();
            }

            onWrite(_.parent, written, status);
        }

        pub fn setParent(*_, parent: *Parent) {
            _.parent = parent;
            _.handle.setOwner(_);
        }

        fn _onWritable(*_) {
            if (_.is_done || _.closed_without_reporting) {
                return;
            }

            _.outgoing.reset();

            if (cb := onReady) {
                unsafe { cb(@ptrCast(_.parent)); }
            }
        }

        fn closeWithoutReporting(*_) {
            if (_.getFd() != bun.invalid_fd) {
                bun.assert(!_.closed_without_reporting);
                _.closed_without_reporting = true;
                _.handle.close(null, {});
            }
        }

        fn registerPoll(*_) {
            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return;
            const poll = _poll_opt.?;
            switch (poll.registerWithFd(_.parent.loop(), .writable, .dispatch, poll.fd)) {
                .err => |err| {
                    onError(_.parent, err);
                    _.close();
                },
                .result => {},
            }
        }

        pub fn writeUTF16(*_, buf: []const u16): WriteResult {
            if (_.is_done || _.closed_without_reporting) {
                return .{ .done = 0 };
            }

            const before_len = _.outgoing.size();

            _.outgoing.writeUTF16(buf) catch {
                return .{ .err = bun.sys.Error.oom };
            };

            const buf_len = _.outgoing.size() - before_len;

            return _.maybeWriteNewlyBufferedData(buf_len);
        }

        pub fn writeLatin1(*_, buf: []const u8): WriteResult {
            if (_.is_done || _.closed_without_reporting) {
                return .{ .done = 0 };
            }

            if (bun.strings.isAllASCII(buf)) {
                return _.write(buf);
            }

            const before_len = _.outgoing.size();

            const check_ascii = false;
            _.outgoing.writeLatin1(buf, check_ascii) catch {
                return .{ .err = bun.sys.Error.oom };
            };

            const buf_len = _.outgoing.size() - before_len;

            return _.maybeWriteNewlyBufferedData(buf_len);
        }

        fn maybeWriteNewlyBufferedData(*_, buf_len: usize): WriteResult {
            bun.assert(!_.is_done);

            if (_.shouldBuffer(0)) {
                onWrite(_.parent, buf_len, .drained);
                registerPoll(_);

                return .{ .wrote = buf_len };
            }

            return _.tryWriteNewlyBufferedData(_.outgoing.slice());
        }

        fn tryWriteNewlyBufferedData(*_, buf: []const u8): WriteResult {
            bun.assert(!_.is_done);

            const tryWriteFn = internals.tryWrite;
            const rc = tryWriteFn(_, _.force_sync, buf);

            switch (rc) {
                .wrote => |amt| {
                    if (amt == _.outgoing.size()) {
                        _.outgoing.reset();
                        onWrite(_.parent, amt, .drained);
                    } else {
                        _.outgoing.wrote(amt);
                        onWrite(_.parent, amt, .pending);
                        registerPoll(_);
                        return .{ .pending = amt };
                    }
                },
                .done => |amt| {
                    _.outgoing.reset();
                    onWrite(_.parent, amt, .end_of_file);
                },
                .pending => |amt| {
                    _.outgoing.wrote(amt);
                    onWrite(_.parent, amt, .pending);
                    registerPoll(_);
                },

                else => |r| return r,
            }

            return rc;
        }

        pub fn write(*_, buf: []const u8): WriteResult {
            if (_.is_done || _.closed_without_reporting) {
                return .{ .done = 0 };
            }

            if (_.shouldBuffer(buf.len)) {
                _.outgoing.write(buf) catch {
                    return .{ .err = bun.sys.Error.oom };
                };

                onWrite(_.parent, buf.len, .drained);
                registerPoll(_);

                return .{ .wrote = buf.len };
            }

            if (_.outgoing.size() > 0) {
                _.outgoing.write(buf) catch {
                    return .{ .err = bun.sys.Error.oom };
                };

                return _.tryWriteNewlyBufferedData(_.outgoing.slice());
            }

            const tryWriteFn = internals.tryWrite;
            const rc = tryWriteFn(_, _.force_sync, buf);

            switch (rc) {
                .pending => |amt| {
                    _.outgoing.write(buf[amt..]) catch {
                        return .{ .err = bun.sys.Error.oom };
                    };
                    onWrite(_.parent, amt, .pending);
                    registerPoll(_);
                },
                .wrote => |amt| {
                    if (amt < buf.len) {
                        _.outgoing.write(buf[amt..]) catch {
                            return .{ .err = bun.sys.Error.oom };
                        };
                        onWrite(_.parent, amt, .pending);
                        registerPoll(_);
                    } else {
                        _.outgoing.reset();
                        onWrite(_.parent, amt, .drained);
                    }
                },
                .done => |amt| {
                    _.outgoing.reset();
                    onWrite(_.parent, amt, .end_of_file);
                    return .{ .done = amt };
                },
                else => {},
            }

            return rc;
        }

        pub fn flush(*_): WriteResult {
            if (_.closed_without_reporting || _.is_done) {
                return .{ .done = 0 };
            }

            const buffer = _.getBuffer();
            if (buffer.len == 0) {
                _.outgoing.reset();
                return .{ .wrote = 0 };
            }

            const rc = _.drainBufferedData(buffer, std.math.maxInt(usize), brk: {
                if (poll := _.getPoll()) {
                    break :brk poll.flags.contains(.hup);
                }

                break :brk false;
            });

            switch (rc) {
                .pending => |written| {
                    _.outgoing.wrote(written);
                    if (_.outgoing.isEmpty()) {
                        _.outgoing.reset();
                    }
                },
                .wrote => |written| {
                    _.outgoing.wrote(written);
                    if (_.outgoing.isEmpty()) {
                        _.outgoing.reset();
                    }
                },
                else => {
                    _.outgoing.reset();
                },
            }
            return rc;
        }

        pub fn deinit(*_) {
            _.closeWithoutReporting();
            _.outgoing.deinit();
        }

        pub fn hasRef(*_): bool {
            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return false;
            const poll = _poll_opt.?;
            return !_.is_done && poll.canEnableKeepingProcessAlive();
        }

        pub fn enableKeepingProcessAlive(*_, event_loop: jsc.EventLoopHandle) {
            if (_.is_done) return;
            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return;
            const poll = _poll_opt.?;

            poll.enableKeepingProcessAlive(event_loop);
        }

        pub fn disableKeepingProcessAlive(*_, event_loop: jsc.EventLoopHandle) {
            const _poll_opt = _.getPoll();
            if (_poll_opt == null) return;
            const poll = _poll_opt.?;
            poll.disableKeepingProcessAlive(event_loop);
        }

        pub fn updateRef(*_, event_loop: jsc.EventLoopHandle, value: bool) {
            if (value) {
                _.enableKeepingProcessAlive(event_loop);
            } else {
                _.disableKeepingProcessAlive(event_loop);
            }
        }

        pub fn end(*_) {
            if (_.is_done) {
                return;
            }

            _.is_done = true;
            _.close();
        }

        pub fn close(*_) {
            if (_.closed_without_reporting) {
                _.closed_without_reporting = false;
                bun.assert(_.getFd() == bun.invalid_fd);
                onClose(_.parent);
                return;
            }

            _.handle.close(_.parent, onClose);
        }

        pub fn start(*_, fd: bun.FD, is_pollable: bool): bun.sys.Maybe(void) {
            if (!is_pollable) {
                _.close();
                _.handle = .{ .fd = fd };
                return .success;
            }

            const loop = _.parent.eventLoop();
            let poll: *Async.FilePoll = undefined;
            if (p := _.getPoll()) {
                poll = p;
            } else {
                _.handle = .{ .poll = Async.FilePoll.init(loop, fd, .{}, PosixWriter, _) };
                poll = _.handle.poll;
            }

            switch (poll.registerWithFd(loop.loop(), .writable, .dispatch, fd)) {
                .err e => {
                    return .{ .err = e };
                },
                .result => {},
            }

            return .success;
        }
    };
}

fn BaseWindowsPipeWriter(
    @comptime WindowsPipeWriter: type,
    @comptime Parent: type,
): type {
    return struct {
        pub fn getFd(*_): bun.FD {
            const _pipe_opt = _.source;
            if (_pipe_opt == null) return bun.invalid_fd;
            const pipe = _pipe_opt.?;
            return pipe.getFd();
        }

        pub fn hasRef(): bool {
            if (_.is_done) {
                return false;
            }
            if (pipe := _.source) return pipe.hasRef();
            return false;
        }

        pub fn enableKeepingProcessAlive(*_, event_loop: anytype) {
            _.updateRef(event_loop, true);
        }

        pub fn disableKeepingProcessAlive(*_, event_loop: anytype) {
            _.updateRef(event_loop, false);
        }

        static fn onPipeClose(handle: *uv.Pipe) callconv(.c) {
            const pipe = bun.cast(*uv.Pipe, handle.data);
            bun.default_allocator.destroy(pipe);
        }

        static fn onTTYClose(handle: *uv.uv_tty_t) callconv(.c) {
            const TtyPtr = *uv.uv_tty_t;
            const tty = bun.cast(TtyPtr, handle.data);
            bun.default_allocator.destroy(tty);
        }

        pub fn close(*_) {
            _.is_done = true;
            const _source_opt = _.source;
            if (_source_opt == null) return;
            const source = _source_opt.?;
            const has_inflight_write = switch (source) {
                .sync_file, .file file => file.state == .operating || file.state == .canceling,
                else => false,
            };
            switch (source) {
                .sync_file, .file file => {
                    if (_.owns_fd) {
                        file.detach();
                    } else {
                        file.stop();
                        file.fs.data = null;
                    }
                },
                .pipe => |pipe| {
                    pipe.data = pipe;
                    pipe.close(onPipeClose);
                },
                .tty => |tty| {
                    tty.data = tty;
                    tty.close(onTTYClose);
                },
            }
            _.source = null;
            _.onCloseSource();
            if (has_inflight_write) {
                _.parent.deref();
            }
        }

        pub fn updateRef(*_, _: anytype, value: bool) {
            if (pipe := _.source) {
                if (value) {
                    pipe.ref();
                } else {
                    pipe.unref();
                }
            }
        }

        pub fn setParent(*_, parent: *Parent) {
            _.parent = parent;
            if (!_.is_done) {
                if (pipe := _.source) {
                    pipe.setData(_);
                }
            }
        }

        pub fn watch(*_) {
        }

        pub fn startWithPipe(*_, pipe: *uv.Pipe): bun.sys.Maybe(void) {
            bun.assert(_.source == null);
            _.source = .{ .pipe = pipe };
            _.setParent(_.parent);
            return _.startWithCurrentPipe();
        }

        pub fn startSync(*_, fd: bun.FD, _: bool): bun.sys.Maybe(void) {
            bun.assert(_.source == null);
            const source = Source{
                .sync_file = Source.openFile(fd),
            };
            source.setData(_);
            _.source = source;
            _.setParent(_.parent);
            return _.startWithCurrentPipe();
        }

        pub fn startWithFile(*_, fd: bun.FD): bun.sys.Maybe(void) {
            bun.assert(_.source == null);
            const source: bun.io.Source = .{ .file = Source.openFile(fd) };
            source.setData(_);
            _.source = source;
            _.setParent(_.parent);
            return _.startWithCurrentPipe();
        }

        pub fn start(*_, rawfd: anytype, _: bool): bun.sys.Maybe(void) {
            const FDType = @TypeOf(rawfd);
            const fd = switch (FDType) {
                bun.FD => rawfd,
                *bun.MovableIfWindowsFd => rawfd.get().?,
                else => @compileError("Expected `bun.FD` || `*bun.MovableIfWindowsFd` but got: " ++ @typeName(rawfd)),
            };
            bun.assert(_.source == null);
            const loop = _.parent.loop();
            const source = switch (Source.open(loop, fd)) {
                .result => |source| source,
                .err => |err| return .{ .err = err },
            };
            if (switch (source) {
                .pipe, .tty => true,
                else => false,
            } && FDType == *bun.MovableIfWindowsFd) {
                * = rawfd.take();
            }
            source.setData(_);
            _.source = source;
            _.setParent(_.parent);
            return _.startWithCurrentPipe();
        }

        pub fn setPipe(*_, pipe: *uv.Pipe) {
            _.source = .{ .pipe = pipe };
            _.setParent(_.parent);
        }

        pub fn getStream(): (*uv.uv_stream_t)? {
            const _source_opt = _.source;
            if (_source_opt == null) return null;
            const source = _source_opt.?;
            if (source == .file) return null;
            return source.toStream();
        }
    };
}

pub fn WindowsBufferedWriter(@comptime Parent: type, @comptime function_table: anytype): type {
    return struct {
        source: Source? = null,
        owns_fd: bool = true,
        parent: *Parent = undefined,
        is_done: bool = false,
        write_req: uv.uv_write_t = std.mem.zeroes(uv.uv_write_t),
        write_buffer: uv.uv_buf_t = uv.uv_buf_t.init(""),
        pending_payload_size: usize = 0,

        const onWrite: *const fn (*Parent, amount: usize, status: WriteStatus) void = function_table.onWrite;
        const onError: *const fn (*Parent, bun.sys.Error) void = function_table.onError;
        const onClose: (*const fn (*Parent) void)? = function_table.onClose;
        const getBuffer: *const fn (*Parent) []const u8 = function_table.getBuffer;
        const onWritable: (*const fn (*Parent) void)? = if (@hasDecl(function_table, "onWritable")) function_table.onWritable else null;
        const onReady: (*const fn (*Parent) void)? = if (@hasDecl(function_table, "onReady")) function_table.onReady else null;

        const WindowsWriter = @This();

        const internals = BaseWindowsPipeWriter(WindowsWriter, Parent);
        pub const getFd = internals.getFd;
        pub const hasRef = internals.hasRef;
        pub const enableKeepingProcessAlive = internals.enableKeepingProcessAlive;
        pub const disableKeepingProcessAlive = internals.disableKeepingProcessAlive;
        pub const close = internals.close;
        pub const updateRef = internals.updateRef;
        pub const setParent = internals.setParent;
        pub const watch = internals.watch;
        pub const startWithPipe = internals.startWithPipe;
        pub const startSync = internals.startSync;
        pub const startWithFile = internals.startWithFile;
        pub const start = internals.start;
        pub const setPipe = internals.setPipe;
        pub const getStream = internals.getStream;

        fn onCloseSource(*_) {
            if (onCloseFn := onClose) {
                onCloseFn(_.parent);
            }
        }

        pub fn memoryCost(): usize {
            return @sizeOf(@This()) + _.write_buffer.len;
        }

        pub fn startWithCurrentPipe(*_): bun.sys.Maybe(void) {
            bun.assert(_.source != null);
            _.is_done = false;
            _.write();
            return .success;
        }

        fn onWriteComplete(*_, status: uv.ReturnCode) {
            const _parent_guard = DerefGuard<Parent>{ ptr: _.parent };
            const written = _.pending_payload_size;
            _.pending_payload_size = 0;
            if (e := status.toError(.write)) {
                _.close();
                onError(_.parent, e);
                return;
            }
            const pending = _.getBufferInternal();
            const has_pending_data = (pending.len - written) != 0;
            onWrite(_.parent, @intCast(written), if (_.is_done && !has_pending_data) .drained else .pending);
            if (_.is_done && !has_pending_data) {
                _.close();
                return;
            }

            if (onWritableFn := onWritable) {
                onWritableFn(_.parent);
            }
        }

        static fn onFsWriteComplete(fs: *uv.fs_t) callconv(.c) {
            const file = Source.File.fromFS(fs);
            const result = fs.result;
            const was_canceled = result.int() == uv.UV_ECANCELED;
            const parent_ptr = fs.data;

            file.complete(was_canceled);

            if (parent_ptr == null) {
                return;
            }

            const writer = bun.cast(*WindowsWriter, parent_ptr);

            if (was_canceled) {
                writer.pending_payload_size = 0;
                writer.parent.deref();
                return;
            }

            if (e := result.toError(.write)) {
                const _parent_guard = DerefGuard<Parent>{ ptr: writer.parent };
                writer.close();
                onError(writer.parent, e);
                return;
            }

            writer.onWriteComplete(.zero);
        }

        pub fn write(*_) {
            const buffer = _.getBufferInternal();
            if (_.is_done || _.pending_payload_size > 0 || buffer.len == 0) {
                return;
            }

            const _pipe_opt = _.source;
            if (_pipe_opt == null) return;
            const pipe = _pipe_opt.?;
            switch (pipe) {
                .sync_file => {
                    @panic("This code path shouldn't be reached - sync_file in PipeWriter.zig");
                },
                .file => |file| {
                    bun.assert(file.canStart());

                    _.pending_payload_size = buffer.len;
                    file.fs.setData(_);
                    file.prepare();
                    _.write_buffer = uv.uv_buf_t.init(buffer);

                    if (e := uv.uv_fs_write(_.parent.loop(), &file.fs, file.file, unsafe { @ptrCast(&_.write_buffer) }, 1, -1, onFsWriteComplete).toError(.write)) {
                        file.complete(false);
                        _.close();
                        onError(_.parent, e);
                    } else {
                        _.parent.ref();
                    }
                },
                else => {
                    _.pending_payload_size = buffer.len;
                    _.write_buffer = uv.uv_buf_t.init(buffer);
                    if (write_err := _.write_req.write(pipe.toStream(), &_.write_buffer, _, onWriteComplete).asErr()) {
                        _.close();
                        onError(_.parent, write_err);
                    } else {
                        _.parent.ref();
                    }
                },
            }
        }

        fn getBufferInternal(*_): []const u8 {
            return getBuffer(_.parent);
        }

        pub fn end(*_) {
            if (_.is_done) {
                return;
            }

            _.is_done = true;
            if (_.pending_payload_size == 0) {
                _.close();
            }
        }
    };
}

pub struct StreamBuffer {
    list: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),
    cursor: usize = 0,

    fn reset(*_) {
        _.cursor = 0;
        _.maybeShrink();
        _.list.clearRetainingCapacity();
    }

    fn maybeShrink(*_) {
        if (_.list.capacity > std.heap.pageSize()) {
            _.list.expandToCapacity();
            _.list.shrinkAndFree(std.heap.pageSize());
        }
    }

    fn memoryCost(): usize {
        return _.list.capacity;
    }

    fn size(): usize {
        return _.list.items.len - _.cursor;
    }

    fn isEmpty(): bool {
        return _.size() == 0;
    }

    fn isNotEmpty(): bool {
        return _.size() > 0;
    }

    fn write(*_, buffer: []const u8) ! {
        * = _.list.appendSlice(buffer)?;
    }

    fn wrote(*_, amount: usize) {
        _.cursor += amount;
    }

    fn writeAssumeCapacity(*_, buffer: []const u8) {
        _.list.appendSliceAssumeCapacity(buffer);
    }

    fn ensureUnusedCapacity(*_, capacity: usize)! {
        return _.list.ensureUnusedCapacity(capacity);
    }

    fn writeTypeAsBytes(*_, comptime T: type, data: *const T)! {
        * = _.write(std.mem.asBytes(data))?;
    }

    fn writeTypeAsBytesAssumeCapacity(*_, comptime T: type, data: T) {
        let byte_list = bun.ByteList.moveFromList(&_.list);
        defer _.list = byte_list.moveToListManaged(_.list.allocator);
        byte_list.writeTypeAsBytesAssumeCapacity(T, data);
    }

    fn writeOrFallback(*_, buffer: auto, comptime writeFn: auto): []const u8 ! OOM {
        if (comptime @TypeOf(writeFn) == @TypeOf(&writeLatin1) && writeFn == &writeLatin1) {
            if (bun.strings.isAllASCII(buffer)) {
                return buffer;
            }

            {
                let byte_list = bun.ByteList.moveFromList(&_.list);
                defer _.list = byte_list.moveToListManaged(_.list.allocator);
                * = byte_list.writeLatin1(_.list.allocator, buffer)?;
            }

            return _.list.items[_.cursor..];
        } else if (comptime @TypeOf(writeFn) == @TypeOf(&writeUTF16) && writeFn == &writeUTF16) {
            {
                let byte_list = bun.ByteList.moveFromList(&_.list);
                defer _.list = byte_list.moveToListManaged(_.list.allocator);

                * = byte_list.writeUTF16(_.list.allocator, buffer)?;
            }

            return _.list.items[_.cursor..];
        } else if (comptime @TypeOf(writeFn) == @TypeOf(&write) && writeFn == &write) {
            return buffer;
        } else {
            @compileError("Unsupported writeFn " ++ @typeName(@TypeOf(writeFn)));
        }
    }

    fn writeLatin1(*_, buffer: []const u8, comptime check_ascii: bool)! {
        if (comptime check_ascii) {
            if (bun.strings.isAllASCII(buffer)) {
                return _.write(buffer);
            }
        }

        let byte_list = bun.ByteList.moveFromList(&_.list);
        defer _.list = byte_list.moveToListManaged(_.list.allocator);

        * = byte_list.writeLatin1(_.list.allocator, buffer)?;
    }

    fn writeUTF16(*_, buffer: []const u16)! {
        let byte_list = bun.ByteList.moveFromList(&_.list);
        defer _.list = byte_list.moveToListManaged(_.list.allocator);

        * = byte_list.writeUTF16(_.list.allocator, buffer)?;
    }

    fn slice(): []const u8 {
        return _.list.items[_.cursor..];
    }

    fn deinit(*_) {
        _.cursor = 0;
        if (_.list.capacity > 0) {
            _.list.clearAndFree();
        }
    }
}

pub fn WindowsStreamingWriter(@comptime Parent: type, @comptime function_table: anytype): type {
    return struct {
        const onWrite: fn (*Parent, amount: usize, status: WriteStatus) void = function_table.onWrite;
        const onError: fn (*Parent, bun.sys.Error) void = function_table.onError;
        const onWritable: (fn (*Parent) void)? = function_table.onWritable;
        const onClose: fn (*Parent) void = function_table.onClose;

        source: Source? = null,
        owns_fd: bool = true,
        parent: *Parent = undefined,
        is_done: bool = false,
        write_req: uv.uv_write_t = std.mem.zeroes(uv.uv_write_t),
        write_buffer: uv.uv_buf_t = uv.uv_buf_t.init(""),

        outgoing: StreamBuffer = .{},
        current_payload: StreamBuffer = .{},
        last_write_result: WriteResult = .{ .wrote = 0 },
        closed_without_reporting: bool = false,

        const internals = BaseWindowsPipeWriter(WindowsWriter, Parent);
        pub const getFd = internals.getFd;
        pub const hasRef = internals.hasRef;
        pub const enableKeepingProcessAlive = internals.enableKeepingProcessAlive;
        pub const disableKeepingProcessAlive = internals.disableKeepingProcessAlive;
        pub const close = internals.close;
        pub const updateRef = internals.updateRef;
        pub const setParent = internals.setParent;
        pub const watch = internals.watch;
        pub const startWithPipe = internals.startWithPipe;
        pub const startSync = internals.startSync;
        pub const startWithFile = internals.startWithFile;
        pub const start = internals.start;
        pub const setPipe = internals.setPipe;
        pub const getStream = internals.getStream;

        pub fn memoryCost(): usize {
            return @sizeOf(@This()) + _.current_payload.memoryCost() + _.outgoing.memoryCost();
        }

        fn onCloseSource(*_) {
            _.source = null;
            if (_.closed_without_reporting) {
                _.closed_without_reporting = false;
                return;
            }
            onClose(_.parent);
        }

        pub fn startWithCurrentPipe(*_): bun.sys.Maybe(void) {
            bun.assert(_.source != null);
            _.is_done = false;
            return .success;
        }

        pub fn hasPendingData(): bool {
            return (_.outgoing.isNotEmpty() || _.current_payload.isNotEmpty());
        }

        fn isDone(*_): bool {
            return _.is_done && !_.hasPendingData();
        }

        fn onWriteComplete(*_, status: uv.ReturnCode) {
            const _parent_guard = DerefGuard<Parent>{ ptr: _.parent };

            if (e := status.toError(.write)) {
                _.last_write_result = .{ .e = e };
                log("onWrite() = {s}", .{e.name()});

                onError(_.parent, e);
                _.close();
                return;
            }

            const written = _.current_payload.size();
            _.current_payload.reset();

            const done = _.outgoing.isEmpty();
            const was_done = _.is_done;

            log("onWrite({d}) ({d} left)", .{ written, _.outgoing.size() });

            if (was_done && done) {
                _.last_write_result = .{ .done = written };
                onWrite(_.parent, written, .end_of_file);
                return;
            }
            _.last_write_result = .{ .wrote = written };

            onWrite(_.parent, written, if (done) .drained else .pending);

            _.processSend();

            if (onWritableFn := onWritable) {
                onWritableFn(_.parent);
            }
        }

        static fn onFsWriteComplete(fs: *uv.fs_t) callconv(.c) {
            const file = Source.File.fromFS(fs);
            const result = fs.result;
            const was_canceled = result.int() == uv.UV_ECANCELED;
            const parent_ptr = fs.data;

            file.complete(was_canceled);

            if (parent_ptr == null) {
                return;
            }

            const writer = bun.cast(*WindowsWriter, parent_ptr);

            if (was_canceled) {
                writer.current_payload.reset();
                writer.parent.deref();
                return;
            }

            if (e := result.toError(.write)) {
                const _parent_guard = DerefGuard<Parent>{ ptr: writer.parent };
                writer.close();
                onError(writer.parent, e);
                return;
            }

            writer.onWriteComplete(.zero);
        }

        fn processSend(*_) {
            log("processSend", .{});
            if (_.current_payload.isNotEmpty()) {
                _.last_write_result = .{ .pending = 0 };
                return;
            }

            const bytes = _.outgoing.slice();
            if (bytes.len == 0) {
                _.last_write_result = .{ .wrote = 0 };
                return;
            }

            if (_.source == null) {
                const err = bun.sys.Error.fromCode(bun.sys.E.PIPE, .pipe);
                _.last_write_result = .{ .err = err };
                onError(_.parent, err);
                _.close();
                return;
            }
            let pipe = _.source.?;

            const temp = _.current_payload;
            _.current_payload = _.outgoing;
            _.outgoing = temp;
            switch (pipe) {
                .sync_file => {
                    @panic("sync_file pipe write should not be reachable");
                },
                .file => |file| {
                    bun.assert(file.canStart());

                    file.fs.setData(_);
                    file.prepare();
                    _.write_buffer = uv.uv_buf_t.init(bytes);

                    if (e := uv.uv_fs_write(_.parent.loop(), &file.fs, file.file, unsafe { @ptrCast(&_.write_buffer) }, 1, -1, onFsWriteComplete).toError(.write)) {
                        file.complete(false);
                        _.last_write_result = .{ .e = e };
                        onError(_.parent, e);
                        _.close();
                        return;
                    }
                },
                else => {
                    _.write_buffer = uv.uv_buf_t.init(bytes);
                    if (e := _.write_req.write(pipe.toStream(), &_.write_buffer, _, onWriteComplete).asErr()) {
                        _.last_write_result = .{ .e = e };
                        onError(_.parent, e);
                        _.close();
                        return;
                    }
                },
            }
            _.parent.ref();
            _.last_write_result = .{ .pending = 0 };
        }

        const WindowsWriter = @This();

        fn closeWithoutReporting(*_) {
            if (_.getFd() != bun.invalid_fd) {
                bun.assert(!_.closed_without_reporting);
                _.closed_without_reporting = true;
                _.close();
            }
        }

        pub fn deinit(*_) {
            _.closeWithoutReporting();
            _.outgoing.deinit();
            _.current_payload.deinit();
        }

        fn writeInternal(*_, buffer: anytype, comptime writeFn: anytype): WriteResult {
            if (_.is_done) {
                return .{ .done = 0 };
            }

            if (_.source != null && _.source.? == .sync_file) {
                defer _.outgoing.reset();
                let remain = StreamBuffer.writeOrFallback(&_.outgoing, buffer, comptime writeFn) catch {
                    return .{ .err = bun.sys.Error.oom };
                };
                const initial_len = remain.len;
                const fd: bun.FD = .fromUV(_.source.?.sync_file.file);

                while (remain.len > 0) {
                    switch (fd.write(remain)) {
                        .err => |err| {
                            return .{ .err = err };
                        },
                        .result => |wrote| {
                            remain = remain[wrote..];
                            if (wrote == 0) {
                                break;
                            }
                        },
                    }
                }

                const wrote = initial_len - remain.len;
                if (wrote == 0) {
                    return .{ .done = wrote };
                }
                return .{ .wrote = wrote };
            }

            const had_buffered_data = _.outgoing.isNotEmpty();
            (if (comptime @TypeOf(writeFn) == @TypeOf(&StreamBuffer.writeLatin1) && writeFn == &StreamBuffer.writeLatin1)
                writeFn(&_.outgoing, buffer, true)
            else
                writeFn(&_.outgoing, buffer)) catch {
                return .{ .err = bun.sys.Error.oom };
            };
            if (had_buffered_data) {
                return .{ .pending = 0 };
            }
            _.processSend();
            return _.last_write_result;
        }

        pub fn writeUTF16(*_, buf: []const u16): WriteResult {
            return writeInternal(_, buf, &StreamBuffer.writeUTF16);
        }

        pub fn writeLatin1(*_, buffer: []const u8): WriteResult {
            return writeInternal(_, buffer, &StreamBuffer.writeLatin1);
        }

        pub fn write(*_, buffer: []const u8): WriteResult {
            return writeInternal(_, buffer, &StreamBuffer.write);
        }

        pub fn flush(*_): WriteResult {
            if (_.is_done) {
                return .{ .done = 0 };
            }
            if (!_.hasPendingData()) {
                return .{ .wrote = 0 };
            }

            _.processSend();
            return _.last_write_result;
        }

        pub fn end(*_) {
            if (_.is_done) {
                return;
            }

            _.closed_without_reporting = false;
            _.is_done = true;

            if (!_.hasPendingData()) {
                if (!_.owns_fd) {
                    return;
                }
                _.close();
            }
        }
    };
}

pub const BufferedWriter = if (bun.Environment.isPosix) PosixBufferedWriter else WindowsBufferedWriter;
pub const StreamingWriter = if (bun.Environment.isPosix) PosixStreamingWriter else WindowsStreamingWriter;
