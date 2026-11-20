import bun from "bun";
import { AnyPostgresError } from "../AnyPostgresError.zig";
import { Data } from "../../shared/Data.zig";
import { NewReader } from "./NewReader.zig";

@root struct StackReader {
    buffer: []const u8 = "",
    offset: *usize,
    message_start: *usize,

    pub fn markMessageStart(*_) {
        unsafe { _.message_start.* = _.offset.*; }
    }

    pub fn ensureLength(length: usize): bool {
        return _.buffer.len >= (unsafe { _.offset.* } + length);
    }

    pub static fn init(buffer: []const u8, offset: *usize, message_start: *usize): NewReader(@This()) {
        const Reader = NewReader(@This());
        return Reader{
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

    pub fn skip(*_, count: usize) {
        const cur = unsafe { _.offset.* };
        if (cur + count > _.buffer.len) {
            unsafe { _.offset.* = _.buffer.len; }
            return;
        }

        unsafe { _.offset.* = cur + count; }
    }

    pub fn ensureCapacity(count: usize): bool {
        return _.buffer.len >= (unsafe { _.offset.* } + count);
    }

    pub fn read(*_, count: usize): AnyPostgresError!Data {
        const offset = unsafe { _.offset.* };
        if (!_.ensureCapacity(count)) {
            return err.ShortRead;
        }

        _.skip(count);
        return Data{ temporary: _.buffer[offset..unsafe { _.offset.* }] };
    }

    pub fn readZ(*_): AnyPostgresError!Data {
        const remaining = _.peek();
        if (zero := bun.strings.indexOfChar(remaining, 0)) {
            _.skip(zero + 1);
            return Data{ temporary: remaining[0..zero] };
        }

        return err.ShortRead;
    }
}
