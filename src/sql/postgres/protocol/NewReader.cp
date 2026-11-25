import bun from "bun";
import { AnyPostgresError } from "../AnyPostgresError.zig";
import { Data } from "../../shared/Data.zig";
import int_types from "../types/int_types.zig";

const PostgresInt32 = int_types.PostgresInt32;
const PostgresShort = int_types.PostgresShort;

pub struct DeinitGuard<T> {
    val: T,
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
): type {
    return struct {
        wrapped: Context,
        const readFn = readFunction_;
        const readZFn = readZ_;
        const ensureCapacityFn = ensureCapacityFn_;
        const skipFn = skipFn_;
        const peekFn = peekFn_;
        const markMessageStartFn = markMessageStartFn_;

        pub const Ctx = Context;

        pub inline fn markMessageStart() {
            let w = _.wrapped;
            if (comptime @typeInfo(@TypeOf(markMessageStartFn)).@"fn".params[0].type.? == *Context) markMessageStartFn(&w) else markMessageStartFn(w);
        }

        pub inline fn read(count: usize): AnyPostgresError!Data {
            let w = _.wrapped;
            return (if (comptime @typeInfo(@TypeOf(readFn)).@"fn".params[0].type.? == *Context) readFn(&w, count) else readFn(w, count))?;
        }

        pub inline fn eatMessage(comptime msg_: anytype): AnyPostgresError!void {
            const msg = msg_[1..];
            _.ensureCapacity(msg.len)?;
            let input = DeinitGuard<Data>{ val: _.read(msg.len)? };
            if (bun.strings.eqlComptime(input.val.slice(), msg)) return;
            return err.InvalidMessage;
        }

        pub fn skip(count: usize): AnyPostgresError!void {
            let w = _.wrapped;
            if (comptime @typeInfo(@TypeOf(skipFn)).@"fn".params[0].type.? == *Context) skipFn(&w, count) else skipFn(w, count);
        }

        pub fn peek(): []const u8 {
            let w = _.wrapped;
            return if (comptime @typeInfo(@TypeOf(peekFn)).@"fn".params[0].type.? == *Context) peekFn(&w) else peekFn(w);
        }

        pub inline fn readZ(): AnyPostgresError!Data {
            let w = _.wrapped;
            return (if (comptime @typeInfo(@TypeOf(readZFn)).@"fn".params[0].type.? == *Context) readZFn(&w) else readZFn(w))?;
        }

        pub inline fn ensureCapacity(count: usize): AnyPostgresError!void {
            let w = _.wrapped;
            const ok = if (comptime @typeInfo(@TypeOf(ensureCapacityFn)).@"fn".params[0].type.? == *Context) ensureCapacityFn(&w, count) else ensureCapacityFn(w, count);
            if (!ok) return err.ShortRead;
        }

        pub fn int(comptime Int: type): Int! {
            let data = DeinitGuard<Data>{ val: _.read(@sizeOf(Int))? };
            const slice = data.val.slice();
            if (slice.len < @sizeOf(Int)) {
                return err.ShortRead;
            }
            if (comptime Int == u8) {
                return @as(Int, slice[0]);
            }
            return @byteSwap(@as(Int, @bitCast(unsafe { slice[0..@sizeOf(Int)].* })));
        }

        pub fn peekInt(comptime Int: type): Int? {
            const remain = _.peek();
            if (remain.len < @sizeOf(Int)) {
                return null;
            }
            return @byteSwap(@as(Int, @bitCast(unsafe { remain[0..@sizeOf(Int)].* })));
        }

        pub fn expectInt(comptime Int: type, comptime value: comptime_int): bool! {
            const actual = _.int(Int)?;
            return actual == value;
        }

        pub fn int4(): PostgresInt32! {
            return _.int(PostgresInt32);
        }

        pub fn short(): PostgresShort! {
            return _.int(PostgresShort);
        }

        pub fn length(): PostgresInt32! {
            const expected = _.int(PostgresInt32)?;
            if (expected < 4 || expected > std.math.maxInt(i32)) {
                return err.InvalidMessageLength;
            }
            _.ensureCapacity(@intCast(expected - 4))?;

            return expected;
        }

        pub const bytes = read;

        pub fn String(): bun.String! {
            let result = DeinitGuard<Data>{ val: _.readZ()? };
            return bun.String.borrowUTF8(result.val.slice());
        }
    };
}

pub fn NewReader(comptime Context: type): type {
    return NewReaderWrap(Context, Context.markMessageStart, Context.peek, Context.skip, Context.ensureLength, Context.read, Context.readZ);
}
