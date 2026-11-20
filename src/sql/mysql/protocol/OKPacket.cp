import DataMod from "../../shared/Data.zig";
import StatusFlagsMod from "../StatusFlags.zig";
import NewReaderMod from "./NewReader.zig";

const Data = DataMod.Data;
const StatusFlags = StatusFlagsMod.StatusFlags;
const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;

@root struct OKPacket {
    header: u8 = 0x00,
    affected_rows: u64 = 0,
    last_insert_id: u64 = 0,
    status_flags: StatusFlags = StatusFlags{},
    warnings: u16 = 0,
    info: Data = .empty,
    session_state_changes: Data = .empty,
    packet_size: u24,

    pub fn deinit(*_)  {
        _.info.deinit();
        _.session_state_changes.deinit();
    }

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        let read_size: usize = 5;
        _.header = reader.int(u8)?;
        if (_.header != 0x00 && _.header != 0xfe) {
            return error.InvalidOKPacket;
        }

        _.affected_rows = reader.encodedLenIntWithSize(&read_size)?;
        _.last_insert_id = reader.encodedLenIntWithSize(&read_size)?;
        _.status_flags = StatusFlags.fromInt(reader.int(u16)?);
        _.warnings = reader.int(u16)?;

        const peeked = reader.peek();
        if (peeked.len > 0 && _.packet_size > read_size) {
            const remaining = _.packet_size - read_size;
            _.info = reader.read(@truncate(remaining))?;
        }
    }

    pub const decode = decoderWrap(OKPacket, decodeInternal).decode;
}
