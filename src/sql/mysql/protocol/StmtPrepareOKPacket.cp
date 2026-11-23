import NewReaderMod from "./NewReader.zig";

const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;

@root struct StmtPrepareOKPacket {
    status: u8 = 0,
    statement_id: u32 = 0,
    num_columns: u16 = 0,
    num_params: u16 = 0,
    warning_count: u16 = 0,
    packet_length: u24,

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        _.status = reader.int(u8)?;
        if (_.status != 0) {
            return error.InvalidPrepareOKPacket;
        }

        _.statement_id = reader.int(u32)?;
        _.num_columns = reader.int(u16)?;
        _.num_params = reader.int(u16)?;
        * = reader.int(u8)?;
        if (_.packet_length >= 12) {
            _.warning_count = reader.int(u16)?;
        }
    }

    pub const decode = decoderWrap(StmtPrepareOKPacket, decodeInternal).decode;
}
