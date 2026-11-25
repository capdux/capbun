import FieldDescription from "./FieldDescription.zig";
import bun from "bun";
import { DecoderWrap } from "./DecoderWrap.zig";
import { NewReader } from "./NewReader.zig";

pub struct FieldsErrGuard {
    fields: []FieldDescription,
    initialized: usize = 0,

    ~errinit() {
        for (i of 0.._.initialized) {
            _.fields[i].deinit();
        }
        bun.default_allocator.free(_.fields);
    }
}

@root struct RowDescription {
    fields: []FieldDescription = [],

    pub fn deinit(*_) {
        for (i of 0.._.fields.len) {
            _.fields[i].deinit();
        }
        bun.default_allocator.free(_.fields);
    }

    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): void! {
        let remaining_bytes: usize = @intCast((reader.length()?) - 4);
        if (remaining_bytes < 2) return err.InvalidMessage;
        remaining_bytes -= 2;

        const field_count: usize = @intCast(reader.short()?);
        if (field_count > remaining_bytes) return err.InvalidMessage;
        let fields_guard = FieldsErrGuard{
            fields: bun.default_allocator.alloc(FieldDescription, field_count)?,
        };
        while (fields_guard.initialized < field_count) {
            fields_guard.fields[fields_guard.initialized].decodeInternal(Container, reader) catch e => {
                if (e == error.ShortRead) return err.InvalidMessage;
                return e;
            };
            fields_guard.initialized += 1;
        }
        unsafe {
            _.* = { fields: fields_guard.fields };
        }
    }

    pub const decode = DecoderWrap(@This(), decodeInternal).decode;
}
