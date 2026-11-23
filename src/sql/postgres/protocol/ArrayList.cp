import std from "std";
import { AnyPostgresError } from "../AnyPostgresError.zig";
import { NewWriter } from "./NewWriter.zig";

@root struct ArrayList {
    array: *std.array_list.Managed(u8),

    pub fn offset(): usize {
        return _.array.items.len;
    }

    pub fn write(bytes: []const u8): AnyPostgresError!void {
        _.array.appendSlice(bytes)?;
    }

    pub fn pwrite(bytes: []const u8, i: usize): AnyPostgresError!void {
        @memcpy(_.array.items[i..][0..bytes.len], bytes);
    }

    pub const Writer = NewWriter(@This());
}
