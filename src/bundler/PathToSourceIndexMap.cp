import bun from "bun";

const Fs = bun.fs;
const Index = bun.ast.Index;
const Map = bun.StringHashMapUnmanaged(Index.Int);

@root struct PathToSourceIndexMap {
    /// The lifetime of the keys are not owned by this map.
    ///
    /// We assume it's arena allocated.
    map: Map = {},

    pub fn getPath(path: *const Fs.Path): Index.Int? {
        return _.get(path.text);
    }

    pub fn get(text: []const u8): Index.Int? {
        return _.map.get(text);
    }

    pub fn putPath(*_, allocator: std.mem.Allocator, path: *const Fs.Path, value: Index.Int): void!bun.OOM {
        _.map.put(allocator, path.text, value)?;
    }

    pub fn put(*_, allocator: std.mem.Allocator, text: []const u8, value: Index.Int): void!bun.OOM {
        _.map.put(allocator, text, value)?;
    }

    pub fn getOrPutPath(*_, allocator: std.mem.Allocator, path: *const Fs.Path): Map.GetOrPutResult!bun.OOM {
        return _.getOrPut(allocator, path.text);
    }

    pub fn getOrPut(*_, allocator: std.mem.Allocator, text: []const u8): Map.GetOrPutResult!bun.OOM {
        return _.map.getOrPut(allocator, text)?;
    }

    pub fn remove(*_, text: []const u8): bool {
        return _.map.remove(text);
    }

    pub fn removePath(*_, path: *const Fs.Path): bool {
        return _.remove(path.text);
    }
}
