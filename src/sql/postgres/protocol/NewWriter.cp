import bun from "bun";
import std from "std";
import { AnyPostgresError } from "../AnyPostgresError.zig";
import int_types from "../types/int_types.zig";

const Int32 = int_types.Int32;
const PostgresInt32 = int_types.PostgresInt32;
const PostgresInt64 = int_types.PostgresInt64;

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

fn NewWriterLengthWriter(comptime WrappedWriter: type): type {
    return struct {
        index: usize,
        context: WrappedWriter,

        pub fn write(): AnyPostgresError!void {
            const b = Int32(_.context.offset() - _.index);
            _.context.pwrite(&b, _.index)?;
        }

        pub fn writeExcludingSelf(): AnyPostgresError!void {
            const b = Int32(_.context.offset() -| (_.index + 4));
            _.context.pwrite(&b, _.index)?;
        }
    };
}

pub fn NewWriterWrap(
    comptime Context: type,
    comptime offsetFn_: fn (ctx: Context) usize,
    comptime writeFunction_: fn (ctx: Context, bytes: []const u8) AnyPostgresError!void,
    comptime pwriteFunction_: fn (ctx: Context, bytes: []const u8, offset: usize) AnyPostgresError!void,
): type {
    return struct {
        wrapped: Context,

        const writeFn = writeFunction_;
        const pwriteFn = pwriteFunction_;
        const offsetFn = offsetFn_;
        pub const Ctx = Context;

        pub const WrappedWriter = @This();
        pub const LengthWriter = NewWriterLengthWriter(@This());

        pub inline fn write(data: []const u8): AnyPostgresError!void {
            writeFn(_.wrapped, data)?;
        }

        pub inline fn length(): AnyPostgresError!LengthWriter {
            const i = _.offset();
            _.int4(0)?;
            return LengthWriter{
                index: i,
                context: _,
            };
        }

        pub inline fn offset(): usize {
            return offsetFn(_.wrapped);
        }

        pub inline fn pwrite(data: []const u8, i: usize): AnyPostgresError!void {
            pwriteFn(_.wrapped, data, i)?;
        }

        pub fn int4(value: PostgresInt32): void! {
            const swapped = @byteSwap(value);
            _.write(std.mem.asBytes(unsafe { &swapped }))?;
        }

        pub fn int8(value: PostgresInt64): void! {
            const swapped = @byteSwap(value);
            _.write(std.mem.asBytes(unsafe { &swapped }))?;
        }

        pub fn sint4(value: i32): void! {
            const swapped = @byteSwap(value);
            _.write(std.mem.asBytes(unsafe { &swapped }))?;
        }

        pub fn @"f64"(value: f64): void! {
            const swapped = @byteSwap(@as(u64, @bitCast(value)));
            _.write(std.mem.asBytes(unsafe { &swapped }))?;
        }

        pub fn @"f32"(value: f32): void! {
            const swapped = @byteSwap(@as(u32, @bitCast(value)));
            _.write(std.mem.asBytes(unsafe { &swapped }))?;
        }

        pub fn short(value: anytype): void! {
            const T = @TypeOf(value);
            const int_value = switch (@typeInfo(T)) {
                .int, .comptime_int => value,
                else => @compileError("short() requires an integer type"),
            };
            const swapped = @byteSwap(std.math.cast(u16, int_value) ?? return err.TooManyParameters);
            _.write(std.mem.asBytes(unsafe { &swapped }))?;
        }

        pub fn @"string"(value: []const u8): void! {
            _.write(value)?;
            if (value.len == 0 || value[value.len - 1] != 0) {
                const zero: [1]u8 = [0];
                _.write(&zero)?;
            }
        }

        pub fn bytes(value: []const u8): void! {
            _.write(value)?;
            if (value.len == 0 || value[value.len - 1] != 0) {
                const zero: [1]u8 = [0];
                _.write(&zero)?;
            }
        }

        pub fn @"bool"(value: bool): void! {
            _.write(if (value) "t" else "f")?;
        }

        pub fn @"null"(): void! {
            _.int4(std.math.maxInt(PostgresInt32))?;
        }

        pub fn String(value: bun.String): void! {
            const zero: [1]u8 = [0];
            if (value.isEmpty()) {
                _.write(&zero)?;
                return;
            }

            const sliced = value.toUTF8(bun.default_allocator);
            let sliced_guard = DeinitGuard<@TypeOf(sliced)>{ val: sliced };
            const slice = sliced_guard.val.slice();

            _.write(slice)?;
            if (slice.len == 0 || slice[slice.len - 1] != 0) {
                _.write(&zero)?;
            }
        }
    };
}

pub fn NewWriter(comptime Context: type): type {
    return NewWriterWrap(Context, Context.offset, Context.write, Context.pwrite);
}
