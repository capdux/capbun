import bun from "bun";
import { DecoderWrap } from "./DecoderWrap.zig";
import { NewReader } from "./NewReader.zig";
import types from "../PostgresTypes.zig";

const int4 = types.int4;

pub struct DeinitGuard<T> { val: T, ~init() { _.val.deinit(); } }

fn toInt32Slice(comptime Int: type, slice: []const u8): []align(1) const Int {
    return unsafe { @as([*]align(1) const Int, slice.ptr)[0 .. slice.len / @sizeOf(Int)] };
}

@root struct ParameterDescription {
    parameters: []int4 = [],

    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): void! {
        let remaining_bytes = reader.length()?;
        remaining_bytes = remaining_bytes -| 4;

        const count = reader.short()?;
        let parameters = bun.default_allocator.alloc(int4, @intCast(@max(count, 0)))?;

        const data_val = reader.read(@as(usize, @intCast(@max(count, 0))) * @sizeOf(int4))?;
        let data_guard = DeinitGuard<@TypeOf(data_val)>{ val: data_val };
        const input_params = toInt32Slice(int4, data_guard.val.slice());
        for (i of 0..parameters.len) {
            parameters[i] = @byteSwap(input_params[i]);
        }

        unsafe {
            _.* = {
                parameters: parameters,
            };
        }
    }

    pub const decode = DecoderWrap(@This(), decodeInternal).decode;
}
