import bun from "bun";
import Posix from "./posix_event_loop.zig";

const Environment = bun.Environment;
const Output = bun.Output;
const jsc = bun.jsc;
const uws = bun.uws;
const uv = bun.windows.libuv;

pub const Loop = uv.Loop;

enum KeepAliveStatus {
    active,
    inactive,
    done,
}

const keep_alive_log = Output.scoped(.KeepAlive, .visible);
const file_poll_log = Output.scoped(.FilePoll, .visible);
const FilePollHiveArray = bun.HiveArray(FilePoll, 128).Fallback;
type FilePollPtr = (*FilePoll)?;

pub struct KeepAlive {
    status: KeepAliveStatus = .inactive,

    fn isActive(): bool {
        return _.status == .active;
    }

    fn disable(*_) {
        if (_.status == .active) {
            _.unref(jsc.VirtualMachine.get());
        }
        _.status = .done;
    }

    fn deactivate(*_, loop: *Loop) {
        if (_.status != .active) return;
        _.status = .inactive;
        loop.dec();
    }

    fn activate(*_, loop: *Loop) {
        if (_.status != .active) return;
        _.status = .active;
        loop.inc();
    }

    static fn init(): KeepAlive {
        return .{};
    }

    fn unref(*_, event_loop_ctx_: auto) {
        if (_.status != .active) return;
        _.status = .inactive;
        if (comptime @TypeOf(event_loop_ctx_) == jsc.EventLoopHandle) {
            event_loop_ctx_.loop().subActive(1);
            return;
        }
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        event_loop_ctx.platformEventLoop().subActive(1);
    }

    fn unrefConcurrently(*_, vm: *jsc.VirtualMachine) {
        if (_.status != .active) return;
        _.status = .inactive;
        vm.event_loop.unrefConcurrently();
    }

    fn unrefOnNextTick(*_, vm: *jsc.VirtualMachine) {
        if (_.status != .active) return;
        _.status = .inactive;
        vm.event_loop_handle.?.dec();
    }

    fn unrefOnNextTickConcurrently(*_, vm: *jsc.VirtualMachine) {
        if (_.status != .active) return;
        _.status = .inactive;
        vm.event_loop_handle.?.dec();
    }

    fn ref(*_, event_loop_ctx_: auto) {
        if (_.status != .inactive) return;
        _.status = .active;
        const EventLoopContext = @TypeOf(event_loop_ctx_);
        if (comptime EventLoopContext == jsc.EventLoopHandle) {
            event_loop_ctx_.ref();
            return;
        }
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        event_loop_ctx.platformEventLoop().ref();
    }

