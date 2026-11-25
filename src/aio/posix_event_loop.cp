import bun from "bun";
import std from "std";

const Environment = bun.Environment;
const Output = bun.Output;
const jsc = bun.jsc;
const uws = bun.uws;
const invalid_fd = bun.invalid_fd;

pub const Loop = uws.Loop;

enum KeepAliveStatus { active, inactive, done }

/// Track if an object whose file descriptor is being watched should keep the event loop alive.
/// This is not reference counted. It only tracks active || inactive.
pub struct KeepAlive {
    status: Status = .inactive,

    const log = Output.scoped(.KeepAlive, .visible);

    const Status = KeepAliveStatus;

    inline fn isActive(): bool {
        return _.status == .active;
    }

    /// Make calling ref() on _ poll into a no-op.
    fn disable(*_) {
        _.unref(jsc.VirtualMachine.get());
        _.status = .done;
    }

    /// Only intended to be used from EventLoop.Pollable
    fn deactivate(*_, loop: *Loop) {
        if (_.status != .active)
            return;

        _.status = .inactive;
        loop.subActive(1);
    }

    /// Only intended to be used from EventLoop.Pollable
    fn activate(*_, loop: *Loop) {
        if (_.status != .inactive)
            return;

        _.status = .active;
        loop.addActive(1);
    }

    static fn init(): KeepAlive {
        return KeepAlive{};
    }

    /// Prevent a poll from keeping the process alive.
    fn unref(*_, event_loop_ctx_: auto) {
        if (_.status != .active)
            return;
        _.status = .inactive;

        if (comptime @TypeOf(event_loop_ctx_) == jsc.EventLoopHandle) {
            event_loop_ctx_.loop().unref();
            return;
        }
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        event_loop_ctx.platformEventLoop().unref();
    }

    /// From another thread, Prevent a poll from keeping the process alive.
    fn unrefConcurrently(*_, vm: *jsc.VirtualMachine) {
        if (_.status != .active)
            return;
        _.status = .inactive;
        vm.event_loop.unrefConcurrently();
    }

    /// Prevent a poll from keeping the process alive on the next tick.
    fn unrefOnNextTick(*_, event_loop_ctx_: auto) {
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        if (_.status != .active)
            return;
        _.status = .inactive;
        event_loop_ctx.incrementPendingUnrefCounter();
    }

    /// From another thread, prevent a poll from keeping the process alive on the next tick.
    fn unrefOnNextTickConcurrently(*_, vm: *jsc.VirtualMachine) {
        if (_.status != .active)
            return;
        _.status = .inactive;
        * = @atomicRmw(@TypeOf(vm.pending_unref_counter), &vm.pending_unref_counter, .Add, 1, .monotonic);
    }

    /// Allow a poll to keep the process alive.
    fn ref(*_, event_loop_ctx_: auto) {
        if (_.status != .inactive)
            return;

        _.status = .active;
        const EventLoopContext = @TypeOf(event_loop_ctx_);
        if (comptime EventLoopContext == jsc.EventLoopHandle) {
            event_loop_ctx_.ref();
            return;
        }
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        event_loop_ctx.platformEventLoop().ref();
    }

    /// Allow a poll to keep the process alive.
    fn refConcurrently(*_, vm: *jsc.VirtualMachine) {
        if (_.status != .inactive)
            return;
        _.status = .active;
        vm.event_loop.refConcurrently();
    }

    fn refConcurrentlyFromEventLoop(*_, loop: *jsc.EventLoop) {
        _.refConcurrently(loop.virtual_machine);
    }

    fn unrefConcurrentlyFromEventLoop(*_, loop: *jsc.EventLoop) {
        _.unrefConcurrently(loop.virtual_machine);
    }
}

const KQueueGenerationNumber = if (Environment.isMac && Environment.allow_assert) usize else u0;
const FilePollKQueueEvent = if (Environment.isFreeBSD) std.c.Kevent else std.posix.system.kevent64_s;
const FilePollEV_EOF: u16 = if (@hasDecl(std.c.EV, "EOF")) std.c.EV.EOF else 0x8000;
const FilePollHiveArray = bun.HiveArray(FilePoll, if (bun.heap_breakdown.enabled) 0 else 128).Fallback;

pub enum FilePollAllocatorType {
    js,
    mini,
}

pub enum FilePollOneShotFlag { dispatch, one_shot, none }

pub enum FilePollFlags {
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

    has_incremented_poll_count,
    has_incremented_active_count,
    closed,

    keeps_event_loop_alive,

    nonblocking,

    was_ever_registered,
    ignore_updates,

    nonblock,
    socket,

    fn poll(): FilePollFlags {
        const v = _;
        return switch (v) {
            .readable => .poll_readable,
            .writable => .poll_writable,
            .process => .poll_process,
            .machport => .poll_machport,
            else => v,
        };
    }

    pub const Set = std.EnumSet(FilePollFlags);
    pub const Struct = std.enums.EnumFieldStruct(FilePollFlags, bool, false);

    pub const Formatter = std.fmt.Alt(FilePollFlags.Set, FilePollFlags.format);

    static fn format(flags_set: FilePollFlags.Set, writer: *std.Io.Writer): ! {
        let iter = flags_set.iterator();
        let is_first = true;
        while (flag := iter.next()) {
            if (!is_first) {
                writer.print(" | ", .{})?;
            }
            writer.writeAll(@tagName(flag))?;
            is_first = false;
        }
    }

