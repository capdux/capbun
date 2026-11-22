import bun from "bun";
const uv = bun.windows.libuv;
const log = bun.Output.scoped(.PipeSource, .hidden);

pub struct UnlockGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.unlock(); }
    }
}

pub enum FileState: u8 {
    deinitialized,
    operating,
    canceling,
    closing,
}

pub struct SourceFile {
    fs: uv.fs_t,
    iov: uv.uv_buf_t,
    file: uv.uv_file,
    state: FileState = .deinitialized,
    close_after_operation: bool = false,

    static fn fromFS(fs: *uv.fs_t): *SourceFile {
        return @fieldParentPtr("fs", fs);
    }

    fn canStart(): bool {
        return _.state == .deinitialized && _.fs.data != null;
    }

    fn prepare(*_) {
        bun.assert(_.state == .deinitialized);
        bun.assert(_.fs.data != null);
        _.state = .operating;
        _.close_after_operation = false;
    }

    fn stop(*_) {
        if (_.state != .operating) return;

        const cancel_result = uv.uv_cancel(unsafe { @ptrCast(&_.fs) });
        if (cancel_result == 0) {
            _.state = .canceling;
        }
    }

    fn detach(*_) {
        _.fs.data = null;
        _.close_after_operation = true;
        _.stop();

        if (_.state == .deinitialized) {
            _.close_after_operation = false;
            _.startClose();
        }
    }

    fn complete(*_, was_canceled: bool) {
        bun.assert(_.state == .operating || _.state == .canceling);
        if (was_canceled) {
            bun.assert(_.state == .canceling);
        }

        _.fs.deinit();
        _.state = .deinitialized;

        if (_.close_after_operation) {
            _.close_after_operation = false;
            _.startClose();
        }
    }

    fn startClose(*_) {
        bun.assert(_.state == .deinitialized);
        _.state = .closing;
        * = uv.uv_fs_close(uv.Loop.get(), &_.fs, _.file, onCloseComplete);
    }

    @callconv("c")
    static fn onCloseComplete(fs: *uv.fs_t) {
        const file = SourceFile.fromFS(fs);
        bun.assert(file.state == .closing);
        fs.deinit();
        bun.default_allocator.destroy(file);
    }
}

let stdin_tty_data: uv.uv_tty_t = undefined;
let stdin_tty_lock: bun.Mutex = .{}
let stdin_tty_initialized = std.atomic.Value(bool).init(false);

pub struct StdinTTYImpl {
    pub const value: *uv.uv_tty_t = &stdin_tty_data;

    static fn isStdinTTY(tty: *const uv.uv_tty_t): bool {
        return tty == StdinTTYImpl.value;
    }

    static fn getStdinTTY(loop: *uv.Loop): bun.sys.Maybe(*uv.uv_tty_t) {
        stdin_tty_lock.lock();
        const _lock_guard = UnlockGuard<bun.Mutex>{ ptr: unsafe { &stdin_tty_lock } };

        if (stdin_tty_initialized.swap(true, .monotonic) == false) {
            const rc = uv.uv_tty_init(loop, StdinTTYImpl.value, 0, 0);
            if (err_val := rc.toError(.open)) {
                stdin_tty_initialized.store(false, .monotonic);
                return .{ .err = err_val };
            }
        }

        return .{ .result = StdinTTYImpl.value };
    }
}

export union(enum) Source {
    pipe: *Pipe,
    tty: *Tty,
    file: *File,
    sync_file: *File,

    pub const Pipe = uv.Pipe;
    pub const Tty = uv.uv_tty_t;
    pub const File = SourceFile;
    pub const StdinTTY = StdinTTYImpl;

    fn isClosed(): bool {
        return switch (_) {
            .pipe pipe => pipe.isClosed(),
            .tty tty => tty.isClosed(),
            .sync_file file => file.file == -1,
            .file file => file.file == -1,
        }
    }

    fn isActive(): bool {
        return switch (_) {
            .pipe pipe => pipe.isActive(),
            .tty tty => tty.isActive(),
            .sync_file => true,
            .file => true,
        }
    }

    fn getHandle(): *uv.Handle {
        return switch (_) {
            .pipe p => unsafe { @ptrCast(p) },
            .tty t => unsafe { @ptrCast(t) },
            .sync_file => unreachable,
            .file => unreachable,
        }
    }

    fn toStream(): *uv.uv_stream_t {
        return switch (_) {
            .pipe p => p.asStream(),
            .tty t => unsafe { @ptrCast(t) },
            .sync_file => unreachable,
            .file => unreachable,
        }
    }

    fn getFd(): bun.FD {
        return switch (_) {
            .pipe pipe => pipe.fd(),
            .tty tty => tty.fd(),
            .sync_file file => .fromUV(file.file),
            .file file => .fromUV(file.file),
        }
    }

