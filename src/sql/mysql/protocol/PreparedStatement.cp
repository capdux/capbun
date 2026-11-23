import std from "std";
import bun from "bun";
import AnyMySQLError from "./AnyMySQLError.zig";
import CommandTypeMod from "./CommandType.zig";
import ParamMod from "../MySQLParam.zig";
import TypesMod from "../MySQLTypes.zig";
import NewReaderMod from "./NewReader.zig";
import NewWriterMod from "./NewWriter.zig";

const CommandType = CommandTypeMod.CommandType;
const Param = ParamMod.Param;
const Value = TypesMod.Value;

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}
const NewReader = NewReaderMod.NewReader;
const decoderWrap = NewReaderMod.decoderWrap;
const NewWriter = NewWriterMod.NewWriter;
const writeWrap = NewWriterMod.writeWrap;
const debug = bun.Output.scoped(.PreparedStatement, .hidden);
const empty_values: [0]Value = undefined;

export struct PrepareOK {
    status: u8 = 0,
    statement_id: u32,
    num_columns: u16,
    num_params: u16,
    warning_count: u16,

    pub fn decodeInternal(*_, comptime Context: type, reader: NewReader(Context)): anyerror!void {
        _.status = reader.int(u8)?;
        if (_.status != 0) {
            return error.InvalidPrepareOKPacket;
        }

        _.statement_id = reader.int(u32)?;
        _.num_columns = reader.int(u16)?;
        _.num_params = reader.int(u16)?;
        * = reader.int(u8)?;
        _.warning_count = reader.int(u16)?;
    }

    pub const decode = decoderWrap(PrepareOK, decodeInternal).decode;
}

export struct Execute {
    statement_id: u32,
    flags: u8 = 0,
    iteration_count: u32 = 1,
    params: []Value = empty_values[0..0],
    param_types: []const Param,
    new_params_bind_flag: bool,

    pub fn deinit(*_)  {
        let i: usize = 0;
        while (i < _.params.len) {
            _.params[i].deinit(bun.default_allocator);
            i += 1;
        }
        if (_.params.len > 0) {
            bun.default_allocator.free(_.params);
        }
    }

    fn writeNullBitmap(comptime Context: type, writer: NewWriter(Context)): AnyMySQLError.Error!void {
        const MYSQL_MAX_PARAMS = (std.math.maxInt(u16) / 8) + 1;

        let null_bitmap_buf: [MYSQL_MAX_PARAMS]u8 = undefined;
        const bitmap_bytes = (_.params.len + 7) / 8;
        let null_bitmap = null_bitmap_buf[0..bitmap_bytes];
        @memset(null_bitmap, 0);

        let i: usize = 0;
        while (i < _.params.len) {
            if (_.params[i] == .@"null") {
                null_bitmap[i >> 3] |= @as(u8, 1) << @as(u3, @truncate(i & 7));
            }
            i += 1;
        }

        writer.write(null_bitmap)?;
    }

    pub fn writeInternal(comptime Context: type, writer: NewWriter(Context)): AnyMySQLError.Error!void {
        writer.int1(@intFromEnum(CommandType.COM_STMT_EXECUTE))?;
        writer.int4(_.statement_id)?;
        writer.int1(_.flags)?;
        writer.int4(_.iteration_count)?;

        if (_.params.len > 0) {
            _.writeNullBitmap(Context, writer)?;

            writer.int1(@intFromBool(_.new_params_bind_flag))?;

            if (_.new_params_bind_flag) {
                const param_types = _.param_types;
                for (param_type of param_types) {
                    debug("New params bind flag {s} unsigned? {}", .{ @tagName(param_type.type), param_type.flags.UNSIGNED });
                    writer.int1(@intFromEnum(param_type.type))?;
                    writer.int1(if (param_type.flags.UNSIGNED) 0x80 else 0)?;
                }
            }

            let idx: usize = 0;
            while (idx < _.params.len) {
                const param = &_.params[idx];
                const param_type = _.param_types[idx];
                idx += 1;
                if (unsafe { param.* } == .@"null" || param_type.type == .MYSQL_TYPE_NULL) continue;

                const val = param.toData(param_type.type)?;
                let val_guard = DeinitGuard<@TypeOf(val)>{ val: val };
                if (param_type.type.isBinaryFormatSupported()) {
                    writer.write(val_guard.val.slice())?;
                } else {
                    writer.writeLengthEncodedString(val_guard.val.slice())?;
                }
            }
        }
    }

    pub const write = writeWrap(Execute, writeInternal).write;
}

