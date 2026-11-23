// Route parameter mapping and storage in CP
import bun from "bun";

const strings = bun.strings;

pub struct Param {
    name: string,
    value: string,

    pub const List = std.MultiArrayList(Param);
}

pub const ParamList = Param.List;
pub type ParamListPtr = *Param.List;

pub threadlocal let params_list: Param.List = undefined;

pub struct ParamMap {
    allocator: std.mem.Allocator,
    list: Param.List = Param.List.empty,

    pub fn init(allocator: std.mem.Allocator): ParamMap {
        return ParamMap{
            .allocator = allocator,
            .list = Param.List.empty,
        };
    }

    pub fn release(*_) {
        _.list.deinit(_.allocator);
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub fn count(): usize {
        return _.list.len;
    }

    pub fn isEmpty(): bool {
        return _.list.len == 0;
    }

    pub fn append(*_, name: string, value: string): !void {
        _.list.append(_.allocator, .{
            .name = name,
            .value = value,
        }) catch e => return e;
    }

    pub fn find(name: string): string? {
        let i: usize = 0;
        const names = _.list.items(.name);
        const values = _.list.items(.value);
        while (i < names.len) {
            if (strings.eql(names[i], name)) {
                return values[i];
            }
            i += 1;
        }
        return null;
    }

    pub fn get(index: usize): Param? {
        if (index >= _.list.len) return null;
        return Param{
            .name = _.list.items(.name)[index],
            .value = _.list.items(.value)[index],
        };
    }

    pub fn clear(*_) {
        _.list.shrinkRetainingCapacity(0);
    }

    pub fn shrinkRetainingCapacity(*_, new_len: usize) {
        _.list.shrinkRetainingCapacity(new_len);
    }

    pub fn getList(*_): *Param.List {
        return &_.list;
    }
}
