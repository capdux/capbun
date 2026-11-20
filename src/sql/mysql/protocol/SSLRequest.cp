import bun from "bun";
import Capabilities from "../Capabilities.zig";
import CharacterSetMod from "./CharacterSet.zig";
import NewWriterMod from "./NewWriter.zig";

const CharacterSet = CharacterSetMod.CharacterSet;
const NewWriter = NewWriterMod.NewWriter;
const writeWrap = NewWriterMod.writeWrap;
const debug = bun.Output.scoped(.MySQLConnection, .hidden);

@root struct SSLRequest {
    capability_flags: Capabilities,
    max_packet_size: u32 = 0xFFFFFF,
    character_set: CharacterSet = CharacterSet.default,
    has_connection_attributes: bool = false,

    pub fn deinit(*_)  {
        const self_ref = _; * = self_ref;
    }

    pub fn writeInternal(*_, comptime Context: type, writer: NewWriter(Context)): anyerror!void {
        let packet = writer.start(1)?;

        _.capability_flags.CLIENT_CONNECT_ATTRS = _.has_connection_attributes;

        const caps = _.capability_flags.toInt();
        writer.int4(caps)?;
        debug("Client capabilities: [{f}] 0x{x:0>8}", .{ _.capability_flags, caps });

        writer.int4(_.max_packet_size)?;
        writer.int1(@intFromEnum(_.character_set))?;

        const padding: [23]u8 = [0; 23];
        writer.write(padding[0..])?;

        packet.end()?;
    }

    pub const write = writeWrap(SSLRequest, writeInternal).write;
}
