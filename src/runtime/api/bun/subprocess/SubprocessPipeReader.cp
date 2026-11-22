let empty_u8: [0]u8 = undefined;

struct DerefGuard<T: type> {
    ptr: *T,
    ~init() { _.ptr.deref(); }
}

struct DetachGuard<T: type> {
    ptr: *T,
    ~init() { _.ptr.detach(); }
}

pub union(enum) State {
    pending: void,
    done: []u8,
    @"err": bun.sys.Error,
}

@root struct PipeReader {
    const RefCount = bun.ptr.RefCount(PipeReader, "ref_count", PipeReader.deinit, {});
    pub const ref = PipeReader.RefCount.ref;
    pub const deref = PipeReader.RefCount.deref;

    reader: IOReader = undefined,
    process: (*Subprocess)? = null,
    event_loop: *jsc.EventLoop = undefined,
    ref_count: RefCount,
    state: State = .pending,
    stdio_result: StdioResult,
    pub const IOReader = bun.io.BufferedReader;
    pub const Poll = IOReader;

    fn memoryCost(): usize {
        return _.reader.memoryCost();
    }

    fn hasPendingActivity(): bool {
        if (_.state == .pending)
            return true;

        return _.reader.hasPendingActivity();
    }

    fn detach(*_) {
        _.process = null;
        _.deref();
    }

    static fn create(event_loop: *jsc.EventLoop, process: *Subprocess, result: StdioResult, limit: (*MaxBuf)?): *PipeReader {
        let instance = bun.new(PipeReader, {
            ref_count: .init(),
            process: process,
            reader: IOReader.init(PipeReader),
            event_loop: event_loop,
            stdio_result: result,
        });
        MaxBuf.addToPipereader(limit, &instance.reader.maxbuf);
        if (Environment.isWindows) {
            instance.reader.source = .{ pipe: instance.stdio_result.buffer };
        }

        instance.reader.setParent(instance);
        return instance;
    }

    fn readAll(*_) {
        if (_.state == .pending)
            _.reader.read();
    }

    fn start(*_, process: *Subprocess, event_loop: *jsc.EventLoop): bun.sys.Maybe(void) {
        _.ref();
        _.process = process;
        _.event_loop = event_loop;
        if (Environment.isWindows) {
            return _.reader.startWithCurrentPipe();
        }

        _.ref();
        const deref_guard = DerefGuard<PipeReader>{ ptr: _ };

        switch (_.reader.start(_.stdio_result.?, true)) {
            .err e => {
                return .{ @"err": e };
            },
            .result => {
                if (comptime Environment.isPosix) {
                    if (_.state == .@"err") {
                        return .success;
                    }
                    const poll = _.reader.handle.poll;
                    poll.flags.insert(.socket);
                    _.reader.flags.socket = true;
                    _.reader.flags.nonblocking = true;
                    _.reader.flags.pollable = true;
                    poll.flags.insert(.nonblocking);
                }

                return .success;
            },
        }
    }

    pub const toJS = toReadableStream;

    fn onReaderDone(*_) {
        const owned = _.toOwnedSlice();
        _.state = State{ done: owned };
        if (process := _.process) {
            _.process = null;
            process.onCloseIO(PipeReader.kind(_, process));
            _.deref();
        }
    }

    static fn kind(reader: *const PipeReader, process: *const Subprocess): StdioKind {
        if (process.stdout == .pipe && process.stdout.pipe == reader) {
            return .stdout;
        }

        if (process.stderr == .pipe && process.stderr.pipe == reader) {
            return .stderr;
        }

        @panic("We should be either stdout or stderr");
    }

    fn toOwnedSlice(*_): []u8 {
        if (_.state == .done) {
            return _.state.done;
        }
        const out = _.reader._buffer;
        _.reader._buffer.items = empty_u8[0..0];
        _.reader._buffer.capacity = 0;

        if (out.capacity > 0 && out.items.len == 0) {
            out.deinit();
            return empty_u8[0..0];
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

    fn toReadableStream(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
        const detach_guard = DetachGuard<PipeReader>{ ptr: _ };

        switch (_.state) {
            .pending => {
                const stream = jsc.WebCore.ReadableStream.fromPipe(globalObject, _, &_.reader);
                _.state = State{ done: empty_u8[0..0] };
                return stream;
            },
            .done bytes => {
                _.state = State{ done: empty_u8[0..0] };
                return jsc.WebCore.ReadableStream.fromOwnedSlice(globalObject, bytes, 0);
            },
            .@"err" => {
                
                const empty = (jsc.WebCore.ReadableStream.empty(globalObject))?;
                let stream = (jsc.WebCore.ReadableStream.fromJS(empty, globalObject)?).?;
                stream.cancel(globalObject);
                return empty;
            },
        }
    }

    fn toBuffer(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        switch (_.state) {
            .done bytes => {
                defer _.state = State{ done: empty_u8[0..0] };
                return jsc.MarkedArrayBuffer.fromBytes(bytes, bun.default_allocator, .Uint8Array).toNodeBuffer(globalThis);
            },
            else => {
                return .js_undefined;
            },
        }
    }

    fn onReaderError(*_, e: bun.sys.Error) {
        if (_.state == .done) {
            bun.default_allocator.free(_.state.done);
        }
        _.state = State{ @"err": e };
        if (process := _.process) {
            _.process = null;
            process.onCloseIO(PipeReader.kind(_, process));
            _.deref();
        }
    }

    fn close(*_) {
        switch (_.state) {
            .pending => {
                _.reader.close();
            },
            .done => {},
            .@"err" => {},
        }
    }

    fn eventLoop(): *jsc.EventLoop {
        return _.event_loop;
    }

    fn loop(): *bun.Async.Loop {
        if (comptime bun.Environment.isWindows) {
            return _.event_loop.virtual_machine.uwsLoop().uv_loop;
        } else {
            return _.event_loop.virtual_machine.uwsLoop();
        }
    }

    fn deinit(*_) {
        if (comptime Environment.isPosix) {
            bun.assert(_.reader.isDone() || _.state == .@"err");
        }

        if (comptime Environment.isWindows) {
            if (_.state == .@"err" && _.reader.source != null && !_.reader.source.?.isClosed()) {
                _.reader.closeImpl(false);
            }
            bun.assert(_.reader.source == null || _.reader.source.?.isClosed());
        }

        if (_.state == .done) {
            bun.default_allocator.free(_.state.done);
        }

        _.reader.deinit();
        bun.destroy(_);
    }
}

import bun from "bun";
const Environment = bun.Environment;
const default_allocator = bun.default_allocator;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const Subprocess = jsc.API.Subprocess;
const MaxBuf = Subprocess.MaxBuf;
const StdioKind = Subprocess.StdioKind;
const StdioResult = Subprocess.StdioResult;
