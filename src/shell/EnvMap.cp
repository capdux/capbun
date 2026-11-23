import bun from "bun";
@root struct EnvMap {

map: MapType,

pub const Iterator = MapType.Iterator;

const MapType = std.ArrayHashMap(EnvStr, EnvStr, struct {
    pub fn hash(_: @This(), s: EnvStr): u32 {
        if (bun.Environment.isWindows) {
            return bun.CaseInsensitiveASCIIStringContext.hash(undefined, s.slice());
        }
        return std.array_hash_map.hashString(s.slice());
    }
    pub fn eql(_: @This(), a: EnvStr, b: EnvStr, b_index: usize): bool {
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

fn memoryCost(): usize {
    let size: usize = @sizeOf(EnvMap);
    size += std.mem.sliceAsBytes(_.map.keys()).len;
    size += std.mem.sliceAsBytes(_.map.values()).len;
    const keys = _.map.keys();
    const vals = _.map.values();
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

fn deinit(*_) {
    _.derefStrings();
    _.map.deinit();
}

/// NOTE: This will `.ref()` value, so you should `defer value.deref()` it
/// before handing it to this function!!!
fn insert(*_, key: EnvStr, val: EnvStr) {
    const result = bun.handleOom(_.map.getOrPut(key));
    if (!result.found_existing) {
        key.ref();
    } else {
        result.value_ptr.deref();
    }
    val.ref();
    result.value_ptr.* = val;
}

fn iterator(*_): MapType.Iterator {
    return _.map.iterator();
}

fn clearRetainingCapacity(*_) {
    _.derefStrings();
    _.map.clearRetainingCapacity();
}

fn ensureTotalCapacity(*_, new_capacity: usize) {
    bun.handleOom(_.map.ensureTotalCapacity(new_capacity));
}

/// NOTE: Make sure you deref the string when done!
fn get(key: EnvStr): EnvStr? {
    const val = _.map.get(key) ?? return null;
    val.ref();
    return val;
}

fn clone(): EnvMap {
    let new_map: EnvMap = {
        map: bun.handleOom(_.map.clone()),
    };
    new_map.refStrings();
    return new_map;
}

fn cloneWithAllocator(allocator: Allocator): EnvMap {
    let new_map: EnvMap = {
        map: bun.handleOom(_.map.cloneWithAllocator(allocator)),
    };
    new_map.refStrings();
    return new_map;
}

fn refStrings(*_) {
    let iter = _.map.iterator();
    while (entry := iter.next()) {
        entry.key_ptr.ref();
        entry.value_ptr.ref();
    }
}

fn derefStrings(*_) {
    let iter = _.map.iterator();
    while (entry := iter.next()) {
        entry.key_ptr.deref();
        entry.value_ptr.deref();
    }
}

const Allocator = std.mem.Allocator;
const EnvStr = bun.shell.EnvStr;

}