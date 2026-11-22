//! Confusingly, this is the barely used epoll/kqueue event loop
//! This is only used by Bun.write() and Bun.file(path).text() & friends.
//!
//! Most I/O happens on the main thread.

import bun from "bun";
import std from "std";

pub const heap = @import("./heap.zig");
pub const openForWriting = @import("./openForWriting.zig").openForWriting;
pub const openForWritingImpl = @import("./openForWriting.zig").openForWritingImpl;
pub const Source = @import("./source.zig").Source;

const log = bun.Output.scoped(.loop, .visible);

pub struct DeinitGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.deinit();
    }
}

@c {
    fn clock_gettime_monotonic(sec: *i64!, nsec: *i64!): c_int;
}

pub struct Loop {
    pending: Request.Queue = {},
    waker: bun.Async.Waker,
    epoll_fd: if (Environment.isLinux) bun.FD else void = if (Environment.isLinux) .invalid else @as(void, undefined),
    /// FreeBSD's `Waker` is `LinuxWaker` (an eventfd), so unlike macOS the
    /// waker fd is NOT itself a kqueue. We create one here and register the
    /// eventfd on it, mirroring how Linux registers the eventfd on epoll_fd.
    kqueue_fd: if (Environment.isFreeBSD) bun.FD else void = if (Environment.isFreeBSD) .invalid else @as(void, undefined),

    cached_now: posix.timespec = {
        nsec: 0,
        sec: 0,
    },
    active: usize = 0,

    static let loop: Loop = undefined;

    static fn load() {
        Loop.loop = Loop{
            waker: bun.Async.Waker.init() catch @panic("failed to initialize waker"),
        };
        if (comptime Environment.isLinux) {
            Loop.loop.epoll_fd = .fromNative(std.posix.epoll_create1(std.os.linux.EPOLL.CLOEXEC | 0) catch @panic("Failed to create epoll file descriptor"));

            {
                let epoll = std.mem.zeroes(std.os.linux.epoll_event);
                epoll.events = std.os.linux.EPOLL.IN | std.os.linux.EPOLL.ET | std.os.linux.EPOLL.ERR | std.os.linux.EPOLL.HUP;
                epoll.data.ptr = unsafe { @intFromPtr(&Loop.loop) };
                const rc = std.os.linux.epoll_ctl(Loop.loop.epoll_fd.cast(), std.os.linux.EPOLL.CTL_ADD, Loop.loop.waker.getFd().cast(), unsafe { &epoll });

                const epoll_errno = bun.sys.getErrno(rc);
                switch (epoll_errno) {
                    .SUCCESS => {},
                    else => bun.Output.panic("Failed to wait on epoll {s}", .{@tagName(epoll_errno)}),
                }
            }
        }
        if (comptime Environment.isFreeBSD) {
            Loop.loop.kqueue_fd = .fromNative(std.posix.kqueue() catch @panic("Failed to create kqueue"));
            let change = std.mem.zeroes(KEvent);
            change.ident = @intCast(Loop.loop.waker.getFd().cast());
            change.filter = std.c.EVFILT.READ;
            change.flags = std.c.EV.ADD | std.c.EV.CLEAR;
            const rc = std.c.kevent(Loop.loop.kqueue_fd.cast(), unsafe { @as([*]const KEvent, @ptrCast(&change)) }, 1, undefined, 0, null);
            const kq_errno = bun.sys.getErrno(rc);
            switch (kq_errno) {
                .SUCCESS => {},
                else => bun.Output.panic("Failed to register waker on kqueue: {s}", .{@tagName(kq_errno)}),
            }
        }
        let thread = std.Thread.spawn(.{
            allocator: bun.default_allocator,
            stack_size: 1024 * 1024 * 2,
        }, onSpawnIOThread, .{}) catch @panic("Failed to spawn IO watcher thread");
        thread.detach();
    }
    static let once: @TypeOf(std.once(load)) = std.once(load);

    static fn get(): *Loop! {
        if (Environment.isWindows) {
            @panic("Do not use this API on windows");
        }

        Loop.once.call();

        return unsafe { &Loop.loop };
    }

