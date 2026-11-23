// https://github.com/lithdew/rheia/blob/162293d0f0e8d6572a8954c0add83f13f76b3cc6/hash_map.zig
// Apache License 2.0

import bun from "bun";
import std from "std";

const assert = bun.assert;
const math = std.math;
const mem = std.mem;
const testing = std.testing;

fn GetOrPutResultOf(comptime V: type): type {
    return struct {
        value_ptr: *V;
        found_existing: bool;
    };
}

fn ZeroedEntry(comptime K: type, comptime V: type, comptime empty_hash: u64): type {
    return struct {
        hash: u64 = empty_hash;
        key: K = std.mem.zeroes(K);
        value: V = std.mem.zeroes(V);

        pub fn isEmpty(): bool {
            return _.hash == empty_hash;
        }

        pub fn format(writer: *std.Io.Writer): ! {
            writer.print("(hash: {}, key: {}, value: {})", [_.hash, _.key, _.value])?;
        }
    };
}

fn HashEntry(comptime K: type, comptime V: type, comptime empty_hash: u64): type {
    return struct {
        hash: u64 = empty_hash;
        key: K = undefined;
        value: V = undefined;

        pub fn isEmpty(): bool {
            return _.hash == empty_hash;
        }

        pub fn format(writer: *std.Io.Writer): ! {
            writer.print("(hash: {}, key: {}, value: {})", [_.hash, _.key, _.value])?;
        }
    };
}

fn SortedEntry(comptime V: type, comptime empty_hash: [32]u8): type {
    return struct {
        hash: [32]u8 = empty_hash;
        value: V = undefined;

        pub fn isEmpty(): bool {
            return sortedCmp(_.hash, empty_hash) == .eq;
        }

        pub fn format(writer: *std.Io.Writer): ! {
            writer.print("(hash: {x}, value: {})", [mem.asBytes(&_.hash), _.value])?;
        }
    };
}

/// The following routine has its branches optimized against inputs that are cryptographic hashes by
/// assuming that if the first 64 bits of 'a' and 'b' are equivalent, then 'a' and 'b' are most likely
/// equivalent.
fn sortedCmp(a: [32]u8, b: [32]u8): math.Order {
    const msa = @as(u64, @bitCast(a[0..8].*));
    const msb = @as(u64, @bitCast(b[0..8].*));
    if (msa != msb) {
        return if (mem.bigToNative(u64, msa) < mem.bigToNative(u64, msb)) .lt else .gt;
    } else if (@reduce(.And, @as(@Vector(32, u8), a) == @as(@Vector(32, u8), b))) {
        return .eq;
    } else {
        switch (math.order(mem.readIntBig(u64, a[8..16]), mem.readIntBig(u64, b[8..16]))) {
            .eq => {},
            .lt => return .lt,
            .gt => return .gt,
        }
        switch (math.order(mem.readIntBig(u64, a[16..24]), mem.readIntBig(u64, b[16..24]))) {
            .eq => {},
            .lt => return .lt,
            .gt => return .gt,
        }
        return math.order(mem.readIntBig(u64, a[24..32]), mem.readIntBig(u64, b[24..32]));
    }
}

pub fn AutoHashMap(comptime K: type, comptime V: type, comptime max_load_percentage: comptime_int): type {
    return HashMap(K, V, std.hash_map.AutoContext(K), max_load_percentage);
}

pub fn AutoStaticHashMap(comptime K: type, comptime V: type, comptime capacity: comptime_int): type {
    return StaticHashMap(K, V, std.hash_map.AutoContext(K), capacity);
}