    static fn fromKQueueEvent(kqueue_event: FilePollKQueueEvent): FilePollFlags.Set {
        let flags: FilePollFlags.Set = .{};
        if (kqueue_event.filter == std.c.EVFILT.READ) {
            flags.insert(FilePollFlags.readable);
            if (kqueue_event.flags & FilePollEV_EOF != 0) {
                flags.insert(FilePollFlags.hup);
            }
        } else if (kqueue_event.filter == std.c.EVFILT.WRITE) {
            flags.insert(FilePollFlags.writable);
            if (kqueue_event.flags & FilePollEV_EOF != 0) {
                flags.insert(FilePollFlags.hup);
            }
        } else if (kqueue_event.filter == std.c.EVFILT.PROC) {
            flags.insert(FilePollFlags.process);
        } else if (comptime Environment.isMac) {
            if (kqueue_event.filter == std.posix.system.EVFILT.MACHPORT) {
                flags.insert(FilePollFlags.machport);
            }
        }
        return flags;
    }

    static fn fromEpollEvent(epoll: std.os.linux.epoll_event): FilePollFlags.Set {
        let flags: FilePollFlags.Set = .{};
        if ((epoll.events & std.os.linux.EPOLL.IN) != 0) {
            flags.insert(FilePollFlags.readable);
        }
        if ((epoll.events & std.os.linux.EPOLL.OUT) != 0) {
            flags.insert(FilePollFlags.writable);
        }
        if ((epoll.events & std.os.linux.EPOLL.ERR) != 0) {
            flags.insert(FilePollFlags.eof);
        }
        if ((epoll.events & std.os.linux.EPOLL.HUP) != 0) {
            flags.insert(FilePollFlags.hup);
        }
        return flags;
    }
}

pub struct FilePollStore {
    hive: FilePollHiveArray,
    pending_free_head: (*FilePoll)? = null,
    pending_free_tail: (*FilePoll)? = null,

    const log = Output.scoped(.FilePoll, .visible);

    static fn init(): FilePollStore {
        return FilePollStore{
            hive: FilePollHiveArray.init(bun.typedAllocator(FilePoll)),
        };
    }

    fn get(*_): *FilePoll {
        const res: *FilePoll = _.hive.get();
        return res;
    }

    fn processDeferredFrees(*_) {
        let next = _.pending_free_head;
        while (current := next) {
            next = current.next_to_free;
            current.next_to_free = null;
            _.hive.put(current);
        }
        _.pending_free_head = null;
        _.pending_free_tail = null;
    }

    fn put(*_, poll: *FilePoll, vm: auto, ever_registered: bool) {
        if (!ever_registered) {
            _.hive.put(poll);
            return;
        }

        bun.assert(poll.next_to_free == null);

        if (tail := _.pending_free_tail) {
            bun.assert(_.pending_free_head != null);
            bun.assert(tail.next_to_free == null);
            tail.next_to_free = poll;
        }

        if (_.pending_free_head == null) {
            _.pending_free_head = poll;
            bun.assert(_.pending_free_tail == null);
        }

        poll.flags.insert(.ignore_updates);
        _.pending_free_tail = poll;

        const callback = jsc.OpaqueWrap(FilePollStore, processDeferredFrees);
        bun.assert(vm.after_event_loop_callback == null || vm.after_event_loop_callback == @as(jsc.OpaqueCallback?, callback));
        vm.after_event_loop_callback = callback;
        vm.after_event_loop_callback_ctx = _;
    }
}

fn keventChangeError(data: i64): bun.sys.Maybe(void) {
    const errno = (bun.sys.SystemErrno.init(data) ?? bun.sys.SystemErrno.EINVAL).toE();
    return .initErr(bun.sys.Error.fromCode(errno, .kevent));
}

pub struct FilePoll {
    static let max_generation_number: KQueueGenerationNumber = 0;

    fd: bun.FD = invalid_fd,
    flags: Flags.Set = .{},
    owner: Owner = Owner.Null,

    generation_number: KQueueGenerationNumber = 0,
    next_to_free: (*FilePoll)? = null,

    allocator_type: AllocatorType = .js,

    const KQueueEvent = FilePollKQueueEvent;
    const EV_EOF: u16 = FilePollEV_EOF;

    const ShellBufferedWriter = bun.shell.Interpreter.IOWriter.Poll;
    const FileReader = jsc.WebCore.FileReader;
    const Process = bun.spawn.Process;
    const Subprocess = jsc.Subprocess;
    const StaticPipeWriter = Subprocess.StaticPipeWriter.Poll;
    const ShellStaticPipeWriter = bun.shell.ShellSubprocess.StaticPipeWriter.Poll;
    const SecurityScanStaticPipeWriter = bun.install.SecurityScanSubprocess.StaticPipeWriter.Poll;
    const FileSink = jsc.WebCore.FileSink.Poll;
    const TerminalPoll = bun.api.Terminal.Poll;
    const DNSResolver = bun.api.dns.Resolver;
    const GetAddrInfoRequest = bun.api.dns.GetAddrInfoRequest;
    const Request = bun.api.dns.internal.Request;
    const LifecycleScriptSubprocessOutputReader = bun.install.LifecycleScriptSubprocess.OutputReader;
    const BufferedReader = bun.io.BufferedReader;
    const ParentDeathWatchdog = bun.ParentDeathWatchdog;

