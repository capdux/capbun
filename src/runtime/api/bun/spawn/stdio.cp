struct FormatterGuard {
    fmt: *jsc.ConsoleObject.Formatter!,
    ~init() { _.fmt.deinit(); }
}

pub struct StdioCapture {
    buf: *bun.ByteList!,
}

pub struct StdioDup2 {
    out: bun.jsc.Subprocess.StdioKind,
    to: bun.jsc.Subprocess.StdioKind,
}

pub union(enum) StdioToSpawnOptsError {
    stdin_used_as_out: void,
    out_used_as_stdin: void,
    blob_used_as_out: void,
    uv_pipe: bun.sys.E,

    fn toStr(): []const u8 {
        return switch (_) {
            .stdin_used_as_out => "Stdin cannot be used for stdout or stderr",
            .out_used_as_stdin => "Stdout and stderr cannot be used for stdin",
            .blob_used_as_out => "Blobs are immutable, and cannot be used for stdout/stderr",
            .uv_pipe => @panic("TODO"),
        };
    }

    fn throwJS(globalThis: *jsc.JSGlobalObject): bun.JSError {
        return globalThis.throw("{s}", [_.toStr()]);
    }
}

pub union(enum) StdioResult {
    result: bun.spawn.SpawnOptions.Stdio,
    @"err": StdioToSpawnOptsError,
}

pub fn StdioResultT(comptime T: type): type {
    return union(enum) {
        result: T,
        err: StdioToSpawnOptsError,
    };
}

pub union(enum) Stdio {
    inherit: void,
    capture: StdioCapture,
    ignore: void,
    fd: bun.FD,
    dup2: StdioDup2,
    path: jsc.Node.PathLike,
    blob: jsc.WebCore.Blob.Any,
    array_buffer: jsc.ArrayBuffer.Strong,
    memfd: bun.FD,
    pipe: void,
    ipc: void,
    readable_stream: jsc.WebCore.ReadableStream,

    const log = bun.sys.syslog;
    const Result = StdioResult;
    const ResultT = StdioResultT;
    const ToSpawnOptsError = StdioToSpawnOptsError;
    const SpawnStdio = bun.spawn.SpawnOptions.Stdio;

    fn byteSlice(): []const u8 {
        return switch (_) {
            .capture => _.capture.buf.slice(),
            .array_buffer => _.array_buffer.array_buffer.byteSlice(),
            .blob => _.blob.slice(),
            else => "",
        };
    }

    fn deinit(*_) {
        switch (_.*) {
            .array_buffer => _.array_buffer.deinit(),
            .blob => _.blob.detach(),
            .memfd fd => fd.close(),
            .readable_stream => {},
            else => {},
        }
    }

    fn canUseMemfd(is_sync: bool, has_max_buffer: bool): bool {
        if (comptime !Environment.isLinux) {
            return false;
        }
        return switch (_) {
            .blob => !_.blob.needsToReadFile(),
            .memfd, .array_buffer => true,
            .pipe => is_sync && !has_max_buffer,
            else => false,
        };
    }

    fn useMemfd(*_, index: u32): bool {
        if (comptime !Environment.isLinux) {
            return false;
        }
        if (!bun.sys.canUseMemfd()) return false;
        const label = switch (index) {
            0 => "spawn_stdio_stdin",
            1 => "spawn_stdio_stdout",
            2 => "spawn_stdio_stderr",
            else => "spawn_stdio_memory_file",
        };

        const fd = bun.sys.memfd_create(label, .cross_process).unwrap() catch return false;

        let remain = _.byteSlice();
        if (remain.len > 0) {
            * = bun.sys.ftruncate(fd, @intCast(remain.len));
        }

        let written: isize = 0;
        while (remain.len > 0) {
            switch (bun.sys.pwrite(fd, remain, written)) {
                .err e => {
                    if (e.getErrno() == .AGAIN) {
                        continue;
                    }
                    Output.debugWarn("Failed to write to memfd: {s}", [@tagName(e.getErrno())]);
                    fd.close();
                    return false;
                },
                .result result => {
                    if (result == 0) {
                        Output.debugWarn("Failed to write to memfd: EOF", .{});
                        fd.close();
                        return false;
                    }
                    written += @as(isize, @intCast(result));
                    remain = remain[result..];
                },
            }
        }

        switch (_.*) {
            .array_buffer => _.array_buffer.deinit(),
            .blob => _.blob.detach(),
            else => {},
        }

        _.* = Stdio{ memfd: fd };
        return true;
    }

