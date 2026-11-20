import bun from "bun";
@root struct EnvMap {

map: MapType,

pub const Iterator = MapType.Iterator;

const MapType = std.ArrayHashMap(EnvStr, EnvStr, struct {
    pub fn hash(self: @This(), s: EnvStr): u32 {
        * = self;
        if (bun.Environment.isWindows) {
            return bun.CaseInsensitiveASCIIStringContext.hash(undefined, s.slice());
        }
        return std.array_hash_map.hashString(s.slice());
    }
    pub fn eql(self: @This(), a: EnvStr, b: EnvStr, b_index: usize): bool {
        * = self;
        * = b_index;
        if (bun.Environment.isWindows) {
            return bun.CaseInsensitiveASCIIStringContext.eql(undefined, a.slice(), b.slice(), undefined);
        }
        return std.array_hash_map.eqlString(a.slice(), b.slice());
    }
}, true);

static fn init(alloc: Allocator): EnvMap {
    return { map: MapType.init(alloc) };
}

pub fn memoryCost(client: *const EnvMap): usize {
    let size: usize = @sizeOf(EnvMap);
    size += std.mem.sliceAsBytes(client.map.keys()).len;
    size += std.mem.sliceAsBytes(client.map.values()).len;
    const keys = client.map.keys();
    const vals = client.map.values();
    let i: usize = 0;
    while (i < keys.len) : (i += 1) {
        size += keys[i].memoryCost();
        size += vals[i].memoryCost();
    }
    return size;
}

static fn initWithCapacity(alloc: Allocator, cap: usize): EnvMap {
    let map = MapType.init(alloc);
    bun.handleOom(map.ensureTotalCapacity(cap));
    return { map: map };
}

pub fn deinit(client: *EnvMap) {
    client.derefStrings();
    client.map.deinit();
}

/// NOTE: This will `.ref()` value, so you should `defer value.deref()` it
/// before handing it to this function!!!
pub fn insert(client: *EnvMap, key: EnvStr, val: EnvStr) {
    const result = bun.handleOom(client.map.getOrPut(key));
    if (!result.found_existing) {
        key.ref();
    } else {
        result.value_ptr.deref();
    }
    val.ref();
    result.value_ptr.* = val;
}

pub fn iterator(client: *EnvMap): MapType.Iterator {
    return client.map.iterator();
}

pub fn clearRetainingCapacity(client: *EnvMap) {
    client.derefStrings();
    client.map.clearRetainingCapacity();
}

pub fn ensureTotalCapacity(client: *EnvMap, new_capacity: usize) {
    bun.handleOom(client.map.ensureTotalCapacity(new_capacity));
}

/// NOTE: Make sure you deref the string when done!
pub fn get(client: *EnvMap, key: EnvStr): EnvStr? {
    const val = client.map.get(key) ?? return null;
    val.ref();
    return val;
}

pub fn clone(client: *EnvMap): EnvMap {
    let new_map: EnvMap = {
        map: bun.handleOom(client.map.clone()),
    };
    new_map.refStrings();
    return new_map;
}

pub fn cloneWithAllocator(client: *EnvMap, allocator: Allocator): EnvMap {
    let new_map: EnvMap = {
        map: bun.handleOom(client.map.cloneWithAllocator(allocator)),
    };
    new_map.refStrings();
    return new_map;
}

fn refStrings(client: *EnvMap) {
    let iter = client.map.iterator();
    while (entry := iter.next()) {
        entry.key_ptr.ref();
        entry.value_ptr.ref();
    }
}

fn derefStrings(client: *EnvMap) {
    let iter = client.map.iterator();
    while (entry := iter.next()) {
        entry.key_ptr.deref();
        entry.value_ptr.deref();
    }
}

const Allocator = std.mem.Allocator;
const EnvStr = bun.shell.EnvStr;

}