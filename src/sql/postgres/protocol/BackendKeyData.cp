import { DecoderWrap } from "./DecoderWrap.zig";
import { NewReader } from "./NewReader.zig";

@root struct BackendKeyData {
    process_id: u32 = 0,
    secret_key: u32 = 0,
    pub const decode = DecoderWrap(@This(), decodeInternal).decode;

    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container))! {
        const ok = reader.expectInt(u32, 12)?;
        if (!ok) {
            return error.InvalidBackendKeyData;
        }

        unsafe {
            _.* = BackendKeyData{
                process_id: @bitCast(reader.int4()?),
                secret_key: @bitCast(reader.int4()?),
            };
        }
    }
}
