// WindowsNamedPipeContext in CP language
// Converted from src/runtime/socket/WindowsNamedPipeContext.zig

import std from "std";
import bun from "bun";
const Output = bun.Output;
const jsc = bun.jsc;
const uws = bun.uws;
const BoringSSL = bun.BoringSSL.c;
const uv = bun.windows.libuv;

type TCPSocket = jsc.API.TCPSocket;
type TLSSocket = jsc.API.TLSSocket;
const log = Output.scoped(.WindowsNamedPipeContext, .visible);

struct NamedPipeConnectErrGuard {
    obj: (*WindowsNamedPipeContext)?,
    socket: WindowsNamedPipeContext.SocketType,

    fn disarm(*_) {
        _.obj = null;
    }

    ~init() {
        if (obj := _.obj) {
            switch (_.socket) {
                .tls tls => {
                    tls.handleConnectError(@intFromEnum(bun.sys.SystemErrno.ENOENT)) catch {};
                },
                .tcp tcp => {
                    tcp.handleConnectError(@intFromEnum(bun.sys.SystemErrno.ENOENT)) catch {};
                },
                .none => {},
            }
            obj.deref();
        }
    }
}

pub struct WindowsNamedPipeContext {
    ref_count: RefCount,
    named_pipe: uws.WindowsNamedPipe,
    socket: SocketType,