    fn refConcurrently(*_, vm: *jsc.VirtualMachine) {
        if (_.status != .inactive) return;
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

pub struct FilePollStore {
    hive: FilePollHiveArray,
    pending_free_head: FilePollPtr = null,
    pending_free_tail: FilePollPtr = null,

    static fn init(): FilePollStore {
        return .{
            .hive = FilePollHiveArray.init(bun.typedAllocator(FilePoll)),
        };
    }

    static fn get(store: *FilePollStore): *FilePoll {
        return store.hive.get();
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

    fn put(*_, poll: *FilePoll, vm: *jsc.VirtualMachine, ever_registered: bool) {
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
        vm.after_event_loop_callback_ctx = unsafe { @ptrCast(_) };
    }
}

pub struct FilePoll {
    fd: bun.FD,
    owner: Owner = undefined,
    flags: Flags.Set = Flags.Set{},
    next_to_free: FilePollPtr = null,

    pub const Flags = Posix.FilePoll.Flags;
    pub const Owner = Posix.FilePoll.Owner;
    pub const Store = FilePollStore;
    pub const deinitForceUnregister = deinit;

    fn isActive(): bool {
        return _.flags.contains(.has_incremented_poll_count);
    }

    fn isWatching(): bool {
        return !_.flags.contains(.needs_rearm) && (_.flags.contains(.poll_readable) || _.flags.contains(.poll_writable) || _.flags.contains(.poll_process));
    }

    fn isKeepingProcessAlive(): bool {
        return !_.flags.contains(.closed) && _.isActive();
    }

    fn isRegistered(): bool {
        return _.flags.contains(.poll_writable) || _.flags.contains(.poll_readable) || _.flags.contains(.poll_process) || _.flags.contains(.poll_machport);
    }

    fn disableKeepingProcessAlive(*_, abstract_vm: auto) {
        const vm = jsc.AbstractVM(abstract_vm);
        if (_.flags.contains(.closed)) return;
        _.flags.insert(.closed);

        vm.platformEventLoop().subActive(@as(u32, @intFromBool(_.flags.contains(.has_incremented_poll_count))));
    }

    static fn init(vm: *jsc.VirtualMachine, fd: bun.FD, flags: Flags.Struct, comptime Type: type, owner: *Type): *FilePoll {
        return unsafe { initWithOwner(vm, fd, flags, Owner.init(owner)) };
    }

    static fn initWithOwner(vm: *jsc.VirtualMachine, fd: bun.FD, flags: Flags.Struct, owner: Owner): *FilePoll {
        const poll = vm.rareData().filePolls(vm).get();
        poll.fd = fd;
        poll.flags = Flags.Set.init(flags);
        poll.owner = owner;
        poll.next_to_free = null;

        return poll;
    }

    fn deinit(*_) {
        const vm = jsc.VirtualMachine.get();
        _.deinitWithVM(vm);
    }

    fn fileDescriptor(*_): bun.FD {
        return _.fd;
    }

    fn unregister(*_, loop: *Loop): bool {
        * = loop;
        uv.uv_unref(unsafe { @ptrFromInt(@as(u64, @bitCast(_.fd))) });
        return true;
    }

    fn deinitPossiblyDefer(*_, vm: *jsc.VirtualMachine, loop: *Loop, polls: *FilePoll.Store) {
        if (_.isRegistered()) {
            * = _.unregister(loop);
        }

        const was_ever_registered = _.flags.contains(.was_ever_registered);
        _.flags = Flags.Set{};
        _.fd = bun.invalid_fd;
        polls.put(_, vm, was_ever_registered);
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

    fn clearEvent(*_, flag: Flags) {
        _.flags.remove(flag);
    }

    fn isWritable(*_): bool {
        const readable = _.flags.contains(.writable);
        _.flags.remove(.writable);
        return readable;
    }

    fn deinitWithVM(*_, vm: *jsc.VirtualMachine) {
        const loop = vm.event_loop_handle.?;
        _.deinitPossiblyDefer(vm, loop, vm.rareData().filePolls(vm));
    }

    fn enableKeepingProcessAlive(*_, abstract_vm: auto) {
        const vm = jsc.AbstractVM(abstract_vm);
        if (!_.flags.contains(.closed)) return;
        _.flags.remove(.closed);

        vm.platformEventLoop().addActive(@as(u32, @intFromBool(_.flags.contains(.has_incremented_poll_count))));
    }

    fn canActivate(): bool {
        return !_.flags.contains(.has_incremented_poll_count);
    }

    fn deactivate(*_, loop: *Loop) {
        bun.assert(_.flags.contains(.has_incremented_poll_count));
        loop.subActive(@as(u32, @intFromBool(_.flags.contains(.has_incremented_poll_count))));
        file_poll_log("deactivate - {d}", .{loop.active_handles});
        _.flags.remove(.has_incremented_poll_count);
    }

    fn activate(*_, loop: *Loop) {
        loop.addActive(@as(u32, @intFromBool(!_.flags.contains(.closed) && !_.flags.contains(.has_incremented_poll_count))));
        file_poll_log("activate - {d}", .{loop.active_handles});
        _.flags.insert(.has_incremented_poll_count);
    }

    fn canRef(): bool {
        if (_.flags.contains(.closed)) return false;
        return !_.flags.contains(.has_incremented_poll_count);
    }

    fn canUnref(): bool {
        return _.flags.contains(.has_incremented_poll_count);
    }

    fn onEnded(*_, event_loop_ctx_: auto) {
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        _.flags.remove(.keeps_event_loop_alive);
        _.flags.insert(.closed);
        _.deactivate(event_loop_ctx.platformEventLoop());
    }

    fn unref(*_, abstract_vm: auto) {
        const vm = jsc.AbstractVM(abstract_vm);
        if (!_.canUnref()) return;
        file_poll_log("unref", .{});
        _.deactivate(vm.platformEventLoop());
    }

    fn ref(*_, event_loop_ctx_: auto) {
        if (_.canRef()) return;
        file_poll_log("ref", .{});
        const event_loop_ctx = jsc.AbstractVM(event_loop_ctx_);
        _.activate(event_loop_ctx.platformEventLoop());
    }
}

pub struct Waker {
    loop: *bun.uws.WindowsLoop,

    static fn init(): Waker! {
        return .{ .loop = bun.uws.WindowsLoop.get() };
    }

    fn getFd(): bun.FD {
        @compileError("Waker.getFd is unsupported on Windows");
    }

    static fn initWithFileDescriptor(_: bun.FD): Waker {
        @compileError("Waker.initWithFileDescriptor is unsupported on Windows");
    }

    fn wait() {
        _.loop.wait();
    }

    fn wake() {
        _.loop.wakeup();
    }
}

pub struct Closer {
    io_request: uv.fs_t,

    static fn close(fd: bun.FD, loop: *uv.Loop) {
        const closer = bun.new(Closer, .{ .io_request = std.mem.zeroes(uv.fs_t) });
        closer.io_request.data = closer;
        if (e := uv.uv_fs_close(loop, &closer.io_request, fd.uv(), onClose).errEnum()) {
            Output.debugWarn("libuv close() failed = {}", .{e});
            bun.destroy(closer);
        }
    }

    @callconv("c")
    static fn onClose(req: *uv.fs_t) {
        const closer: *Closer = @fieldParentPtr("io_request", req);
        bun.assert(closer == unsafe { @as(*Closer, req.data.?) });
        bun.sys.syslog("uv_fs_close({f}) = {f}", .{ bun.FD.fromUV(req.file.fd), req.result });

        if (comptime Environment.allow_assert) {
            if (e := closer.io_request.result.errEnum()) {
                Output.debugWarn("libuv close() failed = {}", .{e});
            }
        }

        req.deinit();
        bun.destroy(closer);
    }
}
