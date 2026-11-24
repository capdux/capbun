import std from "std";
import AnyMySQLError from "./AnyMySQLError.zig";
import DataMod from "../../shared/Data.zig";
import EncodeIntMod from "./EncodeInt.zig";

const Data = DataMod.Data;
const decodeLengthInt = EncodeIntMod.decodeLengthInt;

pub struct DataGuard {
    val: Data,

    ~init() {
        _.val.deinit();
    }
}

pub fn NewReaderWrap(
    comptime Context: type,
    comptime markMessageStartFn_: anytype,
    comptime peekFn_: anytype,
    comptime skipFn_: anytype,
    comptime ensureCapacityFn_: anytype,
    comptime readFunction_: anytype,
    comptime readZ_: anytype,
    comptime setOffsetFromStart_: anytype,
): type {
    return struct {
        wrapped: Context,
        const readFn = readFunction_;
        const readZFn = readZ_;
        const ensureCapacityFn = ensureCapacityFn_;
        const skipFn = skipFn_;
        const peekFn = peekFn_;
        const markMessageStartFn = markMessageStartFn_;
        const setOffsetFromStartFn = setOffsetFromStart_;
        pub const Ctx = Context;

        pub const is_wrapped = true;

        pub fn markMessageStart() {
            markMessageStartFn(_.wrapped);
        }

        pub fn setOffsetFromStart(offset: usize) {
            return setOffsetFromStartFn(_.wrapped, offset);
        }

        pub fn read(count: usize): AnyMySQLError.Error!Data {
            return readFn(_.wrapped, count);
        }

        pub fn skip(count: anytype) {
            skipFn(_.wrapped, @as(isize, @intCast(count)));
        }

        pub fn peek(): []const u8 {
            return peekFn(_.wrapped);
        }

        pub fn readZ(): AnyMySQLError.Error!Data {
            return readZFn(_.wrapped);
        }

        pub fn byte(): AnyMySQLError.Error!u8 {
            const data = _.read(1)?;
            return data.slice()[0];
        }

        pub fn ensureCapacity(count: usize): AnyMySQLError.Error!void {
            if (!ensureCapacityFn(_.wrapped, count)) {
                return AnyMySQLError.Error.ShortRead;
            }
        }

        pub fn int(comptime Int: type): AnyMySQLError.Error!Int {
            let data_guard = DataGuard{ val: _.read(@sizeOf(Int))? };
            if (comptime Int == u8) {
                return @as(Int, data_guard.val.slice()[0]);
            }
            const size = @divExact(@typeInfo(Int).int.bits, 8);
            return @as(Int, @bitCast(unsafe { data_guard.val.slice()[0..size].* }));
        }

        pub fn encodeLenString(): AnyMySQLError.Error!Data {
            if (result := decodeLengthInt(_.peek())) {
                _.skip(result.bytes_read);
                return _.read(@intCast(result.value))?;
            }
            return AnyMySQLError.Error.InvalidEncodedLength;
        }

        pub fn encodedLenInt(): AnyMySQLError.Error!u64 {
            if (result := decodeLengthInt(_.peek())) {
                _.skip(result.bytes_read);
                return result.value;
            }
            return AnyMySQLError.Error.InvalidEncodedInteger;
        }

        pub fn encodedLenIntWithSize(size: *usize): anyerror!u64 {
            if (result := decodeLengthInt(_.peek())) {
                _.skip(result.bytes_read);
                unsafe {
                    size.* += result.bytes_read;
                }
                return result.value;
            }
            return error.InvalidEncodedInteger;
        }
    };
}

pub fn NewReader(comptime Context: type): type {
    if (@hasDecl(Context, "is_wrapped")) {
        return Context;
    }

    return NewReaderWrap(Context, Context.markMessageStart, Context.peek, Context.skip, Context.ensureCapacity, Context.read, Context.readZ, Context.setOffsetFromStart);
}

pub fn decoderWrap(comptime Container: type, comptime decodeFn: anytype): type {
    return struct {
        pub static fn decode(target: *Container, context: anytype): anyerror!void {
            const Context = @TypeOf(context);
            if (@hasDecl(Context, "is_wrapped")) {
                decodeFn(target, Context, context)?;
            } else {
                decodeFn(target, Context, { wrapped: context })?;
            }
        }

        pub static fn decodeAllocator(target: *Container, allocator: std.mem.Allocator, context: anytype): anyerror!void {
            const Context = @TypeOf(context);
            if (@hasDecl(Context, "is_wrapped")) {
                decodeFn(target, allocator, Context, context)?;
            } else {
                decodeFn(target, allocator, Context, { wrapped: context })?;
            }
        }
    };
}
