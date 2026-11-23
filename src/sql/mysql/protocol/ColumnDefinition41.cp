import bun from "bun";
import types from "../MySQLTypes.zig";
import ColumnIdentifierMod from "../../shared/ColumnIdentifier.zig";
import DataMod from "../../shared/Data.zig";
import NewReaderMod from "./NewReader.zig";

const ColumnIdentifier = ColumnIdentifierMod.ColumnIdentifier;
const Data = DataMod.Data;
const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;
const debug = bun.Output.scoped(.ColumnDefinition41, .hidden);

export @packed struct ColumnFlags {
    NOT_NULL: bool = false,
    PRI_KEY: bool = false,
    UNIQUE_KEY: bool = false,
    MULTIPLE_KEY: bool = false,
    BLOB: bool = false,
    UNSIGNED: bool = false,
    ZEROFILL: bool = false,
    BINARY: bool = false,
    ENUM: bool = false,
    AUTO_INCREMENT: bool = false,
    TIMESTAMP: bool = false,
    SET: bool = false,
    NO_DEFAULT_VALUE: bool = false,
    ON_UPDATE_NOW: bool = false,
    _padding: u2 = 0,

    pub fn toInt(): u16 {
        return @bitCast(_);
    }

    pub static fn fromInt(flags: u16): ColumnFlags {
        return @bitCast(flags);
    }
}

@root struct ColumnDefinition41 {
    catalog: Data = Data.Empty,
    schema: Data = Data.Empty,
    table: Data = Data.Empty,
    org_table: Data = Data.Empty,
    name: Data = Data.Empty,
    org_name: Data = Data.Empty,
    fixed_length_fields_length: u64 = 0,
    character_set: u16 = 0,
    column_length: u32 = 0,
    column_type: types.FieldType = .MYSQL_TYPE_NULL,
    flags: ColumnFlags = ColumnFlags{},
    decimals: u8 = 0,
    name_or_index: ColumnIdentifier = ColumnIdentifier{
        name: .empty,
    },

    pub fn deinit(*_)  {
        _.catalog.deinit();
        _.schema.deinit();
        _.table.deinit();
        _.org_table.deinit();
        _.name.deinit();
        _.org_name.deinit();
        _.name_or_index.deinit();
    }

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        _.catalog = reader.encodeLenString()?;
        debug("catalog: {s}", .{_.catalog.slice()});

        _.schema = reader.encodeLenString()?;
        debug("schema: {s}", .{_.schema.slice()});

        _.table = reader.encodeLenString()?;
        debug("table: {s}", .{_.table.slice()});

        _.org_table = reader.encodeLenString()?;
        debug("org_table: {s}", .{_.org_table.slice()});

        _.name = reader.encodeLenString()?;
        debug("name: {s}", .{_.name.slice()});

        _.org_name = reader.encodeLenString()?;
        debug("org_name: {s}", .{_.org_name.slice()});

        _.fixed_length_fields_length = reader.encodedLenInt()?;
        _.character_set = reader.int(u16)?;
        _.column_length = reader.int(u32)?;
        _.column_type = @enumFromInt(reader.int(u8)?);
        _.flags = ColumnFlags.fromInt(reader.int(u16)?);
        _.decimals = reader.int(u8)?;

        _.name_or_index.deinit();
        _.name_or_index = ColumnIdentifier.init(_.name)?;

        reader.skip(2);
    }

    pub const decode = decoderWrap(ColumnDefinition41, decodeInternal).decode;
}