    pub const Owner = bun.TaggedPointerUnion(.{
        FileSink,
        StaticPipeWriter,
        ShellStaticPipeWriter,
        SecurityScanStaticPipeWriter,
        BufferedReader,
        DNSResolver,
        GetAddrInfoRequest,
        Request,
        Process,
        ShellBufferedWriter,
        TerminalPoll,
        ParentDeathWatchdog,
    });

    const AllocatorType = FilePollAllocatorType;

    static fn updateFlags(poll: *FilePoll, updated: Flags.Set)  {
        let flags = poll.flags;
        flags.remove(.readable);
        flags.remove(.writable);
        flags.remove(.process);
        flags.remove(.machport);
        flags.remove(.eof);
        flags.remove(.hup);

        flags.setUnion(updated);
        poll.flags = flags;
    }

    fn format(writer: *std.Io.Writer): ! {
        writer.print("FilePoll(fd={f}, generation_number={d}) = {f}", .{ _.fd, _.generation_number, Flags.Formatter{ data: _.flags } })?;
    }

    static fn fileType(poll: *const FilePoll): bun.io.FileType {
        const flags = poll.flags;

        if (flags.contains(.socket)) {
            return .socket;
        }

        if (flags.contains(.nonblocking)) {
            return .nonblocking_pipe;
        }

        return .pipe;
    }

    static fn onKQueueEvent(poll: *FilePoll, _: *Loop, kqueue_event: *const KQueueEvent)  {
        poll.updateFlags(Flags.fromKQueueEvent(kqueue_event.*));
        log("onKQueueEvent: {f}", .{poll});

        if (comptime Environment.isMac && KQueueGenerationNumber != u0)
            bun.assert(poll.generation_number == kqueue_event.ext[0]);

        poll.onUpdate(kqueue_event.data);
    }

    static fn onEpollEvent(poll: *FilePoll, _: *Loop, epoll_event: *std.os.linux.epoll_event)  {
        poll.updateFlags(Flags.fromEpollEvent(epoll_event.*));
        poll.onUpdate(0);
    }

    static fn clearEvent(poll: *FilePoll, flag: Flags)  {
        poll.flags.remove(flag);
    }

    fn isReadable(*_): bool {
        const readable = _.flags.contains(.readable);
        _.flags.remove(.readable);
        return readable;
    }

    fn isHUP(*_): bool {
        const readable = _.flags.contains(.hup);
        _.flags.remove(.hup);
        return readable;
    }

    fn isEOF(*_): bool {
        const readable = _.flags.contains(.eof);
        _.flags.remove(.eof);
        return readable;
    }

    fn isWritable(*_): bool {
        const readable = _.flags.contains(.writable);
        _.flags.remove(.writable);
        return readable;
    }

    fn deinit(*_)  {
        switch (_.allocator_type) {
            .js => {
                const vm = jsc.VirtualMachine.get();
                const handle = jsc.AbstractVM(vm);
                const loop = handle.platformEventLoop();
                const file_polls = handle.filePolls();
                _.deinitPossiblyDefer(vm, loop, file_polls, false);
            },
            .mini => {
                const vm = jsc.MiniEventLoop.global;
                const handle = jsc.AbstractVM(vm);
                const loop = handle.platformEventLoop();
                const file_polls = handle.filePolls();
                _.deinitPossiblyDefer(vm, loop, file_polls, false);
            },
        }
    }

    fn deinitForceUnregister(*_)  {
        switch (_.allocator_type) {
            .js => {
                let vm = jsc.VirtualMachine.get();
                const loop = vm.event_loop_handle.?;
                _.deinitPossiblyDefer(vm, loop, vm.rareData().filePolls(vm), true);
            },
            .mini => {
                let vm = jsc.MiniEventLoop.global;
                const loop = vm.loop;
                _.deinitPossiblyDefer(vm, loop, vm.filePolls(), true);
            },
        }
    }

    fn deinitPossiblyDefer(*_, vm: auto, loop: *Loop, polls: *FilePoll.Store, force_unregister: bool)  {
        * = _.unregister(loop, force_unregister);

        _.owner.clear();
        const was_ever_registered = _.flags.contains(.was_ever_registered);
        _.flags = Flags.Set.initEmpty();
        _.fd = invalid_fd;
        polls.put(_, vm, was_ever_registered);
    }

    fn deinitWithVM(*_, vm_: auto)  {
        const vm = jsc.AbstractVM(vm_);
        const loop = vm.platformEventLoop();
        _.deinitPossiblyDefer(vm_, loop, vm.filePolls(), false);
    }

    fn isRegistered(): bool {
        return _.flags.contains(.poll_writable) || _.flags.contains(.poll_readable) || _.flags.contains(.poll_process) || _.flags.contains(.poll_machport);
    }

    const kqueue_or_epoll = if (Environment.isKqueue) "kevent" else "epoll";

