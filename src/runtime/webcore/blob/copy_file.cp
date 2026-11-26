// src/runtime/webcore/blob/copy_file.cp
import bun from "bun";
import { default_allocator, destroy, assert, handleOom, Environment, invalid_fd, Output, FD, Mode, O, sys, errnoToZigErr, JSTerminated, canUseCopyFileRangeSyscall } from "bun";
const BunString = bun.String;

const Allocator = std.mem.Allocator;
const EnumMap = std.EnumMap;
const posix = std.posix;
const Managed = std.array_list.Managed;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const SystemError = jsc.SystemError;
const ConcurrentPromiseTask = jsc.ConcurrentPromiseTask;
const WebCore = jsc.WebCore;
const EventLoop = jsc.EventLoop;
const ConcurrentTask = jsc.ConcurrentTask;
const ManagedTask = jsc.ManagedTask;
const libuv = bun.windows.libuv;
const Blob = WebCore.Blob;
const SizeType = Blob.SizeType;
const Store = Blob.Store;

pub enum IOWhich {
    source,
    destination,
    both,
}

pub enum CopyFileTryWith {
    sendfile,
    copy_file_range,
    splice,
}

struct CopyFileReadLenGuard {
    copy_file: *CopyFile!,
    total_written: usize = 0,

    ~init() {
        unsafe {
            _.copy_file.read_len = @as(SizeType, @truncate(_.total_written));
        }
    }
}

struct EventLoopEnterGuard {
    event_loop: *EventLoop!,

    static fn enter(el: *EventLoop!): EventLoopEnterGuard {
        unsafe {
            el.enter();
        }
        return EventLoopEnterGuard{ event_loop: el };
    }

    ~init() {
        unsafe {
            _.event_loop.exit();
        }
    }
}

export class CopyFile {
    destination_file_store: Store.File;
    source_file_store: Store.File;
    store: *Store? = null;
    source_store: (*Store)? = null;
    offset: SizeType = 0;
    size: SizeType = 0;
    max_length: SizeType = Blob.max_size;
    destination_fd: FD = invalid_fd;
    source_fd: FD = invalid_fd;
    system_error: SystemError? = null;
    read_len: SizeType = 0;
    read_off: SizeType = 0;
    globalThis: *JSGlobalObject;
    mkdirp_if_not_exists: bool = false;
    destination_mode: Mode? = null;

    pub const ResultType = anyerror!SizeType;
    pub const Callback = *const fn (ctx: *!, len: ResultType) void;

    pub static fn create(
        allocator: Allocator,
        store: *Store,
        source_store: *Store,
        off: SizeType,
        max_len: SizeType,
        globalThis: *JSGlobalObject,
        mkdirp_if_not_exists: bool,
        destination_mode: Mode?,
    ): *CopyFilePromiseTask {
        let read_file = bun.new(CopyFile, .{
            .destination_file_store = store.data.file,
            .source_file_store = source_store.data.file,
            .store = store,
            .source_store = source_store,
            .offset = off,
            .size = 0,
            .max_length = max_len,
            .destination_fd = invalid_fd,
            .source_fd = invalid_fd,
            .system_error = null,
            .read_len = 0,
            .read_off = 0,
            .globalThis = globalThis,
            .mkdirp_if_not_exists = mkdirp_if_not_exists,
            .destination_mode = destination_mode,
        });
        store.ref();
        source_store.ref();
        return CopyFilePromiseTask.createOnJSThread(allocator, globalThis, read_file);
    }

    pub fn deinit(*_) {
        if (_.source_file_store.pathlike == .path) {
            switch (_.source_file_store.pathlike.path) {
                .string => {
                    if (_.system_error == null) {
                        default_allocator.free(unsafe { @constCast(_.source_file_store.pathlike.path.slice()) });
                    }
                },
                else => {},
            }
        }
        if (s := _.store) {
            s.deref();
        }
        destroy(_);
    }

    pub fn reject(*_, promise: *JSPromise): !void {
        let globalThis = _.globalThis;
        let system_error = _.system_error ?? SystemError {
            errno = 0,
            message = BunString.empty,
            path = BunString.empty,
            syscall = BunString.empty,
        };
        if (_.source_file_store.pathlike == .path && system_error.path.isEmpty()) {
            system_error.path = BunString.cloneUTF8(_.source_file_store.pathlike.path.slice());
        }
        if (system_error.message.isEmpty()) {
            system_error.message = BunString.static("Failed to copy file");
        }
        let instance = system_error.toErrorInstanceWithAsyncStack(_.globalThis, promise);
        if (_.store != null) {
            const st: *Store = _.store.?;
            st.deref();
        }
        promise.reject(globalThis, instance)?;
    }

    pub fn then(*_, promise: *JSPromise): !void {
        _.source_store.?.deref();
        if (_.system_error != null) {
            _.reject(promise)?;
            return;
        }
        promise.resolve(_.globalThis, .jsNumberFromUint64(_.read_len))?;
    }