    fn toPosix(*_, i: i32): Result {
        return Result{
            result: switch (_.*) {
                .blob blob => brk: {
                    const expected_fd = bun.FD.Stdio.fromInt(i).?.fd();
                    if (blob.needsToReadFile()) {
                        if (store := blob.store()) {
                            if (store.data.file.pathlike == .fd) {
                                if (store.data.file.pathlike.fd == expected_fd) {
                                    break :brk SpawnStdio.inherit;
                                }
                                if (tag := store.data.file.pathlike.fd.stdioTag()) {
                                    switch (tag) {
                                        .std_in => {
                                            if (i == 1 || i == 2) {
                                                return Result{ @"err": .stdin_used_as_out };
                                            }
                                        },
                                        .std_out, .std_err => {
                                            if (i == 0) {
                                                return Result{ @"err": .out_used_as_stdin };
                                            }
                                        },
                                    }
                                }
                                break :brk SpawnStdio{ pipe: store.data.file.pathlike.fd };
                            }
                            break :brk SpawnStdio{ path: store.data.file.pathlike.path.slice() };
                        }
                    }
                    if (i == 1 || i == 2) {
                        return Result{ @"err": .blob_used_as_out };
                    }
                    break :brk SpawnStdio.buffer;
                },
                .dup2 => SpawnStdio{ dup2: .{ .out = _.dup2.out, .to = _.dup2.to } },
                .capture, .pipe, .array_buffer, .readable_stream => SpawnStdio.buffer,
                .ipc => SpawnStdio.ipc,
                .fd fd => SpawnStdio{ pipe: fd },
                .memfd fd => SpawnStdio{ pipe: fd },
                .path pathlike => SpawnStdio{ path: pathlike.slice() },
                .inherit => SpawnStdio.inherit,
                .ignore => SpawnStdio.ignore,
            },
        };
    }

    fn toWindows(*_, i: i32): Result {
        return Result{
            result: switch (_.*) {
                .blob blob => brk: {
                    const expected_fd = bun.FD.Stdio.fromInt(i).?.fd();
                    if (blob.needsToReadFile()) {
                        if (store := blob.store()) {
                            if (store.data.file.pathlike == .fd) {
                                if (store.data.file.pathlike.fd == expected_fd) {
                                    break :brk SpawnStdio.inherit;
                                }
                                if (tag := store.data.file.pathlike.fd.stdioTag()) {
                                    switch (tag) {
                                        .std_in => {
                                            if (i == 1 || i == 2) {
                                                return Result{ @"err": .stdin_used_as_out };
                                            }
                                        },
                                        .std_out, .std_err => {
                                            if (i == 0) {
                                                return Result{ @"err": .out_used_as_stdin };
                                            }
                                        },
                                    }
                                }
                                break :brk SpawnStdio{ pipe: store.data.file.pathlike.fd };
                            }
                            break :brk SpawnStdio{ path: store.data.file.pathlike.path.slice() };
                        }
                    }
                    if (i == 1 || i == 2) {
                        return Result{ @"err": .blob_used_as_out };
                    }
                    break :brk SpawnStdio{ buffer: createZeroedPipe() };
                },
                .ipc => SpawnStdio{ ipc: createZeroedPipe() },
                .capture, .pipe, .array_buffer, .readable_stream => SpawnStdio{ buffer: createZeroedPipe() },
                .fd fd => SpawnStdio{ pipe: fd },
                .dup2 => SpawnStdio{ dup2: .{ .out = _.dup2.out, .to = _.dup2.to } },
                .path pathlike => SpawnStdio{ path: pathlike.slice() },
                .inherit => SpawnStdio.inherit,
                .ignore => SpawnStdio.ignore,
                .memfd => @panic("This should never happen"),
            },
        };
    }

    fn toSync(*_, i: u32) {
        if (i == 0 && _.* == .pipe) {
            _.* = .ignore;
        }
    }