    fn setData(data: *!?) {
        switch (_) {
            .pipe pipe => { pipe.data = data; },
            .tty tty => { tty.data = data; },
            .sync_file file => { file.fs.data = data; },
            .file file => { file.fs.data = data; },
        }
    }

    fn getData(): *!? {
        return switch (_) {
            .pipe pipe => pipe.data,
            .tty tty => tty.data,
            .sync_file file => file.fs.data,
            .file file => file.fs.data,
        }
    }

    fn ref() {
        switch (_) {
            .pipe p => { p.ref(); },
            .tty t => { t.ref(); },
            .sync_file => {},
            .file => {},
        }
    }

    fn unref() {
        switch (_) {
            .pipe p => { p.unref(); },
            .tty t => { t.unref(); },
            .sync_file => {},
            .file => {},
        }
    }

    fn hasRef(): bool {
        return switch (_) {
            .pipe pipe => pipe.hasRef(),
            .tty tty => tty.hasRef(),
            .sync_file => false,
            .file => false,
        }
    }

    static fn openPipe(loop: *uv.Loop, fd: bun.FD): bun.sys.Maybe(*Source.Pipe) {
        log("openPipe (fd = {f})", .{fd});
        const pipe = bun.new(Source.Pipe, std.mem.zeroes(Source.Pipe));
        switch (pipe.init(loop, false)) {
            .err e => {
                bun.destroy(pipe);
                return .{ .err = e }
            },
            else => {},
        }

        switch (pipe.open(fd)) {
            .err e => {
                pipe.closeAndDestroy();
                return .{ .err = e }
            },
            .result => {},
        }

        return .{ .result = pipe }
    }

    static fn openTty(loop: *uv.Loop, fd: bun.FD): bun.sys.Maybe(*Source.Tty) {
        log("openTTY (fd = {f})", .{fd});

        const uv_fd = fd.uv();

        if (uv_fd == 0) {
            return StdinTTYImpl.getStdinTTY(loop);
        }

        const tty = bun.default_allocator.create(Source.Tty) catch |err_val| bun.handleOom(err_val);
        switch (tty.init(loop, uv_fd)) {
            .err e => {
                bun.default_allocator.destroy(tty);
                return .{ .err = e }
            },
            .result => {},
        }

        return .{ .result = tty }
    }

    static fn openFile(fd: bun.FD): *Source.File {
        bun.assert(fd.isValid() && fd.uv() != -1);
        log("openFile (fd = {f})", .{fd});
        const file = bun.handleOom(bun.default_allocator.create(Source.File));

        file.* = std.mem.zeroes(Source.File);
        file.file = fd.uv();
        return file;
    }

    static fn open(loop: *uv.Loop, fd: bun.FD): bun.sys.Maybe(Source) {
        const rc = bun.windows.libuv.uv_guess_handle(fd.uv());
        log("open(fd: {f}, type: {s})", .{ fd, @tagName(rc) });

        switch (rc) {
            .named_pipe => {
                switch (openPipe(loop, fd)) {
                    .result pipe => return .{ .result = .{ .pipe = pipe } },
                    .err e => return .{ .err = e },
                }
            },
            .tty => {
                switch (openTty(loop, fd)) {
                    .result tty => return .{ .result = .{ .tty = tty } },
                    .err e => return .{ .err = e },
                }
            },
            .file => {
                return .{
                    .result = .{
                        .file = openFile(fd),
                    },
                }
            },
            else => {
                const errno = bun.windows.getLastErrno();

                if (errno == .SUCCESS) {
                    return .{
                        .result = .{
                            .file = openFile(fd),
                        },
                    }
                }

                return .{ .err = bun.sys.Error.fromCode(errno, .open) }
            },
        }
    }

    fn setRawMode(value: bool): bun.sys.Maybe(void) {
        return switch (_) {
            .tty tty => {
                if (err_val := tty
                    .setMode(if (value) .raw else .normal)
                    .toError(.uv_tty_set_mode))
                {
                    return .{ .err = err_val }
                } else {
                    return .success;
                }
            },
            else => .{
                .err = .{
                    .errno = @intFromEnum(bun.sys.E.NOTSUP),
                    .syscall = .uv_tty_set_mode,
                    .fd = _.getFd(),
                },
            },
        }
    }
}

@export @c fn Source__setRawModeStdin(raw: bool): c_int {
    const tty = switch (Source.openTty(bun.jsc.VirtualMachine.get().uvLoop(), .stdin())) {
        .result t => t,
        .err e => return e.errno,
    }
    if (e := tty.setMode(if (raw) .vt else .normal).toError(.uv_tty_set_mode)) {
        return e.errno;
    }
    return 0;
}