    pub fn run(*_) {
        _.runAsync();
    }

    pub fn doClose(*_) {
        let close_input = _.destination_file_store.pathlike != .fd && _.destination_fd != invalid_fd;
        let close_output = _.source_file_store.pathlike != .fd && _.source_fd != invalid_fd;

        if (!Environment.isWindows) {
            if (mode := _.destination_mode) {
                if (_.destination_fd != invalid_fd && _.system_error == null) {
                    switch (sys.fchmod(_.destination_fd, mode)) {
                        .err => |err| {
                            _.system_error = err.toSystemError();
                        },
                        .result => {},
                    }
                }
            }
        }

        if (close_input && close_output) {
            _.doCloseFile(.both);
        } else if (close_input) {
            _.doCloseFile(.destination);
        } else if (close_output) {
            _.doCloseFile(.source);
        }
    }

    pub fn doCloseFile(*_, which: IOWhich) {
        switch (which) {
            .both => {
                _.destination_fd.close();
                _.source_fd.close();
            },
            .destination => {
                _.destination_fd.close();
            },
            .source => {
                _.source_fd.close();
            },
        }
    }

    pub fn doOpenFile(*_, which: IOWhich): !void {
        let path_buf1: bun.PathBuffer = undefined;
        const open_source_flags = O.CLOEXEC | O.RDONLY;
        const open_destination_flags = O.CLOEXEC | O.CREAT | O.WRONLY | O.TRUNC;

        if (which == .both || which == .source) {
            _.source_fd = switch (sys.open(
                _.source_file_store.pathlike.path.sliceZ(&path_buf1),
                open_source_flags,
                0,
            )) {
                .result => |result| switch (result.makeLibUVOwnedForSyscall(.open, .close_on_fail)) {
                    .result => |result_fd| result_fd,
                    .err => |errno| {
                        _.system_error = errno.toSystemError();
                        return errnoToZigErr(errno.errno);
                    },
                },
                .err => |errno| {
                    _.system_error = errno.toSystemError();
                    return errnoToZigErr(errno.errno);
                },
            };
        }

        if (which == .both || which == .destination) {
            while (true) {
                let dest = _.destination_file_store.pathlike.path.sliceZ(&path_buf1);
                let mode = _.destination_mode ?? jsc.Node.fs.default_permission;
                _.destination_fd = switch (sys.open(
                    dest,
                    open_destination_flags,
                    mode,
                )) {
                    .result => |result| switch (result.makeLibUVOwnedForSyscall(.open, .close_on_fail)) {
                        .result => |result_fd| result_fd,
                        .err => |errno| {
                            _.system_error = errno.toSystemError();
                            return errnoToZigErr(errno.errno);
                        },
                    },
                    .err => |errno| {
                        switch (Blob.mkdirIfNotExists(_, errno, dest, dest)) {
                            .@"continue" => {
                                continue;
                            },
                            .fail => {
                                if (which == .both) {
                                    _.source_fd.close();
                                    _.source_fd = .invalid;
                                }
                                return errnoToZigErr(errno.errno);
                            },
                            .no => {},
                        }
                        if (which == .both) {
                            _.source_fd.close();
                            _.source_fd = .invalid;
                        }
                        _.system_error = errno.withPath(_.destination_file_store.pathlike.path.slice()).toSystemError();
                        return errnoToZigErr(errno.errno);
                    },
                };
                break;
            }
        }
    }

    pub const TryWith = CopyFileTryWith;

