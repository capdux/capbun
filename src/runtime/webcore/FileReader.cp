let empty_u8_slice: [0]u8 = undefined;

import bun from "bun";
import { Environment, Output, jsc } from "bun";

const webcore = bun.webcore;
const Blob = webcore.Blob;
const ReadableStream = webcore.ReadableStream;
const streams = webcore.streams;

const log = Output.scoped(.FileReader, .visible);

@c {
    fn open_as_nonblocking_tty(fd: i32, flags: i32): i32;
}

pub union(enum) ReadDuringJSOnPullResult {
    none: void,
    js: []u8,
    amount_read: usize,
    temporary: []const u8,
    use_buffered: usize,
}

pub struct FileReaderOpenedFileBlob {
    fd: bun.FD,
    pollable: bool = false,
    nonblocking: bool = true,
    file_type: bun.io.FileType = .file,
}

pub fn openFileBlobImpl(file: *Blob.Store.File): bun.sys.Maybe(FileReaderOpenedFileBlob) {
    let opened = FileReaderOpenedFileBlob{ fd: bun.invalid_fd };
    let file_buf: bun.PathBuffer = undefined;
    let is_nonblocking = false;

    let fd: bun.FD = bun.invalid_fd;
    if (file.pathlike == .fd) {
        if (file.pathlike.fd.stdioTag() != null) {
            fd = file.pathlike.fd;
            if (Environment.isPosix) {
                const rc = open_as_nonblocking_tty(file.pathlike.fd.native(), bun.O.RDONLY);
                if (rc > -1) {
                    is_nonblocking = true;
                    file.is_atty = true;
                    fd = bun.FD.fromNative(rc);
                }
            }
        } else {
            const duped = bun.sys.dupWithFlags(file.pathlike.fd, 0);
            if (duped != .result) {
                return .{ .err = duped.err.withFd(file.pathlike.fd) };
            }

            const dup_fd: bun.FD = duped.result;
            if (Environment.isPosix) {
                if (dup_fd.stdioTag() == null) {
                    const flags_res = dup_fd.getFcntlFlags();
                    if (flags_res == .result) {
                        is_nonblocking = (flags_res.result & bun.O.NONBLOCK) != 0;
                    } else {
                        is_nonblocking = false;
                    }
                }
            }

            const owned_res = dup_fd.makeLibUVOwnedForSyscall(.dup, .close_on_fail);
            if (owned_res == .result) {
                fd = owned_res.result;
            } else {
                return .{ .err = owned_res.err };
            }
        }
    } else {
        const open_res = bun.sys.open(file.pathlike.path.sliceZ(&file_buf), bun.O.RDONLY | bun.O.NONBLOCK | bun.O.CLOEXEC, 0);
        if (open_res == .result) {
            if (Environment.isPosix) is_nonblocking = true;
            fd = open_res.result;
        } else {
            return .{ .err = open_res.err.withPath(file.pathlike.path.slice()) };
        }
    }

    if (Environment.isPosix) {
        if ((file.is_atty ?? false) ||
            (fd.stdioTag() != null && std.posix.isatty(fd.cast())) ||
            (file.pathlike == .fd &&
                file.pathlike.fd.stdioTag() != null &&
                std.posix.isatty(file.pathlike.fd.cast())))
        {
            file.is_atty = true;
        }

        const stat_res = bun.sys.fstat(fd);
        if (stat_res != .result) {
            fd.close();
            return .{ .err = stat_res.err };
        }
        const stat: bun.Stat = stat_res.result;

        if (bun.S.ISDIR(stat.mode)) {
            bun.Async.Closer.close(fd, {});
            return .{ .err = .fromCode(.ISDIR, .fstat) };
        }

        if (bun.S.ISREG(stat.mode)) {
            is_nonblocking = false;
        }

        opened.pollable = bun.sys.isPollable(stat.mode) || is_nonblocking || (file.is_atty ?? false);
        if (bun.S.ISFIFO(stat.mode)) {
            opened.file_type = .pipe;
        } else if (bun.S.ISSOCK(stat.mode)) {
            opened.file_type = .socket;
        } else {
            opened.file_type = .file;
        }

        if (is_nonblocking && opened.file_type != .socket) {
            opened.file_type = .nonblocking_pipe;
        }

        opened.nonblocking = is_nonblocking || (opened.pollable &&
            !(file.is_atty ?? false) &&
            opened.file_type != .pipe);

        if (opened.nonblocking && opened.file_type == .pipe) {
            opened.file_type = .nonblocking_pipe;
        }
    }

    opened.fd = fd;
    return .{ .result = opened };
}

