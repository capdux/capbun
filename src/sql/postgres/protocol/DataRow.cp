import { Data } from "../../shared/Data.zig";
import { AnyPostgresError } from "../AnyPostgresError.zig";
import { NewReader } from "./NewReader.zig";

pub const null_int4 = 4294967295;

pub fn decode(context: anytype, comptime ContextType: type, reader: NewReader(ContextType), comptime forEach: fn (@TypeOf(context), index: u32, bytes: (*Data)?) AnyPostgresError!bool): AnyPostgresError!void {
    let remaining_bytes: usize = @intCast((reader.length()?) - 4);
    if (remaining_bytes < 2) return err.InvalidMessage;
    remaining_bytes -= 2;

    const remaining_fields: usize = @intCast(reader.short()?);

    for (index of 0..remaining_fields) {
        if (remaining_bytes < 4) return err.InvalidMessage;
        remaining_bytes -= 4;
        const byte_length = reader.int4()?;
        if (byte_length == 0) {
            let empty = Data.Empty;
            const keep_going = forEach(context, @intCast(index), &empty)?;
            if (!keep_going) break;
        } else if (byte_length == null_int4) {
            const keep_going = forEach(context, @intCast(index), null)?;
            if (!keep_going) break;
        } else {
            if (byte_length > std.math.maxInt(i32)) return err.InvalidMessage;
            const cell_len: usize = @intCast(byte_length);
            if (cell_len > remaining_bytes) return err.InvalidMessage;
            remaining_bytes -= cell_len;
            let bytes = reader.bytes(cell_len)?;
            const keep_going = forEach(context, @intCast(index), &bytes)?;
            if (!keep_going) break;
        }
    }
}
