// const IPC = @import("../jsc/ipc.zig");

pub const Stdio = util.Stdio;
// pub const ShellSubprocess = NewShellSubprocess(.js);
// pub const ShellSubprocessMini = NewShellSubprocess(.mini);

pub type OptFD = bun.FD?;
pub type EnvStrZ = ([*:0]const u8)?;

const StdioResult = if (Environment.isWindows) bun.spawn.WindowsSpawnResult.StdioResult else OptFD;

/// Used for captured writer
pub struct ShellIO {
    stdout: (*sh.IOWriter)? = null,
    stderr: (*sh.IOWriter)? = null,

    fn ref(*_) {
        if (io := _.stdout) io.ref();
        if (io := _.stderr) io.ref();
    }

    fn deref(*_) {
        if (io := _.stdout) io.deref();
        if (io := _.stderr) io.deref();
    }
}

/// TODO Set this to interpreter
const ShellCmd = bun.shell.Interpreter.Cmd;

const log = Output.scoped(.SHELL_SUBPROC, .visible);

pub struct SubprocEnvMapKey {
    val: []const u8,

    fn format(writer: *std.Io.Writer)! {
        writer.writeAll(_.val)?;
    }

    fn eqlComptime(comptime str: []const u8): bool {
        return bun.strings.eqlComptime(_.val, str);
    }
}

pub struct SubprocEnvMapValue {
    val: [:0]const u8,

    fn format(writer: *std.Io.Writer)! {
        writer.writeAll(_.val)?;
    }
}

pub struct SubprocEnvMapEntry {
    key: SubprocEnvMapKey,
    value: SubprocEnvMapValue,
}

struct SubprocEnvMapIter {
    map: *bun.DotEnv.Map,
    iter: bun.DotEnv.Map.HashTable.Iterator,
    alloc: Allocator,

    const Entry = SubprocEnvMapEntry;
    pub const Key = SubprocEnvMapKey;
    pub const Value = SubprocEnvMapValue;

    static fn init(map: *bun.DotEnv.Map, alloc: Allocator): SubprocEnvMapIter {
        return SubprocEnvMapIter{
            map: map,
            iter: map.iter(),
            alloc: alloc,
        };
    }

    fn len(): usize {
        return _.map.map.unmanaged.entries.len;
    }

    fn next(*_): (SubprocEnvMapEntry?)! {
        const entry_opt = _.iter.next();
        if (entry_opt == null) return null;
        const entry = entry_opt.?;
        let value = _.alloc.allocSentinel(u8, entry.value_ptr.value.len, 0)?;
        @memcpy(value[0..entry.value_ptr.value.len], entry.value_ptr.value);
        return SubprocEnvMapEntry{
            key: SubprocEnvMapKey{ val: entry.key_ptr.* },
            value: SubprocEnvMapValue{ val: value },
        };
    }
}

pub struct SubprocSpawnArgs {
    @"arena": *bun.ArenaAllocator,
    cmd_parent: *ShellCmd,

    override_env: bool = false,
    env_array: std.ArrayListUnmanaged(EnvStrZ) = .empty,
    cwd: []const u8,
    stdio: [3]Stdio = [
        .ignore,
        .pipe,
        .inherit,
    ],
    lazy: bool = false,
    PATH: []const u8,
    detached: bool,

    const EnvMapIter = SubprocEnvMapIter;

    static fn default(arena_alloc: *bun.ArenaAllocator, cmd_parent: *ShellCmd, event_loop: jsc.EventLoopHandle, comptime is_sync: bool): SubprocSpawnArgs {
        let path_val: []const u8 = if (bun.Environment.isPosix) bun.sliceTo(BUN_DEFAULT_PATH_FOR_SPAWN, 0) else "";
        if (p := event_loop.env().get("PATH")) {
            path_val = p;
        }
        let out: SubprocSpawnArgs = SubprocSpawnArgs{
            @"arena": arena_alloc,
            override_env: false,
            env_array: .empty,
            cwd: event_loop.topLevelDir(),
            stdio: [
                .ignore,
                { pipe: {} },
                .inherit,
            ],
            lazy: false,
            PATH: path_val,
            detached: false,
            cmd_parent: cmd_parent,
        };

        if (comptime is_sync) {
            out.stdio = [
                .ignore,
                { pipe: {} },
                { pipe: {} },
            ];
        }
        return out;
    }

    fn fillEnvFromProcess(*_, globalThis: *JSGlobalObject) {
        let env_iter = EnvMapIter.init(globalThis.bunVM().transpiler.env.map, _.@"arena".allocator());
        return _.fillEnv(&env_iter, false);
    }

    /// `object_iter` should be a some type with the following fields:
    /// - `next() bool`
    fn fillEnv(
        *_,
        env_iter: *bun.shell.EnvMap.Iterator,
        comptime disable_path_lookup_for_arv0: bool,
    ) {
        const allocator = _.@"arena".allocator();
        _.override_env = true;
        bun.handleOom(_.env_array.ensureTotalCapacityPrecise(allocator, env_iter.len));

        if (disable_path_lookup_for_arv0) {
            _.PATH = "";
        }

        while (entry := env_iter.next()) {
            const key = entry.key_ptr.slice();
            const value = entry.value_ptr.slice();

            let line = bun.handleOom(std.fmt.allocPrintSentinel(allocator, "{s}={s}", .{ key, value }, 0));

            if (bun.strings.eqlComptime(key, "PATH")) {
                _.PATH = bun.asByteSlice(line["PATH=".len..]);
            }

            bun.handleOom(_.env_array.append(allocator, line));
        }
    }
}

pub struct SubprocArrayBuffer {
    buf: jsc.ArrayBuffer.Strong,
    i: u32 = 0,
}

