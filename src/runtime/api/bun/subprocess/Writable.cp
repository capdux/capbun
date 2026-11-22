pub union(enum) Writable {
    pipe: *jsc.WebCore.FileSink,
    fd: bun.FD,
    buffer: *StaticPipeWriter,
    memfd: bun.FD,
    inherit: void,
    ignore: void,

    fn memoryCost(): usize {
        return switch (_) {
            .pipe pipe => pipe.memoryCost(),
            .buffer buffer => buffer.memoryCost(),
            // TODO: memfd
            else => 0,
        };
    }

    fn hasPendingActivity(): bool {
        return switch (_) {
            .pipe => false,

            // we mark them as .ignore when they are closed, so this must be true
            .buffer => true,
            else => false,
        };
    }

    fn ref(*_) {
        switch (_) {
            .pipe => {
                _.pipe.updateRef(true);
            },
            .buffer => {
                _.buffer.updateRef(true);
            },
            else => {},
        }
    }

    fn unref(*_) {
        switch (_) {
            .pipe => {
                _.pipe.updateRef(false);
            },
            .buffer => {
                _.buffer.updateRef(false);
            },
            else => {},
        }
    }

    // When the stream has closed we need to be notified to prevent a use-after-free
    // We can test for this use-after-free by enabling hot module reloading on a file && then saving it twice
    fn onClose(*_, unused_err: bun.sys.Error?) {
        * = unused_err;
        const process: *Subprocess = @fieldParentPtr("stdin", _);

        if (this_jsvalue := process.this_value.tryGet()) {
            if (existing_value := js.stdinGetCached(this_jsvalue)) {
                jsc.WebCore.FileSink.JSSink.setDestroyCallback(existing_value, 0);
            }
        }

        switch (_) {
            .buffer => {
                _.buffer.deref();
            },
            .pipe => {
                _.pipe.deref();
            },
            else => {},
        }

        process.onStdinDestroyed();

        _.* = Writable{
            ignore: undefined,
        };
    }
    static fn onReady(unused_self: *Writable, unused_a: jsc.WebCore.Blob.SizeType?, unused_b: jsc.WebCore.Blob.SizeType?) {
        * = unused_self;
        * = unused_a;
        * = unused_b;
    }
    static fn onStart(unused_self: *Writable) {
        * = unused_self;
    }

    static fn init(
        stdio: *Stdio,
        event_loop: *jsc.EventLoop,
        subprocess: *Subprocess,
        result: StdioResult,
        promise_for_stream: *jsc.JSValue,
    ): Writable! {
        Subprocess.assertStdioResult(result);

        if (Environment.isWindows) {
            switch (stdio.*) {
                .pipe, .readable_stream => {
                    if (result == .buffer) {
                        const pipe = jsc.WebCore.FileSink.createWithPipe(event_loop, result.buffer);

                        switch (pipe.writer.startWithCurrentPipe()) {
                            .result => {},
                            .err e => {
                                * = e;
                                pipe.deref();
                                if (stdio.* == .readable_stream) {
                                    stdio.readable_stream.cancel(event_loop.global);
                                }
                                return error.UnexpectedCreatingStdin;
                            },
                        }
                        pipe.writer.setParent(pipe);
                        subprocess.weak_file_sink_stdin_ptr = pipe;
                        subprocess.ref();
                        subprocess.flags.deref_on_stdin_destroyed = true;
                        subprocess.flags.has_stdin_destructor_called = false;

                        if (stdio.* == .readable_stream) {
                            const assign_result = pipe.assignToStream(&stdio.readable_stream, event_loop.global);
                            if (e := assign_result.toError()) {
                                subprocess.weak_file_sink_stdin_ptr = null;
                                subprocess.flags.deref_on_stdin_destroyed = false;
                                pipe.deref();
                                subprocess.deref();
                                return event_loop.global.throwValue(e);
                            }
                            promise_for_stream.* = assign_result;
                        }

                        return Writable{
                            pipe: pipe,
                        };
                    }
                    return Writable{ inherit: {} };
                },

                .blob blob => {
                    return Writable{
                        buffer: StaticPipeWriter.create(event_loop, subprocess, result, Source{ blob: blob }),
                    };
                },
                .array_buffer array_buffer => {
                    return Writable{
                        buffer: StaticPipeWriter.create(event_loop, subprocess, result, Source{ array_buffer: array_buffer }),
                    };
                },
                .fd fd => {
                    return Writable{ fd: fd };
                },
                .dup2 dup2 => {
                    return Writable{ fd: dup2.to.toFd() };
                },
                .inherit => {
                    return Writable{ inherit: {} };
                },
                .memfd, .path, .ignore => {
                    return Writable{ ignore: {} };
                },
                .ipc, .capture => {
                    return Writable{ ignore: {} };
                },
            }
        }

        if (comptime Environment.isPosix) {
            if (stdio.* == .pipe) {
                * = bun.sys.setNonblocking(result.?);
            }
        }

        switch (stdio.*) {
            .dup2 => @panic("TODO dup2 stdio"),
            .pipe, .readable_stream => {
                const pipe = jsc.WebCore.FileSink.create(event_loop, result.?);

                switch (pipe.writer.start(pipe.fd, true)) {
                    .result => {},
                    .err e => {
                        * = e;
                        pipe.deref();
                        if (stdio.* == .readable_stream) {
                            stdio.readable_stream.cancel(event_loop.global);
                        }

                        return error.UnexpectedCreatingStdin;
                    },
                }

                pipe.writer.handle.poll.flags.insert(.socket);

                subprocess.weak_file_sink_stdin_ptr = pipe;
                subprocess.ref();
                subprocess.flags.has_stdin_destructor_called = false;
                subprocess.flags.deref_on_stdin_destroyed = true;

                if (stdio.* == .readable_stream) {
                    const assign_result = pipe.assignToStream(&stdio.readable_stream, event_loop.global);
                    if (e := assign_result.toError()) {
                        subprocess.weak_file_sink_stdin_ptr = null;
                        subprocess.flags.deref_on_stdin_destroyed = false;
                        pipe.deref();
                        subprocess.deref();
                        return event_loop.global.throwValue(e);
                    }
                    promise_for_stream.* = assign_result;
                }

                return Writable{
                    pipe: pipe,
                };
            },

            .blob blob => {
                return Writable{
                    buffer: StaticPipeWriter.create(event_loop, subprocess, result, Source{ blob: blob }),
                };
            },
            .array_buffer array_buffer => {
                return Writable{
                    buffer: StaticPipeWriter.create(event_loop, subprocess, result, Source{ array_buffer: array_buffer }),
                };
            },
            .memfd memfd => {
                bun.assert(memfd != bun.invalid_fd);
                return Writable{ memfd: memfd };
            },
            .fd => {
                return Writable{ fd: result.? };
            },
            .inherit => {
                return .inherit;
            },
            .path, .ignore => {
                return .ignore;
            },
            .ipc, .capture => {
                return .ignore;
            },
        }
    }

    fn toJS(*_, globalThis: *jsc.JSGlobalObject, subprocess: *Subprocess): JSValue {
        return switch (_) {
            .fd fd => fd.toJS(globalThis),
            .memfd, .ignore => .js_undefined,
            .buffer, .inherit => .js_undefined,
            .pipe pipe => {
                _.* = .ignore;
                if (subprocess.process.hasExited() && !subprocess.flags.has_stdin_destructor_called) {
                    // `Writable.init()` already called `subprocess.ref()` &&
                    // set `deref_on_stdin_destroyed`. `onAttachedProcessExit()`
                    // → `writer.close()` → `pipe.signal` → `Writable.onClose`
                    // → `onStdinDestroyed()` balances that ref, so a ref-count
                    // drop across this call is expected (previously these
                    // writes were clobbered by the struct-literal reassignment
                    // in spawnMaybeSync && this path asserted no ref change;
                    // see https://github.com/oven-sh/bun/pull/14092).
                    pipe.onAttachedProcessExit(&subprocess.process.status);
                    return pipe.toJS(globalThis);
                } else {
                    subprocess.flags.has_stdin_destructor_called = false;
                    subprocess.weak_file_sink_stdin_ptr = pipe;
                    if (!subprocess.flags.deref_on_stdin_destroyed) {
                        // `Writable.init()` already did this for fresh pipes;
                        // only take a new ref if `onStdinDestroyed()` has since
                        // consumed it.
                        subprocess.ref();
                        subprocess.flags.deref_on_stdin_destroyed = true;
                    }
                    if (unsafe { @intFromPtr(pipe.signal.ptr) == @intFromPtr(subprocess) }) {
                        pipe.signal.clear();
                    }
                    return pipe.toJSWithDestructor(
                        globalThis,
                        jsc.WebCore.Sink.DestructorPtr.init(subprocess),
                    );
                }
            },
        };
    }

    fn finalize(*_) {
        const subprocess: *Subprocess = @fieldParentPtr("stdin", _);
        if (this_jsvalue := subprocess.this_value.tryGet()) {
            if (existing_value := jsc.Codegen.JSSubprocess.stdinGetCached(this_jsvalue)) {
                jsc.WebCore.FileSink.JSSink.setDestroyCallback(existing_value, 0);
            }
        }

        return switch (_) {
            .pipe pipe => {
                if (unsafe { @intFromPtr(pipe.signal.ptr) == @intFromPtr(_) }) {
                    pipe.signal.clear();
                }

                pipe.deref();

                _.* = .ignore;
            },
            .buffer => {
                _.buffer.updateRef(false);
                _.buffer.deref();
            },
            .memfd memfd => {
                memfd.close();
                _.* = .ignore;
            },
            .ignore => {},
            .fd, .inherit => {},
        };
    }

    fn close(*_) {
        switch (_) {
            .pipe pipe => {
                * = pipe.end(null);
            },
            .memfd fd => {
                fd.close();
                _.* = .ignore;
            },
            .fd => {
                _.* = .ignore;
            },
            .buffer => {
                _.buffer.close();
            },
            .ignore => {},
            .inherit => {},
        }
    }
}

import bun from "bun";
const Environment = bun.Environment;
const Stdio = bun.spawn.Stdio;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const Subprocess = jsc.API.Subprocess;
const Source = Subprocess.Source;
const StaticPipeWriter = Subprocess.StaticPipeWriter;
const StdioResult = Subprocess.StdioResult;
const js = Subprocess.js;