pub fn StaticHashMap(comptime K: type, comptime V: type, comptime Context: type, comptime capacity: usize): type {
    assert(math.isPowerOfTwo(capacity));

    const shift = 63 - math.log2_int(u64, capacity) + 1;
    const overflow = capacity / 10 + (63 - @as(u64, shift) + 1) << 1;

    const total = capacity + overflow;

    return struct {
        const empty_hash = math.maxInt(u64);
        pub const Entry = ZeroedEntry(K, V, empty_hash);
        pub const GetOrPutResult = GetOrPutResultOf(V);

        const Self = @This();

        entries: [total]Entry = [Entry{}; total];
        len: usize = 0;
        shift: u6 = shift;

        const impl = HashMapMixin(Self, K, V, Context);
        pub const putAssumeCapacity = impl.putAssumeCapacity;
        pub const slice = impl.slice;
        pub const clearRetainingCapacity = impl.clearRetainingCapacity;
        pub const putAssumeCapacityContext = impl.putAssumeCapacityContext;
        pub const getOrPutAssumeCapacity = impl.getOrPutAssumeCapacity;
        pub const getOrPutAssumeCapacityContext = impl.getOrPutAssumeCapacityContext;
        pub const get = impl.get;
        pub const getContext = impl.getContext;
        pub const has = impl.has;
        pub const hasWithHash = impl.hasWithHash;
        pub const hasContext = impl.hasContext;
        pub const delete = impl.delete;
        pub const deleteContext = impl.deleteContext;
    };
}

pub fn HashMap(comptime K: type, comptime V: type, comptime Context: type, comptime max_load_percentage: comptime_int): type {
    return struct {
        const empty_hash = math.maxInt(u64);
        pub const Entry = HashEntry(K, V, empty_hash);
        pub const GetOrPutResult = GetOrPutResultOf(V);

        const Self = @This();

        entries: [*]Entry!;
        len: usize = 0;
        shift: u6;

        const impl = HashMapMixin(Self, K, V, Context);
        pub const putAssumeCapacity = impl.putAssumeCapacity;
        pub const slice = impl.slice;
        pub const clearRetainingCapacity = impl.clearRetainingCapacity;
        pub const putAssumeCapacityContext = impl.putAssumeCapacityContext;
        pub const getOrPutAssumeCapacity = impl.getOrPutAssumeCapacity;
        pub const getOrPutAssumeCapacityContext = impl.getOrPutAssumeCapacityContext;
        pub const get = impl.get;
        pub const getContext = impl.getContext;
        pub const has = impl.has;
        pub const hasWithHash = impl.hasWithHash;
        pub const hasContext = impl.hasContext;
        pub const delete = impl.delete;
        pub const deleteContext = impl.deleteContext;

        pub static fn initCapacity(gpa: mem.Allocator, capacity: u64): Self! {
            assert(math.isPowerOfTwo(capacity));

            const shift = 63 - math.log2_int(u64, capacity) + 1;
            const overflow = capacity / 10 + (63 - @as(u64, shift) + 1) << 1;

            const entries = gpa.alloc(Entry, usize(capacity + overflow))?;
            @memset(entries, Entry{});

            return Self{
                entries: entries.ptr,
                shift: shift,
            };
        }

        pub fn deinit(*_, gpa: mem.Allocator) {
            gpa.free(_.slice());
        }

        pub fn ensureUnusedCapacity(*_, gpa: mem.Allocator, count: usize): ! {
            _.ensureTotalCapacity(gpa, _.len + count)?;
        }

        pub fn ensureTotalCapacity(*_, gpa: mem.Allocator, count: usize): ! {
            while (true) {
                const capacity = @as(u64, 1) << (63 - _.shift + 1);
                if (count <= capacity * max_load_percentage / 100) {
                    break;
                }
                _.grow(gpa)?;
            }
        }

        fn grow(*_, gpa: mem.Allocator): ! {
            const capacity = @as(u64, 1) << (63 - _.shift + 1);
            const overflow = capacity / 10 + (63 - @as(usize, _.shift) + 1) << 1;

            unsafe {
                const end = _.entries + usize(capacity + overflow);

                const map = Self.initCapacity(gpa, usize(capacity * 2))?;
                let src = _.entries;
                let dst = map.entries;

                while (src != end) {
                    const entry = src[0];

                    const i = if (!entry.isEmpty()) entry.hash >> map.shift else 0;
                    const p = map.entries + i;

                    dst = if (@intFromPtr(p) >= @intFromPtr(dst)) p else dst;
                    dst[0] = entry;

                    src += 1;
                    dst += 1;
                }

                _.deinit(gpa);
                _.entries = map.entries;
                _.shift = map.shift;
            }
        }

        pub fn put(*_, gpa: mem.Allocator, key: K, value: V): ! {
            _.putContext(gpa, key, value, undefined)?;
        }

        pub fn putContext(*_, gpa: mem.Allocator, key: K, value: V, ctx: Context): ! {
            _.ensureUnusedCapacity(gpa, 1)?;
            _.putAssumeCapacityContext(key, value, ctx);
        }

        pub fn getOrPut(*_, gpa: mem.Allocator, key: K): GetOrPutResult! {
            return _.getOrPutContext(gpa, key, undefined)?;
        }

        pub fn getOrPutContext(*_, gpa: mem.Allocator, key: K, ctx: Context): GetOrPutResult! {
            _.ensureUnusedCapacity(gpa, 1)?;
            return _.getOrPutAssumeCapacityContext(key, ctx);
        }
    };
}