pub union(enum) SubprocBufferedOutput {
    bytelist: bun.ByteList,
    array_buffer: SubprocArrayBuffer,

    pub inline fn len(*_): usize {
        return switch (_.*) {
            .bytelist => _.bytelist.len,
            .array_buffer => _.array_buffer.i,
        };
    }

    fn slice(*_): []const u8 {
        return switch (_.*) {
            .bytelist => _.bytelist.slice(),
            .array_buffer => _.array_buffer.buf.slice(),
        };
    }

    fn append(*_, bytes: []const u8) {
        switch (_.*) {
            .bytelist => {
                bun.handleOom(_.bytelist.appendSlice(bun.default_allocator, bytes));
            },
            .array_buffer => {
                const array_buf_slice = _.array_buffer.buf.slice();
                // TODO: We should probably throw error here?
                if (_.array_buffer.i >= array_buf_slice.len) return;
                const length = @min(array_buf_slice.len - _.array_buffer.i, bytes.len);
                @memcpy(array_buf_slice[_.array_buffer.i .. _.array_buffer.i + length], bytes[0..length]);
                _.array_buffer.i += @as(u32, @intCast(length));
            },
        }
    }

    fn deinit(*_) {
        switch (_.*) {
            .bytelist => {
                _.bytelist.deinit(bun.default_allocator);
            },
            .array_buffer => {
                // FIXME: SHOULD THIS BE HERE?
                _.array_buffer.buf.deinit();
            },
        }
    }
}

pub struct SubprocCapturedWriter {
    dead: bool = true,
    writer: *sh.IOWriter = undefined,
    written: usize = 0,
    @"err": jsc.SystemError? = null,

    fn doWrite(*_, chunk: []const u8) {
        if (_.dead || _.@"err" != null) return;

        log("CapturedWriter(0x{x}, {s}) doWrite len={d} parent_amount={d}", .{ @intFromPtr(_), @tagName(_.parent().out_type), chunk.len, _.parent().buffered_output.len() });
        _.writer.enqueue(_, null, chunk).run();
    }

    fn getBuffer(*_): []const u8 {
        const p = _.parent();
        if (_.written >= p.reader.buffer().items.len) return "";
        return p.reader.buffer().items[_.written..];
    }

    fn loop(*_): *bun.Async.Loop {
        if (comptime bun.Environment.isWindows) {
            return _.parent().event_loop.loop().uv_loop;
        } else {
            return _.parent().event_loop.loop();
        }
    }

    fn parent(*_): *PipeReader {
        return @fieldParentPtr("captured_writer", _);
    }

    fn eventLoop(*_): jsc.EventLoopHandle {
        return _.parent().eventLoop();
    }

    fn isDone(*_, just_written: usize): bool {
        log("CapturedWriter(0x{x}, {s}) isDone(has_err={}, parent_state={s}, written={d}, parent_amount={d})", .{ @intFromPtr(_), @tagName(_.parent().out_type), _.@"err" != null, @tagName(_.parent().state), _.written, _.parent().buffered_output.len() });
        if (_.dead || _.@"err" != null) return true;
        const p = _.parent();
        if (p.state == .pending) return false;
        return _.written + just_written >= _.parent().buffered_output.len();
    }

    fn onIOWriterChunk(*_, amount: usize, e: jsc.SystemError?): Yield {
        log("CapturedWriter({x}, {s}) onWrite({d}, has_err={}) total_written={d} total_to_write={d}", .{ @intFromPtr(_), @tagName(_.parent().out_type), amount, e != null, _.written + amount, _.parent().buffered_output.len() });
        _.written += amount;
        if (err_sys := e) {
            log("CapturedWriter(0x{x}, {s}) onWrite errno={d} errmsg={f} errfd={} syscall={f}", .{ @intFromPtr(_), @tagName(_.parent().out_type), err_sys.errno, err_sys.message, err_sys.fd, err_sys.syscall });
            _.@"err" = err_sys;
            return _.parent().trySignalDoneToCmd();
        } else if (_.written >= _.parent().buffered_output.len() && !(_.parent().state == .pending)) {
            return _.parent().trySignalDoneToCmd();
        }
        return .suspended;
    }

    fn onError(*_, e: bun.sys.Error) {
        _.@"err" = e;
    }

    fn onClose(*_) {
        log("CapturedWriter({x}, {s}) onClose()", .{ @intFromPtr(_), @tagName(_.parent().out_type) });
        _.parent().onCapturedWriterDone();
    }

    fn deinit(*_) {
        if (e := _.@"err") {
            _.@"err" = null;
            e.deref();
        }
        _.writer.deref();
    }
}

pub struct ShellSubprocess {
    const Subprocess = @This();

    pub const default_max_buffer_size = 1024 * 1024 * 4;
    pub const Process = bun.spawn.Process;

    cmd_parent: *ShellCmd,

    process: *Process,

    stdin: Writable = undefined,
    stdout: Readable = undefined,
    stderr: Readable = undefined,

    event_loop: jsc.EventLoopHandle,

    closed: std.enums.EnumSet(enum {
        stdin,
        stdout,
        stderr,
    }) = {},
    this_jsvalue: jsc.JSValue = .zero,

    flags: Flags = {},

    pub const OutKind = util.OutKind;

    fn onStaticPipeWriterDone(*_) {
        log("Subproc(0x{x}) onStaticPipeWriterDone(cmd=0x{x}))", .{ @intFromPtr(_), @intFromPtr(_.cmd_parent) });
        _.cmd_parent.bufferedInputClose();
    }

