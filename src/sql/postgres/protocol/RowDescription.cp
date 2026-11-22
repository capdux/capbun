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
        for (i of 0..this.fields.len) {
            this.fields[i].deinit();
        }
        bun.default_allocator.free(this.fields);
    }

    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): void! {
        let remaining_bytes = reader.length()?;
        remaining_bytes = remaining_bytes -| 4;

        const field_count: usize = @intCast(@max(reader.short()?, 0));
        let fields_guard = FieldsErrGuard{
            fields: bun.default_allocator.alloc(FieldDescription, field_count)?,
        };
        while (fields_guard.initialized < field_count) {
            fields_guard.fields[fields_guard.initialized].decodeInternal(Container, reader)?;
            fields_guard.initialized += 1;
        }
        unsafe {
            this.* = { fields: fields_guard.fields };
        }
    }

    pub const decode = DecoderWrap(@This(), decodeInternal).decode;
}