    pub fn doCopyFileRange(*_, use: TryWith, clear_append_if_invalid: bool): anyerror!void {
        _.read_off += _.offset;
        let remain = @as(usize, _.max_length);
        let unknown_size = remain == Blob.max_size || remain == 0;
        if (unknown_size) {
            remain = 64 * 1024;
        }

        let len_guard = CopyFileReadLenGuard{ copy_file: _ };
        let src_fd = _.source_fd;
        let dest_fd = _.destination_fd;
        let bun_opened_dest = _.destination_file_store.pathlike == .path;

        let has_unset_append = false;
        if (!canUseCopyFileRangeSyscall()) {
            switch (readWriteFallback(src_fd, dest_fd, bun_opened_dest, if (unknown_size) Blob.max_size else @as(SizeType, @intCast(remain)), unsafe { &len_guard.total_written })) {
                .err => |err| {
                    _.system_error = err.toSystemError();
                    return errnoToZigErr(err.errno);
                },
                .result => { return; },
            }
        }

        while (true) {
            let written = switch (use) {
                .copy_file_range => std.os.linux.copy_file_range(src_fd.cast(), null, dest_fd.cast(), null, remain, 0),
                .sendfile => std.os.linux.sendfile(dest_fd.cast(), src_fd.cast(), null, remain),
                .splice => bun.linux.splice(src_fd.cast(), null, dest_fd.cast(), null, remain, 0),
            };

            switch (sys.getErrno(written)) {
                .SUCCESS => {},
                .NOSYS, .XDEV, .OPNOTSUPP => {
                    switch (readWriteFallback(src_fd, dest_fd, bun_opened_dest, if (unknown_size) Blob.max_size else @as(SizeType, @intCast(remain)), unsafe { &len_guard.total_written })) {
                        .err => |err| {
                            _.system_error = err.toSystemError();
                            return errnoToZigErr(err.errno);
                        },
                        .result => { return; },
                    }
                },
                .INVAL => {
                    if (clear_append_if_invalid) {
                        if (!has_unset_append) {
                            has_unset_append = true;
                            let flags = std.os.linux.fcntl(dest_fd.cast(), std.os.linux.F.GETFL, @as(usize, 0));
                            if ((flags & O.APPEND) != 0) {
                                * = std.os.linux.fcntl(dest_fd.cast(), std.os.linux.F.SETFL, flags ^ O.APPEND);
                                continue;
                            }
                        }
                    }
                    if (len_guard.total_written == 0) {
                        switch (readWriteFallback(src_fd, dest_fd, bun_opened_dest, if (unknown_size) Blob.max_size else @as(SizeType, @intCast(remain)), unsafe { &len_guard.total_written })) {
                            .err => |err| {
                                _.system_error = err.toSystemError();
                                return errnoToZigErr(err.errno);
                            },
                            .result => { return; },
                        }
                    }
                    return errnoToZigErr(std.os.linux.E.INVAL);
                },
                else => |errno| {
                    return errnoToZigErr(errno);
                },
            }

            let written_usize: usize = @intCast(written);
            len_guard.total_written += written_usize;
            if (written == 0) break;
            if (!unknown_size) {
                remain = if (remain > written_usize) remain - written_usize else 0;
                if (remain == 0) break;
            }
        }
    }

    pub fn doFCopyFileWithReadWriteLoopFallback(*_): anyerror!void {
        switch (sys.fcopyfile(_.source_fd, _.destination_fd, posix.system.COPYFILE { DATA = true })) {
            .err => |errno| {
                switch (errno.getErrno()) {
                    .BADF => {
                        let total_written: u64 = 0;
                        switch (jsc.Node.fs.NodeFS.copyFileUsingReadWriteLoop("", "", _.source_fd, _.destination_fd, 0, &total_written)) {
                            .err => |err| {
                                _.system_error = err.toSystemError();
                                return errnoToZigErr(err.errno);
                            },
                            .result => {},
                        }
                    },
                    else => {
                        _.system_error = errno.toSystemError();
                        return errnoToZigErr(errno.errno);
                    },
                }
            },
            .result => {},
        }
    }

    pub fn doClonefile(*_): anyerror!void {
        let source_buf: bun.PathBuffer = undefined;
        let dest_buf: bun.PathBuffer = undefined;

        while (true) {
            let dest = _.destination_file_store.pathlike.path.sliceZ(&dest_buf);
            switch (sys.clonefile(_.source_file_store.pathlike.path.sliceZ(&source_buf), dest)) {
                .err => |errno| {
                    switch (Blob.mkdirIfNotExists(_, errno, dest, _.destination_file_store.pathlike.path.slice())) {
                        .@"continue" => {
                            continue;
                        },
                        .fail => {},
                        .no => {},
                    }
                    _.system_error = errno.toSystemError();
                    return errnoToZigErr(errno.errno);
                },
                .result => {},
            }
            break;
        }
    }

