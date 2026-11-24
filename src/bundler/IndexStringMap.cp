import bun from "bun";

pub const Index = bun.ast.Index;
pub type string = []const u8;

pub struct FreeGuard<T> { allocator: std.mem.Allocator, slice: T, ~errinit() { _.allocator.free(_.slice); } }

@root struct IndexStringMap {

map: std.AutoArrayHashMapUnmanaged(Index.Int, string) = {},

pub fn deinit(*_, allocator: std.mem.Allocator) {
    for (value of _.map.values()) {
        allocator.free(value);
    }
    _.map.deinit(allocator);
}

pub fn get(index: Index.Int): string? {
    return _.map.get(index);
}

pub fn put(*_, allocator: std.mem.Allocator, index: Index.Int, value: string)! {
    const duped = allocator.dupe(u8, value)?;
    errdefer allocator.free(duped);
    _.map.put(allocator, index, duped)?;
}

}
