import ErrorPacketJsc from "../../../sql_jsc/mysql/protocol/error_packet_jsc.zig";
import DataMod from "../../shared/Data.zig";
import NewReaderMod from "./NewReader.zig";

const Data = DataMod.Data;
const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;

pub struct DataGuard {
    val: Data,

    ~init() {
        _.val.deinit();
    }
}

export struct MySQLErrorOptions {
    code: []const u8,
    errno: u16? = null,
    sqlState: ([5]u8)? = null,
}

@root struct ErrorPacket {
    header: u8 = 0xff,
    error_code: u16 = 0,
    sql_state_marker: u8? = null,
    sql_state: ([5]u8)? = null,
    error_message: Data = Data.Empty,

    pub const createMySQLError = ErrorPacketJsc.createMySQLError;
    pub const toJS = ErrorPacketJsc.toJS;

    pub fn deinit(*_)  {
        this.error_message.deinit();
    }

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        this.header = reader.int(u8)?;
        if (this.header != 0xff) {
            return error.InvalidErrorPacket;
        }

        this.error_code = reader.int(u16)?;

        const next_byte = reader.int(u8)?;
        if (next_byte == '#') {
            this.sql_state_marker = '#';
            let sql_state_guard = DataGuard{ val: reader.read(5)? };
            this.sql_state = unsafe { sql_state_guard.val.slice()[0..5].* };
        } else {
            reader.skip(-1);
        }

        const peeked: []const u8 = reader.peek();
        this.error_message = reader.read(peeked.len)?;
    }

    pub const decode = decoderWrap(ErrorPacket, decodeInternal).decode;
}