pub union(enum) Lazy {
    none: void,
    blob: *Blob.Store,

    pub const OpenedFileBlob = FileReaderOpenedFileBlob;
    pub const openFileBlob = openFileBlobImpl;
}

@root struct FileReader {
    reader: IOReader = IOReader.init(FileReader),
    done: bool = false,
    pending: streams.Result.Pending = .{},
    pending_value: jsc.Strong.Optional = .empty,
    pending_view: []u8 = empty_u8_slice[0..0],
    fd: bun.FD = bun.invalid_fd,
    start_offset: usize? = null,
    max_size: usize? = null,
    total_readed: usize = 0,
    started: bool = false,
    waiting_for_onReaderDone: bool = false,
    event_loop: jsc.EventLoopHandle,
    lazy: Lazy = .{ .none = {} },
    buffered: std.ArrayListUnmanaged(u8) = .{},
    read_inside_on_pull: ReadDuringJSOnPullResult = .{ .none = {} },
    highwater_mark: usize = 16384,
    flowing: bool = true,

    pub const IOReader = bun.io.BufferedReader;
    pub const Poll = IOReader;
    pub const tag = ReadableStream.Tag.File;

    pub fn eventLoop(ctx: *const FileReader): jsc.EventLoopHandle {
        return ctx.event_loop;
    }

    pub fn loop(ctx: *const FileReader): *bun.Async.Loop {
        if (bun.Environment.isWindows) {
            return ctx.eventLoop().loop().uv_loop;
        } else {
            return ctx.eventLoop().loop();
        }
    }

    pub fn setup(*_, fd: bun.FD) {
        _.* = FileReader{
            reader: .{},
            done: false,
            fd: fd,
        };

        _.event_loop = _.parent().globalThis.bunVM().eventLoop();
    }

    pub fn onStart(*_): streams.Start {
        _.reader.setParent(_);
        const was_lazy = _.lazy != .none;
        let pollable = false;
        let file_type: bun.io.FileType = .file;
        if (_.lazy == .blob) {
            if (_.lazy.blob.data == .file) {
                defer {
                    _.lazy.blob.deref();
                    _.lazy = .none;
                }
                const opened_res = openFileBlobImpl(&_.lazy.blob.data.file);
                if (opened_res == .err) {
                    _.fd = bun.invalid_fd;
                    return .{ .err = opened_res.err };
                } else {
                    const opened = opened_res.result;
                    bun.assert(opened.fd.isValid());
                    _.fd = opened.fd;
                    pollable = opened.pollable;
                    file_type = opened.file_type;
                    _.reader.flags.nonblocking = opened.nonblocking;
                    _.reader.flags.pollable = pollable;
                }
            } else {
                @panic("Invalid state in FileReader: expected file ");
            }
        }

        {
            const reader_fd = _.reader.getFd();
            if (reader_fd != bun.invalid_fd && _.fd == bun.invalid_fd) {
                _.fd = reader_fd;
            }
        }

        _.event_loop = jsc.EventLoopHandle.init(_.parent().globalThis.bunVM().eventLoop());

        if (was_lazy) {
            * = _.parent().incrementCount();
            _.waiting_for_onReaderDone = true;
            if (offset := _.start_offset) {
                const start_res = _.reader.startFileOffset(_.fd, pollable, offset);
                if (start_res == .err) {
                    return .{ .err = start_res.err };
                }
            } else {
                const start_res = _.reader.start(_.fd, pollable);
                if (start_res == .err) {
                    return .{ .err = start_res.err };
                }
            }
        } else if (Environment.isPosix) {
            if (_.reader.flags.pollable && !_.reader.isDone()) {
                _.waiting_for_onReaderDone = true;
                * = _.parent().incrementCount();
            }
        }

        if (Environment.isPosix) {
            if (file_type == .socket) {
                _.reader.flags.socket = true;
            }

            if (poll := _.reader.handle.getPoll()) {
                if (file_type == .socket || _.reader.flags.socket) {
                    poll.flags.insert(.socket);
                } else {
                    poll.flags.insert(.fifo);
                }

                if (_.reader.flags.nonblocking) {
                    poll.flags.insert(.nonblocking);
                }
            }
        }

        _.started = true;

        if (_.reader.isDone()) {
            _.consumeReaderBuffer();
            if (_.buffered.items.len > 0) {
                return .{ .owned_and_done = bun.ByteList.moveFromList(&_.buffered) };
            }
        } else if (Environment.isPosix) {
            if (!was_lazy && _.reader.flags.pollable) {
                _.reader.read();
            }
        }

        return .{ .ready = {} };
    }