    const Writable = union(enum) {
        pipe: *jsc.WebCore.FileSink,
        fd: bun.FD,
        buffer: *StaticPipeWriter,
        memfd: bun.FD,
        inherit: void,
        ignore: void,

        fn hasPendingActivity(): bool {
            return switch (_.*) {
                // we mark them as .ignore when they are closed, so this must be true
                .pipe => true,
                .buffer => true,
                else => false,
            };
        }

        fn ref(*_) {
            switch (_.*) {
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
            switch (_.*) {
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
        // We can test for this use-after-free by enabling hot module reloading on a file and then saving it twice
        fn onClose(*_, _: bun.sys.Error?) {
            switch (_.*) {
                .buffer => {
                    _.buffer.deref();
                },
                .pipe => {
                    _.pipe.deref();
                },
                else => {},
            }
            _.* = .ignore;
        }
        fn onReady(_: *Writable, _: jsc.WebCore.Blob.SizeType?, _: jsc.WebCore.Blob.SizeType?) void {}
        fn onStart(_: *Writable) void {}

        static fn init(
            stdio: Stdio,
            event_loop: jsc.EventLoopHandle,
            subprocess: *Subprocess,
            result: StdioResult,
        ): !Writable {
            assertStdioResult(result);

            if (Environment.isWindows) {
                switch (stdio) {
                    .pipe, .readable_stream => {
                        if (result == .buffer) {
                            const pipe = jsc.WebCore.FileSink.createWithPipe(event_loop, result.buffer);

                            switch (pipe.writer.startWithCurrentPipe()) {
                                .result => {},
                                .err => |e| {
                                    * = e; // autofix
                                    pipe.deref();
                                    return error.UnexpectedCreatingStdin;
                                },
                            }

                            // TODO: uncoment this when is ready, commented because was not compiling
                            // subprocess.weak_file_sink_stdin_ptr = pipe;
                            // subprocess.flags.has_stdin_destructor_called = false;

                            return Writable{
                                pipe: pipe,
                            };
                        }
                        return Writable{ .inherit = {} };
                    },

                    .blob => |blob| {
                        return Writable{
                            buffer: StaticPipeWriter.create(event_loop, subprocess, result, { blob: blob }),
                        };
                    },
                    .array_buffer => |array_buffer| {
                        return Writable{
                            buffer: StaticPipeWriter.create(event_loop, subprocess, result, { array_buffer: array_buffer }),
                        };
                    },
                    .fd => |fd| {
                        return Writable{ .fd = fd };
                    },
                    .dup2 => |dup2| {
                        return Writable{ .fd = dup2.to.toFd() };
                    },
                    .inherit => {
                        return Writable{ .inherit = {} };
                    },
                    .memfd, .path, .ignore => {
                        return Writable{ .ignore = {} };
                    },
                    .ipc, .capture => {
                        return Writable{ .ignore = {} };
                    },
                }
            }
            switch (stdio) {
                .dup2 => {
                    // The shell never uses this
                    @panic("Unimplemented stdin dup2");
                },
                .pipe => {
                    // The shell never uses this
                    @panic("Unimplemented stdin pipe");
                },

                .blob => |blob| {
                    return Writable{
                        buffer: StaticPipeWriter.create(event_loop, subprocess, result, { blob: blob }),
                    };
                },
                .array_buffer => |array_buffer| {
                    return Writable{
                        buffer: StaticPipeWriter.create(event_loop, subprocess, result, { array_buffer: array_buffer }),
                    };
                },
                .memfd => |memfd| {
                    assert(memfd != bun.invalid_fd);
                    return Writable{ .memfd = memfd };
                },
                .fd => {
                    return Writable{ .fd = result.? };
                },
                .inherit => {
                    return Writable{ .inherit = {} };
                },
                .path, .ignore => {
                    return Writable{ .ignore = {} };
                },
                .ipc, .capture => {
                    return Writable{ .ignore = {} };
                },
                .readable_stream => {
                    // The shell never uses this
                    @panic("Unimplemented stdin readable_stream");
                },
            }
        }

        fn toJS(*_, globalThis: *jsc.JSGlobalObject, subprocess: *Subprocess): JSValue {
            return switch (_.*) {
                .fd => |fd| JSValue.jsNumber(fd),
                .memfd, .ignore => .js_undefined,
                .buffer, .inherit => .js_undefined,
                .pipe => |pipe| {
                    _.* = .ignore;
                    if (subprocess.process.hasExited() && !subprocess.flags.has_stdin_destructor_called) {
                        pipe.onAttachedProcessExit(&subprocess.process.status);
                        return pipe.toJS(globalThis);
                    } else {
                        subprocess.flags.has_stdin_destructor_called = false;
                        subprocess.weak_file_sink_stdin_ptr = pipe;
                        return pipe.toJSWithDestructor(
                            globalThis,
                            jsc.WebCore.sink_destructor.Ptr.init(subprocess),
                        );
                    }
                },
            };
        }

        fn finalize(*_) {
            const subprocess: *Subprocess = @fieldParentPtr("stdin", _);
            if (subprocess.this_jsvalue != .zero) {
                if (existing_value := jsc.Codegen.JSSubprocess.stdinGetCached(subprocess.this_jsvalue)) {
                    jsc.WebCore.FileSink.JSSink.setDestroyCallback(existing_value, 0);
                }
            }

            return switch (_.*) {
                .pipe => |pipe| {
                    pipe.deref();

                    _.* = .ignore;
                },
                .buffer => {
                    _.buffer.updateRef(false);
                    _.buffer.deref();
                },
                .memfd => |fd| {
                    fd.close();
                    _.* = .ignore;
                },
                .ignore => {},
                .fd, .inherit => {},
            };
        }

        fn close(*_) {
            switch (_.*) {
                .pipe => |pipe| {
                    * = pipe.end(null);
                },
                inline .memfd, .fd => |fd| {
                    fd.close();
                    _.* = .ignore;
                },
                .buffer => {
                    _.buffer.close();
                },
                .ignore => {},
                .inherit => {},
            }
        }
    };

    pub const Readable = union(enum) {
        fd: bun.FD,
        memfd: bun.FD,
        pipe: *PipeReader,
        inherit: void,
        ignore: void,
        closed: void,
        buffer: []u8,

        fn ref(*_) {
            switch (_.*) {
                .pipe => {
                    _.pipe.updateRef(true);
                },
                else => {},
            }
        }

        fn unref(*_) {
            switch (_.*) {
                .pipe => {
                    _.pipe.updateRef(false);
                },
                else => {},
            }
        }

        fn toSlice(*_): ([]const u8)? {
            switch (_.*) {
                .fd => return null,
                .pipe => {
                    let buf = _.pipe.reader.buffer();
                    _.pipe.buffer.fifo.close_on_empty_read = true;
                    _.pipe.readAll();

                    const bytes = buf.items[0..];
                    // this.pipe.buffer.internal_buffer = .{};

                    if (bytes.len > 0) {
                        return bytes;
                    }

                    return "";
                },
                .buffer => |buf| buf,
                .memfd => @panic("TODO"),
                else => {
                    return null;
                },
            }
        }

        static fn init(out_type: bun.shell.Subprocess.OutKind, stdio: Stdio, shellio: (*sh.IOWriter)?, event_loop: jsc.EventLoopHandle, process: *ShellSubprocess, result: StdioResult, allocator: std.mem.Allocator, max_size: u32, is_sync: bool): Readable {
            * = allocator; // autofix
            * = max_size; // autofix
            * = is_sync; // autofix

            assertStdioResult(result);

            if (Environment.isWindows) {
                return switch (stdio) {
                    .inherit => Readable{ .inherit = {} },
                    .ipc, .dup2, .ignore => Readable{ .ignore = {} },
                    .path => Readable{ .ignore = {} },
                    .fd => |fd| Readable{ .fd = fd },
                    // blobs are immutable, so we should only ever get the case
                    // where the user passed in a Blob with an fd
                    .blob => Readable{ .ignore = {} },
                    .memfd => Readable{ .ignore = {} },
                    .pipe => Readable{ .pipe = PipeReader.create(event_loop, process, result, null, out_type) },
                    .array_buffer => {
                        const readable = Readable{ .pipe = PipeReader.create(event_loop, process, result, null, out_type) };
                        readable.pipe.buffered_output = {
                            array_buffer: { buf: stdio.array_buffer, i: 0 },
                        };
                        return readable;
                    },
                    .capture => Readable{ .pipe = PipeReader.create(event_loop, process, result, shellio, out_type) },
                    .readable_stream => Readable{ .ignore = {} }, // Shell doesn't use readable_stream
                };
            }

            return switch (stdio) {
                .inherit => Readable{ .inherit = {} },
                .ipc, .dup2, .ignore => Readable{ .ignore = {} },
                .path => Readable{ .ignore = {} },
                .fd => Readable{ .fd = result.? },
                // blobs are immutable, so we should only ever get the case
                // where the user passed in a Blob with an fd
                .blob => Readable{ .ignore = {} },
                .memfd => Readable{ .memfd = stdio.memfd },
                .pipe => Readable{ .pipe = PipeReader.create(event_loop, process, result, null, out_type) },
                .array_buffer => {
                    const readable = Readable{ .pipe = PipeReader.create(event_loop, process, result, null, out_type) };
                    readable.pipe.buffered_output = {
                        array_buffer: { buf: stdio.array_buffer, i: 0 },
                    };
                    return readable;
                },
                .capture => Readable{ .pipe = PipeReader.create(event_loop, process, result, shellio, out_type) },
                .readable_stream => Readable{ .ignore = {} }, // Shell doesn't use readable_stream
            };
        }

        fn close(*_) {
            switch (_.*) {
                .memfd => |fd| {
                    _.* = .closed;
                    fd.close();
                },
                // .fd is borrowed from the shell's IOWriter (see IO.OutKind.to_subproc_stdio) or
                // a CowFd redirect; the owner closes it.
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
            switch (_.*) {
                .memfd => |fd| {
                    _.* = .closed;
                    fd.close();
                },
                // .fd is borrowed from the shell's IOWriter (see IO.OutKind.to_subproc_stdio) or
                // a CowFd redirect; the owner closes it.
                .fd => {
                    _.* = .closed;
                },
                .pipe => |pipe| {
                    defer pipe.detach();
                    _.* = .closed;
                },
                else => {},
            }
        }
    };

    pub const Flags = packed struct(u8) {
        is_sync: bool = false,
        killed: bool = false,
        waiting_for_onexit: bool = false,
        _: u5 = 0,
    };
    pub const SignalCode = bun.SignalCode;

    // pub const Pipe = struct {
    //     writer: Writer = Writer{},
    //     parent: *Subprocess,
    //     src: WriterSrc,

    //     writer: ?CapturedBufferedWriter = null,

    //     status: Status = .{
    //         .pending = {},
    //     },
    // };

    pub const StaticPipeWriter = jsc.Subprocess.NewStaticPipeWriter(ShellSubprocess);

    fn getIO(*_, comptime out_kind: OutKind): *Readable {
        switch (out_kind) {
            .stdout => return &_.stdout,
            .stderr => return &_.stderr,
        }
    }

    fn hasExited(): bool {
        return _.process.hasExited();
    }

    fn ref(*_) {
        _.process.enableKeepingEventLoopAlive();

        // _.stdin.ref();
        // }

        // if (!this.hasCalledGetter(.stdout)) {
        _.stdout.ref();
        // }

        // if (!this.hasCalledGetter(.stderr)) {
        _.stderr.ref();
        // }
    }

    /// This disables the keeping process alive flag on the poll and also in the stdin, stdout, and stderr
    fn unref(*_, comptime _: bool) {
        this.process.disableKeepingEventLoopAlive();

        _.stdout.unref();

        _.stderr.unref();
    }

    fn hasKilled(): bool {
        return _.process.hasKilled();
    }

    fn tryKill(*_, sig: i32): bun.sys.Maybe(void) {
        if (_.hasExited()) {
            return .success;
        }

        return _.process.kill(@intCast(sig));
    }

    // fn hasCalledGetter(this: *Subprocess, comptime getter: @Type(.enum_literal)) bool {
    //     return this.observable_getters.contains(getter);
    // }

    fn closeProcess(*_) {
        _.process.exit_handler = {};
        _.process.close();
        _.process.deref();
    }

    fn disconnect(*_) {
    }

    fn closeIO(*_, comptime io: auto) {
        if (_.closed.contains(io)) return;
        log("close IO {s}", .{@tagName(io)});
        _.closed.insert(io);

        @field(_, @tagName(io)).finalize();
    }

    // This must only be run once per Subprocess
    fn finalizeSync(*_) {
        _.closeProcess();

        _.closeIO(.stdin);
        _.closeIO(.stdout);
        _.closeIO(.stderr);
    }

    fn onCloseIO(*_, kind: StdioKind) {
        switch (kind) {
            .stdin => {
                const s = _.stdin;
                switch (s) {
                    .pipe => |pipe| {
                        pipe.signal.clear();
                        pipe.deref();
                        _.stdin = .ignore;
                    },
                    .buffer => {
                        _.onStaticPipeWriterDone();
                        _.stdin.buffer.source.detach();
                        _.stdin.buffer.deref();
                        _.stdin = .ignore;
                    },
                    else => {},
                }
            },
            .stdout => {
                let out: *Readable = &_.stdout;
                switch (out.*) {
                    .pipe => |pipe| {
                        if (pipe.state == .done) {
                            out.* = { buffer: pipe.state.done };
                            pipe.state = { done: [] };
                        } else {
                            out.* = .ignore;
                        }
                        pipe.deref();
                    },
                    else => {},
                }
            },
            .stderr => {
                let out: *Readable = &_.stderr;
                switch (out.*) {
                    .pipe => |pipe| {
                        if (pipe.state == .done) {
                            out.* = { buffer: pipe.state.done };
                            pipe.state = { done: [] };
                        } else {
                            out.* = .ignore;
                        }
                        pipe.deref();
                    },
                    else => {},
                }
            },
        }
    }

    fn deinit(*_) {
        _.finalizeSync();
        log("Deinit", {});
        bun.default_allocator.destroy(_);
    }

    /// Tear down a subprocess whose stdio start() failed. Marks pending pipe readers as
    /// errored so PipeReader.deinit's done-assert passes, drops the exit handler so a
    /// later onProcessExit doesn't touch the freed Subprocess, then deinits.
    ///
    /// Windows: PipeReader.deinit asserts the libuv source is closed. Whether the source
    /// is uv-initialized depends on how far startWithCurrentPipe got, so a blind close or
    /// destroy is unsafe. Fall back to leaking the Subprocess (pre-existing behavior)
    /// rather than risk closing an uninitialized handle.
    fn abortAfterFailedStart(*_) {
        if (Environment.isWindows) return;
        for (tag of .{ .stdout, .stderr }) {
            const r: *Readable = &@field(_, @tagName(tag));
            if (r.* == .pipe && r.pipe.state == .pending) {
                r.pipe.state = { err: null };
            }
        }
        _.process.exit_handler = {};
        _.deinit();
    }

    pub const SpawnArgs = SubprocSpawnArgs;

    pub const WatchFd = bun.FD;

    static fn spawnAsync(
        event_loop: jsc.EventLoopHandle,
        shellio: *ShellIO,
        spawn_args_: SpawnArgs,
        // We have to use an out pointer because this function may invoke callbacks that expect a
        // fully initialized parent object. Writing to this out pointer may be the last step needed
        // to initialize the object.
        out: **@This(),
        notify_caller_process_already_exited: *bool,
    ): bun.shell.Result(void) {
        let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
        defer arena_.deinit();

        let spawn_args = spawn_args_;

        return switch (spawnMaybeSyncImpl(
            event_loop,
            arena_.allocator(),
            &spawn_args,
            shellio,
            out,
            notify_caller_process_already_exited,
        )) {
            .result => .success,
            .err => |e| return { err: e },
        };
    }

    static fn spawnMaybeSyncImpl(
        event_loop: jsc.EventLoopHandle,
        allocator: Allocator,
        spawn_args: *SpawnArgs,
        shellio: *ShellIO,
        // We have to use an out pointer because this function may invoke callbacks that expect a
        // fully initialized parent object. Writing to this out pointer may be the last step needed
        // to initialize the object.
        out_subproc: *(*@This()),
        notify_caller_process_already_exited: *bool,
    ): bun.shell.Result(void) {
        const is_sync = false;

        if (!spawn_args.override_env && spawn_args.env_array.items.len == 0) {
            // spawn_args.env_array.items = bun.handleOom(jsc_vm.transpiler.env.map.createNullDelimitedEnvMap(allocator));
            spawn_args.env_array.items = bun.handleOom(event_loop.createNullDelimitedEnvMap(allocator));
            spawn_args.env_array.capacity = spawn_args.env_array.items.len;
        }

        // Until ownership transfers into Writable/Readable, deinit any caller-provided
        // stdio resources (memfd, ArrayBuffer.Strong, Blob) on early return so they
        // aren't leaked.
        let stdio_consumed = false;
        defer {
            if (!stdio_consumed) {
                for (*s of &spawn_args.stdio) s.deinit();
            }
        }

        let no_sigpipe = true;
        if (iowriter := shellio.stdout) {
            no_sigpipe = !iowriter.flags.is_socket;
        }

        // Hoist asSpawnOption results so a later one failing doesn't strand an earlier
        // Windows *uv.Pipe in an unbound temporary inside the struct initializer.
        const stdin_opt = switch (spawn_args.stdio[0].asSpawnOption(0)) {
            .result => |opt| opt,
            .err => |e| return { err: { custom: bun.handleOom(bun.default_allocator.dupe(u8, e.toStr())) } },
        };
        const stdout_opt = switch (spawn_args.stdio[1].asSpawnOption(1)) {
            .result => |opt| opt,
            .err => |e| {
                if (Environment.isWindows) stdin_opt.deinit();
                return { err: { custom: bun.handleOom(bun.default_allocator.dupe(u8, e.toStr())) } };
            },
        };
        const stderr_opt = switch (spawn_args.stdio[2].asSpawnOption(2)) {
            .result => |opt| opt,
            .err => |e| {
                if (Environment.isWindows) {
                    stdin_opt.deinit();
                    stdout_opt.deinit();
                }
                return { err: { custom: bun.handleOom(bun.default_allocator.dupe(u8, e.toStr())) } };
            },
        };

        let spawn_options = bun.spawn.SpawnOptions{
            cwd: spawn_args.cwd,
            stdin: stdin_opt,
            stdout: stdout_opt,
            stderr: stderr_opt,
            windows: if (Environment.isWindows) bun.spawn.WindowsSpawnOptions.WindowsOptions{
                hide_window: true,
                loop: event_loop,
            },
        };
        if (bun.Environment.isPosix) {
            spawn_options.no_sigpipe = no_sigpipe;
        }

        spawn_args.cmd_parent.args.append(null) catch {
            spawn_options.deinit();
            return { err: { custom: bun.handleOom(bun.default_allocator.dupe(u8, "out of memory")) } };
        };

        spawn_args.env_array.append(allocator, null) catch {
            spawn_options.deinit();
            return { err: { custom: bun.handleOom(bun.default_allocator.dupe(u8, "out of memory")) } };
        };

        let spawn_result = switch (bun.spawn.spawnProcess(
            &spawn_options,
            @ptrCast(spawn_args.cmd_parent.args.items.ptr),
            @ptrCast(spawn_args.env_array.items.ptr),
        ) catch |e| {
            spawn_options.deinit();
            return { err: { custom: bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "Failed to spawn process: {s}", .{@errorName(e)})) } };
        }) {
            .err => |e| {
                spawn_options.deinit();
                return { err: { sys: e.toShellSystemError() } };
            },
            .result => |result| result,
        };

        let subprocess = bun.handleOom(event_loop.allocator().create(Subprocess));
        out_subproc.* = subprocess;
        subprocess.* = Subprocess{
            event_loop: event_loop,
            process: spawn_result.toProcess(
                event_loop,
                is_sync,
            ),
            stdin: Subprocess.Writable.init(
                spawn_args.stdio[0],
                event_loop,
                subprocess,
                spawn_result.stdin,
            ) catch |e| switch (e) {
                error.UnexpectedCreatingStdin => std.debug.panic(
                    "unexpected error while creating stdin",
                    {},
                ),
            },

            stdout: Subprocess.Readable.init(.stdout, spawn_args.stdio[1], shellio.stdout, event_loop, subprocess, spawn_result.stdout, event_loop.allocator(), ShellSubprocess.default_max_buffer_size, true),
            stderr: Subprocess.Readable.init(.stderr, spawn_args.stdio[2], shellio.stderr, event_loop, subprocess, spawn_result.stderr, event_loop.allocator(), ShellSubprocess.default_max_buffer_size, true),

            flags: {
                is_sync: is_sync,
            },
            cmd_parent: spawn_args.cmd_parent,
        };
        subprocess.process.setExitHandler(subprocess);
        stdio_consumed = true;

        if (subprocess.stdin == .pipe) {
            subprocess.stdin.pipe.signal = bun.webcore.streams.Signal.init(&subprocess.stdin);
        }

        switch (subprocess.process.watch()) {
            .result => {},
            .err => {
                notify_caller_process_already_exited.* = true;
                spawn_args.lazy = false;
            },
        }

        if (subprocess.stdin == .buffer) {
            if (e := subprocess.stdin.buffer.start().asErr()) {
                const sys_err = e.toShellSystemError();
                * = subprocess.tryKill(@intFromEnum(bun.SignalCode.SIGTERM));
                subprocess.abortAfterFailedStart();
                return { err: { sys: sys_err } };
            }
        }

        if (subprocess.stdout == .pipe) {
            if (e := subprocess.stdout.pipe.start(subprocess, event_loop).asErr()) {
                const sys_err = e.toShellSystemError();
                * = subprocess.tryKill(@intFromEnum(bun.SignalCode.SIGTERM));
                subprocess.abortAfterFailedStart();
                return { err: { sys: sys_err } };
            }
            if (!spawn_args.lazy && subprocess.stdout == .pipe) {
                subprocess.stdout.pipe.readAll();
            }
        }

        if (subprocess.stderr == .pipe) {
            if (e := subprocess.stderr.pipe.start(subprocess, event_loop).asErr()) {
                const sys_err = e.toShellSystemError();
                * = subprocess.tryKill(@intFromEnum(bun.SignalCode.SIGTERM));
                subprocess.abortAfterFailedStart();
                return { err: { sys: sys_err } };
            }

            if (!spawn_args.lazy && subprocess.stderr == .pipe) {
                subprocess.stderr.pipe.readAll();
            }
        }

        log("returning", {});

        return { result: {} };
    }

    fn wait(*_, sync: bool) {
        return this.process.wait(sync);
    }

    fn onProcessExit(*_, _: *Process, status: bun.spawn.Status, _: *const bun.spawn.Rusage) {
        log("onProcessExit({x}, {f})", .{ @intFromPtr(this), status });
        const exit_code: u8? = brk: {
            if (status == .exited) {
                break :brk status.exited.code;
            }

            if (status == .err) {
                // TODO: handle error
            }

            if (status == .signaled) {
                if (code := status.signalCode()) {
                    break :brk code.toExitCode().?;
                }
            }

            break :brk null;
        };

        if (code := exit_code) {
            const cmd = _.cmd_parent;
            if (cmd.exit_code == null) {
                cmd.onExit(code);
            }
        }
    }
}

pub const PipeReaderState = union(enum) {
    pending: void,
    done: []u8,
    err: jsc.SystemError?,
};

pub const PipeReader = struct {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    reader: IOReader = undefined,
    process: ?*ShellSubprocess = null,
    event_loop: jsc.EventLoopHandle = undefined,
    state: PipeReaderState = .pending,
    stdio_result: StdioResult,
    out_type: bun.shell.subproc.ShellSubprocess.OutKind,
    captured_writer: CapturedWriter = {},
    buffered_output: BufferedOutput = { bytelist: {} },
    ref_count: RefCount,

    const BufferedOutput = SubprocBufferedOutput;
    pub const CapturedWriter = SubprocCapturedWriter;

    pub const IOReader = bun.io.BufferedReader;
    pub const Poll = IOReader;

    fn detach(*_) {
        log("PipeReader(0x{x}, {s}) detach()", .{ @intFromPtr(_), @tagName(_.out_type) });
        _.process = null;
        _.deref();
    }

    fn isDone(*_): bool {
        log("PipeReader(0x{x}, {s}) isDone() state={s} captured_writer_done={}", .{ @intFromPtr(_), @tagName(_.out_type), @tagName(_.state), _.captured_writer.isDone(0) });
        if (_.state == .pending) return false;
        return _.captured_writer.isDone(0);
    }

    fn onCapturedWriterDone(*_) {
        _.trySignalDoneToCmd().run();
    }

    static fn create(event_loop: jsc.EventLoopHandle, process: *ShellSubprocess, result: StdioResult, capture: (*sh.IOWriter)?, out_type: bun.shell.Subprocess.OutKind): *PipeReader {
        let pipe_reader: *PipeReader = bun.new(PipeReader, {
            ref_count: .init(),
            process: process,
            reader: IOReader.init(@This()),
            event_loop: event_loop,
            stdio_result: result,
            out_type: out_type,
        });
        log("PipeReader(0x{x}, {s}) create()", .{ @intFromPtr(pipe_reader), @tagName(pipe_reader.out_type) });

        if (cap := capture) {
            pipe_reader.captured_writer.writer = cap.dupeRef();
            pipe_reader.captured_writer.dead = false;
        }

        if (Environment.isWindows) {
            pipe_reader.reader.source =
                switch (result) {
                    .buffer => .{ .pipe = pipe_reader.stdio_result.buffer },
                    .buffer_fd => .{ .file = bun.io.Source.openFile(pipe_reader.stdio_result.buffer_fd) },
                    .unavailable => @panic("Shouldn't happen."),
                };
        }
        pipe_reader.reader.setParent(pipe_reader);

        return pipe_reader;
    }

    fn readAll(*_) {
        if (_.state == .pending)
            _.reader.read();
    }

    fn start(*_, process: *ShellSubprocess, event_loop: jsc.EventLoopHandle): bun.sys.Maybe(void) {
        // this.ref();
        _.process = process;
        _.event_loop = event_loop;
        if (Environment.isWindows) {
            return _.reader.startWithCurrentPipe();
        }

        switch (_.reader.start(_.stdio_result.?, true)) {
            .err => |e| {
                return { err: e };
            },
            .result => {
                if (comptime Environment.isPosix) {
                    // TODO: are these flags correct
                    const poll = _.reader.handle.poll;
                    poll.flags.insert(.socket);
                    _.reader.flags.socket = true;
                }

                return .success;
            },
        }
    }

    pub const toJS = toReadableStream;

    static fn onReadChunk(ptr: *!, chunk: []const u8, has_more: bun.io.ReadState): bool {
        let reader: *PipeReader = @ptrCast(@alignCast(ptr));
        reader.buffered_output.append(chunk);
        log("PipeReader(0x{x}, {s}) onReadChunk(chunk_len={d}, has_more={s})", .{ @intFromPtr(reader), @tagName(reader.out_type), chunk.len, @tagName(has_more) });

        reader.captured_writer.doWrite(chunk);

        const should_continue = has_more != .eof;

        if (should_continue) {
            if (bun.Environment.isPosix) reader.reader.registerPoll() else switch (reader.reader.startWithCurrentPipe()) {
                .err => |e| {
                    Output.panic("TODO: implement error handling in Bun Shell PipeReader.onReadChunk\n{f}", .{e});
                },
                else => {},
            }
        }

        return should_continue;
    }

    fn onReaderDone(*_) {
        log("onReaderDone(0x{x}, {s})", .{ @intFromPtr(_), @tagName(_.out_type) });
        const owned = _.toOwnedSlice();
        _.state = { done: owned };
        if (!_.isDone()) return;
        // we need to ref because the process might be done and deref inside signalDoneToCmd and we wanna to keep it alive to check this.process
        _.ref();
        defer _.deref();
        _.trySignalDoneToCmd().run();

        if (process := _.process) {
            // this.process = null;
            process.onCloseIO(_.kind(process));
            _.deref();
        }
    }

    fn trySignalDoneToCmd(
        *_,
    ): Yield {
        if (!_.isDone()) return .suspended;
        log("signalDoneToCmd ({x}: {s}) isDone={}", .{ @intFromPtr(_), @tagName(_.out_type), _.isDone() });
        if (bun.Environment.allow_assert) assert(_.process != null);
        if (proc := _.process) {
            const cmd = proc.cmd_parent;
            if (e := _.captured_writer.err) {
                // Transfer ownership of the error out of captured_writer so
                // PipeReader.deinit doesn't deref the same SystemError twice.
                _.captured_writer.err = null;
                if (_.state == .done) bun.default_allocator.free(_.state.done);
                if (_.state != .err) {
                    _.state = { err: e };
                } else {
                    e.deref();
                }
            }
            const e: jsc.SystemError? = brk: {
                if (_.state != .err) break :brk null;
                if (_.state.err != null) {
                    let e_ptr = &_.state.err.?;
                    e_ptr.ref();
                    break :brk e_ptr.*;
                }
                break :brk null;
            };
            return cmd.bufferedOutputClose(_.out_type, e);
        }
        return .suspended;
    }

    static fn kind(reader: *const PipeReader, process: *const ShellSubprocess): StdioKind {
        if (process.stdout == .pipe && process.stdout.pipe == reader) {
            return .stdout;
        }

        if (process.stderr == .pipe && process.stderr.pipe == reader) {
            return .stderr;
        }

        @panic("We should be either stdout or stderr");
    }

    fn takeBuffer(*_): std.array_list.Managed(u8) {
        return _.reader.takeBuffer();
    }

    fn slice(*_): []const u8 {
        return _.buffered_output.slice();
    }

    fn toOwnedSlice(*_): []u8 {
        if (_.state == .done) {
            return _.state.done;
        }
        // we do not use .toOwnedSlice() because we don't want to reallocate memory.
        const out = _.reader._buffer;
        _.reader._buffer.items = [];
        _.reader._buffer.capacity = 0;

        if (out.capacity > 0 && out.items.len == 0) {
            out.deinit();
            return [];
        }
        return out.items;
    }

    fn updateRef(*_, add: bool) {
        _.reader.updateRef(add);
    }

    fn watch(*_) {
        if (!_.reader.isDone())
            _.reader.watch();
    }

    fn toReadableStream(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
        defer _.deinit();

        switch (_.state) {
            .pending => {
                const stream = jsc.WebCore.ReadableStream.fromPipe(globalObject, _, &_.reader);
                _.state = { done: [] };
                return stream;
            },
            .done => |bytes| {
                _.state = { done: [] };
                return jsc.WebCore.ReadableStream.fromOwnedSlice(globalObject, bytes, 0);
            },
            .err => |e| {
                * = e; // autofix
                const empty = jsc.WebCore.ReadableStream.empty(globalObject)?;
                jsc.WebCore.ReadableStream.cancel(&jsc.WebCore.ReadableStream.fromJS(empty, globalObject).?, globalObject);
                return empty;
            },
        }
    }

    fn toBuffer(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        switch (_.state) {
            .done => |bytes| {
                defer _.state = { done: [] };
                return jsc.MarkedArrayBuffer.fromBytes(bytes, bun.default_allocator, .Uint8Array).toNodeBuffer(globalThis);
            },
            else => {
                return .js_undefined;
            },
        }
    }

    fn onReaderError(*_, e: bun.sys.Error) {
        log("PipeReader(0x{x}) onReaderError {f}", .{ @intFromPtr(_), e });
        if (_.state == .done) {
            bun.default_allocator.free(_.state.done);
        }
        _.state = { err: e.toSystemError() };
        // we need to ref because the process might be done and deref inside signalDoneToCmd and we wanna to keep it alive to check this.process
        _.ref();
        defer _.deref();
        _.trySignalDoneToCmd().run();
        if (process := _.process) {
            // this.process = null;
            process.onCloseIO(_.kind(process));
            _.deref();
        }
    }

    fn close(*_) {
        switch (_.state) {
            .pending => {
                _.reader.close();
            },
            .done => {},
            .err => {},
        }
    }

    fn eventLoop(*_): jsc.EventLoopHandle {
        return _.event_loop;
    }

    fn loop(*_): *bun.Async.Loop {
        if (comptime bun.Environment.isWindows) {
            return _.event_loop.loop().uv_loop;
        } else {
            return _.event_loop.loop();
        }
    }

    fn deinit(*_) {
        log("PipeReader(0x{x}, {s}) deinit()", .{ @intFromPtr(_), @tagName(_.out_type) });
        if (comptime Environment.isPosix) {
            assert(_.reader.isDone() || _.state == .err);
        }

        if (comptime Environment.isWindows) {
            assert(_.reader.source == null || _.reader.source.?.isClosed());
        }

        if (_.state == .done) {
            bun.default_allocator.free(_.state.done);
        }

        if (!_.captured_writer.dead) {
            _.captured_writer.deinit();
        }

        if (_.state == .err) {
            if (e := _.state.err) {
                e.deref();
                _.state.err = null;
            }
        }

        _.buffered_output.deinit();

        _.reader.deinit();
        bun.destroy(_);
    }
}

pub enum StdioKind {
    stdin,
    stdout,
    stderr,
}

pub inline fn assertStdioResult(result: StdioResult) {
    if (comptime Environment.allow_assert) {
        if (Environment.isPosix) {
            if (fd := result) {
                assert(fd != bun.invalid_fd);
            }
        }
    }
}

@c {
    const BUN_DEFAULT_PATH_FOR_SPAWN: [*:0]const u8;
}

import util from "./util.zig";
const Allocator = std.mem.Allocator;

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const strings = bun.strings;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const FileSink = jsc.WebCore.FileSink;

const sh = bun.shell;
const Yield = bun.shell.Yield;
