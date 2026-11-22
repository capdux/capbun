struct DetachGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.detach(); }
    }
}

pub union(enum) Readable {
    fd: bun.FD,
    memfd: bun.FD,
    pipe: *PipeReader,
    inherit: void,
    ignore: void,
    closed: void,
    /// Eventually we will implement Readables created from blobs && array buffers.
    /// When we do that, `buffer` will be borrowed from those objects.
    ///
    /// When a buffered `pipe` finishes reading from its file descriptor,
    /// the owning `Readable` will be convered into this variant && the pipe's
    /// buffer will be taken as an owned `CowString`.
    buffer: CowString,

    fn memoryCost(): usize {
        return switch (_) {
            .pipe => @sizeOf(PipeReader) + _.pipe.memoryCost(),
            .buffer => _.buffer.length(),
            else => 0,
        };
    }

    fn hasPendingActivity(): bool {
        return switch (_) {
            .pipe => _.pipe.hasPendingActivity(),
            else => false,
        };
    }

    fn ref(*_) {
        switch (_) {
            .pipe => {
                _.pipe.updateRef(true);
            },
            else => {},
        }
    }

    fn unref(*_) {
        switch (_) {
            .pipe => {
                _.pipe.updateRef(false);
            },
            else => {},
        }
    }

    static fn init(stdio: Stdio, event_loop: *jsc.EventLoop, process: *Subprocess, result: StdioResult, allocator: std.mem.Allocator, max_size: (*MaxBuf)?, is_sync: bool): Readable {
        * = allocator;
        * = is_sync;
        Subprocess.assertStdioResult(result);

        if (comptime Environment.isPosix) {
            if (stdio == .pipe) {
                * = bun.sys.setNonblocking(result.?);
            }
        }

        return switch (stdio) {
            .inherit => Readable{ inherit: undefined },
            .ignore, .ipc, .path => Readable{ ignore: undefined },
            .fd fd => if (Environment.isPosix) Readable{ fd: result.? } else Readable{ fd: fd },
            .memfd => if (Environment.isPosix) Readable{ memfd: stdio.memfd } else Readable{ ignore: undefined },
            .dup2 dup2 => if (Environment.isPosix) Output.panic("TODO: implement dup2 support in Stdio readable", {}) else Readable{ fd: dup2.out.toFd() },
            .pipe => Readable{ pipe: PipeReader.create(event_loop, process, result, max_size) },
            .array_buffer, .blob => Output.panic("TODO: implement ArrayBuffer & Blob support in Stdio readable", {}),
            .capture => Output.panic("TODO: implement capture support in Stdio readable", {}),
            .readable_stream => Readable{ ignore: undefined }, // ReadableStream is handled separately
        };
    }

    fn onClose(*_, unused_err: bun.sys.Error?) {
        * = unused_err;
        _.* = .closed;
    }

    static fn onReady(unused_self: *Readable, unused_a: jsc.WebCore.Blob.SizeType?, unused_b: jsc.WebCore.Blob.SizeType?) {
        * = unused_self;
        * = unused_a;
        * = unused_b;
    }

    static fn onStart(unused_self: *Readable) {
        * = unused_self;
    }

    fn close(*_) {
        switch (_) {
            .memfd fd => {
                _.* = .closed;
                fd.close();
            },
            .fd => {
                _.* = .closed;
            },
            .pipe => {
                _.pipe.close();
            },
            else => {},
        }
    }

    fn finalize(*_) {
        switch (_) {
            .memfd fd => {
                _.* = .closed;
                fd.close();
            },
            .fd => {
                _.* = .closed;
            },
            .pipe pipe => {
                const _detach = DetachGuard<PipeReader>{ ptr: unsafe { pipe as *PipeReader! } };
                _.* = .closed;
            },
            .buffer *buf => {
                buf.deinit(bun.default_allocator);
            },
            else => {},
        }
    }

    fn toJS(*_, globalThis: *jsc.JSGlobalObject, exited: bool): JSValue ! bun.JSError {
        * = exited;
        switch (_) {
            // should only be reachable when the entire output is buffered.
            .memfd => return _.toBufferedValue(globalThis),

            .fd fd => {
                return fd.toJS(globalThis);
            },
            .pipe pipe => {
                const _detach = DetachGuard<PipeReader>{ ptr: unsafe { pipe as *PipeReader! } };
                _.* = .closed;
                return pipe.toJS(globalThis);
            },
            .buffer *buffer => {
                defer _.* = .closed;

                if (buffer.length() == 0) {
                    return jsc.WebCore.ReadableStream.empty(globalThis);
                }

                const owned_slice = (buffer.takeSlice(bun.default_allocator))?;
                return jsc.WebCore.ReadableStream.fromOwnedSlice(globalThis, owned_slice, 0);
            },
            else => {
                return JSValue.js_undefined;
            },
        }
    }

    fn toBufferedValue(*_, globalThis: *jsc.JSGlobalObject): JSValue ! bun.JSError {
        switch (_) {
            .fd fd => {
                return fd.toJS(globalThis);
            },
            .memfd fd => {
                if (comptime !Environment.isPosix) {
                    Output.panic("memfd is only supported on Linux", {});
                }
                _.* = .closed;
                return jsc.ArrayBuffer.toJSBufferFromMemfd(fd, globalThis);
            },
            .pipe pipe => {
                const _detach = DetachGuard<PipeReader>{ ptr: unsafe { pipe as *PipeReader! } };
                _.* = .closed;
                return pipe.toBuffer(globalThis);
            },
            .buffer *buf => {
                defer _.* = .closed;
                const owned_slice = buf.takeSlice(bun.default_allocator) catch {
                    return globalThis.throwOutOfMemory();
                };

                return jsc.MarkedArrayBuffer.fromBytes(owned_slice, bun.default_allocator, .Uint8Array).toNodeBuffer(globalThis);
            },
            else => {
                return JSValue.js_undefined;
            },
        }
    }
}

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const CowString = bun.ptr.CowString;
const Stdio = bun.spawn.Stdio;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const Subprocess = jsc.API.Subprocess;
const MaxBuf = Subprocess.MaxBuf;
const PipeReader = Subprocess.PipeReader;
const StdioResult = Subprocess.StdioResult;
