import DataMod from "../../shared/Data.zig";
import NewWriterMod from "./NewWriter.zig";

const Data = DataMod.Data;
const NewWriter = NewWriterMod.NewWriter;
const writeWrap = NewWriterMod.writeWrap;

@root struct AuthSwitchResponse {
    auth_response: Data = Data{ empty: undefined },

    pub fn deinit(*_)  {
        _.auth_response.deinit();
    }

    pub fn writeInternal(comptime Context: type, writer: NewWriter(Context)): anyerror!void {
        writer.write(_.auth_response.slice())?;
    }

    pub const write = writeWrap(AuthSwitchResponse, writeInternal).write;
}