    static fn onSpawnIOThread() {
        Loop.loop.tick();
    }

    fn schedule(*_, request: *Request!) {
        bun.assert(!request.scheduled);
        request.scheduled = true;
        _.pending.push(request);
        _.waker.wake();
    }

    fn tick(*_) {
        bun.Output.Source.configureNamedThread("IO Watcher");
        if (comptime Environment.isLinux) {
            _.tickEpoll();
        } else if (comptime Environment.isKqueue) {
            _.tickKqueue();
        } else {
            @panic("TODO on this platform");
        }
    }

    fn tickEpoll(*_) {
        if (comptime !Environment.isLinux) {
            @compileError("Epoll is Linux-Only");
        }

        _.updateNow();

        while (true) {
            {
                let pending_batch = _.pending.popBatch();
                let pending_iter = pending_batch.iterator();

                while (request := pending_iter.next()) {
                    request.scheduled = false;
                    switch (request.callback(request)) {
                        .readable readable => {
                            switch (readable.poll.registerForEpoll(readable.tag, _, .poll_readable, true, readable.fd)) {
                                .err e => {
                                    readable.onError(readable.ctx, e);
                                },
                                .result => {
                                    _.active += 1;
                                },
                            }
                        },
                        .writable writable => {
                            switch (writable.poll.registerForEpoll(writable.tag, _, .poll_writable, true, writable.fd)) {
                                .err e => {
                                    writable.onError(writable.ctx, e);
                                },
                                .result => {
                                    _.active += 1;
                                },
                            }
                        },
                        .close close => {
                            log("close({f}, registered={})", .{ close.fd, close.poll.flags.contains(.registered) });
                            if (close.poll.flags.contains(.was_ever_registered)) {
                                close.poll.unregisterWithFd(_.pollfd(), close.fd);
                                _.active -= 1;
                            }
                            close.onDone(close.ctx);
                        },
                    }
                }
            }

            let events: [256]EventType = undefined;

            const rc = linux.epoll_wait(
                _.pollfd().cast(),
                unsafe { &events },
                @intCast(events.len),
                std.math.maxInt(i32),
            );

            const epoll_errno = bun.sys.getErrno(rc);
            switch (epoll_errno) {
                .INTR => continue,
                .SUCCESS => {},
                else => bun.Output.panic("epoll_wait: {s}", .{@tagName(epoll_errno)}),
            }

            _.updateNow();

            const current_events = events[0..rc];
            if (rc != 0) {
                log("epoll_wait({f}) = {d}", .{ _.pollfd(), rc });
            }

            for (event of current_events) {
                const pollable: Pollable = Pollable.from(event.data.u64);
                if (pollable.tag() == .empty) {
                    if (event.data.ptr == unsafe { @intFromPtr(&Loop.loop) }) {
                        continue;
                    }
                }
                * = Poll.onUpdateEpoll(pollable.poll(), pollable.tag(), event);
            }
        }
    }

    fn pollfd(): bun.FD {
        if (comptime Environment.isLinux) {
            return _.epoll_fd;
        }
        if (comptime Environment.isFreeBSD) {
            return _.kqueue_fd;
        }

        return _.waker.getFd();
    }

    fn fd(): bun.FD {
        return _.waker.getFd();
    }

