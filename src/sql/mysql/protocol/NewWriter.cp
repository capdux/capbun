import std from "std";
import bun from "bun";
import AnyMySQLError from "./AnyMySQLError.zig";
import PacketHeader from "./PacketHeader.zig";
import EncodeIntMod from "./EncodeInt.zig";
import types from "../MySQLTypes.zig";

const encodeLengthInt = EncodeIntMod.encodeLengthInt;
const MySQLInt32 = types.MySQLInt32;
const MySQLInt64 = types.MySQLInt64;
const debug = bun.Output.scoped(.NewWriter, .hidden);

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

pub fn NewWriterPacket(comptime WrappedWriter: type): type {
    return struct {
        header: PacketHeader,
        offset: usize,
        ctx: WrappedWriter,

        pub fn end(*_): AnyMySQLError.Error!void {
            const new_offset = _.ctx.offset();
            const length = new_offset - _.offset - PacketHeader.size;
            _.header.length = @intCast(length);
            debug("writing packet header: {d}", .{_.header.length});
            const encoded = _.header.encode();
            _.ctx.pwrite(encoded[0..], _.offset)?;
        }
    };
}

pub fn NewWriterWrap(
    comptime Context: type,
    comptime offsetFn_: anytype,
    comptime writeFunction_: anytype,
    comptime pwriteFunction_: anytype,
): type {
    return struct {
        wrapped: Context,

        const writeFn = writeFunction_;
        const pwriteFn = pwriteFunction_;
        const offsetFn = offsetFn_;
        pub const Ctx = Context;

        pub const is_wrapped = true;

        pub const WrappedWriter = @This();
        const Packet = NewWriterPacket(WrappedWriter);

        pub inline fn writeLengthEncodedInt(data: u64): AnyMySQLError.Error!void {
            const encoded = encodeLengthInt(data);
            writeFn(_.wrapped, encoded.slice())?;
        }

        pub inline fn writeLengthEncodedString(data: []const u8): AnyMySQLError.Error!void {
            _.writeLengthEncodedInt(data.len)?;
            writeFn(_.wrapped, data)?;
        }

        pub fn write(data: []const u8): AnyMySQLError.Error!void {
            writeFn(_.wrapped, data)?;
        }

        pub fn start(sequence_id: u8): AnyMySQLError.Error!Packet {
            const o = offsetFn(_.wrapped);
            debug("starting packet: {d}", .{o});
            const zeros = std.mem.zeroes([PacketHeader.size]u8);
            _.write(zeros[0..])?;
            return Packet{
                header: PacketHeader{ sequence_id: sequence_id, length: 0 },
                offset: o,
                ctx: _,
            };
        }

        pub fn offset(): usize {
            return offsetFn(_.wrapped);
        }

        pub fn pwrite(data: []const u8, i: usize): AnyMySQLError.Error!void {
            pwriteFn(_.wrapped, data, i)?;
        }

        pub fn int4(value: MySQLInt32): AnyMySQLError.Error!void {
            const bytes = std.mem.toBytes(value);
            _.write(bytes[0..])?;
        }

        pub fn int8(value: MySQLInt64): AnyMySQLError.Error!void {
            const bytes = std.mem.toBytes(value);
            _.write(bytes[0..])?;
        }

        pub fn int1(value: u8): AnyMySQLError.Error!void {
            const bytes: [1]u8 = [value];
            _.write(bytes[0..])?;
        }

        pub fn writeZ(value: []const u8): AnyMySQLError.Error!void {
            _.write(value)?;
            if (value.len == 0 || value[value.len - 1] != 0) {
                const zero: [1]u8 = [0];
                _.write(zero[0..])?;
            }
        }

        pub fn String(value: bun.String): AnyMySQLError.Error!void {
            const zero: [1]u8 = [0];
            if (value.isEmpty()) {
                _.write(zero[0..])?;
                return;
            }

            const sliced = value.toUTF8(bun.default_allocator);
            let sliced_guard = DeinitGuard<@TypeOf(sliced)>{ val: sliced };
            const slice = sliced_guard.val.slice();

            _.write(slice)?;
            if (slice.len == 0 || slice[slice.len - 1] != 0) {
                _.write(zero[0..])?;
            }
        }
    };
}

pub fn NewWriter(comptime Context: type): type {
    if (@hasDecl(Context, "is_wrapped")) {
        return Context;
    }

    return NewWriterWrap(Context, Context.offset, Context.write, Context.pwrite);
}

pub fn writeWrap(comptime Container: type, comptime writeFn: anytype): type {
    return struct {
        pub static fn write(target: *Container, context: anytype): AnyMySQLError.Error!void {
            const Context = @TypeOf(context);
            const first_param = @typeInfo(@TypeOf(writeFn)).@"fn".params[0].type;
            const self_arg = if (comptime first_param == Container) unsafe { target.* } else target;
            if (@hasDecl(Context, "is_wrapped")) {
                writeFn(self_arg, Context, context) catch e => return @errorCast(e);
            } else {
                writeFn(self_arg, Context, .{ .wrapped = context }) catch e => return @errorCast(e);
            }
        }
    };
}