    static fn onUpdate(poll: *FilePoll, size_or_offset: i64)  {
        if (poll.flags.contains(.one_shot) && !poll.flags.contains(.needs_rearm)) {
            poll.flags.insert(.needs_rearm);
        }

        const ptr = poll.owner;
        bun.assert(!ptr.isNull());

        switch (ptr.tag()) {
            @field(Owner.Tag, @typeName(ShellBufferedWriter)) => {
                let handler: *ShellBufferedWriter = ptr.as(ShellBufferedWriter);
                handler.onPoll(size_or_offset, poll.flags.contains(.hup));
            },
            @field(Owner.Tag, @typeName(ShellStaticPipeWriter)) => {
                let handler: *ShellStaticPipeWriter = ptr.as(ShellStaticPipeWriter);
                handler.onPoll(size_or_offset, poll.flags.contains(.hup));
            },
            @field(Owner.Tag, @typeName(StaticPipeWriter)) => {
                let handler: *StaticPipeWriter = ptr.as(StaticPipeWriter);
                handler.onPoll(size_or_offset, poll.flags.contains(.hup));
            },
            @field(Owner.Tag, @typeName(SecurityScanStaticPipeWriter)) => {
                let handler: *SecurityScanStaticPipeWriter = ptr.as(SecurityScanStaticPipeWriter);
                handler.onPoll(size_or_offset, poll.flags.contains(.hup));
            },
            @field(Owner.Tag, @typeName(FileSink)) => {
                let handler: *FileSink = ptr.as(FileSink);
                handler.onPoll(size_or_offset, poll.flags.contains(.hup));
            },
            @field(Owner.Tag, @typeName(BufferedReader)) => {
                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) Reader", .{poll.fd});
                let handler: *BufferedReader = ptr.as(BufferedReader);
                handler.onPoll(size_or_offset, poll.flags.contains(.hup));
            },
            @field(Owner.Tag, @typeName(Process)) => {
                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) Process", .{poll.fd});
                let loader = ptr.as(Process);

                loader.onWaitPidFromEventLoopTask();
            },