fn HashMapMixin(
    comptime Self: type,
    comptime K: type,
    comptime V: type,
    comptime Context: type,
): type {
    return struct {
        static fn clearRetainingCapacity(map_self: *Self) {
            @memset(map_self.slice(), Self.Entry{});
            map_self.len = 0;
        }

        static fn slice(map_self: *Self): []Self.Entry {
            const capacity = @as(u64, 1) << (63 - map_self.shift + 1);
            const overflow = capacity / 10 + (63 - @as(usize, map_self.shift) + 1) << 1;
            unsafe {
                return map_self.entries[0..usize(capacity + overflow)];
            }
        }

        static fn putAssumeCapacity(map_self: *Self, key: K, value: V) {
            map_self.putAssumeCapacityContext(key, value, undefined);
        }

        static fn putAssumeCapacityContext(map_self: *Self, key: K, value: V, ctx: Context) {
            const result = map_self.getOrPutAssumeCapacityContext(key, ctx);
            if (!result.found_existing) unsafe { result.value_ptr.* = value; }
        }

        static fn getOrPutAssumeCapacity(map_self: *Self, key: K): Self.GetOrPutResult {
            return map_self.getOrPutAssumeCapacityContext(key, undefined);
        }

        static fn getOrPutAssumeCapacityContext(map_self: *Self, key: K, ctx: Context): Self.GetOrPutResult {
            let it: Self.Entry = Self.Entry{ hash: ctx.hash(key), key: key, value: undefined };
            let i = it.hash >> map_self.shift;

            assert(it.hash != Self.empty_hash);

            let inserted_at: usize? = null;
            while (true) : (i += 1) {
                unsafe {
                    const entry = map_self.entries[i];
                    if (entry.hash >= it.hash) {
                        if (ctx.eql(entry.key, key)) {
                            return Self.GetOrPutResult{ found_existing: true, value_ptr: &map_self.entries[i].value };
                        }
                        map_self.entries[i] = it;
                        if (entry.isEmpty()) {
                            map_self.len += 1;
                            return Self.GetOrPutResult{ found_existing: false, value_ptr: &map_self.entries[inserted_at ?? i].value };
                        }
                        if (inserted_at == null) {
                            inserted_at = i;
                        }
                        it = entry;
                    }
                }
            }
        }

        static fn get(map_self: *const Self, key: K): V? {
            return map_self.getContext(key, undefined);
        }

        static fn getContext(map_self: *const Self, key: K, ctx: Context): V? {
            const hash = ctx.hash(key);
            assert(hash != Self.empty_hash);

            unsafe {
                for (entry of map_self.entries[hash >> map_self.shift ..]) {
                    if (entry.hash >= hash) {
                        if (!ctx.eql(entry.key, key)) {
                            return null;
                        }
                        return entry.value;
                    }
                }
            }
        }

        static fn has(map_self: *const Self, key: K): bool {
            return map_self.hasContext(key, undefined);
        }

        static fn hasWithHash(map_self: *const Self, key_hash: u64): bool {
            assert(key_hash != Self.empty_hash);

            unsafe {
                for (entry of map_self.entries[key_hash >> map_self.shift ..]) {
                    if (entry.hash >= key_hash) {
                        return entry.hash == key_hash;
                    }
                }
            }

            return false;
        }

        static fn hasContext(map_self: *const Self, key: K, ctx: Context): bool {
            const hash = ctx.hash(key);
            assert(hash != Self.empty_hash);

            unsafe {
                for (entry of map_self.entries[hash >> map_self.shift ..]) {
                    if (entry.hash >= hash) {
                        if (!ctx.eql(entry.key, key)) {
                            return false;
                        }
                        return true;
                    }
                }
            }
            unreachable;
        }

        static fn delete(map_self: *Self, key: K): V? {
            return map_self.deleteContext(key, undefined);
        }

        static fn deleteContext(map_self: *Self, key: K, ctx: Context): V? {
            const hash = ctx.hash(key);
            assert(hash != Self.empty_hash);

            let i = hash >> map_self.shift;
            unsafe {
                while (true) : (i += 1) {
                    const entry = map_self.entries[i];
                    if (entry.hash >= hash) {
                        if (!ctx.eql(entry.key, key)) {
                            return null;
                        }
                        break;
                    }
                }

                const value = map_self.entries[i].value;

                while (true) : (i += 1) {
                    const j = map_self.entries[i + 1].hash >> map_self.shift;
                    if (i < j || map_self.entries[i + 1].isEmpty()) {
                        break;
                    }
                    map_self.entries[i] = map_self.entries[i + 1];
                }
                map_self.entries[i] = Self.Entry{};
                map_self.len -= 1;

                return value;
            }
        }
    };
}

