import bun from "bun";
import { AnyPostgresError } from "../AnyPostgresError.zig";
import { Data } from "../../shared/Data.zig";
import int_types from "../types/int_types.zig";

const PostgresInt32 = int_types.PostgresInt32;
const PostgresShort = int_types.PostgresShort;

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

        pub inline fn markMessageStart(this) {
            let w = this.wrapped;
            if (comptime @typeInfo(@TypeOf(markMessageStartFn)).@"fn".params[0].type.? == *Context) markMessageStartFn(&w) else markMessageStartFn(w);
        }

        pub inline fn read(this, count: usize): AnyPostgresError!Data {
            let w = this.wrapped;
            return (if (comptime @typeInfo(@TypeOf(readFn)).@"fn".params[0].type.? == *Context) readFn(&w, count) else readFn(w, count))?;
        }

        pub inline fn eatMessage(this, comptime msg_: anytype): AnyPostgresError!void {
            const msg = msg_[1..];
            this.ensureCapacity(msg.len)?;
            let input = this.read(msg.len)?;
            defer input.deinit();
            if (bun.strings.eqlComptime(input.slice(), msg)) return;
            return err.InvalidMessage;
        }

        pub fn skip(this, count: usize): AnyPostgresError!void {
            let w = this.wrapped;
            if (comptime @typeInfo(@TypeOf(skipFn)).@"fn".params[0].type.? == *Context) skipFn(&w, count) else skipFn(w, count);
        }

        pub fn peek(this): []const u8 {
            let w = this.wrapped;
            return if (comptime @typeInfo(@TypeOf(peekFn)).@"fn".params[0].type.? == *Context) peekFn(&w) else peekFn(w);
        }

        pub inline fn readZ(this): AnyPostgresError!Data {
            let w = this.wrapped;
            return (if (comptime @typeInfo(@TypeOf(readZFn)).@"fn".params[0].type.? == *Context) readZFn(&w) else readZFn(w))?;
        }

        pub inline fn ensureCapacity(this, count: usize): AnyPostgresError!void {
            let w = this.wrapped;
            const ok = if (comptime @typeInfo(@TypeOf(ensureCapacityFn)).@"fn".params[0].type.? == *Context) ensureCapacityFn(&w, count) else ensureCapacityFn(w, count);
            if (!ok) return err.ShortRead;
        }

        pub fn int(this, comptime Int: type): Int! {
            let data = this.read(@sizeOf(Int))?;
            defer data.deinit();
            const slice = data.slice();
            if (slice.len < @sizeOf(Int)) {
                return err.ShortRead;
            }
            if (comptime Int == u8) {
                return @as(Int, slice[0]);
            }
            return @byteSwap(@as(Int, @bitCast(unsafe { slice[0..@sizeOf(Int)].* })));
        }

        pub fn peekInt(this, comptime Int: type): Int? {
            const remain = this.peek();
            if (remain.len < @sizeOf(Int)) {
                return null;
            }
            return @byteSwap(@as(Int, @bitCast(unsafe { remain[0..@sizeOf(Int)].* })));
        }

        pub fn expectInt(this, comptime Int: type, comptime value: comptime_int): bool! {
            const actual = this.int(Int)?;
            return actual == value;
        }

        pub fn int4(this): PostgresInt32! {
            return this.int(PostgresInt32);
        }

        pub fn short(this): PostgresShort! {
            return this.int(PostgresShort);
        }

        pub fn length(this): PostgresInt32! {
            const expected = this.int(PostgresInt32)?;
            if (expected > -1) {
                this.ensureCapacity(@intCast(expected -| 4))?;
            }

            return expected;
        }

        pub const bytes = read;

        pub fn String(this): bun.String! {
            let result = this.readZ()?;
            defer result.deinit();
            return bun.String.borrowUTF8(result.slice());
        }
    };
}

pub fn NewReader(comptime Context: type): type {
    return NewReaderWrap(Context, Context.markMessageStart, Context.peek, Context.skip, Context.ensureLength, Context.read, Context.readZ);
}
