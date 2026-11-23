import bun from "bun";
import DataMod from "../../shared/Data.zig";
import NewReaderMod from "./NewReader.zig";

const Data = DataMod.Data;
const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;

@root struct AuthSwitchRequest {
    header: u8 = 0xfe,
    plugin_name: Data = Data{ empty: undefined },
    plugin_data: Data = Data{ empty: undefined },
    packet_size: u24,

    pub fn deinit(*_)  {
        _.plugin_name.deinit();
        _.plugin_data.deinit();
    }

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        _.header = reader.int(u8)?;
        if (_.header != 0xfe) {
            return error.InvalidAuthSwitchRequest;
        }

        const remaining = reader.read(_.packet_size - 1)?;
        const remaining_slice = remaining.slice();
        bun.assert(remaining == .temporary);

        if (zero := bun.strings.indexOfChar(remaining_slice, 0)) {
            _.plugin_name = Data{
                temporary: remaining_slice[0..zero],
            };
            _.plugin_data = Data{
                temporary: remaining_slice[zero + 1 ..],
            };
            return;
        }
        return error.InvalidAuthSwitchRequest;
    }

    pub const decode = decoderWrap(AuthSwitchRequest, decodeInternal).decode;
}