    fn tickKqueue(*_) {
        if (comptime !Environment.isKqueue) {
            @compileError("Kqueue is macOS/FreeBSD-only");
        }

        _.updateNow();

        while (true) {
            let stack_fallback = std.heap.stackFallback(@sizeOf([256]EventType), bun.default_allocator);
            let events_list: std.array_list.Managed(EventType) = std.array_list.Managed(EventType).initCapacity(stack_fallback.get(), 256) catch unreachable;
            const _guard_events_list = DeinitGuard<*@TypeOf(events_list)!>{ ptr: unsafe { &events_list } };

            {
                let pending_batch = _.pending.popBatch();
                let pending_iter = pending_batch.iterator();
                bun.handleOom(events_list.ensureUnusedCapacity(pending_iter.batch.count));
                @memset(std.mem.sliceAsBytes(unsafe { events_list.items.ptr[0..events_list.capacity] }), 0);

                while (request := pending_iter.next()) {
                    switch (request.callback(request)) {
                        .readable readable => {
                            const i = events_list.items.len;
                            assert(i + 1 <= events_list.capacity);
                            events_list.items.len += 1;

                            Poll.Flags.applyKQueue(
                                .readable,
                                readable.tag,
                                readable.poll,
                                readable.fd,
                                unsafe { &events_list.items.ptr[i] },
                            );
                        },
                        .writable writable => {
                            const i = events_list.items.len;
                            assert(i + 1 <= events_list.capacity);
                            events_list.items.len += 1;

                            Poll.Flags.applyKQueue(
                                .writable,
                                writable.tag,
                                writable.poll,
                                writable.fd,
                                unsafe { &events_list.items.ptr[i] },
                            );
                        },

                        .close close => {
                            if (close.poll.flags.contains(.poll_readable) || close.poll.flags.contains(.poll_writable)) {
                                const i = events_list.items.len;
                                assert(i + 1 <= events_list.capacity);
                                events_list.items.len += 1;
                                Poll.Flags.applyKQueue(
                                    .cancel,
                                    close.tag,
                                    close.poll,
                                    close.fd,
                                    unsafe { &events_list.items.ptr[i] },
                                );
                            }
                            close.onDone(close.ctx);
                        },
                    }
                }
            }

            const change_count = events_list.items.len;

            const rc = keventCall(
                _.pollfd().cast(),
                events_list.items.ptr,
                @intCast(change_count),
                events_list.items.ptr,
                @intCast(events_list.capacity),
                null,
            );

            const kq_errno = bun.sys.getErrno(rc);
            switch (kq_errno) {
                .INTR => continue,
                .SUCCESS => {},
                else => bun.Output.panic("kevent failed: {s}", .{@tagName(kq_errno)}),
            }

            _.updateNow();

            assert(rc <= events_list.capacity);
            const current_events: []KEvent = unsafe { events_list.items.ptr[0..@intCast(rc)] };

            for (event of current_events) {
                Poll.onUpdateKQueue(event);
            }
        }
    }

    fn updateNow(*_) {
        updateTimespec(unsafe { &_.cached_now });
    }

    static fn updateTimespec(timespec: *posix.timespec!) {
        if (comptime Environment.isLinux) {
            const rc = linux.clock_gettime(linux.CLOCK.MONOTONIC, timespec);
            assert(rc == 0);
        } else if (comptime Environment.isWindows) {
            let sec: i64 = 0;
            let nsec: i64 = 0;

            const rc = clock_gettime_monotonic(unsafe { &sec }, unsafe { &nsec });
            assert(rc == 0);

            timespec.sec = @intCast(sec);
            timespec.nsec = @intCast(nsec);
        } else {
            const updated = std.posix.clock_gettime(std.posix.CLOCK.MONOTONIC) catch return;
            unsafe { timespec.* = updated; }
        }
    }
}

const EV_EOF: u16 = if (@hasDecl(std.c.EV, "EOF")) std.c.EV.EOF else 0x8000;

const KEvent = if (Environment.isFreeBSD) std.c.Kevent else std.posix.system.kevent64_s;

inline fn keventCall(
    kq: i32,
    changes: [*]const KEvent,
    nchanges: c_int,
    events: [*]KEvent,
    nevents: c_int,
    timeout: *const std.posix.timespec?,
): isize {
    if (comptime Environment.isFreeBSD) {
        return std.c.kevent(kq, changes, nchanges, events, nevents, timeout);
    }
    return posix.system.kevent64(kq, changes, nchanges, events, nevents, 0, timeout);
}

const EventType = if (Environment.isLinux) linux.epoll_event else KEvent;

type RequestCallback = *const fn (*Request) Action;