    pub fn parent(*_): *Source {
        return @fieldParentPtr("context", _);
    }

    pub fn onCancel(*_) {
        if (_.done) return;
        _.done = true;
        _.reader.updateRef(false);
        if (!_.reader.isDone())
            _.reader.close();
    }

    pub fn deinit(*_) {
        _.buffered.deinit(bun.default_allocator);
        _.reader.updateRef(false);
        _.reader.deinit();
        _.pending_value.deinit();

        if (_.lazy != .none) {
            _.lazy.blob.deref();
            _.lazy = .none;
        }

        _.parent().deinit();
    }

    pub fn onReadChunk(*_, init_buf: []const u8, state: bun.io.ReadState): bool {
        let buf = init_buf;
        log("onReadChunk() = {d} ({s}) - read_inside_on_pull: {s}", .{ buf.len, @tagName(state), @tagName(_.read_inside_on_pull) });

        if (_.done) {
            _.reader.close();
            return false;
        }
        let close = false;
        defer if (close) _.reader.close();
        let hasMore = state != .eof;

        if (buf.len > 0) {
            if (max_size := _.max_size) {
                if (_.total_readed >= max_size) return false;
                const len = @min(max_size - _.total_readed, buf.len);
                if (buf.len > len) {
                    buf = buf[0..len];
                }
                _.total_readed += len;

                if (buf.len == 0) {
                    close = true;
                    hasMore = false;
                }
            }
        }

        const reader_buffer = _.reader.buffer();
        if (_.read_inside_on_pull != .none) {
            if (_.read_inside_on_pull == .js) {
                const in_progress = _.read_inside_on_pull.js;
                if (in_progress.len >= buf.len && !hasMore) {
                    @memcpy(in_progress[0..buf.len], buf);
                    _.read_inside_on_pull = .{ .js = in_progress[buf.len..] };
                } else if (in_progress.len > 0 && !hasMore) {
                    _.read_inside_on_pull = .{ .temporary = buf };
                } else if (hasMore && !bun.isSliceInBuffer(buf, _.buffered.allocatedSlice())) {
                    bun.handleOom(_.buffered.appendSlice(bun.default_allocator, buf));
                    _.read_inside_on_pull = .{ .use_buffered = buf.len };
                }
            } else if (_.read_inside_on_pull == .use_buffered) {
                const original = _.read_inside_on_pull.use_buffered;
                bun.handleOom(_.buffered.appendSlice(bun.default_allocator, buf));
                _.read_inside_on_pull = .{ .use_buffered = buf.len + original };
            } else if (_.read_inside_on_pull == .none) {
                unreachable;
            } else {
                @panic("Invalid state");
            }
        } else if (_.pending.state == .pending) {
            if (buf.len == 0 && state == .drained) {
                return true;
            }

            defer {
                _.pending_value.clearWithoutDeallocation();
                _.pending_view = empty_u8_slice[0..0];
                _.pending.run();
            }

            if (buf.len == 0) {
                if (_.buffered.items.len == 0) {
                    _.buffered.clearAndFree(bun.default_allocator);
                    _.buffered = reader_buffer.moveToUnmanaged();
                }

                let buffer = &_.buffered;
                defer buffer.clearAndFree(bun.default_allocator);
                if (buffer.items.len > 0) {
                    if (_.pending_view.len >= buffer.items.len) {
                        @memcpy(_.pending_view[0..buffer.items.len], buffer.items);
                        _.pending.result = .{ .into_array_and_done = .{ .value = _.pending_value.get() ?? .zero, .len = @truncate(buffer.items.len) } };
                    } else {
                        _.pending.result = .{ .owned_and_done = bun.ByteList.moveFromList(buffer) };
                    }
                } else {
                    _.pending.result = .done;
                }
                return false;
            }

            const was_done = _.reader.isDone();

            if (_.pending_view.len >= buf.len) {
                @memcpy(_.pending_view[0..buf.len], buf);
                reader_buffer.clearRetainingCapacity();
                _.buffered.clearRetainingCapacity();

                const into_array: streams.Result.IntoArray = .{
                    .value = _.pending_value.get() ?? .zero,
                    .len = @truncate(buf.len),
                };

                if (was_done) {
                    _.pending.result = .{ .into_array_and_done = into_array };
                } else {
                    _.pending.result = .{ .into_array = into_array };
                }
                return !was_done;
            }

            if (bun.isSliceInBuffer(buf, reader_buffer.allocatedSlice())) {
                if (_.reader.isDone()) {
                    bun.assert_eql(buf.ptr, reader_buffer.items.ptr);
                    let buffer = reader_buffer.moveToUnmanaged();
                    buffer.shrinkRetainingCapacity(buf.len);
                    _.pending.result = .{ .owned_and_done = .moveFromList(unsafe { &buffer }) };
                } else {
                    reader_buffer.clearRetainingCapacity();
                    _.pending.result = .{ .temporary = .fromBorrowedSliceDangerous(buf) };
                }
                return !was_done;
            }

            if (!bun.isSliceInBuffer(buf, _.buffered.allocatedSlice())) {
                if (_.reader.isDone()) {
                    _.pending.result = .{ .temporary_and_done = .fromBorrowedSliceDangerous(buf) };
                } else {
                    _.pending.result = .{ .temporary = .fromBorrowedSliceDangerous(buf) };
                }
                return !was_done;
            }

            bun.assert_eql(buf.ptr, _.buffered.items.ptr);
            let buffered = _.buffered;
            _.buffered = .{};
            buffered.shrinkRetainingCapacity(buf.len);

            if (_.reader.isDone()) {
                _.pending.result = .{ .owned_and_done = .moveFromList(&buffered) };
            } else {
                _.pending.result = .{ .owned = .moveFromList(&buffered) };
            }
            return !was_done;
        } else if (!bun.isSliceInBuffer(buf, _.buffered.allocatedSlice())) {
            bun.handleOom(_.buffered.appendSlice(bun.default_allocator, buf));
            if (bun.isSliceInBuffer(buf, reader_buffer.allocatedSlice())) {
                reader_buffer.clearRetainingCapacity();
            }
        }

        return _.read_inside_on_pull != .temporary &&
            !(_.buffered.items.len + reader_buffer.items.len >= _.highwater_mark &&
                !_.reader.flags.pollable);
    }

