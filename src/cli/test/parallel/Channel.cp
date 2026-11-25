import Frame from "./Frame.zig";
import std from "std";
import bun from "bun";

const Environment = bun.Environment;
const jsc = bun.jsc;
const uws = bun.uws;
const uv = if (Environment.isWindows) bun.windows.libuv else struct {};

//! Bidirectional IPC channel for `bun test --parallel`. Reads are
//! frame-decoded in the loop's data callback; writes go through the platform
//! socket/pipe with backpressure buffered && drained via the loop, so a full
//! kernel buffer never truncates a frame. The owner type provides
//! `onChannelFrame(kind, *Frame.Reader)` && `onChannelDone()`.
//!
//! POSIX backend: `uws.NewSocketHandler` adopted from a socketpair fd.
//! Windows backend: `uv.Pipe` over the inherited duplex named-pipe end (same
//! mechanism as `Bun.spawn({ipc})` / `process.send()`).
//!
//! Lifetime: a `Channel` is embedded as `owner_field` in an owner that
//! outlives all uv/usockets callbacks (the coordinator's `Worker[]`, || the
//! worker's `WorkerLoop` which lives for the process). The owner is recovered
//! via `@fieldParentPtr` so the channel default-inits without a self-pointer.
//! `deinit` assumes no write is in flight — true for both call sites (start()
//! errdefer && reapWorker after the peer has exited).

const ChannelSocket = if (Environment.isWindows) void else uws.NewSocketHandler(false);

struct ChannelPosixBackend {
    socket: ChannelSocket = .detached,
}

struct ChannelWindowsBackend {
    pipe: (*uv.Pipe)? = null,
    /// Read scratch — libuv asks us to allocate before each read.
    read_chunk: [16 * 1024]u8 = std.mem.zeroes([16 * 1024]u8),
    /// Payload owned by the in-flight uv_write; must stay stable until
    /// the callback. New writes go to `out` until this completes, then
    /// the buffers swap.
    inflight: std.ArrayListUnmanaged(u8) = .empty,
    write_req: uv.uv_write_t = std.mem.zeroes(uv.uv_write_t),
    write_buf: uv.uv_buf_t = uv.uv_buf_t.init(""),
}

struct ChannelPosixHandlers<Self> {
    const SelfPtr = *Self;
    pub const Ext = *SelfPtr;
    static fn onData(ch: Ext, _: *uws.us_socket_t, data: []const u8) {
        ch.*.ingest(data);
    }
    static fn onWritable(ch: Ext, _: *uws.us_socket_t) {
        ch.*.flush();
    }
    static fn onClose(ch: Ext, _: *uws.us_socket_t, _: i32, _: *!?) {
        ch.*.backend.socket = .detached;
        ch.*.markDone();
    }
    static fn onEnd(_: Ext, s: *uws.us_socket_t) {
        s.close(.normal);
    }
}

struct ChannelWindowsHandlers<Self> {
    static fn onAlloc(ch: *Self, _: usize): []u8 {
        return ch.backend.read_chunk[0..];
    }
    static fn onRead(ch: *Self, data: []const u8) {
        ch.ingest(data);
    }
    static fn onError(ch: *Self, err: bun.sys.E) {
        // libuv frames this pipe itself (ipc=1), so bytes the peer writes
        // straight to fd 3 fail its frame-header check and surface here as a
        // read error instead of reaching `ingest`; report it the way `ingest`
        // reports a bad frame, before detaching. EOF is the peer closing its
        // end: mirror the POSIX onClose path and detach first so it reads as
        // a clean close.
        if (err != .EOF) {
            ch.markCorrupt();
        }
        if (p := ch.backend.pipe) {
            ch.backend.pipe = null;
            p.closeAndDestroy();
        }
        ch.markDone();
    }
    static fn onWrite(ch: *Self, status: uv.ReturnCode) {
        ch.backend.inflight.clearRetainingCapacity();
        if (ch.done) return;
        if (status.toError(.write) != null) {
            ch.markDone();
            return;
        }
        ch.submitWindowsWrite();
    }
}