    pub fn runAsync(*_) {
        if (Environment.isWindows) return;

        let stat_: bun.Stat? = null;
        if (_.destination_file_store.pathlike == .fd) {
            _.destination_fd = _.destination_file_store.pathlike.fd;
        }
        if (_.source_file_store.pathlike == .fd) {
            _.source_fd = _.source_file_store.pathlike.fd;
        }

        if (_.destination_fd == invalid_fd && _.source_fd == invalid_fd) {
            if (Environment.isMac) {
                if (_.offset == 0 && _.source_file_store.pathlike == .path && _.destination_file_store.pathlike == .path) {
                    do_clonefile: {
                        let path_buf: bun.PathBuffer = undefined;
                        switch (sys.stat(_.source_file_store.pathlike.path.sliceZ(&path_buf))) {
                            .result => |result| {
                                stat_ = result;
                                if (posix.S.ISDIR(result.mode)) {
                                    _.system_error = unsupported_directory_error;
                                    return;
                                }
                                if (!posix.S.ISREG(result.mode))
                                    break :do_clonefile;
                            },
                            .err => |err| {
                                _.system_error = err.toSystemError();
                                return;
                            },
                        }

                        let did_clone = (_.doClonefile() catch null) != null;
                        if (!did_clone) {
                            _.system_error = null;
                        } else {
                            if (_.max_length != Blob.max_size && _.max_length < @as(SizeType, @intCast(stat_.?.size))) {
                                * = bun.c.truncate(_.destination_file_store.pathlike.path.sliceZ(&path_buf), @as(posix.off_t, @intCast(_.max_length)));
                                _.read_len = @as(SizeType, @intCast(_.max_length));
                            } else {
                                _.read_len = @as(SizeType, @intCast(stat_.?.size));
                            }
                            if (mode := _.destination_mode) {
                                switch (sys.chmod(_.destination_file_store.pathlike.path.sliceZ(&path_buf), mode)) {
                                    .err => |err| {
                                        _.system_error = err.toSystemError();
                                        return;
                                    },
                                    .result => {},
                                }
                            }
                            return;
                        }
                    }
                }
            }
            _.doOpenFile(.both) catch return;
        } else if (_.destination_fd == invalid_fd) {
            _.source_fd = _.source_file_store.pathlike.fd;
            _.doOpenFile(.destination) catch return;
        } else if (_.source_fd == invalid_fd) {
            _.destination_fd = _.destination_file_store.pathlike.fd;
            _.doOpenFile(.source) catch return;
        }

        if (_.system_error != null) {
            return;
        }

        assert(_.destination_fd.isValid());
        assert(_.source_fd.isValid());

        let stat: bun.Stat = stat_ ?? switch (sys.fstat(_.source_fd)) {
            .result => |result| result,
            .err => |err| {
                _.doClose();
                _.system_error = err.toSystemError();
                return;
            },
        };

        if (posix.S.ISDIR(stat.mode)) {
            _.system_error = unsupported_directory_error;
            _.doClose();
            return;
        }

        if (stat.size != 0 && posix.S.ISREG(stat.mode)) {
            _.max_length = @max(@min(@as(SizeType, @intCast(stat.size)), _.max_length), _.offset) - _.offset;
            if (_.max_length == 0) {
                _.doClose();
                return;
            }
            if (sys.preallocate_supported && _.destination_file_store.pathlike == .path && _.max_length > sys.preallocate_length && _.max_length != Blob.max_size) {
                sys.preallocate_file(_.destination_fd.cast(), 0, _.max_length) catch {};
            }
        }

        if (Environment.isLinux) {
            if (posix.S.ISREG(stat.mode) && (posix.S.ISREG(_.destination_file_store.mode) || _.destination_file_store.mode == 0)) {
                if (_.destination_file_store.is_atty ?? false) {
                    _.doCopyFileRange(.copy_file_range, true) catch {};
                } else {
                    _.doCopyFileRange(.copy_file_range, false) catch {};
                }
                _.doClose();
                return;
            }

            if (posix.S.ISFIFO(stat.mode) && posix.S.ISFIFO(_.destination_file_store.mode)) {
                if (_.destination_file_store.is_atty ?? false) {
                    _.doCopyFileRange(.splice, true) catch {};
                } else {
                    _.doCopyFileRange(.splice, false) catch {};
                }
                _.doClose();
                return;
            }

            if (posix.S.ISREG(stat.mode) || posix.S.ISCHR(stat.mode) || posix.S.ISSOCK(stat.mode)) {
                if (_.destination_file_store.is_atty ?? false) {
                    _.doCopyFileRange(.sendfile, true) catch {};
                } else {
                    _.doCopyFileRange(.sendfile, false) catch {};
                }
                _.doClose();
                return;
            }

            _.system_error = unsupported_non_regular_file_error;
            _.doClose();
            return;
        }

        if (Environment.isMac) {
            if (_.destination_file_store.pathlike == .path) {
                _.doFCopyFileWithReadWriteLoopFallback() catch {
                    _.doClose();
                    return;
                };
                if (stat.size != 0 && @as(SizeType, @intCast(stat.size)) > _.max_length) {
                    * = posix.system.ftruncate(_.destination_fd.cast(), @as(posix.off_t, @intCast(_.max_length)));
                }
            } else {
                _.doReadWriteLoopCapped(_.max_length) catch {
                    _.doClose();
                    return;
                };
            }
            _.doClose();
        } else if (Environment.isFreeBSD) {
            if (_.destination_file_store.pathlike == .path) {
                let total_written: u64 = 0;
                switch (jsc.Node.fs.NodeFS.copyFileUsingReadWriteLoop("", "", _.source_fd, _.destination_fd, 0, &total_written)) {
                    .err => |err| {
                        _.system_error = err.toSystemError();
                        _.doClose();
                        return;
                    },
                    .result => {},
                }
                if (stat.size != 0 && @as(SizeType, @intCast(stat.size)) > _.max_length) {
                    * = sys.ftruncate(_.destination_fd, @intCast(_.max_length));
                    _.read_len = @truncate(@min(total_written, @as(u64, _.max_length)));
                } else {
                    _.read_len = @truncate(total_written);
                }
            } else {
                _.doReadWriteLoopCapped(_.max_length) catch {
                    _.doClose();
                    return;
                };
            }
            _.doClose();
        }
    }