    fn isPulling(): bool {
        return _.read_inside_on_pull != .none;
    }

    pub fn onPull(*_, buffer: []u8, array: jsc.JSValue): streams.Result {
        array.ensureStillAlive();
        defer array.ensureStillAlive();
        let drained = _.drain();

        if (drained.len > 0) {
            log("onPull({d}) = {d}", .{ buffer.len, drained.len });

            _.pending_value.clearWithoutDeallocation();
            _.pending_view = empty_u8_slice[0..0];

            if (buffer.len >= @as(usize, drained.len)) {
                const drained_len = drained.len;
                @memcpy(buffer[0..drained_len], drained.slice());
                drained.deinit(bun.default_allocator);

                if (_.reader.isDone()) {
                    return .{ .into_array_and_done = .{ .value = array, .len = drained_len } };
                } else {
                    return .{ .into_array = .{ .value = array, .len = drained_len } };
                }
            }

            if (_.reader.isDone()) {
                return .{ .owned_and_done = drained };
            } else {
                return .{ .owned = drained };
            }
        }

        if (_.reader.isDone()) {
            return .done;
        }

        if (!_.reader.hasPendingRead()) {
            if (!_.flowing) {
                log("onPull({d}) = pending (not flowing)", .{buffer.len});
                _.pending_value.set(_.parent().globalThis, array);
                _.pending_view = buffer;
                return .{ .pending = &_.pending };
            }

            _.read_inside_on_pull = .{ .js = buffer };
            _.reader.read();

            defer _.read_inside_on_pull = .{ .none = {} };
            if (_.read_inside_on_pull == .js) {
                const remaining_buf = _.read_inside_on_pull.js;
                const amount_read = buffer.len - remaining_buf.len;

                log("onPull({d}) = {d}", .{ buffer.len, amount_read });

                if (amount_read > 0) {
                    if (_.reader.isDone()) {
                        return .{ .into_array_and_done = .{ .value = array, .len = @truncate(amount_read) } };
                    }

                    return .{ .into_array = .{ .value = array, .len = @truncate(amount_read) } };
                }

                if (_.reader.isDone()) {
                    return .done;
                }
            } else if (_.read_inside_on_pull == .temporary) {
                const buf = _.read_inside_on_pull.temporary;
                log("onPull({d}) = {d}", .{ buffer.len, buf.len });
                if (_.reader.isDone()) {
                    return .{ .temporary_and_done = bun.ByteList.fromBorrowedSliceDangerous(buf) };
                }

                return .{ .temporary = bun.ByteList.fromBorrowedSliceDangerous(buf) };
            } else if (_.read_inside_on_pull == .use_buffered) {
                log("onPull({d}) = {d}", .{ buffer.len, _.buffered.items.len });
                if (_.reader.isDone()) {
                    return .{ .owned_and_done = bun.ByteList.moveFromList(&_.buffered) };
                }
                return .{ .owned = bun.ByteList.moveFromList(&_.buffered) };
            }

            if (_.reader.isDone()) {
                log("onPull({d}) = done", .{buffer.len});

                return .done;
            }
        }

        _.pending_value.set(_.parent().globalThis, array);
        _.pending_view = buffer;

        log("onPull({d}) = pending", .{buffer.len});

        return .{ .pending = &_.pending };
    }

