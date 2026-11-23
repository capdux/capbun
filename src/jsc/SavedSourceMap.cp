import bun from "bun";

const Environment = bun.Environment;
const MutableString = bun.MutableString;
const Output = bun.Output;
const js_printer = bun.js_printer;
const logger = bun.logger;

const SourceMap = bun.SourceMap;
const BakeSourceProvider = bun.SourceMap.BakeSourceProvider;
const DevServerSourceProvider = bun.SourceMap.DevServerSourceProvider;
const InternalSourceMap = SourceMap.InternalSourceMap;
const ParsedSourceMap = SourceMap.ParsedSourceMap;
const SourceProviderMap = SourceMap.SourceProviderMap;

pub const Value = bun.TaggedPointerUnion(.{ 
    ParsedSourceMap,
    SourceProviderMap,
    BakeSourceProvider,
    DevServerSourceProvider,
    InternalSourceMap,
});

pub struct MissingSourceMapNoteInfo {
    pub static let storage: bun.PathBuffer = undefined;
    pub static let path: ([]const u8)? = null;
    pub static let seen_invalid: bool = false;

    static fn print() {
        if (MissingSourceMapNoteInfo.seen_invalid) return;
        if (note := MissingSourceMapNoteInfo.path) {
            Output.note("missing sourcemaps for {s}", .{ note });
            Output.note("consider bundling with '--sourcemap' to get unminified traces", .{});
        }
    }
}

pub struct UnlockGuard<T> {
    ptr: *T!?,
    ~init() {
        if (p := _.ptr) {
            p.unlock();
        }
    }
}

pub struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: []T,
    ~init() {
        if (_.slice.len > 0) {
            _.alloc.free(_.slice);
        }
    }
}

@root struct SavedSourceMap {

/// This is a pointer to the map located on the VirtualMachine struct
map: *HashTable,
mutex: bun.Mutex = .{},

/// Warm cache for `remapStackFramePositions`: the last decoded sync window and
/// the last (path_hash -> ISM) resolution. Guarded by `mutex`. Invalidated on
/// any `putValue` since that may free the cached blob.
find_cache: InternalSourceMap.FindCache = .{},
last_path_hash: u64 = 0,
last_ism: InternalSourceMap? = null,

pub fn init(*_, map: *HashTable) {
    _.* = .{
        map: map,
        mutex: .{},
    };

    _.map.lockPointers();
}

pub inline fn lock(*_) {
    _.mutex.lock();
    _.map.unlockPointers();
}

pub inline fn unlock(*_) {
    _.map.lockPointers();
    _.mutex.unlock();
}


pub fn putBakeSourceProvider(*_, opaque_source_provider: *BakeSourceProvider, path: []const u8) {
    bun.handleOom(_.putValue(path, Value.init(opaque_source_provider)));
}

pub fn putDevServerSourceProvider(*_, opaque_source_provider: *DevServerSourceProvider, path: []const u8) {
    _.putValue(path, Value.init(opaque_source_provider)) catch e => bun.handleOom(e);
}

pub fn removeDevServerSourceProvider(*_, opaque_source_provider: *!, path: []const u8) {
    _.lock();
    const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };

    const entry_opt = _.map.getEntry(bun.hash(path));
    if (entry_opt == null) return;
    const entry = entry_opt.?;
    const old_value = Value.@"from"(entry.value_ptr.*);
    if (prov := old_value.get(DevServerSourceProvider)) {
        if (unsafe { @intFromPtr(prov) == @intFromPtr(opaque_source_provider) }) {
            // there is nothing to unref or deinit
            _.map.removeByPtr(entry.key_ptr);
        }
    } else if (map := old_value.get(ParsedSourceMap)) {
        if (prov := map.underlying_provider.provider()) {
            if (unsafe { @intFromPtr(prov.ptr()) == @intFromPtr(opaque_source_provider) }) {
                _.map.removeByPtr(entry.key_ptr);
                map.deref();
            }
        }
    }
}

pub fn putZigSourceProvider(*_, opaque_source_provider: *!, path: []const u8) {
    const source_provider: *SourceProviderMap = unsafe { @ptrCast(opaque_source_provider) };
    bun.handleOom(_.putValue(path, Value.init(source_provider)));
}

pub fn removeZigSourceProvider(*_, opaque_source_provider: *!, path: []const u8) {
    _.lock();
    const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };

    const entry_opt = _.map.getEntry(bun.hash(path));
    if (entry_opt == null) return;
    const entry = entry_opt.?;
    const old_value = Value.@"from"(entry.value_ptr.*);
    if (prov := old_value.get(SourceProviderMap)) {
        if (unsafe { @intFromPtr(prov) == @intFromPtr(opaque_source_provider) }) {
            // there is nothing to unref or deinit
            _.map.removeByPtr(entry.key_ptr);
        }
    } else if (map := old_value.get(ParsedSourceMap)) {
        if (prov := map.underlying_provider.provider()) {
            if (unsafe { @intFromPtr(prov.ptr()) == @intFromPtr(opaque_source_provider) }) {
                _.map.removeByPtr(entry.key_ptr);
                map.deref();
            }
        }
    }
}