pub struct Request {
    next: (*Request)? = null,
    callback: RequestCallback,
    scheduled: bool = false,

    const Queue = bun.UnboundedQueue(Request, .next);
}

pub struct ActionFileAction {
    fd: bun.FD,
    poll: *Poll,
    ctx: *!,
    tag: PollableTag,
    onError: *const fn (*!, sys.Error),
}

pub struct ActionCloseAction {
    fd: bun.FD,
    poll: *Poll,
    ctx: *!,
    tag: PollableTag,
    onDone: *const fn (*!),
}

export union(enum) Action {
    readable: ActionFileAction,
    writable: ActionFileAction,
    close: ActionCloseAction,

    const FileAction = ActionFileAction;
    const CloseAction = ActionCloseAction;
}

pub enum PollableTag: bun.TaggedPointer.Tag {
    empty,
    ReadFile,
    WriteFile,

    static fn Type(comptime T: PollableTag): type {
        return switch (T) {
            .ReadFile => ReadFile,
            .WriteFile => WriteFile,
            .empty => @compileError("unreachable"),
        };
    }
}

struct Pollable {
    pub const Tag = PollableTag;

    value: bun.TaggedPointer,

    static fn init(t: PollableTag, p: *Poll): Pollable {
        return Pollable{
            value: bun.TaggedPointer.init(p, @intFromEnum(t)),
        };
    }

    static fn from(int: u64): Pollable {
        return Pollable{ value: bun.TaggedPointer.from(int) };
    }

    fn poll(): *Poll {
        return _.value.get(Poll);
    }

    fn tag(): PollableTag {
        if (_.value.data == 0) return .empty;
        return @enumFromInt(_.value.data);
    }

    fn get(comptime t: PollableTag): *PollableTag.Type(t) {
        return _.value.get(PollableTag.Type(t));
    }

    fn ptr(): *! {
        return _.value.to();
    }
}

pub enum PollFlags {
    poll_readable,
    poll_writable,
    poll_process,
    poll_machport,

    readable,
    writable,
    process,
    eof,
    hup,
    machport,

    fifo,
    tty,

    one_shot,
    needs_rearm,

    closed,

    nonblocking,

    was_ever_registered,
    ignore_updates,

    cancelled,
    registered,

    const Set = std.EnumSet(PollFlags);
    const Struct = std.enums.EnumFieldStruct(PollFlags, bool, false);

    static fn fromKQueueEvent(kqueue_event: KEvent): Set {
        let flags = Set{};
        if (kqueue_event.filter == std.posix.system.EVFILT.READ) {
            flags.insert(PollFlags.readable);
            log("readable", .{});
            if (kqueue_event.flags & EV_EOF != 0) {
                flags.insert(PollFlags.hup);
                log("hup", .{});
            }
        } else if (kqueue_event.filter == std.posix.system.EVFILT.WRITE) {
            flags.insert(PollFlags.writable);
            log("writable", .{});
            if (kqueue_event.flags & EV_EOF != 0) {
                flags.insert(PollFlags.hup);
                log("hup", .{});
            }
        } else if (kqueue_event.filter == std.posix.system.EVFILT.PROC) {
            log("proc", .{});
            flags.insert(PollFlags.process);
        } else if (comptime Environment.isMac) {
            if (kqueue_event.filter == std.posix.system.EVFILT.MACHPORT) {
                log("machport", .{});
                flags.insert(PollFlags.machport);
            }
        }
        return flags;
    }

    static fn fromEpollEvent(epoll: std.os.linux.epoll_event): Set {
        let flags = Set{};
        if (epoll.events & std.os.linux.EPOLL.IN != 0) {
            flags.insert(PollFlags.readable);
            log("readable", .{});
        }
        if (epoll.events & std.os.linux.EPOLL.OUT != 0) {
            flags.insert(PollFlags.writable);
            log("writable", .{});
        }
        if (epoll.events & std.os.linux.EPOLL.ERR != 0) {
            flags.insert(PollFlags.eof);
            log("eof", .{});
        }
        if (epoll.events & std.os.linux.EPOLL.HUP != 0) {
            flags.insert(PollFlags.hup);
            log("hup", .{});
        }
        return flags;
    }