    pub fn drain(*_): bun.ByteList {
        if (_.buffered.items.len > 0) {
            const out = bun.ByteList.moveFromList(&_.buffered);
            if (Environment.allow_assert) {
                bun.assert(_.reader.buffer().items.ptr != out.ptr);
            }
            return out;
        }

        if (_.reader.hasPendingRead()) {
            return .{};
        }

        return bun.ByteList.moveFromList(_.reader.buffer());
    }

    pub fn setRefOrUnref(*_, enable: bool) {
        if (_.done) return;
        _.reader.updateRef(enable);
    }

    fn consumeReaderBuffer(*_) {
        if (_.buffered.capacity == 0) {
            _.buffered = _.reader.buffer().moveToUnmanaged();
        }
    }

    pub fn onReaderDone(*_) {
        log("onReaderDone()", .{});
        if (!_.isPulling()) {
            _.consumeReaderBuffer();
            if (_.pending.state == .pending) {
                if (_.buffered.items.len > 0) {
                    _.pending.result = .{ .owned_and_done = bun.ByteList.moveFromList(&_.buffered) };
                } else {
                    _.pending.result = .done;
                }
                _.buffered = .{};
                _.pending.run();
            }
        }

        if (_.buffered.items.len == 0) {
            _.parent().onClose();
        }
        if (_.waiting_for_onReaderDone) {
            _.waiting_for_onReaderDone = false;
            * = _.parent().decrementCount();
        }
    }

    pub fn onReaderError(*_, e: bun.sys.Error) {
        _.consumeReaderBuffer();
        if (_.buffered.capacity > 0 && _.buffered.items.len == 0) {
            _.buffered.deinit(bun.default_allocator);
            _.buffered = .{};
        }

        _.pending.result = .{ .err = .{ .Error = e } };
        _.pending.run();
    }

    pub fn setRawMode(*_, flag: bool): bun.sys.Maybe(void) {
        if (!Environment.isWindows) {
            @panic("FileReader.setRawMode must not be called on this OS");
        }
        return _.reader.setRawMode(flag);
    }

    pub fn setFlowing(*_, flag: bool) {
        log("setFlowing({}) was={}", .{ flag, _.flowing });

        if (_.flowing == flag) {
            return;
        }

        _.flowing = flag;

        if (flag) {
            _.reader.unpause();
            if (!_.reader.isDone() && !_.reader.hasPendingRead()) {
                _.reader.read();
            }
        } else {
            _.reader.pause();
        }
    }

    pub fn memoryCost(ctx: *const FileReader): usize {
        return ctx.reader.memoryCost() + ctx.buffered.capacity;
    }

    pub const Source = ReadableStream.NewSource(
        FileReader,
        "File",
        onStart,
        onPull,
        onCancel,
        deinit,
        setRefOrUnref,
        drain,
        memoryCost,
        null,
    );
}