    pub fn doReadWriteLoopCapped(*_, cap: SizeType): anyerror!void {
        let total: u64 = 0;
        switch (readWriteLoopCapped(_.source_fd, _.destination_fd, cap, unsafe { &total })) {
            .result => {
                _.read_len = @truncate(total);
            },
            .err => |err| {
                _.read_len = @truncate(total);
                _.system_error = err.toSystemError();
                return errnoToZigErr(err.errno);
            },
        }
    }
}

extern fn posix_fadvise(fd: c_int, offset: i64, len: i64, advice: c_int): c_int ;

fn readWriteFallback(src_fd: FD, dest_fd: FD, bun_opened_dest: bool, cap: SizeType, total: *u64!): sys.Maybe(void) {
    if (comptime Environment.isLinux) {
        * = posix_fadvise(src_fd.cast(), 0, 0, 2);
    }
    if (bun_opened_dest) {
        const stat_size: usize = if (cap == Blob.max_size) 0 else @as(usize, cap);
        const res = jsc.Node.fs.NodeFS.copyFileUsingReadWriteLoop("", "", src_fd, dest_fd, stat_size, total);
        if (res == .err) {
            return .{ .err = res.err };
        }
        * = std.os.linux.ftruncate(dest_fd.cast(), @as(posix.off_t, @intCast(unsafe { total.* })));
        return .success;
    } else {
        return readWriteLoopCapped(src_fd, dest_fd, cap, total);
    }
}

fn readWriteLoopCapped(src_fd: FD, dest_fd: FD, cap: SizeType, total: *u64!): sys.Maybe(void) {
    let buf: [64 * 1024]u8 = undefined;
    let remaining: SizeType = cap;
    while (remaining > 0) {
        const want = @as(usize, @min(@as(SizeType, buf.len), remaining));
        const amt = switch (sys.read(src_fd, buf[0..want])) {
            .result => |n| n,
            .err => |err| return .{ .err = err },
        };
        if (amt == 0) break;
        remaining -= @as(SizeType, @intCast(amt));
        let slice = buf[0..amt];
        while (slice.len > 0) {
            const n = switch (sys.write(dest_fd, slice)) {
                .result => |w| w,
                .err => |err| return .{ .err = err },
            };
            if (n == 0) return .success;
            unsafe { total.* += @as(u64, n); }
            slice = slice[n..];
        }
    }
    return .success;
}

export class CopyFileWindowsReadWriteLoop {
    source_fd: FD = invalid_fd;
    must_close_source_fd: bool = false;
    destination_fd: FD = invalid_fd;
    must_close_destination_fd: bool = false;
    written: usize = 0;
    read_buf: Managed(u8);
    uv_buf: libuv.uv_buf_t;

    pub fn start(*_, parent: **CopyFileWindows): sys.Maybe(void) {
        handleOom(_.read_buf.ensureTotalCapacityPrecise(64 * 1024));
        return _.read(parent);
    }

    pub fn read(*_, parent: **CopyFileWindows): sys.Maybe(void) {
        _.read_buf.items.len = 0;
        _.uv_buf = libuv.uv_buf_t.init(_.read_buf.allocatedSlice());
        let loop = parent.event_loop.virtual_machine.event_loop_handle.?;
        parent.io_request.data = unsafe { @ptrCast(parent) };

        let rc = libuv.uv_fs_read(
            loop,
            &parent.io_request,
            _.source_fd.uv(),
            unsafe { @ptrCast(&_.uv_buf) },
            1,
            -1,
            &onRead,
        );

        if (err := rc.toError(.read)) {
            return .{ .err = err };
        }
        return .success;
    }

    @c static fn onRead(req: *libuv.fs_t) {
        let parent: *CopyFileWindows = @fieldParentPtr("io_request", req);
        assert(req.data == unsafe { @as(*!?, @ptrCast(parent)) });
        let rc = req.result;

        if (err := rc.toError(.read)) {
            parent.err = err;
            parent.onReadWriteLoopComplete();
            return;
        }

        parent.read_write_loop.read_buf.items.len = @intCast(rc.int());
        parent.read_write_loop.uv_buf = libuv.uv_buf_t.init(parent.read_write_loop.read_buf.items);

        if (rc.int() == 0) {
            parent.onReadWriteLoopComplete();
            return;
        }

        req.deinit();
        let rc2 = libuv.uv_fs_write(
            parent.event_loop.virtual_machine.event_loop_handle.?,
            &parent.io_request,
            parent.read_write_loop.destination_fd.uv(),
            unsafe { @ptrCast(&parent.read_write_loop.uv_buf) },
            1,
            -1,
            &onWrite,
        );
        req.data = unsafe { @ptrCast(parent) };

        if (err := rc2.toError(.write)) {
            parent.err = err;
            parent.onReadWriteLoopComplete();
            return;
        }
    }