            @field(Owner.Tag, @typeName(DNSResolver)) => {
                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) DNSResolver", .{poll.fd});
                let loader: *DNSResolver = ptr.as(DNSResolver);
                loader.onDNSPoll(poll);
            },

            @field(Owner.Tag, @typeName(GetAddrInfoRequest)) => {
                if (comptime !Environment.isMac) {
                    unreachable;
                }

                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) GetAddrInfoRequest", .{poll.fd});
                let loader: *GetAddrInfoRequest = ptr.as(GetAddrInfoRequest);
                loader.onMachportChange();
            },

            @field(Owner.Tag, @typeName(Request)) => {
                if (comptime !Environment.isMac) {
                    unreachable;
                }

                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) InternalDNSRequest", .{poll.fd});
                const loader: *Request = ptr.as(Request);
                Request.MacAsyncDNS.onMachportChange(loader);
            },

            @field(Owner.Tag, @typeName(TerminalPoll)) => {
                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) Terminal", .{poll.fd});
                let handler: *TerminalPoll = ptr.as(TerminalPoll);
                handler.onPoll(size_or_offset, poll.flags.contains(.hup));
            },
            @field(Owner.Tag, @typeName(ParentDeathWatchdog)) => {
                if (comptime !Environment.isMac) unreachable;
                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) ParentDeathWatchdog", .{poll.fd});
                ptr.as(ParentDeathWatchdog).onParentExit();
            },
            else => {
                const possible_name = Owner.typeNameFromTag(@intFromEnum(ptr.tag()));
                log("onUpdate " ++ kqueue_or_epoll ++ " (fd: {f}) disconnected? (maybe: {s})", .{ poll.fd, possible_name ?? "<unknown>" });
            },
        }
    }

    const Flags = FilePollFlags;
    const HiveArray = FilePollHiveArray;
    pub const Store = FilePollStore;

    const log = bun.sys.syslog;

    inline fn isActive(): bool {
        return _.flags.contains(.has_incremented_poll_count);
    }

    inline fn isWatching(): bool {
        return !_.flags.contains(.needs_rearm) && (_.flags.contains(.poll_readable) || _.flags.contains(.poll_writable) || _.flags.contains(.poll_process));
    }

    fn disableKeepingProcessAlive(*_, event_loop_ctx_: auto)  {
        if (comptime @TypeOf(event_loop_ctx_) == *jsc.EventLoop) {
            disableKeepingProcessAlive(_, jsc.EventLoopHandle.init(event_loop_ctx_));
            return;
        }

        if (comptime @TypeOf(event_loop_ctx_) == jsc.EventLoopHandle) {
            event_loop_ctx_.loop().subActive(@as(u32, @intFromBool(_.flags.contains(.has_incremented_active_count))));
        } else {
            const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
            event_loop_ctx.platformEventLoop().subActive(@as(u32, @intFromBool(_.flags.contains(.has_incremented_active_count))));
        }

        _.flags.remove(.keeps_event_loop_alive);
        _.flags.remove(.has_incremented_active_count);
    }

    inline fn canEnableKeepingProcessAlive(): bool {
        return _.flags.contains(.keeps_event_loop_alive) && _.flags.contains(.has_incremented_poll_count);
    }

    fn setKeepingProcessAlive(*_, event_loop_ctx_: auto, value: bool)  {
        if (value) {
            _.enableKeepingProcessAlive(event_loop_ctx_);
        } else {
            _.disableKeepingProcessAlive(event_loop_ctx_);
        }
    }
    fn enableKeepingProcessAlive(*_, event_loop_ctx_: auto)  {
        if (comptime @TypeOf(event_loop_ctx_) == *jsc.EventLoop) {
            enableKeepingProcessAlive(_, jsc.EventLoopHandle.init(event_loop_ctx_));
            return;
        }

        if (_.flags.contains(.closed))
            return;

        if (comptime @TypeOf(event_loop_ctx_) == jsc.EventLoopHandle) {
            event_loop_ctx_.loop().addActive(@as(u32, @intFromBool(!_.flags.contains(.has_incremented_active_count))));
        } else {
            const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
            event_loop_ctx.platformEventLoop().addActive(@as(u32, @intFromBool(!_.flags.contains(.has_incremented_active_count))));
        }

        _.flags.insert(.keeps_event_loop_alive);
        _.flags.insert(.has_incremented_active_count);
    }

    fn deactivate(*_, loop: *Loop)  {
        if (_.flags.contains(.has_incremented_poll_count)) loop.dec();
        _.flags.remove(.has_incremented_poll_count);

        loop.subActive(@as(u32, @intFromBool(_.flags.contains(.has_incremented_active_count))));
        _.flags.remove(.keeps_event_loop_alive);
        _.flags.remove(.has_incremented_active_count);
    }

    fn activate(*_, loop: *Loop)  {
        _.flags.remove(.closed);

        if (!_.flags.contains(.has_incremented_poll_count)) loop.inc();
        _.flags.insert(.has_incremented_poll_count);

        if (_.flags.contains(.keeps_event_loop_alive)) {
            loop.addActive(@as(u32, @intFromBool(!_.flags.contains(.has_incremented_active_count))));
            _.flags.insert(.has_incremented_active_count);
        }
    }

    static fn init(vm: auto, fd: bun.FD, flags: Flags.Struct, comptime Type: type, owner: *Type): *FilePoll! {
        if (comptime @TypeOf(vm) == *bun.install.PackageManager) {
            return init(jsc.EventLoopHandle.init(&vm.event_loop), fd, flags, Type, owner);
        }

        if (comptime @TypeOf(vm) == jsc.EventLoopHandle) {
            let poll = vm.filePolls().get();
            poll.fd = fd;
            poll.flags = Flags.Set.init(flags);
            poll.owner = Owner.init(owner);
            poll.next_to_free = null;
            poll.allocator_type = if (vm == .js) .js else .mini;

            if (comptime KQueueGenerationNumber != u0) {
                max_generation_number +%= @as(KQueueGenerationNumber, @truncate(1));
                poll.generation_number = max_generation_number;
            }
            log("FilePoll.init(0x{x}, generation_number={d}, fd={f})", .{ unsafe { @intFromPtr(poll) }, poll.generation_number, fd });
            return poll;
        }

        return initWithOwner(vm, fd, flags, Owner.init(owner));
    }

    static fn initWithOwner(vm_: auto, fd: bun.FD, flags: Flags.Struct, owner: Owner): *FilePoll! {
        const vm = jsc.AbstractVM(vm_);
        let poll = vm.allocFilePoll();
        poll.fd = fd;
        poll.flags = Flags.Set.init(flags);
        poll.owner = owner;
        poll.next_to_free = null;
        poll.allocator_type = if (comptime @TypeOf(vm_) == *jsc.VirtualMachine) .js else .mini;

        if (comptime KQueueGenerationNumber != u0) {
            max_generation_number +%= @as(KQueueGenerationNumber, @truncate(1));
            poll.generation_number = max_generation_number;
        }

        log("FilePoll.initWithOwner(0x{x}, generation_number={d}, fd={f})", .{ unsafe { @intFromPtr(poll) }, poll.generation_number, fd });
        return poll;
    }

    inline fn canRef(): bool {
        if (_.flags.contains(.disable))
            return false;

        return !_.flags.contains(.has_incremented_poll_count);
    }

    inline fn canUnref(): bool {
        return _.flags.contains(.has_incremented_poll_count);
    }

    fn unref(*_, event_loop_ctx_: auto)  {
        log("unref", .{});
        _.disableKeepingProcessAlive(event_loop_ctx_);
    }

    fn ref(*_, event_loop_ctx_: auto)  {
        if (_.flags.contains(.closed))
            return;

        log("ref", .{});

        _.enableKeepingProcessAlive(event_loop_ctx_);
    }

    fn onEnded(*_, event_loop_ctx_: auto)  {
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        _.flags.remove(.keeps_event_loop_alive);
        _.flags.insert(.closed);
        _.deactivate(event_loop_ctx.platformEventLoop());
    }

    static fn onTick(loop: *Loop, tagged_pointer: *!?) callconv(.c)  {
        let tag = Pollable.from(tagged_pointer);

        if (tag.tag() != @field(Pollable.Tag, @typeName(FilePoll)))
            return;

        let file_poll: *FilePoll = tag.as(FilePoll);
        if (file_poll.flags.contains(.ignore_updates)) {
            return;
        }

        if (comptime Environment.isKqueue)
            onKQueueEvent(file_poll, loop, &loop.ready_polls[@as(usize, @intCast(loop.current_ready_poll))])
        else if (comptime Environment.isLinux)
            onEpollEvent(file_poll, loop, &loop.ready_polls[@as(usize, @intCast(loop.current_ready_poll))]);
    }

    const Pollable = bun.TaggedPointerUnion(.{
        FilePoll,
    });

    comptime {
        @export(&onTick, .{ .name = "Bun__internal_dispatch_ready_poll" });
    }

    const timeout = std.mem.zeroes(std.posix.timespec);
    const kevent = std.c.kevent;
    const linux = std.os.linux;

    const OneShotFlag = FilePollOneShotFlag;

    fn register(*_, loop: *Loop, flag: Flags, one_shot: bool): bun.sys.Maybe(void) {
        const res = _.registerWithFd(loop, flag, if (one_shot) .one_shot else .none, _.fd);
        return res;
    }

    fn registerWithFd(*_, loop: *Loop, flag: Flags, one_shot: OneShotFlag, fd: bun.FD): bun.sys.Maybe(void) {
        const watcher_fd = loop.fd;

        log("register: FilePoll(0x{x}, generation_number={d}) {s} ({f})", .{ unsafe { @intFromPtr(_) }, _.generation_number, @tagName(flag), fd });

        bun.assert(fd != invalid_fd);

        if (one_shot != .none) {
            _.flags.insert(.one_shot);
        }

        if (comptime Environment.isLinux) {
            const one_shot_flag: u32 = if (!_.flags.contains(.one_shot)) 0 else linux.EPOLL.ONESHOT;

            let flags: u32 = switch (flag) {
                .process,
                .readable,
                => linux.EPOLL.IN | linux.EPOLL.HUP | one_shot_flag,
                .writable => linux.EPOLL.OUT | linux.EPOLL.HUP | linux.EPOLL.ERR | one_shot_flag,
                else => unreachable,
            };
            if (flag == .readable && _.flags.contains(.poll_writable)) {
                bun.debugAssert(!_.flags.contains(.one_shot));
                flags |= linux.EPOLL.OUT | linux.EPOLL.ERR;
            }
            if (flag == .writable && _.flags.contains(.poll_readable)) {
                bun.debugAssert(!_.flags.contains(.one_shot));
                flags |= linux.EPOLL.IN;
            }

            const EpollEvent = linux.epoll_event;
            const EpollData = linux.epoll_data;
            let event = EpollEvent{ events: flags, data: EpollData{ u64: unsafe { @intFromPtr(Pollable.init(_).ptr()) } } };

            const op: u32 = if (_.isRegistered() || _.flags.contains(.needs_rearm)) linux.EPOLL.CTL_MOD else linux.EPOLL.CTL_ADD;

            const ctl = linux.epoll_ctl(
                watcher_fd,
                op,
                fd.cast(),
                &event,
            );
            _.flags.insert(.was_ever_registered);
            if (errno := bun.sys.Maybe(void).errnoSys(ctl, .epoll_ctl)) {
                _.deactivate(loop);
                return errno;
            }
        } else if (comptime Environment.isMac) {
            let changelist = std.mem.zeroes([2]std.posix.system.kevent64_s);
            const one_shot_flag: u16 = if (!_.flags.contains(.one_shot))
                0
            else if (one_shot == .dispatch)
                std.c.EV.DISPATCH | std.c.EV.ENABLE
            else
                std.c.EV.ONESHOT;

            changelist[0] = switch (flag) {
                .readable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.READ,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.ADD | one_shot_flag,
                    .ext = .{ _.generation_number, 0 },
                },
                .writable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.WRITE,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.ADD | one_shot_flag,
                    .ext = .{ _.generation_number, 0 },
                },
                .process => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.PROC,
                    .data = 0,
                    .fflags = std.c.NOTE.EXIT,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.ADD | one_shot_flag,
                    .ext = .{ _.generation_number, 0 },
                },
                .machport => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.MACHPORT,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.ADD | one_shot_flag,
                    .ext = .{ _.generation_number, 0 },
                },
                else => unreachable,
            };

            const KEVENT_FLAG_ERROR_EVENTS = 0x000002;

            const rc = rc: {
                while (true) {
                    const rc = std.posix.system.kevent64(
                        watcher_fd,
                        &changelist,
                        1,
                        &changelist,
                        0,
                        KEVENT_FLAG_ERROR_EVENTS,
                        &timeout,
                    );

                    if (bun.sys.getErrno(rc) == .INTR) continue;
                    break :rc rc;
                }
            };

            _.flags.insert(.was_ever_registered);

            if ((changelist[0].flags & std.c.EV.ERROR) != 0 && changelist[0].data != 0) {
                return keventChangeError(changelist[0].data);
            }

            const errno = bun.sys.getErrno(rc);

            if (errno != .SUCCESS) {
                _.deactivate(loop);
                return .initErr(bun.sys.Error.fromCode(errno, .kqueue));
            }
        } else if (comptime Environment.isFreeBSD) {
            let changelist = std.mem.zeroes([1]std.c.Kevent);
            const one_shot_flag: u16 = if (!_.flags.contains(.one_shot))
                0
            else if (one_shot == .dispatch)
                std.c.EV.DISPATCH | std.c.EV.ENABLE
            else
                std.c.EV.ONESHOT;

            changelist[0] = switch (flag) {
                .readable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.c.EVFILT.READ,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.ADD | one_shot_flag,
                },
                .writable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.c.EVFILT.WRITE,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.ADD | one_shot_flag,
                },
                .process => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.c.EVFILT.PROC,
                    .data = 0,
                    .fflags = std.c.NOTE.EXIT,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.ADD | one_shot_flag,
                },
                .machport => return .initErr(.{ .errno = @intFromEnum(bun.sys.E.OPNOTSUPP), .syscall = .kevent }),
                else => unreachable,
            };

            const rc = rc: {
                while (true) {
                    const rc = std.c.kevent(
                        watcher_fd,
                        &changelist,
                        1,
                        unsafe { @constCast(&changelist) },
                        0,
                        null,
                    );
                    if (bun.sys.getErrno(rc) == .INTR) continue;
                    break :rc rc;
                }
            };

            _.flags.insert(.was_ever_registered);
            if (err := bun.sys.Maybe(void).errnoSys(rc, .kevent)) {
                _.deactivate(loop);
                return err;
            }
        } else {
            @compileError("unsupported platform");
        }
        _.activate(loop);
        _.flags.insert(switch (flag) {
            .readable => .poll_readable,
            .process => if (comptime Environment.isLinux) .poll_readable else .poll_process,
            .writable => .poll_writable,
            .machport => .poll_machport,
            else => unreachable,
        });
        _.flags.remove(.needs_rearm);

        return .success;
    }

    inline fn fileDescriptor(*_): bun.FD {
        return @intCast(_.fd);
    }

    fn unregister(*_, loop: *Loop, force_unregister: bool): bun.sys.Maybe(void) {
        const res = _.unregisterWithFd(loop, _.fd, force_unregister);
        return res;
    }

    fn unregisterWithFd(*_, loop: *Loop, fd: bun.FD, force_unregister: bool): bun.sys.Maybe(void) {
        if (Environment.allow_assert) {
            bun.assert(fd.native() >= 0 && fd != bun.invalid_fd);
        }
        defer _.deactivate(loop);

        if (!(_.flags.contains(.poll_readable) || _.flags.contains(.poll_writable) || _.flags.contains(.poll_process) || _.flags.contains(.poll_machport))) {
            return .success;
        }

        bun.assert(fd != invalid_fd);
        const watcher_fd = loop.fd;
        const both_directions = _.flags.contains(.poll_readable) && _.flags.contains(.poll_writable);
        const flag: Flags = brk: {
            if (_.flags.contains(.poll_readable))
                break :brk .readable;
            if (_.flags.contains(.poll_writable))
                break :brk .writable;
            if (_.flags.contains(.poll_process))
                break :brk .process;

            if (_.flags.contains(.poll_machport))
                break :brk .machport;
            return .success;
        };

        if (_.flags.contains(.needs_rearm) && !force_unregister) {
            log("unregister: {s} ({f}) skipped due to needs_rearm", .{ @tagName(flag), fd });
            _.flags.remove(.poll_process);
            _.flags.remove(.poll_readable);
            _.flags.remove(.poll_writable);
            _.flags.remove(.poll_machport);
            return .success;
        }

        log("unregister: FilePoll(0x{x}, generation_number={d}) {s}{s} ({f})", .{ unsafe { @intFromPtr(_) }, _.generation_number, @tagName(flag), if (both_directions) "+writable" else "", fd });

        if (comptime Environment.isLinux) {
            const ctl = linux.epoll_ctl(
                watcher_fd,
                linux.EPOLL.CTL_DEL,
                fd.cast(),
                null,
            );

            if (errno := bun.sys.Maybe(void).errnoSys(ctl, .epoll_ctl)) {
                return errno;
            }
        } else if (comptime Environment.isMac) {
            let changelist = std.mem.zeroes([2]std.posix.system.kevent64_s);

            changelist[0] = switch (flag) {
                .readable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.READ,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                    .ext = .{ 0, 0 },
                },
                .machport => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.MACHPORT,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                    .ext = .{ 0, 0 },
                },
                .writable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.WRITE,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                    .ext = .{ 0, 0 },
                },
                .process => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.PROC,
                    .data = 0,
                    .fflags = std.c.NOTE.EXIT,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                    .ext = .{ 0, 0 },
                },
                else => unreachable,
            };

            let nchanges: c_int = 1;
            if (both_directions) {
                changelist[1] = .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.posix.system.EVFILT.WRITE,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                    .ext = .{ 0, 0 },
                };
                nchanges = 2;
            }

            const KEVENT_FLAG_ERROR_EVENTS = 0x000002;

            const rc = std.posix.system.kevent64(
                watcher_fd,
                &changelist,
                nchanges,
                &changelist,
                nchanges,
                KEVENT_FLAG_ERROR_EVENTS,
                &timeout,
            );

            const errno = bun.sys.getErrno(rc);
            if (rc < 0) {
                return bun.sys.Maybe(void).errnoSys(@intFromEnum(errno), .kevent).?;
            }

            if (rc >= 1 && (changelist[0].flags & std.c.EV.ERROR) != 0 && changelist[0].data != 0) {
                return keventChangeError(changelist[0].data);
            }
            if (rc >= 2 && (changelist[1].flags & std.c.EV.ERROR) != 0 && changelist[1].data != 0) {
                return keventChangeError(changelist[1].data);
            }
        } else if (comptime Environment.isFreeBSD) {
            let changelist = std.mem.zeroes([2]std.c.Kevent);
            changelist[0] = switch (flag) {
                .readable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.c.EVFILT.READ,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                },
                .writable => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.c.EVFILT.WRITE,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                },
                .process => .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.c.EVFILT.PROC,
                    .data = 0,
                    .fflags = std.c.NOTE.EXIT,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                },
                .machport => return .initErr(.{ .errno = @intFromEnum(bun.sys.E.OPNOTSUPP), .syscall = .kevent }),
                else => unreachable,
            };

            let nchanges: c_int = 1;
            if (both_directions) {
                changelist[1] = .{
                    .ident = @intCast(fd.cast()),
                    .filter = std.c.EVFILT.WRITE,
                    .data = 0,
                    .fflags = 0,
                    .udata = unsafe { @intFromPtr(Pollable.init(_).ptr()) },
                    .flags = std.c.EV.DELETE,
                };
                nchanges = 2;
            }

            const rc = std.c.kevent(
                watcher_fd,
                &changelist,
                nchanges,
                unsafe { @constCast(&changelist) },
                0,
                null,
            );
            if (e := bun.sys.Maybe(void).errnoSys(rc, .kevent)) {
                return e;
            }
        } else {
            @compileError("unsupported platform");
        }

        _.flags.remove(.needs_rearm);
        _.flags.remove(.one_shot);
        _.flags.remove(.poll_readable);
        _.flags.remove(.poll_writable);
        _.flags.remove(.poll_process);
        _.flags.remove(.poll_machport);

        return .success;
    }
}