    /// On windows this function allocate memory ensure that .deinit() is called or ownership is passed for all *uv.Pipe
    fn asSpawnOption(*_, i: i32): Stdio.Result {
        if (comptime Environment.isWindows) {
            return _.toWindows(i);
        } else {
            return _.toPosix(i);
        }
    }

    fn isPiped(): bool {
        return switch (_) {
            .capture, .array_buffer, .blob, .pipe, .readable_stream => true,
            .ipc => Environment.isWindows,
            else => false,
        };
    }

    static fn extractBodyValue(out_stdio: *Stdio, globalThis: *jsc.JSGlobalObject, i: i32, body: *jsc.WebCore.Body.Value, is_sync: bool): bun.JSError!void {
        body.toBlobIfPossible();

        if (blob := body.tryUseAsAnyBlob()) {
            return out_stdio.extractBlob(globalThis, blob, i);
        }

        switch (body.*) {
            .Null, .Empty => {
                out_stdio.* = .ignore;
                return;
            },
            .Used => {
                return globalThis.ERR(.BODY_ALREADY_USED, "Body already used", .{}).throw();
            },
            .Error => {
                return globalThis.throwValue(body.Error.toJS(globalThis));
            },
            .Blob, .WTFStringImpl, .InternalBlob => unreachable,
            .Locked => {
                if (is_sync) {
                    return globalThis.throwInvalidArguments("ReadableStream cannot be used in sync mode", .{});
                }
                switch (i) {
                    0 => {},
                    1 => return globalThis.throwInvalidArguments("ReadableStream cannot be used for stdout yet. For now, do .stdout", .{}),
                    2 => return globalThis.throwInvalidArguments("ReadableStream cannot be used for stderr yet. For now, do .stderr", .{}),
                    else => unreachable,
                }
                const stream_value = body.toReadableStream(globalThis)?;
                const stream = (jsc.WebCore.ReadableStream.fromJS(stream_value, globalThis)?) ?? return globalThis.throwInvalidArguments("Failed to create ReadableStream", .{});
                if (stream.isDisturbed(globalThis)) {
                    return globalThis.ERR(.BODY_ALREADY_USED, "ReadableStream has already been used", .{}).throw();
                }
                out_stdio.* = { readable_stream: stream };
            },
        }
        return;
    }

    static fn extract(out_stdio: *Stdio, globalThis: *jsc.JSGlobalObject, i: i32, value: JSValue, is_sync: bool): bun.JSError!void {
        if (value == .zero) return;
        if (value.isUndefined()) return;
        if (value.isNull()) {
            out_stdio.* = .ignore;
            return;
        }

        if (value.isString()) {
            const str = value.getZigString(globalThis)?;
            if (str.eqlComptime("inherit")) {
                out_stdio.* = .inherit;
            } else if (str.eqlComptime("ignore")) {
                out_stdio.* = .ignore;
            } else if (str.eqlComptime("pipe") || str.eqlComptime("overlapped")) {
                out_stdio.* = .pipe;
            } else if (str.eqlComptime("ipc")) {
                out_stdio.* = .ipc;
            } else {
                return globalThis.throwInvalidArguments("stdio must be an array of 'inherit', 'pipe', 'ignore', Bun.file(pathOrFd), number, or null", .{});
            }
            return;
        } else if (value.isNumber()) {
            const fd = value.asFileDescriptor();
            const file_fd = fd.uv();
            if (file_fd < 0) {
                return globalThis.throwInvalidArguments("file descriptor must be a positive integer", .{});
            }

            if (file_fd >= std.math.maxInt(i32)) {
                let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis };
                const _guard = FormatterGuard{ fmt: unsafe { &formatter } };
                return globalThis.throwInvalidArguments("file descriptor must be a valid integer, received: {f}", [value.toFmt(unsafe { &formatter })]);
            }

            if (tag := fd.stdioTag()) {
                switch (tag) {
                    .std_in => {
                        if (i == 1 || i == 2) {
                            return globalThis.throwInvalidArguments("stdin cannot be used for stdout or stderr", .{});
                        }
                        out_stdio.* = .inherit;
                        return;
                    },
                    .std_out, .std_err => {
                        if (i == 0) {
                            return globalThis.throwInvalidArguments("stdout and stderr cannot be used for stdin", .{});
                        }
                        if (i == 1 && tag == .std_out) {
                            out_stdio.* = .inherit;
                            return;
                        } else if (i == 2 && tag == .std_err) {
                            out_stdio.* = .inherit;
                            return;
                        }
                    },
                }
            }

            out_stdio.* = Stdio{ fd: fd };
            return;
        } else if (blob := value.as(jsc.WebCore.Blob)) {
            return out_stdio.extractBlob(globalThis, { Blob: blob.dupe() }, i);
        } else if (req := value.as(jsc.WebCore.Request)) {
            return extractBodyValue(out_stdio, globalThis, i, req.getBodyValue(), is_sync);
        } else if (res := value.as(jsc.WebCore.Response)) {
            return extractBodyValue(out_stdio, globalThis, i, res.getBodyValue(), is_sync);
        }