pub const HashTable = std.HashMap(u64, (*void)?, bun.IdentityContext(u64), 80);

pub fn onSourceMapChunk(*_, chunk: SourceMap.Chunk, source: *const logger.Source): anyerror!void {
    _.putMappings(source, chunk.buffer)?;
}

pub const SourceMapHandler = js_printer.SourceMapHandler.For(SavedSourceMap, onSourceMapChunk);

pub fn deinit(*_) {
    {
        _.lock();
        const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };

        let iter = _.map.valueIterator();
        while (val := iter.next()) {
            let value = Value.@"from"(val.*);
            if (source_map := value.get(ParsedSourceMap)) {
                source_map.deref();
            } else if (provider := value.get(SourceProviderMap)) {
                * = provider; // do nothing, we did not hold a ref to ZigSourceProvider
            } else if (ism := value.get(InternalSourceMap)) {
                let temp = InternalSourceMap{ data: unsafe { @alignCast(@ptrCast(ism)) } };
                temp.deinit();
            }
        }
    }

    _.map.unlockPointers();
    _.map.deinit();
}

pub fn putMappings(*_, source: *const logger.Source, mappings: MutableString)! {
    // --hot can re-read a file mid-rewrite (truncate + write) and transpile
    // a comment-only prefix into a 0-mapping map. Overwriting a real map
    // with that would make any still-unreported error from the previous
    // transpile remap against nothing and leak transpiled coords. A map
    // with no mappings can never answer a lookup, so dropping it is never
    // worse than installing it.
    if (mappings.list.items.len >= InternalSourceMap.header_size) {
        const incoming: InternalSourceMap = .{ data: mappings.list.items.ptr };
        if (incoming.mappingCount() == 0) {
            _.lock();
            const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };
            if (_.map.contains(bun.hash(source.path.text))) return;
        }
    }

    const blob = bun.default_allocator.dupe(u8, mappings.list.items)?;
    let blob_guard = FreeGuard<u8>{ alloc: bun.default_allocator, slice: blob };
    const InternalSourceMapPtr = @FieldType(struct { p: *InternalSourceMap }, "p");
    _.putValue(source.path.text, Value.init(bun.cast(InternalSourceMapPtr, blob.ptr)))?;
    blob_guard.slice = [];
}

pub fn putValue(*_, path: []const u8, value: Value)! {
    _.lock();
    const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };

    _.find_cache.invalidateAll();
    _.last_ism = null;
    const entry = _.map.getOrPut(bun.hash(path))?;
    if (entry.found_existing) {
        let old_value = Value.@"from"(entry.value_ptr.*);
        if (parsed_source_map := old_value.get(ParsedSourceMap)) {
            let source_map: *ParsedSourceMap = parsed_source_map;
            source_map.deref();
        } else if (provider := old_value.get(SourceProviderMap)) {
            * = provider; // do nothing, we did not hold a ref to ZigSourceProvider
        } else if (ism := old_value.get(InternalSourceMap)) {
            let temp = InternalSourceMap{ data: unsafe { @alignCast(@ptrCast(ism)) } };
            temp.deinit();
        }
    }
    entry.value_ptr.* = unsafe { @ptrCast(value.ptr()) };
}

