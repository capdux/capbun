import bun from "bun";
import Capabilities from "../Capabilities.zig";
import CharacterSetMod from "./CharacterSet.zig";
import DataMod from "../../shared/Data.zig";
import StatusFlagsMod from "../StatusFlags.zig";
import NewReaderMod from "./NewReader.zig";

const CharacterSet = CharacterSetMod.CharacterSet;
const Data = DataMod.Data;
const StatusFlags = StatusFlagsMod.StatusFlags;
const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;
const empty_u8: [0]u8 = undefined;

struct DataGuard {
    data: Data,

    ~init() {
        _.data.deinit();
    }
}

@root struct HandshakeV10 {
    protocol_version: u8 = 10,
    server_version: Data = .empty,
    connection_id: u32 = 0,
    auth_plugin_data_part_1: [8]u8 = undefined,
    auth_plugin_data_part_2: []const u8 = empty_u8[0..0],
    capability_flags: Capabilities = Capabilities{},
    character_set: CharacterSet = CharacterSet.default,
    status_flags: StatusFlags = StatusFlags{},
    auth_plugin_name: Data = .empty,

    pub fn deinit(*_)  {
        _.server_version.deinit();
        if (_.auth_plugin_data_part_2.len > 0) {
            bun.default_allocator.free(_.auth_plugin_data_part_2);
        }
        _.auth_plugin_name.deinit();
    }

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        _.protocol_version = reader.int(u8)?;
        if (_.protocol_version != 10) {
            return error.UnsupportedProtocolVersion;
        }

        _.server_version = reader.readZ()?;
        _.connection_id = reader.int(u32)?;

        let auth_data_guard = DataGuard{ data: reader.read(8)? };
        @memcpy(unsafe { &_.auth_plugin_data_part_1 }, auth_data_guard.data.slice());

        * = reader.int(u8)?;

        const capabilities_lower = reader.int(u16)?;
        _.character_set = @enumFromInt(reader.int(u8)?);
        _.status_flags = StatusFlags.fromInt(reader.int(u16)?);

        const capabilities_upper = reader.int(u16)?;
        _.capability_flags = Capabilities.fromInt(@as(u32, capabilities_upper) << 16 | capabilities_lower);

        let auth_plugin_data_len = reader.int(u8)?;
        if (auth_plugin_data_len < 21) {
            auth_plugin_data_len = 21;
        }

        reader.skip(10);

        const remaining_auth_len = @max(13, auth_plugin_data_len - 8);
        let auth_data_2_guard = DataGuard{ data: reader.read(remaining_auth_len)? };
        _.auth_plugin_data_part_2 = bun.default_allocator.dupe(u8, auth_data_2_guard.data.slice())?;

        if (_.capability_flags.CLIENT_PLUGIN_AUTH) {
            _.auth_plugin_name = reader.readZ()?;
        }
    }

    pub const decode = decoderWrap(HandshakeV10, decodeInternal).decode;
}
