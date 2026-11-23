import bun from "bun";

const jsc = bun.jsc;
const JSValue = jsc.JSValue;

@root struct ObjectIterator {
    array: JSValue,
    columns: JSValue = .zero,
    globalObject: *jsc.JSGlobalObject,
    cell_i: usize = 0,
    row_i: usize = 0,
    current_row: jsc.JSValue = .zero,
    columns_count: usize = 0,
    array_length: usize = 0,
    any_failed: bool = false,

    pub fn next(*_): jsc.JSValue? {
        if (_.array.isEmptyOrUndefinedOrNull() || _.columns.isEmptyOrUndefinedOrNull()) {
            _.any_failed = true;
            return null;
        }
        if (_.row_i >= _.array_length) {
            return null;
        }

        const cell_i = _.cell_i;
        _.cell_i += 1;
        const row_i = _.row_i;

        const globalObject = _.globalObject;

        if (_.current_row == .zero) {
            _.current_row = jsc.JSObject.getIndex(_.array, globalObject, @intCast(row_i)) catch e => {
                _.any_failed = true;
                return null;
            };
            if (_.current_row.isEmptyOrUndefinedOrNull()) {
                if (globalObject.throw("Expected a row to be returned at index {d}", .{row_i}) == error.JSError) {}
                return null;
            }
        }

        defer {
            if (_.cell_i >= _.columns_count) {
                _.cell_i = 0;
                _.current_row = .zero;
                _.row_i += 1;
            }
        }

        const property = jsc.JSObject.getIndex(_.columns, globalObject, @intCast(cell_i)) catch e => {
            _.any_failed = true;
            return null;
        };
        if (property.isUndefined()) {
            if (globalObject.throw("Expected a column at index {d} in row {d}", .{ cell_i, row_i }) == error.JSError) {}
            return null;
        }

        const value = _.current_row.getOwnByValue(globalObject, property);
        if (value == .zero || (value != null && value.?.isUndefined())) {
            if (!globalObject.hasException()) {
                if (globalObject.throw("Expected a value at index {d} in row {d}", .{ cell_i, row_i }) == error.JSError) {}
            }
            _.any_failed = true;
            return null;
        }
        return value;
    }
}