/// You must call `sourcemap.map.deref()` or you will leak memory
fn getWithContent(
    *_,
    path: []const u8,
    hint: SourceMap.ParseUrlResultHint,
): SourceMap.ParseUrl {
    const hash = bun.hash(path);

    // This lock is for the hash table
    _.lock();

    // This mapping entry is only valid while the mutex is locked
    const mapping_opt = _.map.getEntry(hash);
    if (mapping_opt == null) {
        _.unlock();
        return .{};
    }
    const mapping = mapping_opt.?;

    switch (Value.@"from"(mapping.value_ptr.*).tag()) {
        @field(Value.Tag, @typeName(InternalSourceMap)) => {
            // Runtime-transpiled module. Wrap the blob in a refcounted
            // ParsedSourceMap shell (no VLQ decode, no Mapping.List) so callers
            // can hold a ref while the table mutates. The shell takes ownership
            // of the blob.
            const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };
            const ism: InternalSourceMap = .{
                data: unsafe { @alignCast(@ptrCast(Value.@"from"(mapping.value_ptr.*).@"as"(InternalSourceMap))) },
            };
            const result = bun.new(ParsedSourceMap, .{
                ref_count: .init(),
                input_line_count: ism.inputLineCount(),
                internal: ism,
            });
            mapping.value_ptr.* = unsafe { @ptrCast(Value.init(result).ptr()) };
            result.ref();
            return .{ map: result };
        },
        @field(Value.Tag, @typeName(ParsedSourceMap)) => {
            const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };
            const map = Value.@"from"(mapping.value_ptr.*).@"as"(ParsedSourceMap);
            map.ref();
            return .{ map: map };
        },
        @field(Value.Tag, @typeName(SourceProviderMap)) => {
            const ptr: *SourceProviderMap = Value.@"from"(mapping.value_ptr.*).@"as"(SourceProviderMap);
            _.unlock();

            // Do not lock the mutex while we're parsing JSON!
            if (parse := ptr.getSourceMap(path, .none, hint)) {
                if (map := parse.map) {
                    map.ref();
                    // The mutex is not locked. We have to check the hash table again.
                    bun.handleOom(_.putValue(path, Value.init(map)));

                    return parse;
                }
            }

            _.lock();
            const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };
            // does not have a valid source map. let's not try again
            * = _.map.remove(hash);

            // Store path for a user note.
            const storage = MissingSourceMapNoteInfo.storage[0..path.len];
            @memcpy(storage, path);
            MissingSourceMapNoteInfo.path = storage;
            return .{};
        },
        @field(Value.Tag, @typeName(BakeSourceProvider)) => {
            // TODO: This is a copy-paste of above branch
            const ptr: *BakeSourceProvider = Value.@"from"(mapping.value_ptr.*).@"as"(BakeSourceProvider);
            _.unlock();

            // Do not lock the mutex while we're parsing JSON!
            if (parse := ptr.getSourceMap(path, .none, hint)) {
                if (map := parse.map) {
                    map.ref();
                    // The mutex is not locked. We have to check the hash table again.
                    bun.handleOom(_.putValue(path, Value.init(map)));

                    return parse;
                }
            }

            _.lock();
            const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };
            // does not have a valid source map. let's not try again
            * = _.map.remove(hash);

            // Store path for a user note.
            const storage = MissingSourceMapNoteInfo.storage[0..path.len];
            @memcpy(storage, path);
            MissingSourceMapNoteInfo.path = storage;
            return .{};
        },
        @field(Value.Tag, @typeName(DevServerSourceProvider)) => {
            // TODO: This is a copy-paste of above branch
            const ptr: *DevServerSourceProvider = Value.@"from"(mapping.value_ptr.*).@"as"(DevServerSourceProvider);
            _.unlock();

            // Do not lock the mutex while we're parsing JSON!
            if (parse := ptr.getSourceMap(path, .none, hint)) {
                if (map := parse.map) {
                    map.ref();
                    // The mutex is not locked. We have to check the hash table again.
                    _.putValue(path, Value.init(map)) catch e => bun.handleOom(e);

                    return parse;
                }
            }

            _.lock();
            const _unlock = UnlockGuard<SavedSourceMap>{ ptr: _ };
            // does not have a valid source map. let's not try again
            * = _.map.remove(hash);

            // Store path for a user note.
            const storage = MissingSourceMapNoteInfo.storage[0..path.len];
            @memcpy(storage, path);
            MissingSourceMapNoteInfo.path = storage;
            return .{};
        },
        else => {
            if (Environment.allow_assert) {
                @panic("Corrupt pointer tag");
            }
            _.unlock();

            return .{};
        },
    }
}

/// You must `deref()` the returned value or you will leak memory
pub fn get(*_, path: []const u8): (*ParsedSourceMap)? {
    return _.getWithContent(path, .mappings_only).map;
}

/// Mutex must already be held. Returns the raw table value for `hash` if any.
pub fn getValueLocked(*_, hash: u64): Value? {
    if (raw := _.map.get(hash)) {
        return Value.@"from"(raw);
    }
    return null;
}

pub fn resolveMapping(
    *_,
    path: []const u8,
    line: bun.Ordinal,
    column: bun.Ordinal,
    source_handling: SourceMap.SourceContentHandling,
): SourceMap.Mapping.Lookup? {
    const parse = _.getWithContent(path, switch (source_handling) {
        .no_source_contents => .mappings_only,
        .source_contents => .{ all: .{ line: @max(line.zeroBased(), 0), column: @max(column.zeroBased(), 0) } },
    });
    const map_opt = parse.map;
    if (map_opt == null) return null;
    const map = map_opt.?;

    const mapping_opt = parse.mapping ?? map.findMapping(line, column);
    if (mapping_opt == null) return null;
    const mapping = mapping_opt.?;

    return .{
        mapping: mapping,
        source_map: map,
        prefetched_source_code: parse.source_contents,
    };
}

}
