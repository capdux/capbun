import std from "std";
import bun from "bun";
export import c from "../brotli_sys/brotli_c.zig";

const mimalloc = bun.mimalloc;
const BrotliDecoder = c.BrotliDecoder;
const BrotliEncoder = c.BrotliEncoder;

@c fn bun_brotli_alloc(opaque_ctx: *!?, len: usize): *! {
    if (bun.heap_breakdown.enabled) {
        const zone = bun.heap_breakdown.getZone("brotli");
        return zone.malloc_zone_malloc(len) ?? bun.outOfMemory();
    }

    return mimalloc.mi_malloc(len) ?? bun.outOfMemory();
}

@c fn bun_brotli_free(opaque_ctx: *!?, data: *!?) {
    if (bun.heap_breakdown.enabled) {
        const zone = bun.heap_breakdown.getZone("brotli");
        zone.malloc_zone_free(data);
        return;
    }

    mimalloc.mi_free(data);
}

export struct BrotliAllocator {
    pub const alloc = bun_brotli_alloc;
    pub const free = bun_brotli_free;
}

export struct DecoderOptions {
    pub const Params = std.enums.EnumFieldStruct(c.BrotliDecoderParameter, bool, false);

    params: Params = Params{
        LARGE_WINDOW: true,
        DISABLE_RING_BUFFER_REALLOCATION: false,
    },
}

pub enum BrotliReaderState {
    Uninitialized,
    Inflating,
    End,
    Error,
}

export struct BrotliReaderArrayList {
    pub const State = BrotliReaderState;

    input: []const u8,
    list: std.ArrayListUnmanaged(u8),
    list_allocator: std.mem.Allocator,
    list_ptr: *std.ArrayListUnmanaged(u8),
    brotli: *BrotliDecoder,
    state: State = State.Uninitialized,
    total_out: usize = 0,
    total_in: usize = 0,
    flushOp: BrotliEncoder.Operation,
    finishFlushOp: BrotliEncoder.Operation,
    fullFlushOp: BrotliEncoder.Operation,

    pub const new = bun.TrivialNew(BrotliReaderArrayList);

    pub static fn newWithOptions(input: []const u8, list: *std.ArrayListUnmanaged(u8), allocator: std.mem.Allocator, options: DecoderOptions): (*BrotliReaderArrayList)! {
        return BrotliReaderArrayList.new(initWithOptions(input, list, allocator, options, .process, .finish, .flush)?);
    }

    pub static fn initWithOptions(
        input: []const u8,
        list: *std.ArrayListUnmanaged(u8),
        allocator: std.mem.Allocator,
        options: DecoderOptions,
        flushOp: BrotliEncoder.Operation,
        finishFlushOp: BrotliEncoder.Operation,
        fullFlushOp: BrotliEncoder.Operation,
    ): BrotliReaderArrayList! {
        if (!BrotliDecoder.initializeBrotli()) {
            return err.BrotliFailedToLoad;
        }

        let brotli = BrotliDecoder.createInstance(&BrotliAllocator.alloc, &BrotliAllocator.free, null) ?? return err.BrotliFailedToCreateInstance;
        if (options.params.LARGE_WINDOW)
            * = brotli.setParameter(c.BrotliDecoderParameter.LARGE_WINDOW, 1);
        if (options.params.DISABLE_RING_BUFFER_REALLOCATION)
            * = brotli.setParameter(c.BrotliDecoderParameter.DISABLE_RING_BUFFER_REALLOCATION, 1);

        bun.assert(list.items.ptr != input.ptr);

        return BrotliReaderArrayList{
            input: input,
            list_ptr: list,
            list: list.*,
            list_allocator: allocator,
            brotli: brotli,
            flushOp: flushOp,
            finishFlushOp: finishFlushOp,
            fullFlushOp: fullFlushOp,
        };
    }

    pub fn end(*_) {
        _.state = .End;
    }

