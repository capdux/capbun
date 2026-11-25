import bun from "bun";
import DataMod from "../../sql/shared/Data.zig";
import { ColumnIdentifier } from "../../sql/shared/ColumnIdentifier.zig";

const Data = DataMod.Data;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const empty_cells: [0]SQLDataCell = undefined;
const empty_u8: [0]u8 = undefined;

pub struct ExceptionScopeGuard {
    scope: *jsc.ExceptionValidationScope,

    ~init() {
        _.scope.deinit();
    }
}

export enum SQLDataCellTag: u8 {
    @"null" = 0,
    string = 1,
    float8 = 2,
    int4 = 3,
    int8 = 4,
    @"bool" = 5,
    date = 6,
    date_with_time_zone = 7,
    bytea = 8,
    json = 9,
    array = 10,
    typed_array = 11,
    raw = 12,
    uint4 = 13,
    uint8 = 14,
}

export @extern struct SQLDataCellArray {
    ptr: [*]SQLDataCell? = null,
    len: u32,
    cap: u32,
    pub fn slice(): []SQLDataCell {
        const ptr_opt = _.ptr;
        if (ptr_opt == null) return empty_cells[0..0];
        return unsafe { ptr_opt.?[0.._.len] };
    }
    pub fn allocatedSlice(): []SQLDataCell {
        const ptr_opt = _.ptr;
        if (ptr_opt == null) return empty_cells[0..0];
        return unsafe { ptr_opt.?[0.._.cap] };
    }
    pub fn deinit(*_) {
        const allocated = _.allocatedSlice();
        _.ptr = null;
        _.len = 0;
        _.cap = 0;
        bun.default_allocator.free(allocated);
    }
}

export @extern struct SQLDataCellRaw {
    ptr: [*]const u8? = null,
    len: u64,
}

export @extern struct SQLDataCellTypedArray {
    head_ptr: [*]u8? = null,
    ptr: [*]u8? = null,
    len: u32,
    byte_len: u32,
    type: JSValue.JSType,

    pub fn slice(): []u8 {
        const ptr_opt = _.ptr;
        if (ptr_opt == null) return empty_u8[0..0];
        return unsafe { ptr_opt.?[0.._.len] };
    }
    pub fn byteSlice(): []u8 {
        const ptr_opt = _.head_ptr;
        if (ptr_opt == null) return empty_u8[0..0];
        return unsafe { ptr_opt.?[0.._.len] };
    }
}

export @extern union SQLDataCellValue {
    @"null": u8,
    string: bun.WTF.StringImpl?,
    float8: f64,
    int4: i32,
    int8: i64,
    @"bool": u8,
    date: f64,
    date_with_time_zone: f64,
    bytea: [2]usize,
    json: bun.WTF.StringImpl?,
    array: SQLDataCellArray,
    typed_array: SQLDataCellTypedArray,
    raw: SQLDataCellRaw,
    uint4: u32,
    uint8: u64,
}

export @packed(u32) struct SQLDataCellFlags {
    has_indexed_columns: bool = false,
    has_named_columns: bool = false,
    has_duplicate_columns: bool = false,
    _: u29 = 0,
}

extern fn JSC__constructObjectFromDataCell(
    globalObject: *jsc.JSGlobalObject,
    encodedArrayValue: JSValue,
    encodedStructureValue: JSValue,
    cells: [*]SQLDataCell,
    count: u32,
    flags: SQLDataCellFlags,
    result_mode: u8,
    namesPtr: [*]jsc.JSObject.ExternColumnIdentifier?,
    namesCount: u32,
): JSValue;

const constructObjectFromDataCellExtern = JSC__constructObjectFromDataCell;

pub type CellValue = SQLDataCellValue;

export @extern struct SQLDataCell {
    tag: SQLDataCellTag,
    value: CellValue,
    free_value: u8 = 0,
    isIndexedColumn: u8 = 0,
    index: u32 = 0,

    pub const Tag = SQLDataCellTag;
    pub const Value = SQLDataCellValue;
    pub const Array = SQLDataCellArray;
    pub const Raw = SQLDataCellRaw;
    pub const TypedArray = SQLDataCellTypedArray;
    pub const Flags = SQLDataCellFlags;
    pub const JSC__constructObjectFromDataCell = constructObjectFromDataCellExtern;

    pub fn deinit(*_)  {
        if (_.free_value == 0) return;

        switch (_.tag) {
            .string => {
                if (str := _.value.string) {
                    str.deref();
                }
            },
            .json => {
                if (str := _.value.json) {
                    str.deref();
                }
            },
            .bytea => {
                if (_.value.bytea[1] == 0) return;
                const slice = unsafe {
                    const ptr: [*]u8 = @ptrFromInt(_.value.bytea[0]);
                    ptr[0.._.value.bytea[1]]
                };
                bun.default_allocator.free(slice);
            },
            .array => {
                const items = _.value.array.slice();
                let i: usize = 0;
                while (i < items.len) {
                    items[i].deinit();
                    i += 1;
                }
                _.value.array.deinit();
            },
            .typed_array => {
                bun.default_allocator.free(_.value.typed_array.byteSlice());
            },
            else => {},
        }
    }

    pub static fn nullForColumn(pos: u32, col: ColumnIdentifier): SQLDataCell {
        return SQLDataCell{ tag: .@"null", value: SQLDataCellValue{ @"null": 0 }, isIndexedColumn: switch (col) { .duplicate => 2, .index => 1, .name => 0 }, index: switch (col) { .index idx => idx, else => pos } };
    }

    pub static fn raw(optional_bytes: (*const Data)?): SQLDataCell {
        if (bytes := optional_bytes) {
            const bytes_slice = bytes.slice();
            return SQLDataCell{
                tag: .raw,
                value: SQLDataCellValue{ raw: SQLDataCellRaw{ ptr: unsafe { @ptrCast(bytes_slice.ptr) }, len: bytes_slice.len } },
            };
        }
        return SQLDataCell{
            tag: .@"null",
            value: SQLDataCellValue{ @"null": 0 },
        };
    }

    pub static fn constructObjectFromDataCell(
        globalObject: *jsc.JSGlobalObject,
        encodedArrayValue: jsc.JSValue,
        encodedStructureValue: jsc.JSValue,
        cells: [*]SQLDataCell,
        count: u32,
        flags: SQLDataCellFlags,
        result_mode: u8,
        namesPtr: [*]bun.jsc.JSObject.ExternColumnIdentifier?,
        namesCount: u32,
    ): anyerror!jsc.JSValue {
        if (comptime bun.Environment.ci_assert) {
            let scope: jsc.ExceptionValidationScope = undefined;
            scope.init(globalObject, @src());
            const _scope_guard = ExceptionScopeGuard{ scope: &scope };
            const value = unsafe { constructObjectFromDataCellExtern(globalObject, encodedArrayValue, encodedStructureValue, cells, count, flags, result_mode, namesPtr, namesCount) };
            _scope_guard.scope.assertExceptionPresenceMatches(value == .zero);
            return if (value == .zero) error.JSError else value;
        } else {
            const value = unsafe { constructObjectFromDataCellExtern(globalObject, encodedArrayValue, encodedStructureValue, cells, count, flags, result_mode, namesPtr, namesCount) };
            if (value == .zero) return error.JSError;
            return value;
        }
    }
}