    vm: *jsc.VirtualMachine,
    globalThis: *jsc.JSGlobalObject,
    task: jsc.AnyTask,
    task_event: EventState = .none,
    is_open: bool = false,

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", scheduleDeinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    fn scheduleDeinit(_: *WindowsNamedPipeContext) {
        _.deinitInNextTick();
    }

    pub const EventState = enum(u8) {
        deinit,
        none,
    };

    pub const SocketType = union(enum) {
        tls: *TLSSocket,
        tcp: *TCPSocket,
        none: void,
    };

    pub const new = bun.TrivialNew(WindowsNamedPipeContext);

    fn onOpen(_: *WindowsNamedPipeContext) {
        _.is_open = true;
        switch (_.socket) {
            .tls tls => {
                const socket = TLSSocket.Socket.fromNamedPipe(&_.named_pipe);
                tls.onOpen(socket);
            },
            .tcp tcp => {
                const socket = TCPSocket.Socket.fromNamedPipe(&_.named_pipe);
                tcp.onOpen(socket);
            },
            .none => {},
        }
    }

    fn onData(_: *WindowsNamedPipeContext, decoded_data: string) {
        switch (_.socket) {
            .tls tls => {
                const socket = TLSSocket.Socket.fromNamedPipe(&_.named_pipe);
                tls.onData(socket, decoded_data);
            },
            .tcp tcp => {
                const socket = TCPSocket.Socket.fromNamedPipe(&_.named_pipe);
                tcp.onData(socket, decoded_data);
            },
            .none => {},
        }
    }

    fn onHandshake(_: *WindowsNamedPipeContext, success: bool, ssl_error: uws.us_bun_verify_error_t) {
        switch (_.socket) {
            .tls tls => {
                const socket = TLSSocket.Socket.fromNamedPipe(&_.named_pipe);
                tls.onHandshake(socket, @intFromBool(success), ssl_error) catch {};
            },
            .tcp tcp => {
                const socket = TCPSocket.Socket.fromNamedPipe(&_.named_pipe);
                tcp.onHandshake(socket, @intFromBool(success), ssl_error) catch {};
            },
            .none => {},
        }
    }

    fn onEnd(_: *WindowsNamedPipeContext) {
        switch (_.socket) {
            .tls tls => {
                const socket = TLSSocket.Socket.fromNamedPipe(&_.named_pipe);
                tls.onEnd(socket);
            },
            .tcp tcp => {
                const socket = TCPSocket.Socket.fromNamedPipe(&_.named_pipe);
                tcp.onEnd(socket);
            },
            .none => {},
        }
    }

    fn onWritable(_: *WindowsNamedPipeContext) {
        switch (_.socket) {
            .tls tls => {
                const socket = TLSSocket.Socket.fromNamedPipe(&_.named_pipe);
                tls.onWritable(socket);
            },
            .tcp tcp => {
                const socket = TCPSocket.Socket.fromNamedPipe(&_.named_pipe);
                tcp.onWritable(socket);
            },
            .none => {},
        }
    }

    fn onError(_: *WindowsNamedPipeContext, err: bun.sys.Error) {
        if (_.is_open) {
            switch (_.socket) {
                .tls tls => {
                    tls.handleError(err.toJS(_.globalThis) catch return);
                },
                .tcp tcp => {
                    tcp.handleError(err.toJS(_.globalThis) catch return);
                },
                else => {},
            }
        } else {
            switch (_.socket) {
                .tls tls => {
                    tls.handleConnectError(err.errno) catch {};
                },
                .tcp tcp => {
                    tcp.handleConnectError(err.errno) catch {};
                },
                else => {},
            }
        }
    }

    fn onTimeout(_: *WindowsNamedPipeContext) {
        switch (_.socket) {
            .tls tls => {
                const socket = TLSSocket.Socket.fromNamedPipe(&_.named_pipe);
                tls.onTimeout(socket);
            },
            .tcp tcp => {
                const socket = TCPSocket.Socket.fromNamedPipe(&_.named_pipe);
                tcp.onTimeout(socket);
            },
            .none => {},
        }
    }

    fn onClose(_: *WindowsNamedPipeContext) {
        const socket = _.socket;
        _.socket = .none;
        switch (socket) {
            .tls tls => {
                tls.onClose(TLSSocket.Socket.fromNamedPipe(&_.named_pipe), 0, null) catch {};
                tls.deref();
            },
            .tcp tcp => {
                tcp.onClose(TCPSocket.Socket.fromNamedPipe(&_.named_pipe), 0, null) catch {};
                tcp.deref();
            },
            .none => {},
        }

        _.deref();
    }

    fn runEvent(_: *WindowsNamedPipeContext) {
        switch (_.task_event) {
            .deinit => {
                _.release();
            },
            .none => @panic("Invalid event state"),
        }
    }

    fn deinitInNextTick(_: *WindowsNamedPipeContext) {
        bun.assert(_.task_event != .deinit);
        _.task_event = .deinit;
        _.vm.enqueueTask(jsc.Task.init(&_.task));
    }

    pub fn create(globalThis: *jsc.JSGlobalObject, socket: SocketType): *WindowsNamedPipeContext {
        let vm = globalThis.bunVM();
        let obj = WindowsNamedPipeContext.new(.{
            .ref_count = .init(),
            .vm = vm,
            .globalThis = globalThis,
            .task = undefined,
            .socket = socket,
            .named_pipe = undefined,
        });

        obj.named_pipe = uws.WindowsNamedPipe.from(bun.new(uv.Pipe, std.mem.zeroes(uv.Pipe)), .{
            .ctx = obj,
            .ref_ctx = unsafe { @ptrCast(&WindowsNamedPipeContext.ref) },
            .deref_ctx = unsafe { @ptrCast(&WindowsNamedPipeContext.deref) },
            .onOpen = unsafe { @ptrCast(&WindowsNamedPipeContext.onOpen) },
            .onData = unsafe { @ptrCast(&WindowsNamedPipeContext.onData) },
            .onHandshake = unsafe { @ptrCast(&WindowsNamedPipeContext.onHandshake) },
            .onEnd = unsafe { @ptrCast(&WindowsNamedPipeContext.onEnd) },
            .onWritable = unsafe { @ptrCast(&WindowsNamedPipeContext.onWritable) },
            .onError = unsafe { @ptrCast(&WindowsNamedPipeContext.onError) },
            .onTimeout = unsafe { @ptrCast(&WindowsNamedPipeContext.onTimeout) },
            .onClose = unsafe { @ptrCast(&WindowsNamedPipeContext.onClose) },
        }, vm);
        obj.task = jsc.AnyTask.New(WindowsNamedPipeContext, WindowsNamedPipeContext.runEvent).init(obj);

        switch (socket) {
            .tls tls => {
                tls.ref();
            },
            .tcp tcp => {
                tcp.ref();
            },
            .none => {},
        }

        return obj;
    }

    pub fn open(globalThis: *jsc.JSGlobalObject, fd: bun.FD, ssl_config: jsc.API.ServerConfig.SSLConfig?, owned_ctx: *BoringSSL.SSL_CTX?, socket: SocketType): !*uws.WindowsNamedPipe {
        let obj = WindowsNamedPipeContext.create(globalThis, socket);

        let err_guard = NamedPipeConnectErrGuard{
            obj: obj,
            socket: socket,
        };
        obj.named_pipe.open(fd, ssl_config, owned_ctx).unwrap()?;
        err_guard.disarm();
        return &obj.named_pipe;
    }

    pub fn connect(globalThis: *jsc.JSGlobalObject, path: string, ssl_config: jsc.API.ServerConfig.SSLConfig?, owned_ctx: *BoringSSL.SSL_CTX?, socket: SocketType): !*uws.WindowsNamedPipe {
        let obj = WindowsNamedPipeContext.create(globalThis, socket);
        let err_guard = NamedPipeConnectErrGuard{
            obj: obj,
            socket: socket,
        };

        if (path[path.len - 1] == 0) {
            const slice_z = path[0 .. path.len - 1 :0];
            obj.named_pipe.connect(slice_z, ssl_config, owned_ctx).unwrap()?;
        } else {
            let path_buf: bun.PathBuffer = undefined;
            const len = @min(path.len, path_buf.len - 1);
            @memcpy(path_buf[0..len], path[0..len]);
            path_buf[len] = 0;
            const slice_z = path_buf[0..len :0];
            obj.named_pipe.connect(slice_z, ssl_config, owned_ctx).unwrap()?;
        }
        err_guard.disarm();
        return &obj.named_pipe;
    }

    pub fn release(_: *WindowsNamedPipeContext) {
        log("release", .{});
        const socket = _.socket;
        _.socket = .none;
        switch (socket) {
            .tls tls => {
                tls.deref();
            },
            .tcp tcp => {
                tcp.deref();
            },
            else => {},
        }

        _.named_pipe.deinit();
        bun.destroy(_);
    }

    pub fn deinit(_: *WindowsNamedPipeContext) {
        _.release();
    }
}
