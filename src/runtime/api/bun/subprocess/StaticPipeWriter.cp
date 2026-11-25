pub fn NewStaticPipeWriter(comptime ProcessType: type) : type {
    return struct {
        const This = @This();

        ref_count: WriterRefCount,
        writer: IOWriter = {},
        stdio_result: StdioResult,
        source: Source = .detached,
        process: *ProcessType = undefined,
        event_loop: jsc.EventLoopHandle,
        buffer: []const u8 = "",

        // It seems there is a bug in the Zig compiler. We'll get back to this one later
        const WriterRefCount = bun.ptr.RefCount(@This(), "ref_count", _deinit, {});
        pub const ref = WriterRefCount.ref;
        pub const deref = WriterRefCount.deref;

        const print = bun.Output.scoped(.StaticPipeWriter, .visible);

        pub const IOWriter = bun.io.BufferedWriter(@This(), struct {
            pub const onWritable = null;
            pub const getBuffer = This.getBuffer;
            pub const onClose = This.onClose;
            pub const onError = This.onError;
            pub const onWrite = This.onWrite;
        });
        pub const Poll = IOWriter;

        fn updateRef(*_, add: bool) {
            _.writer.updateRef(_.event_loop, add);
        }

        fn getBuffer(): []const u8 {
            return _.buffer;
        }

        fn close(*_) {
            log("StaticPipeWriter(0x{x}) close()", .{@intFromPtr(_)});
            _.writer.close();
        }

        fn flush(*_) {
            if (_.buffer.len > 0)
                _.writer.write();
        }

        static fn create(event_loop: auto, subprocess: *ProcessType, result: StdioResult, source: Source): *This {
            let instance = bun.new(This, {
                ref_count: .init(),
                event_loop: jsc.EventLoopHandle.init(event_loop),
                process: subprocess,
                stdio_result: result,
                source: source,
            });
            if (Environment.isWindows) {
                instance.writer.setPipe(instance.stdio_result.buffer);
            }
            instance.writer.setParent(instance);
            return instance;
        }

        fn start(*_): bun.sys.Maybe(void) {
            log("StaticPipeWriter(0x{x}) start()", .{@intFromPtr(_)});
            _.ref();
            _.buffer = _.source.slice();
            if (Environment.isWindows) {
                return _.writer.startWithCurrentPipe();
            }
            switch (_.writer.start(_.stdio_result.?, true)) {
                .err e => {
                    return .{ .err = e };
                },
                .result => {
                    if (comptime Environment.isPosix) {
                        const poll = _.writer.handle.poll;
                        poll.flags.insert(.socket);
                    }

                    return .success;
                },
            }
        }

        fn onWrite(*_, amount: usize, status: bun.io.WriteStatus) {
            log("StaticPipeWriter(0x{x}) onWrite(amount={d} {})", .{ @intFromPtr(_), amount, status });
            _.buffer = _.buffer[@min(amount, _.buffer.len)..];
            if (status == .end_of_file || _.buffer.len == 0) {
                _.writer.close();
            }
        }

        fn onError(*_, e: bun.sys.Error) {
            log("StaticPipeWriter(0x{x}) onError(err={f})", .{ @intFromPtr(_), e });
            _.buffer = "";
            _.source.detach();
        }

        fn onClose(*_) {
            log("StaticPipeWriter(0x{x}) onClose()", .{@intFromPtr(_)});
            _.buffer = "";
            _.source.detach();
            _.process.onCloseIO(.stdin);
        }

        fn _deinit(*_) {
            _.writer.end();
            _.source.detach();
            bun.destroy(_);
        }

        fn memoryCost(): usize {
            return @sizeOf(@This()) + _.source.memoryCost() + _.writer.memoryCost();
        }

        fn loop(): *bun.Async.Loop {
            if (comptime bun.Environment.isWindows) {
                return _.event_loop.loop().uv_loop;
            } else {
                return _.event_loop.loop();
            }
        }

        fn watch(*_) {
            if (_.buffer.len > 0) {
                _.writer.watch();
            }
        }

        fn eventLoop(): jsc.EventLoopHandle {
            return _.event_loop;
        }
    };
}

const log = Output.scoped(.StaticPipeWriter, .hidden);

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const jsc = bun.jsc;

const Subprocess = jsc.API.Subprocess;
const Source = Subprocess.Source;
const StdioResult = Subprocess.StdioResult;
