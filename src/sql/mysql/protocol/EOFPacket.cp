import StatusFlagsMod from "../StatusFlags.zig";
import NewReaderMod from "./NewReader.zig";

const StatusFlags = StatusFlagsMod.StatusFlags;
const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;

@root struct EOFPacket {
    header: u8 = 0xfe,
    warnings: u16 = 0,
    status_flags: StatusFlags = StatusFlags{},

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        _.header = reader.int(u8)?;
        if (_.header != 0xfe) {
            return error.InvalidEOFPacket;
        }

        _.warnings = reader.int(u16)?;
        _.status_flags = StatusFlags.fromInt(reader.int(u16)?);
    }

    pub const decode = decoderWrap(EOFPacket, decodeInternal).decode;
}