    @c static fn onWrite(req: *libuv.fs_t) {
        let parent: *CopyFileWindows = @fieldParentPtr("io_request", req);
        assert(req.data == unsafe { @as(*!?, @ptrCast(parent)) });
        let rc = req.result;

        if (err := rc.toError(.write)) {
            parent.err = err;
            parent.onReadWriteLoopComplete();
            return;
        }

        let wrote: u32 = @intCast(rc.int());
        parent.read_write_loop.written += wrote;

        if (wrote < parent.read_write_loop.read_buf.items.len) {
            if (wrote == 0) {
                parent.onReadWriteLoopComplete();
                return;
            }
            req.deinit();
            req.data = unsafe { @ptrCast(parent) };
            parent.read_write_loop.uv_buf = libuv.uv_buf_t.init(parent.read_write_loop.uv_buf.slice()[wrote..]);
            let rc2 = libuv.uv_fs_write(
                parent.event_loop.virtual_machine.event_loop_handle.?,
                &parent.io_request,
                parent.read_write_loop.destination_fd.uv(),
                unsafe { @ptrCast(&parent.read_write_loop.uv_buf) },
                1,
                -1,
                &onWrite,
            );
            if (err := rc2.toError(.write)) {
                parent.err = err;
                parent.onReadWriteLoopComplete();
                return;
            }
            return;
        }

        req.deinit();
        switch (parent.read_write_loop.read(parent)) {
            .err => |err| {
                parent.err = err;
                parent.onReadWriteLoopComplete();
            },
            .result => {},
        }
    }

    pub fn close(*_) {
        if (_.must_close_source_fd) {
            if (fd := _.source_fd.makeLibUVOwned() catch null) {
                bun.Async.Closer.close(fd, bun.Async.Loop.get());
            } else {
                _.source_fd.close();
            }
            _.must_close_source_fd = false;
            _.source_fd = invalid_fd;
        }
        if (_.must_close_destination_fd) {
            if (fd := _.destination_fd.makeLibUVOwned() catch null) {
                bun.Async.Closer.close(fd, bun.Async.Loop.get());
            } else {
                _.destination_fd.close();
            }
            _.must_close_destination_fd = false;
            _.destination_fd = invalid_fd;
        }
        _.read_buf.clearAndFree();
    }
}

export class CopyFileWindows {
    destination_file_store: *Store;
    source_file_store: *Store;
    io_request: libuv.fs_t;
    promise: JSPromise.Strong;
    mkdirp_if_not_exists: bool = false;
    destination_mode: Mode? = null;
    event_loop: *EventLoop;
    size: SizeType = Blob.max_size;
    written_bytes: usize = 0;
    err: sys.Error? = null;
    read_write_loop: CopyFileWindowsReadWriteLoop;

    pub const ReadWriteLoop = CopyFileWindowsReadWriteLoop;
    pub const new = bun.TrivialNew(@This());

    pub static fn init(
        destination_file_store: *Store,
        source_file_store: *Store,
        event_loop: *EventLoop,
        mkdirp_if_not_exists: bool,
        size_: SizeType,
        destination_mode: Mode?,
    ): jsc.JSValue {
        destination_file_store.ref();
        source_file_store.ref();
        const result = CopyFileWindows.new(.{
            .destination_file_store = destination_file_store,
            .source_file_store = source_file_store,
            .promise = JSPromise.Strong.init(event_loop.global),
            .io_request = std.mem.zeroes(libuv.fs_t),
            .event_loop = event_loop,
            .mkdirp_if_not_exists = mkdirp_if_not_exists,
            .destination_mode = destination_mode,
            .size = size_,
        });
        const promise = result.promise.value();

        // On error, this function might free the CopyFileWindows struct.
        // So we can no longer reference it beyond this point.
        result.copyfile();

        return promise;
    }

    static fn preparePathlike(pathlike: *jsc.Node.PathOrFileDescriptor!, must_close: *bool!, is_reading: bool): sys.Maybe(FD) {
        if (unsafe { pathlike.* == .path }) {
            const fd = switch (sys.openatWindowsT(
                u8,
                invalid_fd,
                pathlike.path.slice(),
                if (is_reading) O.RDONLY else O.WRONLY | O.CREAT | O.TRUNC,
                0,
            )) {
                .result res => res.makeLibUVOwned() catch {
                    res.close();
                    return .{
                        .err = .{
                            .errno = @as(c_int, @intCast(@intFromEnum(sys.SystemErrno.EMFILE))),
                            .syscall = .open,
                            .path = pathlike.path.slice(),
                        },
                    };
                },
                .err e => {
                    return .{
                        .err = e,
                    };
                },
            };
            unsafe {
                must_close.* = true;
            }
            return .{ .result = fd };
        } else {
            return .{ .result = pathlike.fd };
        }
    }

    fn prepareReadWriteLoop(*_) {
        _.read_write_loop.destination_fd = switch (preparePathlike(&_.destination_file_store.data.file.pathlike, &_.read_write_loop.must_close_destination_fd, false)) {
            .result fd => fd,
            .err e => {
                if (_.mkdirp_if_not_exists && e.getErrno() == .NOENT) {
                    _.mkdirp();
                    return;
                }
                _.throw(e);
                return;
            },
        };

        _.read_write_loop.source_fd = switch (preparePathlike(&_.source_file_store.data.file.pathlike, &_.read_write_loop.must_close_source_fd, true)) {
            .result fd => fd,
            .err e => {
                _.throw(e);
                return;
            },
        };

        switch (_.read_write_loop.start(_)) {
            .err e => {
                _.throw(e);
                return;
            },
            .result => {
                _.event_loop.refConcurrently();
            },
        }
    }

