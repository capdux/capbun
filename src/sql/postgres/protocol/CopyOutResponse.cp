import bun from "bun";
import { DecoderWrap } from "./DecoderWrap.zig";
import { NewReader } from "./NewReader.zig";

@root struct CopyOutResponse {
    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): void! {
        * = reader;
        const self_ref = _; * = self_ref;
        bun.Output.panic("TODO: not implemented {s}", .{bun.meta.typeBaseName(@typeName(@This()))});
    }

    pub const decode = DecoderWrap(@This(), decodeInternal).decode;
}
