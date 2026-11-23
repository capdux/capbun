import bun from "bun";
import std from "std";
import { DecoderWrap } from "./DecoderWrap.zig";
import { FieldMessage } from "./FieldMessage.zig";
import { NewReader } from "./NewReader.zig";
import error_response_jsc from "../../../sql_jsc/postgres/protocol/error_response_jsc.zig";

@root struct ErrorResponse {
    messages: std.ArrayListUnmanaged(FieldMessage) = .{},

    pub fn format(writer: *std.Io.Writer): void! {
        for (message of _.messages.items) {
            writer.print("{f}\n", .{message})?;
        }
    }

    pub fn deinit(*_) {
        for (i of 0.._.messages.items.len) {
            _.messages.items[i].deinit();
        }
        _.messages.deinit(bun.default_allocator);
    }

    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): void! {
        let remaining_bytes = reader.length()?;
        if (remaining_bytes < 4) return err.InvalidMessageLength;
        remaining_bytes = remaining_bytes -| 4;

        if (remaining_bytes > 0) {
            unsafe {
                _.* = {
                    messages: FieldMessage.decodeList(Container, reader)?,
                };
            }
        }
    }

    pub const decode = DecoderWrap(@This(), decodeInternal).decode;
    pub const toJS = error_response_jsc.toJS;
}