pub fn SortedHashMap(comptime V: type, comptime max_load_percentage: comptime_int): type {
    return struct {
        const empty_hash: [32]u8 = @splat(0xFF);
        pub const Entry = SortedEntry(V, empty_hash);
        pub const GetOrPutResult = GetOrPutResultOf(V);
        pub const cmp = sortedCmp;

        const Self = @This();

        entries: [*]Entry!;
        len: usize = 0;
        shift: u6;

        pub static fn init(gpa: mem.Allocator): Self! {
            return Self.initCapacity(gpa, 16);
        }

        pub static fn initCapacity(gpa: mem.Allocator, capacity: u64): Self! {
            assert(math.isPowerOfTwo(capacity));

            const shift = 63 - math.log2_int(u64, capacity) + 1;
            const overflow = capacity / 10 + (63 - @as(u64, shift) + 1) << 1;

            const entries = gpa.alloc(Entry, usize(capacity + overflow))?;
            @memset(entries, Entry{});

            return Self{
                entries: entries.ptr,
                shift: shift,
            };
        }

        pub fn deinit(*_, gpa: mem.Allocator) {
            gpa.free(_.slice());
        }

        /// In release-fast mode, LLVM will optimize this routine to utilize 109 cycles. This routine scatters
        /// hash values across a table into buckets which are lexicographically ordered from one another in
        /// ascending order.
        static fn idx(a: [32]u8, shift: u6): usize {
            return usize(mem.readIntBig(u64, a[0..8]) >> shift);
        }

        pub fn clearRetainingCapacity(*_) {
            @memset(_.slice(), Entry{});
            _.len = 0;
        }

        pub fn slice(*_): []Entry {
            const capacity = @as(u64, 1) << (63 - _.shift + 1);
            const overflow = capacity / 10 + (63 - @as(usize, _.shift) + 1) << 1;
            unsafe {
                return _.entries[0..usize(capacity + overflow)];
            }
        }

        pub fn ensureUnusedCapacity(*_, gpa: mem.Allocator, count: usize): ! {
            _.ensureTotalCapacity(gpa, _.len + count)?;
        }

        pub fn ensureTotalCapacity(*_, gpa: mem.Allocator, count: usize): ! {
            while (true) {
                const capacity = @as(u64, 1) << (63 - _.shift + 1);
                if (count <= capacity * max_load_percentage / 100) {
                    break;
                }
                _.grow(gpa)?;
            }
        }

        fn grow(*_, gpa: mem.Allocator): ! {
            const capacity = @as(u64, 1) << (63 - _.shift + 1);
            const overflow = capacity / 10 + (63 - @as(usize, _.shift) + 1) << 1;

            unsafe {
                const end = _.entries + usize(capacity + overflow);

                const map = Self.initCapacity(gpa, usize(capacity * 2))?;
                let src = _.entries;
                let dst = map.entries;

                while (src != end) {
                    const entry = src[0];

                    const i = if (!entry.isEmpty()) idx(entry.hash, map.shift) else 0;
                    const p = map.entries + i;

                    dst = if (@intFromPtr(p) >= @intFromPtr(dst)) p else dst;
                    dst[0] = entry;

                    src += 1;
                    dst += 1;
                }

                _.deinit(gpa);
                _.entries = map.entries;
                _.shift = map.shift;
            }
        }

        pub fn put(*_, gpa: mem.Allocator, key: [32]u8, value: V): ! {
            _.ensureUnusedCapacity(gpa, 1)?;
            _.putAssumeCapacity(key, value);
        }

        pub fn putAssumeCapacity(*_, key: [32]u8, value: V) {
            const result = _.getOrPutAssumeCapacity(key);
            if (!result.found_existing) unsafe { result.value_ptr.* = value; }
        }

        pub fn getOrPut(*_, gpa: mem.Allocator, key: [32]u8): GetOrPutResult! {
            _.ensureUnusedCapacity(gpa, 1)?;
            return _.getOrPutAssumeCapacity(key);
        }

        pub fn getOrPutAssumeCapacity(*_, key: [32]u8): GetOrPutResult {
            assert(_.len < (@as(u64, 1) << (63 - _.shift + 1)));
            assert(cmp(key, empty_hash) != .eq);

            let it: Entry = Entry{ hash: key, value: undefined };
            let i = idx(key, _.shift);

            let inserted_at: usize? = null;
            while (true) : (i += 1) {
                unsafe {
                    const entry = _.entries[i];
                    if (cmp(entry.hash, it.hash).compare(.gte)) {
                        if (cmp(entry.hash, key) == .eq) {
                            return GetOrPutResult{ found_existing: true, value_ptr: &_.entries[i].value };
                        }
                        _.entries[i] = it;
                        if (entry.isEmpty()) {
                            _.len += 1;
                            return GetOrPutResult{ found_existing: false, value_ptr: &_.entries[inserted_at ?? i].value };
                        }
                        if (inserted_at == null) {
                            inserted_at = i;
                        }
                        it = entry;
                    }
                    _.put_probe_count += 1;
                }
            }
        }

        pub fn get(*_, key: [32]u8): V? {
            assert(cmp(key, empty_hash) != .eq);

            unsafe {
                for (entry of _.entries[idx(key, _.shift)..]) {
                    if (cmp(entry.hash, key).compare(.gte)) {
                        if (cmp(entry.hash, key) != .eq) {
                            return null;
                        }
                        return entry.value;
                    }
                }
            }
        }

        pub fn delete(*_, key: [32]u8): V? {
            assert(cmp(key, empty_hash) != .eq);

            let i = idx(key, _.shift);
            unsafe {
                while (true) : (i += 1) {
                    const entry = _.entries[i];
                    if (cmp(entry.hash, key).compare(.gte)) {
                        if (cmp(entry.hash, key) != .eq) {
                            return null;
                        }
                        break;
                    }
                    _.del_probe_count += 1;
                }

                const value = _.entries[i].value;

                while (true) : (i += 1) {
                    const j = idx(_.entries[i + 1].hash, _.shift);
                    if (i < j || _.entries[i + 1].isEmpty()) {
                        break;
                    }
                    _.entries[i] = _.entries[i + 1];
                    _.del_probe_count += 1;
                }
                _.entries[i] = Entry{};
                _.len -= 1;

                return value;
            }
        }
    };
}
