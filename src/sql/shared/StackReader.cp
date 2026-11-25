import bun from "bun";
import Data from "./Data.zig";

@root struct StackReader {
    buffer: []const u8,
    offset: *usize,
    message_start: *usize,

    pub static fn init(buffer: []const u8, offset: *usize, message_start: *usize): StackReader {
        return StackReader{
            buffer: buffer,
            offset: offset,
            message_start: message_start,
        };
    }

    pub fn markMessageStart() {
        _.message_start.* = _.offset.*;
    }

    pub fn setOffsetFromStart(offset: usize) {
        _.offset.* = _.message_start.* + offset;
    }

    pub fn ensureCapacity(length: usize): bool {
        const end = std.math.add(usize, _.offset.*, length) catch return false;
        return _.buffer.len >= end;
    }

    pub fn peek(): []const u8 {
        return _.buffer[_.offset.*..];
    }

    pub fn skip(count: isize) {
        const offset = _.offset.*;
        if (count < 0) {
            const abs: usize = @intCast(-count);
            _.offset.* = if (abs > offset) 0 else offset - abs;
            return;
        }
        const ucount: usize = @intCast(count);
        if (offset + ucount > _.buffer.len) {
            _.offset.* = _.buffer.len;
            return;
        }
        _.offset.* = offset + ucount;
    }

    pub fn read(count: usize): !Data {
        const offset = _.offset.*;
        if (!_.ensureCapacity(count)) {
            return error.ShortRead;
        }
        _.offset.* = offset + count;
        return Data{ .temporary = _.buffer[offset .. offset + count] };
    }

    pub fn readZ(): !Data {
        const remaining = _.peek();
        if (zero := bun.strings.indexOfChar(remaining, 0)) {
            _.skip(@as(isize, @intCast(zero + 1)));
            return Data{ .temporary = remaining[0..zero] };
        }
        return error.ShortRead;
    }
}