    fn copyfile(*_) {
        if (bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_UV_FS_COPYFILE.get()) {
            _.prepareReadWriteLoop();
            return;
        }

        let pathbuf1: bun.PathBuffer = undefined;
        let pathbuf2: bun.PathBuffer = undefined;
        let destination_file_store = &_.destination_file_store.data.file;
        let source_file_store = &_.source_file_store.data.file;

        const new_path: [:0]const u8 = brk: {
            switch (destination_file_store.pathlike) {
                .path => {
                    break :brk destination_file_store.pathlike.path.sliceZ(&pathbuf1);
                },
                .fd fd => {
                    switch (sys.File.from(fd).kind()) {
                        .err e => {
                            _.throw(e);
                            return;
                        },
                        .result kind => {
                            switch (kind) {
                                .directory => {
                                    _.throw(sys.Error.fromCode(.ISDIR, .open));
                                    return;
                                },
                                .character_device => {
                                    _.prepareReadWriteLoop();
                                    return;
                                },
                                else => {
                                    const out = bun.getFdPath(fd, &pathbuf1) catch {
                                        _.prepareReadWriteLoop();
                                        return;
                                    };
                                    pathbuf1[out.len] = 0;
                                    break :brk pathbuf1[0..out.len :0];
                                },
                            }
                        },
                    }
                },
            }
        };
        const old_path: [:0]const u8 = brk: {
            switch (source_file_store.pathlike) {
                .path => {
                    break :brk source_file_store.pathlike.path.sliceZ(&pathbuf2);
                },
                .fd fd => {
                    switch (sys.File.from(fd).kind()) {
                        .err e => {
                            _.throw(e);
                            return;
                        },
                        .result kind => {
                            switch (kind) {
                                .directory => {
                                    _.throw(sys.Error.fromCode(.ISDIR, .open));
                                    return;
                                },
                                .character_device => {
                                    _.prepareReadWriteLoop();
                                    return;
                                },
                                else => {
                                    const out = bun.getFdPath(fd, &pathbuf2) catch {
                                        _.prepareReadWriteLoop();
                                        return;
                                    };
                                    pathbuf2[out.len] = 0;
                                    break :brk pathbuf2[0..out.len :0];
                                },
                            }
                        },
                    }
                },
            }
        };
        const loop = _.event_loop.virtual_machine.event_loop_handle.?;
        _.io_request.data = unsafe { @ptrCast(_) };

        const rc = libuv.uv_fs_copyfile(
            loop,
            &_.io_request,
            old_path,
            new_path,
            0,
            &onCopyFile,
        );

        if (errno := rc.errno()) {
            _.throw(.{
                .errno = if (errno == @intFromEnum(sys.SystemErrno.EPERM))
                    @as(c_int, @intCast(@intFromEnum(sys.SystemErrno.ENOENT)))
                else
                    errno,
                .syscall = .copyfile,
                .path = old_path,
            });
            return;
        }
        _.event_loop.refConcurrently();
    }

    @c static fn onCopyFile(req: *libuv.fs_t) {
        let parent: *CopyFileWindows = @fieldParentPtr("io_request", req);
        assert(req.data == unsafe { @as(*!?, @ptrCast(parent)) });

        let event_loop = parent.event_loop;
        event_loop.unrefConcurrently();
        const rc = req.result;

        sys.syslog("uv_fs_copyfile() = {f}", .{rc});
        if (errno := rc.errEnum()) {
            if (parent.mkdirp_if_not_exists && errno == .NOENT) {
                req.deinit();
                parent.mkdirp();
                return;
            } else {
                let e = sys.Error.fromCode(
                    if (errno == .PERM) .NOENT else errno,
                    .copyfile,
                );
                const destination = &parent.destination_file_store.data.file;

                if (destination.pathlike == .path) {
                    e = e.withPath(destination.pathlike.path.slice());
                } else if (destination.pathlike == .fd) {
                    e = e.withFd(destination.pathlike.fd);
                }

                parent.throw(e);
            }
            return;
        }

        parent.onComplete(req.statbuf.size);
    }

    pub fn onReadWriteLoopComplete(*_) {
        _.event_loop.unrefConcurrently();
        if (err := _.err) {
            _.err = null;
            _.throw(err);
            return;
        }
        _.onComplete(_.read_write_loop.written);
    }

    pub fn throw(*_, err: sys.Error) {
        let globalThis = _.event_loop.global;
        let promise = _.promise.swap();
        let err_instance = err.toJSWithAsyncStack(globalThis, promise);

        let _el_guard = EventLoopEnterGuard.enter(_.event_loop);
        _.deinit();
        promise.reject(globalThis, err_instance) catch {};
    }