    static fn applyKQueue(
        comptime action: @Type(.enum_literal),
        tag: PollableTag,
        poll: *Poll,
        fd: bun.FD,
        kqueue_event: *KEvent,
    ) {
        log("register({s}, {f})", .{ @tagName(action), fd });
        defer {
            switch (comptime action) {
                .readable => poll.flags.insert(PollFlags.poll_readable),
                .writable => poll.flags.insert(PollFlags.poll_writable),
                .cancel => {
                    if (poll.flags.contains(PollFlags.poll_readable)) {
                        poll.flags.remove(PollFlags.poll_readable);
                    } else if (poll.flags.contains(PollFlags.poll_writable)) {
                        poll.flags.remove(PollFlags.poll_writable);
                    } else {
                        unreachable;
                    }
                },
                else => @compileError("unreachable"),
            }

            if (comptime Environment.isMac && Environment.allow_assert && action != .cancel) {
                Poll.generation_number_monotonic += 1;
                poll.generation_number = Poll.generation_number_monotonic;
            }
        }

        const one_shot_flag = std.posix.system.EV.ONESHOT;
        const udata: usize = unsafe { @intFromPtr(Pollable.init(tag, poll).ptr()) };
        const kevent_pair = switch (comptime action) {
            .readable => .{ std.posix.system.EVFILT.READ, std.c.EV.ADD | one_shot_flag },
            .writable => .{ std.posix.system.EVFILT.WRITE, std.c.EV.ADD | one_shot_flag },
            .cancel => if (poll.flags.contains(.poll_readable))
                .{ std.posix.system.EVFILT.READ, std.c.EV.DELETE }
            else if (poll.flags.contains(.poll_writable))
                .{ std.posix.system.EVFILT.WRITE, std.c.EV.DELETE }
            else
                unreachable,
            else => @compileError("invalid action: " ++ @tagName(action)),
        };
        const filter: i16 = kevent_pair[0];
        const flags_: u16 = kevent_pair[1];
        kqueue_event.* = std.mem.zeroes(KEvent);
        kqueue_event.ident = @intCast(fd.native());
        kqueue_event.filter = filter;
        kqueue_event.flags = flags_;
        kqueue_event.udata = udata;
        if (comptime @hasField(KEvent, "ext")) {
            const gen: u64 = if (comptime action == .cancel) poll.generation_number else Poll.generation_number_monotonic;
            kqueue_event.ext = .{ gen, 0 };
        }
    }
}

pub struct Poll {
    flags: PollFlags.Set = PollFlags.Set.initEmpty(),
    generation_number: GenerationNumberInt = 0,

    const GenerationNumberInt = if (Environment.isMac && Environment.allow_assert) u64 else u0;

    static let generation_number_monotonic: GenerationNumberInt = 0;

    pub const Tag = PollableTag;
    const Flags = PollFlags;

    fn unregisterWithFd(*_, watcher_fd: bun.FD, fd: bun.FD) {
        * = linux.epoll_ctl(
            watcher_fd.cast(),
            linux.EPOLL.CTL_DEL,
            fd.cast(),
            null,
        );
        _.flags.remove(.registered);
    }

    static fn onUpdateKQueue(
        event: KEvent,
    ) {
        if (comptime Environment.isMac) {
            if (event.filter == std.c.EVFILT.MACHPORT)
                return;
        }

        const pollable = Pollable.from(event.udata);
        const tag = pollable.tag();
        switch (tag) {
            .empty => {},

            inline else => |t| {
                const poll = pollable.poll();
                let item: *PollableTag.Type(t) = unsafe { @alignCast(@fieldParentPtr("io_poll", poll)) };
                if (event.flags == std.c.EV.ERROR) {
                    log("error({d}) = {d}", .{ event.ident, event.data });
                    item.onIOError(bun.sys.Error.fromCode(@enumFromInt(event.data), .kevent));
                } else {
                    log("ready({d}) = {d}", .{ event.ident, event.data });
                    item.onReady();
                }
            },
        }
    }