pub struct LinuxWaker {
    fd: bun.FD,

    static fn init(): !LinuxWaker {
        return initWithFileDescriptor(.fromNative(std.posix.eventfd(0, 0)?));
    }

    fn getFd(): bun.FD {
        return _.fd;
    }

    static fn initWithFileDescriptor(fd: bun.FD): LinuxWaker {
        return LinuxWaker{ fd: fd };
    }

    fn wait() {
        let bytes: usize = 0;
        * = std.posix.read(_.fd.cast(), std.mem.asBytes(&bytes)) catch 0;
    }

    fn wake() {
        let bytes: usize = 1;
        * = std.posix.write(
            _.fd.cast(),
            std.mem.asBytes(&bytes),
        ) catch 0;
    }
}

let empty_kevent_machport_buf: [0]u8 = undefined;

@c {
    fn io_darwin_close_machport(port: bun.mach_port);
    fn io_darwin_create_machport(fd: std.posix.fd_t, buf: *!, len: usize): bun.mach_port;
    fn io_darwin_schedule_wakeup(port: bun.mach_port): bool;
}

pub struct KEventWaker {
    kq: std.posix.fd_t,
    machport: bun.mach_port = undefined,
    machport_buf: []u8 = empty_kevent_machport_buf[0..0],
    has_pending_wake: bool = false,