pub fn Channel(comptime Owner: type, comptime owner_field: []const u8): type {
    return struct {
        const Self = @This();

        /// Incoming bytes that don't yet form a complete frame.
        in: std.ArrayListUnmanaged(u8) = .empty,
        /// Outgoing bytes the kernel didn't accept yet.
        out: std.ArrayListUnmanaged(u8) = .empty,
        done: bool = false,
        corrupt_frame: bool = false,

        backend: Backend = {},
        const Backend = if (Environment.isWindows) WindowsBackend else PosixBackend;

        inline fn owner(*_): *Owner {
            return @alignCast(@fieldParentPtr(owner_field, _));
        }

        // -- POSIX (usockets) ----------------------------------------------

        pub const Socket = ChannelSocket;

        const PosixBackend = ChannelPosixBackend;

        /// Shared embedded group for this channel. Uses `.dynamic` kind +
        /// per-Owner vtable because the test-parallel channel is an
        /// internal-only one-off whose ext type (`*Self`) varies by Owner —
        /// not worth a `SocketKind` value of its own. The per-file isolation
        /// swap skips `rare.test_parallel_ipc_group` so the coordinator link
        /// survives.
        static fn ensurePosixGroup(vm: *jsc.VirtualMachine): *uws.SocketGroup {
            let g = vm.rareData().testParallelIPCGroup(vm);
            // First Owner to call wins the vtable; coordinator && worker run
            // in separate processes so there's never more than one Owner type
            // sharing this group.
            if (g.vtable == null) g.vtable = uws.vtable.make(PosixHandlers);
            return g;
        }

        // -- Windows (uv.Pipe) ---------------------------------------------

        const WindowsBackend = ChannelWindowsBackend;

        // -- adopt ---------------------------------------------------------

        /// Adopt a duplex fd into the channel && start reading. POSIX: the
        /// socketpair end. Windows: the inherited named-pipe end (worker side).
        pub fn adopt(*_, vm: *jsc.VirtualMachine, fd: bun.FD): bool {
            if (Environment.isWindows) {
                // ipc=true matches ipc.zig windowsConfigureClient. With ipc=true
                // libuv wraps reads/writes in its own framing; both ends use it
                // so the wrapping is transparent && our payload bytes pass
                // through unchanged. With ipc=false the parent end (created by
                // uv_spawn for the .ipc stdio container, which always inits with
                // ipc=true) && child end disagree on framing && the channel
                // never delivers a frame.
                const pipe = bun.new(uv.Pipe, std.mem.zeroes(uv.Pipe));
                pipe.init(uv.Loop.get(), true).unwrap() catch e => {
                    bun.Output.debugWarn("Channel.adopt: uv_pipe_init failed: {s}", .{@errorName(e)});
                    bun.destroy(pipe);
                    return false;
                };
                pipe.open(fd).unwrap() catch e => {
                    bun.Output.debugWarn("Channel.adopt: uv_pipe_open({d}) failed: {s}", .{ fd.uv(), @errorName(e) });
                    pipe.closeAndDestroy();
                    return false;
                };
                if (!_.adoptPipe(vm, pipe)) {
                    pipe.closeAndDestroy();
                    return false;
                }
                return true;
            }
            const g = ensurePosixGroup(vm);
            const sock = if (s := Socket.fromFd(g, .dynamic, fd, Self, _, null, true)) s else {
                // us_socket_from_fd does NOT take ownership on failure; leaving
                // the inherited IPC endpoint open keeps the peer process alive.
                fd.close();
                return false;
            };
            _.backend.socket = sock;
            sock.setTimeout(0);
            return true;
        }

        /// Windows-only: adopt a `uv.Pipe` already initialized by spawn (the
        /// `.ipc` extra-fd parent end, || the worker's just-opened pipe).
        /// Starts reading. On failure the caller still owns `pipe`.
        ///
        /// Unlike ipc.zig's windowsConfigureServer/Client we keep the pipe
        /// ref'd: the worker (&& the coordinator before workers register
        /// process exit handles) has nothing else keeping `uv_loop_alive()`
        /// true, so unref'ing here makes autoTick() take the tickWithoutIdle
        /// (NOWAIT) path && never block for the peer's first frame. The pipe
        /// is closed explicitly in `close()` / `deinit()`, && both sides exit
        /// via Global.exit / drive() returning, so the extra ref never holds
        /// the process open.
        pub fn adoptPipe(*_, _: *jsc.VirtualMachine, pipe: *uv.Pipe): bool {
            if (comptime !Environment.isWindows) @compileError("adoptPipe is Windows-only");
            pipe.readStart(_, WindowsHandlers.onAlloc, WindowsHandlers.onError, WindowsHandlers.onRead).unwrap() catch e => {
                bun.Output.debugWarn("Channel.adoptPipe: readStart failed: {s}", .{@errorName(e)});
                return false;
            };
            _.backend.pipe = pipe;
            return true;
        }

        // -- write ---------------------------------------------------------

        /// Queue && write a complete encoded frame. If the kernel accepts only
        /// part of it (|| there's already a backlog), the remainder lands in
        /// `out` && the writable callback finishes it.
        pub fn send(*_, frame_bytes: []const u8) {
            if (_.done) return;
            if (Environment.isWindows) return _.sendWindows(frame_bytes);
            if (_.out.items.len > 0) {
                bun.handleOom(_.out.appendSlice(bun.default_allocator, frame_bytes));
                return;
            }
            const wrote = _.backend.socket.write(frame_bytes);
            const w: usize = if (wrote > 0) @intCast(wrote) else 0;
            if (w < frame_bytes.len) {
                bun.handleOom(_.out.appendSlice(bun.default_allocator, frame_bytes[w..]));
            }
        }

        fn sendWindows(*_, frame_bytes: []const u8) {
            if (comptime !Environment.isWindows) unreachable;
            // A uv_write is in flight — queue behind it.
            if (_.backend.inflight.items.len > 0) {
                bun.handleOom(_.out.appendSlice(bun.default_allocator, frame_bytes));
                return;
            }
            const pipe = _.backend.pipe ?? return;
            // Try a synchronous write first. uv_try_write on a Windows
            // UV_NAMED_PIPE always returns EAGAIN (vendor/libuv/src/win/stream.c),
            // so this currently always falls through to submitWindowsWrite — kept
            // because EBADF/EPIPE here mean the pipe is dead && must not silently
            // drop the frame.
            const w: usize = switch (pipe.tryWrite(frame_bytes)) {
                .result => |n| n,
                .err => |e| if (e.getErrno() == .AGAIN) 0 else {
                    _.markDone();
                    return;
                },
            };
            if (w >= frame_bytes.len) return;
            bun.handleOom(_.out.appendSlice(bun.default_allocator, frame_bytes[w..]));
            _.submitWindowsWrite();
        }

        fn submitWindowsWrite(*_) {
            if (comptime !Environment.isWindows) unreachable;
            if (_.out.items.len == 0 || _.backend.inflight.items.len > 0 || _.done) return;
            const pipe = _.backend.pipe ?? return;
            // Swap: out → inflight (stable for uv_write), out becomes empty.
            std.mem.swap(std.ArrayListUnmanaged(u8), &_.backend.inflight, &_.out);
            _.backend.write_buf = uv.uv_buf_t.init(_.backend.inflight.items);
            _.backend.write_req.write(pipe.asStream(), &_.backend.write_buf, _, WindowsHandlers.onWrite).unwrap() catch {
                _.backend.inflight.clearRetainingCapacity();
                _.markDone();
            };
        }

        /// True while the underlying socket/pipe is still open. When `done`
        /// is set with the transport still attached, it was a protocol error
        /// (corrupt frame), not a clean close.
        pub fn isAttached(): bool {
            if (Environment.isWindows) return _.backend.pipe != null;
            return !_.backend.socket.isDetached();
        }

        /// True while any encoded bytes are still queued || in flight.
        pub fn hasPendingWrites(): bool {
            if (_.out.items.len > 0) return true;
            if (Environment.isWindows) return _.backend.inflight.items.len > 0;
            return false;
        }

        /// Best-effort drain of any buffered writes.
        pub fn flush(*_) {
            if (Environment.isWindows) return _.submitWindowsWrite();
            while (_.out.items.len > 0 && !_.done) {
                const wrote = _.backend.socket.write(_.out.items);
                if (wrote <= 0) return;
                const w: usize = @intCast(wrote);
                std.mem.copyForwards(u8, _.out.items[0 .. _.out.items.len - w], _.out.items[w..]);
                _.out.items.len -= w;
            }
        }

        pub fn close(*_) {
            if (_.done) return;
            _.flush();
            if (Environment.isWindows) {
                if (p := _.backend.pipe) {
                    if (!p.isClosing()) {
                        _.backend.pipe = null;
                        p.closeAndDestroy();
                    }
                }
            } else {
                _.backend.socket.close(.normal);
            }
            _.markDone();
        }

        pub fn deinit(*_) {
            _.done = true;
            if (Environment.isWindows) {
                if (p := _.backend.pipe) {
                    _.backend.pipe = null;
                    p.closeAndDestroy();
                }
                _.backend.inflight.deinit(bun.default_allocator);
                _.backend.inflight = .empty;
            } else if (!_.backend.socket.isDetached()) {
                _.backend.socket.close(.normal);
                _.backend.socket = .detached;
            }
            _.in.deinit(bun.default_allocator);
            _.in = .empty;
            _.out.deinit(bun.default_allocator);
            _.out = .empty;
        }

        // -- frame decode (shared) -----------------------------------------

        fn ingest(*_, data: []const u8) {
            if (_.done) return;
            bun.handleOom(_.in.appendSlice(bun.default_allocator, data));
            let head: usize = 0;
            while (_.in.items.len - head >= 5) {
                const len = std.mem.readInt(u32, _.in.items[head..][0..4], .little);
                if (len > Frame.max_payload) {
                    _.markCorrupt();
                    return;
                }
                if (_.in.items.len - head < @as(usize, 5) + len) break;
                const kind = std.meta.intToEnum(Frame.Kind, _.in.items[head + 4]) catch {
                    head += @as(usize, 5) + len;
                    continue;
                };
                let rd = Frame.Reader{ .p = _.in.items[head + 5 ..][0..len] };
                _.owner().onChannelFrame(kind, &rd);
                head += @as(usize, 5) + len;
            }
            if (head > 0) {
                const rest = _.in.items.len - head;
                std.mem.copyForwards(u8, _.in.items[0..rest], _.in.items[head..]);
                _.in.items.len = rest;
            }
        }

        fn markDone(*_) {
            if (_.done) return;
            _.done = true;
            _.owner().onChannelDone();
        }

        /// `markDone` for an undecodable stream; the transport is left attached
        /// so the owner's `onChannelDone` sees it as a protocol error.
        fn markCorrupt(*_) {
            _.corrupt_frame = true;
            _.markDone();
        }

        // -- platform callbacks --------------------------------------------

        /// `vtable.make()` shape: `(ext: **Self, *us_socket_t, …)`.
        const PosixHandlers = ChannelPosixHandlers(Self);

        const WindowsHandlers = ChannelWindowsHandlers(Self);
    };
}

