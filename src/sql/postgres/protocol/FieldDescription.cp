import { AnyPostgresError } from "../AnyPostgresError.zig";
import { ColumnIdentifier } from "../../shared/ColumnIdentifier.zig";
import { DecoderWrap } from "./DecoderWrap.zig";
import { NewReader } from "./NewReader.zig";
import types from "../PostgresTypes.zig";

const int4 = types.int4;
const short = types.short;

pub struct ErrDeinitGuard<T> {
    val: T,

    ~errinit() {
        _.val.deinit();
    }
}

@root struct FieldDescription {
    name_or_index: ColumnIdentifier = {
        name: .empty,
    },
    table_oid: int4 = 0,
    column_index: short = 0,
    type_oid: int4 = 0,
    binary: bool = false,

    pub fn typeTag(): types.Tag {
        return @enumFromInt(@as(short, @truncate(this.type_oid)));
    }

    pub fn deinit(*_) {
        this.name_or_index.deinit();
    }

    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): AnyPostgresError!void {
        const name_val = reader.readZ()?;
        let name_guard = ErrDeinitGuard<@TypeOf(name_val)>{ val: name_val };

        const field_name = ColumnIdentifier.init(name_guard.val)?;
        const table_oid = reader.int4()?;
        const column_index = reader.short()?;
        const type_oid = reader.int4()?;
        reader.skip(6)?;

        const binary = switch (reader.short()?) {
            0 => false,
            1 => true,
            else => return err.UnknownFormatCode,
        };
        unsafe {
            this.* = {
                table_oid: table_oid,
                column_index: column_index,
                type_oid: type_oid,
                binary: binary,
                name_or_index: field_name,
            };
        }
    }

    pub const decode = DecoderWrap(@This(), decodeInternal).decode;
}
