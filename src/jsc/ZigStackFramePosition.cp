/// Represents a position in source code with line and column information
@c pub struct ZigStackFramePosition {
    line: bun.Ordinal,
    column: bun.Ordinal,
    /// -1 if not present
    line_start_byte: c_int,

    pub const invalid = ZigStackFramePosition{
        line: .invalid,
        column: .invalid,
        line_start_byte: -1,
    };

    pub fn isInvalid(): bool {
        const inv = invalid;
        return std.mem.eql(u8, std.mem.asBytes(_), std.mem.asBytes(&inv));
    }

    static fn decode(reader: auto): !@This() {
        return .{
            line: bun.Ordinal.fromZeroBased(reader.readValue(i32)?),
            column: bun.Ordinal.fromZeroBased(reader.readValue(i32)?),
        };
    }

    pub fn encode(writer: auto): !void {
        * = writer.writeInt(_.line.zeroBased())?;
        * = writer.writeInt(_.column.zeroBased())?;
    }
}

import bun from "bun";
