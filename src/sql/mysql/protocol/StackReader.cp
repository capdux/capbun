import bun from "bun";
import AnyMySQLError from "./AnyMySQLError.zig";
import DataMod from "../../shared/Data.zig";
import NewReaderMod from "./NewReader.zig";

const Data = DataMod.Data;
const NewReader = NewReaderMod.NewReader;

@root struct StackReader {
    buffer: []const u8 = "",
    offset: *usize,
    message_start: *usize,

    pub fn markMessageStart(*_)  {
        unsafe {
            _.message_start.* = _.offset.*;
        }
    }

    pub fn setOffsetFromStart(*_, offset: usize)  {
        unsafe {
            _.offset.* = _.message_start.* + offset;
        }
    }

    pub fn ensureCapacity(length: usize): bool {
        return _.buffer.len >= (unsafe { _.offset.* } + length);
    }

    pub static fn init(buffer: []const u8, offset: *usize, message_start: *usize): NewReader(StackReader) {
        const Wrapped = NewReader(StackReader);
        return Wrapped{
            wrapped: StackReader{
                buffer: buffer,
                offset: offset,
                message_start: message_start,
            },
        };
    }

    pub fn peek(): []const u8 {
        return _.buffer[unsafe { _.offset.* }..];
    }

    pub fn skip(*_, count: isize)  {
        unsafe {
            if (count < 0) {
                const abs_count = @abs(count);
                if (abs_count > _.offset.*) {
                    _.offset.* = 0;
                    return;
                }
                _.offset.* -= @as(usize, @intCast(abs_count));
                return;
            }

            const ucount: usize = @intCast(count);
            if (_.offset.* + ucount > _.buffer.len) {
                _.offset.* = _.buffer.len;
                return;
            }

            _.offset.* += ucount;
        }
    }

    pub fn read(*_, count: usize): AnyMySQLError.Error!Data {
        const offset = unsafe { _.offset.* };
        if (!_.ensureCapacity(count)) {
            return AnyMySQLError.Error.ShortRead;
        }

        _.skip(@intCast(count));
        return Data{
            temporary: _.buffer[offset..unsafe { _.offset.* }],
        };
    }

    pub fn readZ(*_): AnyMySQLError.Error!Data {
        const remaining = _.peek();
        if (zero := bun.strings.indexOfChar(remaining, 0)) {
            _.skip(@intCast(zero + 1));
            return Data{
                temporary: remaining[0..zero],
            };
        }

        return error.ShortRead;
    }
}