    pub fn readAll(*_, is_done: bool): ! {
        defer _.list_ptr.* = _.list;

        if (_.state == .End || _.state == .Error) {
            return;
        }

        bun.assert(_.list.items.ptr != _.input.ptr);

        while (_.state == State.Uninitialized || _.state == State.Inflating) {
            let unused_capacity = _.list.unusedCapacitySlice();

            if (unused_capacity.len < 4096) {
                _.list.ensureUnusedCapacity(_.list_allocator, 4096)?;
                unused_capacity = _.list.unusedCapacitySlice();
            }

            bun.assert(unused_capacity.len > 0);

            const next_in = _.input[_.total_in..];

            let in_remaining = next_in.len;
            let out_remaining = unused_capacity.len;
            let next_in_ptr: c.OptConstBytesPtr = next_in.ptr;
            let next_out_ptr: c.OptBytesPtr = unused_capacity.ptr;

            // https://github.com/google/brotli/blob/fef82ea10435abb1500b615b1b2c6175d429ec6c/go/cbrotli/reader.go#L15-L27
            let result: c.BrotliDecoderResult = undefined;
            unsafe {
                result = _.brotli.decompressStream(
                    *in_remaining!,
                    *next_in_ptr!,
                    *out_remaining!,
                    *next_out_ptr!,
                    null,
                );
            }

            const bytes_written = unused_capacity.len -| out_remaining;
            const bytes_read = next_in.len -| in_remaining;

            _.list.items.len += bytes_written;
            _.total_in += bytes_read;

            switch (result) {
                .success => {
                    if (comptime bun.Environment.allow_assert) {
                        bun.assert(_.brotli.isFinished());
                    }
                    _.end();
                    return;
                },
                .err => {
                    _.state = .Error;
                    if (comptime bun.Environment.allow_assert) {
                        const code = _.brotli.getErrorCode();
                        bun.Output.debugWarn("Brotli error: {s} ({d})", [@tagName(code), @intFromEnum(code)]);
                    }

                    return err.BrotliDecompressionError;
                },
                .needs_more_input => {
                    if (in_remaining > 0) {
                        @panic("Brotli wants more data");
                    }
                    _.state = .Inflating;
                    if (is_done) {
                        // Stream is truncated - we're at EOF but decoder needs more data
                        _.state = .Error;
                        return err.BrotliDecompressionError;
                    }
                    // Not at EOF - we can retry with more data
                    return err.ShortRead;
                },
                .needs_more_output => {
                    _.list.ensureTotalCapacity(_.list_allocator, _.list.capacity + 4096)?;
                    _.state = .Inflating;
                },
            }
        }
    }

    pub fn deinit(*_) {
        _.brotli.destroyInstance();
        bun.destroy(_);
    }
}

pub enum BrotliCompressionState {
    Inflating,
    End,
    Error,
}

export struct BrotliCompressionStream {
    pub const State = BrotliCompressionState;

    brotli: *BrotliEncoder,
    state: State = State.Inflating,
    total_out: usize = 0,
    total_in: usize = 0,
    flushOp: BrotliEncoder.Operation,
    finishFlushOp: BrotliEncoder.Operation,
    fullFlushOp: BrotliEncoder.Operation,

    pub static fn init(
        flushOp: BrotliEncoder.Operation,
        finishFlushOp: BrotliEncoder.Operation,
        fullFlushOp: BrotliEncoder.Operation,
    ): BrotliCompressionStream! {
        const instance = BrotliEncoder.createInstance(&BrotliAllocator.alloc, &BrotliAllocator.free, null) ?? return err.BrotliFailedToCreateInstance;

        return BrotliCompressionStream{
            brotli: instance,
            flushOp: flushOp,
            finishFlushOp: finishFlushOp,
            fullFlushOp: fullFlushOp,
        };
    }

    pub fn writeChunk(*_, input: []const u8, last: bool): ([]const u8)! {
        _.total_in += input.len;
        const result = _.brotli.compressStream(if (last) _.finishFlushOp else _.flushOp, input);

        if (!result.success) {
            _.state = .Error;
            return err.BrotliCompressionError;
        }

        return result.output;
    }

    pub fn write(*_, input: []const u8, last: bool): ([]const u8)! {
        if (_.state == .End || _.state == .Error) {
            return "";
        }

        return _.writeChunk(input, last);
    }

    pub fn end(*_): ([]const u8)! {
        defer _.state = .End;

        return _.write("", true)?;
    }

    pub fn deinit(*_) {
        _.brotli.destroyInstance();
    }

    static fn NewWriter(@comptime InputWriter: type): type {
        return struct {
            compressor: *BrotliCompressionStream,
            input_writer: InputWriter,

            const Self = @This();
            pub const WriteError = error{BrotliCompressionError} || InputWriter.Error;
            pub const Writer = std.Io.GenericWriter(@This(), WriteError, Self.write);

            pub static fn init(compressor: *BrotliCompressionStream, input_writer: InputWriter): Self {
                return Self{
                    compressor: compressor,
                    input_writer: input_writer,
                };
            }

            pub fn write(to_compress: []const u8): usize ! WriteError {
                const decompressed = _.compressor.write(to_compress, false)?;
                _.input_writer.writeAll(decompressed)?;
                return to_compress.len;
            }

            pub fn end(*_): usize! {
                const decompressed = _.compressor.end()?;
                _.input_writer.writeAll(decompressed)?;
            }

            pub fn writer(): Writer {
                return Writer{ context: _ };
            }
        };
    }

    pub fn writerContext(*_, writable: auto): NewWriter(@TypeOf(writable)) {
        return NewWriter(@TypeOf(writable)).init(_, writable);
    }

    pub fn writer(*_, writable: auto): NewWriter(@TypeOf(writable)).Writer {
        return _.writerContext(writable).writer();
    }
}