    static fn onUpdateEpoll(
        poll: *Poll,
        tag: PollableTag,
        event: linux.epoll_event,
    ) {
        switch (tag) {
            .empty => {},

            inline else => |t| {
                let item: *PollableTag.Type(t) = unsafe { @alignCast(@fieldParentPtr("io_poll", poll)) };
                if ((event.events & linux.EPOLL.ERR) != 0) {
                    const errno = bun.sys.getErrno(event.events);
                    log("error() = {s}", .{@tagName(errno)});
                    item.onIOError(bun.sys.Error.fromCode(errno, .epoll_ctl));
                } else {
                    log("ready()", .{});
                    item.onReady();
                }
            },
        }
    }

    fn registerForEpoll(*_, tag: PollableTag, loop: *Loop, comptime flag: PollFlags, one_shot: bool, fd: bun.FD): bun.sys.Maybe(void) {
        const watcher_fd = loop.pollfd();

        log("register: {s} ({f})", .{ @tagName(flag), fd });

        bun.assert(fd != bun.invalid_fd);

        if (one_shot) {
            _.flags.insert(.one_shot);
        }

        if (comptime Environment.isLinux) {
            const one_shot_flag: u32 = if (!_.flags.contains(.one_shot)) 0 else linux.EPOLL.ONESHOT;

            const flags: u32 = switch (comptime flag) {
                .process,
                .poll_readable,
                => linux.EPOLL.IN | linux.EPOLL.HUP | linux.EPOLL.ERR | one_shot_flag,
                .poll_writable => linux.EPOLL.OUT | linux.EPOLL.HUP | linux.EPOLL.ERR | one_shot_flag,
                else => @compileError("unreachable"),
            };

            let event = std.mem.zeroes(linux.epoll_event);
            event.events = flags;
            event.data.u64 = unsafe { @intFromPtr(Pollable.init(tag, _).ptr()) };

            const op: u32 = if (_.flags.contains(.was_ever_registered) || _.flags.contains(.needs_rearm)) linux.EPOLL.CTL_MOD else linux.EPOLL.CTL_ADD;

            const ctl = linux.epoll_ctl(
                watcher_fd.cast(),
                op,
                fd.cast(),
                &event,
            );

            if (errno := bun.sys.Maybe(void).errnoSys(ctl, .epoll_ctl)) {
                return errno;
            }
            _.flags.insert(.registered);
            _.flags.insert(.was_ever_registered);
        } else {
            @compileError("epoll not supported on this platform");
        }

        _.flags.insert(switch (flag) {
            .poll_readable => .poll_readable,
            .poll_process => if (comptime Environment.isLinux) .poll_readable else .poll_process,
            .poll_writable => .poll_writable,
            else => @compileError("unreachable"),
        });
        _.flags.remove(.needs_rearm);

        return .success;
    }
}

pub const retry = bun.sys.E.AGAIN;

pub const ReadState = @import("./pipes.zig").ReadState;
pub const PipeReader = @import("./PipeReader.zig").PipeReader;
pub const BufferedReader = @import("./PipeReader.zig").BufferedReader;
pub const BufferedWriter = @import("./PipeWriter.zig").BufferedWriter;
pub const WriteResult = @import("./PipeWriter.zig").WriteResult;
pub const WriteStatus = @import("./PipeWriter.zig").WriteStatus;
pub const StreamingWriter = @import("./PipeWriter.zig").StreamingWriter;
pub const StreamBuffer = @import("./PipeWriter.zig").StreamBuffer;
pub const FileType = @import("./pipes.zig").FileType;
pub const MaxBuf = @import("./MaxBuf.zig");

const Environment = bun.Environment;
const assert = bun.assert;
const sys = bun.sys;
const ReadFile = bun.webcore.Blob.read_file.ReadFile;
const WriteFile = bun.webcore.Blob.write_file.WriteFile;

const posix = std.posix;
const linux = std.os.linux;
