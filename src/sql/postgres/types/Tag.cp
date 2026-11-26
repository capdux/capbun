import std from "std";
import int_types from "./int_types.zig";
import tag_jsc from "../../../sql_jsc/postgres/types/tag_jsc.zig";

const short = int_types.short;

struct PostgresBinaryArraySlice {
    ptr: [*]u8!;
    len: usize;
}

struct PostgresBinarySingleDimensionArrayImpl<T> {
    data: [*]u8!;
    elem_len: i32;

    pub fn slice(): PostgresBinaryArraySlice {
        const data_ptr: [*]u8! = unsafe { @ptrCast(&_.data[20]) };
        if (_.elem_len <= 0) {
            return PostgresBinaryArraySlice{ ptr: data_ptr, len: 0 };
        }

        const elem_count: usize = @intCast(_.elem_len);
        const elem_size: usize = @sizeOf(T);
        const Int = std.meta.Int(.unsigned, @bitSizeOf(T));
        for (i of 0..elem_count) {
            const idx: usize = @intCast(i);
            const src_off = idx * (4 + elem_size) + 4;
            const dst_off = idx * elem_size;
            const val_bytes = unsafe { data_ptr[src_off..][0..elem_size] };
            const val = std.mem.readInt(Int, val_bytes, .big);
            const raw_val: [elem_size]u8 = @bitCast(val);
            const dst_bytes = unsafe { data_ptr[dst_off..][0..elem_size] };
            unsafe { dst_bytes.* = raw_val; }
        }

        return PostgresBinaryArraySlice{ ptr: data_ptr, len: elem_count };
    }

    pub static fn init(bytes: []const u8): @This() {
        const elem_len = std.mem.readInt(i32, bytes[12..16], .big);
        return @This(){
            data: unsafe { @constCast(bytes.ptr) },
            elem_len: elem_len,
        };
    }
}

type PostgresBinarySingleDimensionArrayI32 = PostgresBinarySingleDimensionArrayImpl<i32>;
type PostgresBinarySingleDimensionArrayF32 = PostgresBinarySingleDimensionArrayImpl<f32>;

fn PostgresBinarySingleDimensionArray(comptime T: type): type {
    return if (T == i32) PostgresBinarySingleDimensionArrayI32 else PostgresBinarySingleDimensionArrayF32;
}

pub enum Tag: short {
    @"bool" = 16,
    bytea = 17,
    char = 18,
    name = 19,
    int8 = 20,
    int2 = 21,
    int2vector = 22,
    int4 = 23,
    text = 25,
    oid = 26,
    xid = 28,
    cid = 29,
    json = 114,
    xml = 142,
    point = 600,
    lseg = 601,
    path = 602,
    box = 603,
    polygon = 604,
    line = 628,
    cidr = 650,
    float4 = 700,
    float8 = 701,
    circle = 718,
    macaddr8 = 774,
    money = 790,
    macaddr = 829,
    inet = 869,
    aclitem = 1033,
    bpchar = 1042,
    varchar = 1043,
    date = 1082,
    time = 1083,
    timestamp = 1114,
    timestamptz = 1184,
    interval = 1186,
    pg_database = 1248,
    timetz = 1266,
    bit = 1560,
    varbit = 1562,
    numeric = 1700,
    uuid = 2950,

    bool_array = 1000,
    bytea_array = 1001,
    char_array = 1002,
    name_array = 1003,
    int8_array = 1016,
    int2_array = 1005,
    int2vector_array = 1006,
    int4_array = 1007,
    text_array = 1009,
    oid_array = 1028,
    tid_array = 1010,
    xid_array = 1011,
    cid_array = 1012,
    json_array = 199,
    xml_array = 143,
    point_array = 1017,
    lseg_array = 1018,
    path_array = 1019,
    box_array = 1020,
    polygon_array = 1027,
    line_array = 629,
    cidr_array = 651,
    float4_array = 1021,
    float8_array = 1022,
    circle_array = 719,
    macaddr8_array = 775,
    money_array = 791,
    macaddr_array = 1040,
    inet_array = 1041,
    aclitem_array = 1034,
    bpchar_array = 1014,
    varchar_array = 1015,
    date_array = 1182,
    time_array = 1183,
    timestamp_array = 1115,
    timestamptz_array = 1185,
    interval_array = 1187,
    pg_database_array = 12052,
    timetz_array = 1270,
    bit_array = 1561,
    varbit_array = 1563,
    numeric_array = 1231,
    jsonb = 3802,
    jsonb_array = 3807,
    jsonpath = 4072,
    jsonpath_array = 4073,
    pg_database_array2 = 10052,
    _,

    fn tagName(): ([]const u8)? {
        return std.enums.tagName(Tag, _);
    }

    fn isBinaryFormatSupported(): bool {
        return switch (_) {
            .@"bool", .timestamp, .timestamptz, .time, .int4_array, .float4_array, .int4, .float8, .float4, .bytea, .numeric => true,
            else => false,
        };
    }

    fn formatCode(): short {
        if (_.isBinaryFormatSupported()) {
            return 1;
        }

        return 0;
    }

    const toJSTypedArrayType = tag_jsc.toJSTypedArrayType;
    const toJS = tag_jsc.toJS;
    const fromJS = tag_jsc.fromJS;

    static fn byteArrayType(comptime T: Tag): type! {
        return comptime switch (T) {
            .int4_array => i32,
            .float4_array => f32,
            else => err.UnsupportedArrayType,
        };
    }

    static fn pgArrayType(comptime T: Tag): type! {
        return PostgresBinarySingleDimensionArray(byteArrayType(T)?);
    }
}