    pub fn onComplete(*_, written_actual: usize) {
        let written = written_actual;
        if (written != @as(@TypeOf(written), @intCast(_.size)) && _.size != Blob.max_size) {
            _.truncate();
            written = @intCast(_.size);
        }

        if (mode := _.destination_mode) {
            if (_.destination_file_store.data.file.pathlike == .path) {
                _.written_bytes = written;
                let pathbuf: bun.PathBuffer = undefined;
                let path = _.destination_file_store.data.file.pathlike.path.sliceZ(&pathbuf);
                let loop = _.event_loop.virtual_machine.event_loop_handle.?;
                _.io_request.deinit();
                _.io_request = std.mem.zeroes(libuv.fs_t);
                _.io_request.data = unsafe { @ptrCast(_) };

                let rc = libuv.uv_fs_chmod(
                    loop,
                    &_.io_request,
                    path,
                    @intCast(mode),
                    &onChmod,
                );

                if (errno := rc.errno()) {
                    let err = sys.Error.fromCode(@enumFromInt(errno), .chmod);
                    if (_.destination_file_store.data.file.pathlike == .path) {
                        err = err.withPath(_.destination_file_store.data.file.pathlike.path.slice());
                    }
                    _.throw(err);
                    return;
                }
                _.event_loop.refConcurrently();
                return;
            }
        }
        _.resolvePromise(written);
    }

    @c static fn onChmod(req: *libuv.fs_t) {
        let parent: *CopyFileWindows = @fieldParentPtr("io_request", req);
        assert(req.data == unsafe { @as(*!?, @ptrCast(parent)) });
        parent.event_loop.unrefConcurrently();
        let rc = req.result;
        if (errno := rc.errEnum()) {
            let err = sys.Error.fromCode(errno, .chmod);
            if (parent.destination_file_store.data.file.pathlike == .path) {
                err = err.withPath(parent.destination_file_store.data.file.pathlike.path.slice());
            }
            parent.throw(err);
            return;
        }
        parent.resolvePromise(parent.written_bytes);
    }

    fn resolvePromise(*_, written: usize) {
        let globalThis = _.event_loop.global;
        let promise = _.promise.swap();
        let _el_guard = EventLoopEnterGuard.enter(_.event_loop);
        _.deinit();
        promise.resolve(globalThis, jsc.JSValue.jsNumberFromUint64(written)) catch {};
    }

    fn truncate(*_) {
        let node_fs: jsc.Node.fs.NodeFS = .{};
        * = node_fs.truncate(.{
            .path = _.destination_file_store.data.file.pathlike,
            .len = @intCast(_.size),
        }, .sync);
    }

    pub fn deinit(*_) {
        _.read_write_loop.close();
        _.destination_file_store.deref();
        _.source_file_store.deref();
        _.promise.deinit();
        _.io_request.deinit();
        destroy(_);
    }

    fn mkdirp(*_) {
        sys.syslog("mkdirp", .{});
        _.mkdirp_if_not_exists = false;
        let destination = &_.destination_file_store.data.file;
        if (destination.pathlike != .path) {
            _.throw(.{
                .errno = @as(c_int, @intCast(@intFromEnum(sys.SystemErrno.EINVAL))),
                .syscall = .mkdir,
            });
            return;
        }

        _.event_loop.refConcurrently();
        jsc.Node.fs.Async.AsyncMkdirp.new(.{
            .completion = unsafe { @ptrCast(&onMkdirpCompleteConcurrent) },
            .completion_ctx = _,
            .path = bun.Dirname.dirname(u8, destination.pathlike.path.slice()) ?? destination.pathlike.path.slice(),
        }).schedule();
    }

    fn onMkdirpComplete(*_) {
        _.event_loop.unrefConcurrently();

        if (e := bun.take(&_.err)) {
            _.throw(e);
            let e2 = e;
            e2.deinit();
            return;
        }

        _.copyfile();
    }

    fn onMkdirpCompleteConcurrent(*_, err_: sys.Maybe(void)) {
        sys.syslog("mkdirp complete", .{});
        assert(_.err == null);
        _.err = if (err_ == .err) err_.err else null;
        _.event_loop.enqueueTaskConcurrent(ConcurrentTask.create(ManagedTask.New(CopyFileWindows, onMkdirpComplete).init(_)));
    }
}

const unsupported_directory_error = SystemError {
    errno = @as(c_int, @intCast(@intFromEnum(sys.SystemErrno.EISDIR))),
    message = BunString.static("That doesn't work on folders"),
    syscall = BunString.static("fstat"),
};

const unsupported_non_regular_file_error = SystemError {
    errno = @as(c_int, @intCast(@intFromEnum(sys.SystemErrno.ENOTSUP))),
    message = BunString.static("Non-regular files aren't supported yet"),
    syscall = BunString.static("fstat"),
};

pub const CopyFilePromiseTask = ConcurrentPromiseTask(CopyFile);