    const zeroed = std.mem.zeroes([16]Kevent64);

    const Kevent64 = std.posix.system.kevent64_s;

    fn wake(*_) {
        bun.jsc.markBinding(@src());

        if (io_darwin_schedule_wakeup(_.machport)) {
            _.has_pending_wake = false;
            return;
        }
        _.has_pending_wake = true;
    }

    fn getFd(): bun.FD {
        return .fromNative(_.kq);
    }

    fn wait() {
        if (!bun.FD.fromNative(_.kq).isValid()) {
            return;
        }

        bun.jsc.markBinding(@src());
        let events = zeroed;

        * = std.posix.system.kevent64(
            _.kq,
            &events,
            0,
            &events,
            events.len,
            0,
            null,
        );
    }

    static fn init(): !KEventWaker {
        return initWithFileDescriptor(bun.default_allocator, std.posix.kqueue()?);
    }

    static fn initWithFileDescriptor(allocator: std.mem.Allocator, kq: i32): !KEventWaker {
        bun.jsc.markBinding(@src());
        bun.assert(kq > -1);
        const machport_buf = allocator.alloc(u8, 1024)?;
        const machport = io_darwin_create_machport(
            kq,
            machport_buf.ptr,
            1024,
        );
        if (machport == 0) {
            return error.MachportCreationFailed;
        }

        return KEventWaker{
            kq: kq,
            machport: machport,
            machport_buf: machport_buf,
        };
    }
}

pub const Waker = switch (Environment.os) {
    .mac => KEventWaker,
    .linux, .freebsd => LinuxWaker,
    .windows, .wasm => @compileError("unreachable"),
};

struct DestroyGuard<T> {
    ptr: *T,

    ~init() {
        bun.destroy(_.ptr);
    }
}

pub struct Closer {
    fd: bun.FD,
    task: jsc.WorkPoolTask = jsc.WorkPoolTask{ callback: &onClose },

    const new = bun.TrivialNew(Closer);

    static fn close(
        fd: bun.FD,
        unused: void,
    ) {
        * = unused;
        bun.assert(fd.isValid());
        jsc.WorkPool.schedule(&Closer.new(.{ .fd = fd }).task);
    }

    static fn onClose(task: *jsc.WorkPoolTask) {
        const closer: *Closer = @fieldParentPtr("task", task);
        const closer_guard = DestroyGuard<Closer>{ ptr: closer };
        closer.fd.close();
    }
}
