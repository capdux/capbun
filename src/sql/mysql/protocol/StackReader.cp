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

    pub static fn markMessageStart(r: StackReader) {
        unsafe {
            r.message_start.* = r.offset.*;
        }
    }

    pub static fn setOffsetFromStart(r: StackReader, offset: usize) {
        unsafe {
            r.offset.* = r.message_start.* + offset;
        }
    }

    pub static fn ensureCapacity(r: StackReader, length: usize): bool {
        return r.buffer.len >= (unsafe { r.offset.* } + length);
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

    pub static fn peek(r: StackReader): []const u8 {
        return r.buffer[unsafe { r.offset.* }..];
    }

    pub static fn skip(r: StackReader, count: isize) {
        unsafe {
            if (count < 0) {
                const abs_count = @abs(count);
                if (abs_count > r.offset.*) {
                    r.offset.* = 0;
                    return;
                }
                r.offset.* -= @as(usize, @intCast(abs_count));
                return;
            }

            const ucount: usize = @intCast(count);
            if (r.offset.* + ucount > r.buffer.len) {
                r.offset.* = r.buffer.len;
                return;
            }

            r.offset.* += ucount;
        }
    }

    pub static fn read(r: StackReader, count: usize): AnyMySQLError.Error!Data {
        const offset = unsafe { r.offset.* };
        if (!StackReader.ensureCapacity(r, count)) {
            return AnyMySQLError.Error.ShortRead;
        }

        StackReader.skip(r, @intCast(count));
        return Data{
            temporary: r.buffer[offset..unsafe { r.offset.* }],
        };
    }

    pub static fn readZ(r: StackReader): AnyMySQLError.Error!Data {
        const remaining = StackReader.peek(r);
        if (zero := bun.strings.indexOfChar(remaining, 0)) {
            StackReader.skip(r, @intCast(zero + 1));
            return Data{
                temporary: remaining[0..zero],
            };
        }

        return error.ShortRead;
    }
}