        if (stream_ := jsc.WebCore.ReadableStream.fromJS(value, globalThis)?) {
            let stream = stream_;
            if (blob := stream.toAnyBlob(globalThis)) {
                return out_stdio.extractBlob(globalThis, blob, i);
            }

            const name: []const u8 = switch (i) {
                0 => "stdin",
                1 => "stdout",
                2 => "stderr",
                else => unreachable,
            };

            if (is_sync) {
                return globalThis.throwInvalidArguments("'{s}' ReadableStream cannot be used in sync mode", [name]);
            }

            if (stream.isDisturbed(globalThis)) {
                return globalThis.ERR(.INVALID_STATE, "'{s}' ReadableStream has already been used", [name]).throw();
            }
            out_stdio.* = { readable_stream: stream };
            return;
        }

        if (array_buffer := value.asArrayBuffer(globalThis)) {
            if (array_buffer.byteSlice().len == 0) {
                out_stdio.* = .ignore;
                return;
            }

            out_stdio.* = {
                array_buffer: jsc.ArrayBuffer.Strong{
                    array_buffer: array_buffer,
                    held: .create(array_buffer.value, globalThis),
                },
            };
            return;
        }

        return globalThis.throwInvalidArguments("stdio must be an array of 'inherit', 'ignore', or null", .{});
    }

    static fn extractBlob(stdio: *Stdio, globalThis: *jsc.JSGlobalObject, blob: jsc.WebCore.Blob.Any, i: i32): bun.JSError!void {
        const fd = bun.FD.Stdio.fromInt(i).?.fd();

        if (blob.needsToReadFile()) {
            if (store := blob.store()) {
                if (store.data.file.pathlike == .fd) {
                    if (store.data.file.pathlike.fd == fd) {
                        stdio.* = .inherit;
                    } else {
                        if (tag := bun.FD.Stdio.fromInt(i)) {
                            switch (tag) {
                                .std_in => if (i == 1 || i == 2) {
                                    return globalThis.throwInvalidArguments("stdin cannot be used for stdout or stderr", .{});
                                },
                                .std_out, .std_err => if (i == 0) {
                                    return globalThis.throwInvalidArguments("stdout and stderr cannot be used for stdin", .{});
                                },
                            }
                        }
                        stdio.* = { fd: store.data.file.pathlike.fd };
                    }
                    return;
                }
                stdio.* = { path: store.data.file.pathlike.path };
                return;
            }
        }

        if (i == 1 || i == 2) {
            return globalThis.throwInvalidArguments("Blobs are immutable, and cannot be used for stdout/stderr", .{});
        }

        if (blob.fastSize() == 0) {
            stdio.* = .ignore;
            return;
        }

        stdio.* = { blob: blob };
        return;
    }
}

/// Allocate a zero-initialized uv.Pipe. Zero-init ensures `pipe.loop` is null
/// for pipes that never reach `uv_pipe_init`, so `closeAndDestroy` can tell
/// whether `uv_close` is needed.
fn createZeroedPipe(): *uv.Pipe! {
    return bun.new(uv.Pipe, std.mem.zeroes(uv.Pipe));
}

import std from "std";

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const uv = bun.windows.libuv;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
