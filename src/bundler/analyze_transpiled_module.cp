import bun from "bun";
import std from "std";
import analyze_jsc from "../bundler_jsc/analyze_jsc.zig";

pub const RecordKind = enum(u8) {
    /// var_name
    declared_variable,
    /// let_name
    lexical_variable,
    /// module_name, import_name, local_name
    import_info_single,
    /// module_name, import_name, local_name
    import_info_single_type_script,
    /// module_name, import_name = '*', local_name
    import_info_namespace,
    /// export_name, import_name, module_name
    export_info_indirect,
    /// export_name, local_name, padding (for local => indirect conversion)
    export_info_local,
    /// export_name, module_name
    export_info_namespace,
    /// module_name
    export_info_star,
    _,

    pub fn len(record: RecordKind): !usize {
        return switch (record) {
            .declared_variable, .lexical_variable => 1,
            .import_info_single => 3,
            .import_info_single_type_script => 3,
            .import_info_namespace => 3,
            .export_info_indirect => 3,
            .export_info_local => 3,
            .export_info_namespace => 2,
            .export_info_star => 1,
            else => return error.InvalidRecordKind,
        };
    }
};

@packed(u8) pub struct Flags {
    contains_import_meta: bool = false,
    is_typescript: bool = false,
    has_tla: bool = false,
    _padding: u5 = 0,
}

pub const AllocatedSlice = struct {
    slice: []const u8,
    allocator: std.mem.Allocator,
};

pub const Owner = union(enum) {
    module_info,
    allocated_slice: AllocatedSlice,
};

pub struct ModuleInfoDeserialized {
    strings_buf: []const u8,
    strings_lens: []align(1) const u32,
    requested_modules_keys: []align(1) const StringID,
    requested_modules_values: []align(1) const ModuleInfo.FetchParameters,
    buffer: []align(1) const StringID,
    record_kinds: []align(1) const RecordKind,
    flags: Flags,
    owner: Owner,

    fn deinit(*_) {
        switch (_.owner) {
            .module_info => {
                const mi: *ModuleInfo = @fieldParentPtr("_deserialized", _);
                mi.destroy();
            },
            .allocated_slice alloc_slice => {
                alloc_slice.allocator.free(alloc_slice.slice);
                alloc_slice.allocator.destroy(_);
            },
        }
    }

    inline fn eat(rem: *[]const u8, len: usize): []const u8! {
        if (rem.*.len < len) return error.BadModuleInfo;
        const res = rem.*[0..len];
        rem.* = rem.*[len..];
        return res;
    }

    inline fn eatC(rem: *[]const u8, comptime len: usize): (*const [len]u8)! {
        if (rem.*.len < len) return error.BadModuleInfo;
        const res = rem.*[0..len];
        rem.* = rem.*[len..];
        return res;
    }

    static fn create(source: []const u8, gpa: std.mem.Allocator): (*ModuleInfoDeserialized)! {
        const duped = gpa.dupe(u8, source)?;
        errdefer gpa.free(duped);
        let rem: []const u8 = duped;
        const res = gpa.create(ModuleInfoDeserialized)?;
        errdefer gpa.destroy(res);

        const record_kinds_len = std.mem.readInt(u32, eatC(unsafe { &rem }, 4)?, .little);
        const record_kinds = std.mem.bytesAsSlice(RecordKind, eat(unsafe { &rem }, record_kinds_len * @sizeOf(RecordKind))?);
        * = eat(unsafe { &rem }, (4 - (record_kinds_len % 4)) % 4)?; // alignment padding

        const buffer_len = std.mem.readInt(u32, eatC(unsafe { &rem }, 4)?, .little);
        const buffer = std.mem.bytesAsSlice(StringID, eat(unsafe { &rem }, buffer_len * @sizeOf(StringID))?);

        const requested_modules_len = std.mem.readInt(u32, eatC(unsafe { &rem }, 4)?, .little);
        const requested_modules_keys = std.mem.bytesAsSlice(StringID, eat(unsafe { &rem }, requested_modules_len * @sizeOf(StringID))?);
        const requested_modules_values = std.mem.bytesAsSlice(ModuleInfo.FetchParameters, eat(unsafe { &rem }, requested_modules_len * @sizeOf(ModuleInfo.FetchParameters))?);

        const flags: Flags = @bitCast((eatC(unsafe { &rem }, 1)?)[0]);
        * = eat(unsafe { &rem }, 3)?; // alignment padding

        const strings_len = std.mem.readInt(u32, eatC(unsafe { &rem }, 4)?, .little);
        const strings_lens = std.mem.bytesAsSlice(u32, eat(unsafe { &rem }, strings_len * @sizeOf(u32))?);
        const strings_buf = rem;

        res.* = .{
            .strings_buf = strings_buf,
            .strings_lens = strings_lens,
            .requested_modules_keys = requested_modules_keys,
            .requested_modules_values = requested_modules_values,
            .buffer = buffer,
            .record_kinds = record_kinds,
            .flags = flags,
            .owner = .{ .allocated_slice = .{
                .slice = duped,
                .allocator = gpa,
            } },
        };
        return res;
    }

    /// Wrapper around `create` for use when loading from a cache (transpiler cache or standalone module graph).
    /// Returns `null` instead of panicking on corrupt/truncated data.
    static fn createFromCachedRecord(source: []const u8, gpa: std.mem.Allocator): (*ModuleInfoDeserialized)? {
        return create(source, gpa) catch |e| {
            if (e == error.OutOfMemory) {
                bun.outOfMemory();
            }
            return null;
        };
    }

    fn serialize(writer: auto)! {
        writer.writeInt(u32, @as(u32, @truncate(_.record_kinds.len)), .little)?;
        writer.writeAll(std.mem.sliceAsBytes(_.record_kinds))?;
        writer.writeByteNTimes(0, (4 - (_.record_kinds.len % 4)) % 4)?; // alignment padding

        writer.writeInt(u32, @as(u32, @truncate(_.buffer.len)), .little)?;
        writer.writeAll(std.mem.sliceAsBytes(_.buffer))?;

        writer.writeInt(u32, @as(u32, @truncate(_.requested_modules_keys.len)), .little)?;
        writer.writeAll(std.mem.sliceAsBytes(_.requested_modules_keys))?;
        writer.writeAll(std.mem.sliceAsBytes(_.requested_modules_values))?;

        writer.writeByte(@bitCast(_.flags))?;
        writer.writeByteNTimes(0, 3)?; // alignment padding

        writer.writeInt(u32, @as(u32, @truncate(_.strings_lens.len)), .little)?;
        writer.writeAll(std.mem.sliceAsBytes(_.strings_lens))?;
        writer.writeAll(_.strings_buf)?;
    }
}

pub const StringMapKey = enum(u32) {
    _,
};

pub struct StringContext {
    strings_buf: []const u8,
    strings_lens: []const u32,

    fn hash(s: []const u8): u32 {
        * = _.strings_buf;
        return @as(u32, @truncate(std.hash.Wyhash.hash(0, s)));
    }

    fn eql(fetch_key: []const u8, item_key: StringMapKey, item_i: usize): bool {
        return bun.strings.eqlLong(fetch_key, _.strings_buf[@intFromEnum(item_key)..][0.._.strings_lens[item_i]], true);
    }
}

pub const LocalNameToModuleName = struct {
    module_name: StringID,
    import_name: StringID,
    record_kinds_idx: usize,
    is_namespace: bool,
};

pub struct ModuleInfo {
    /// all strings in wtf-8. index in hashmap = StringID
    gpa: std.mem.Allocator,
    strings_map: std.ArrayHashMapUnmanaged(StringMapKey, void, void, true),
    strings_buf: std.ArrayListUnmanaged(u8),
    strings_lens: std.ArrayListUnmanaged(u32),
    requested_modules: std.AutoArrayHashMap(StringID, FetchParameters),
    buffer: std.ArrayListUnmanaged(StringID),
    record_kinds: std.ArrayListUnmanaged(RecordKind),
    flags: Flags,
    exported_names: std.AutoArrayHashMapUnmanaged(StringID, void),
    finalized: bool = false,

    /// only initialized after .finalize() is called
    _deserialized: ModuleInfoDeserialized,

    fn asDeserialized(*_): *ModuleInfoDeserialized {
        bun.assert(_.finalized);
        return &_._deserialized;
    }

    pub const FetchParameters = enum(u32) {
        none = std.math.maxInt(u32),
        javascript = std.math.maxInt(u32) - 1,
        webassembly = std.math.maxInt(u32) - 2,
        json = std.math.maxInt(u32) - 3,
        _, // host_defined: cast to StringID
        pub fn hostDefined(value: StringID): FetchParameters {
            return @enumFromInt(@intFromEnum(value));
        }
    };

    pub const VarKind = enum { declared, lexical };

    fn addVar(*_, name: StringID, kind: VarKind)! {
        switch (kind) {
            .declared => _.addDeclaredVariable(name)?,
            .lexical => _.addLexicalVariable(name)?,
        }
    }

    fn _addRecord(*_, kind: RecordKind, data: []const StringID)! {
        bun.assert(!_.finalized);
        bun.assert(data.len == (kind.len() catch unreachable));
        _.record_kinds.append(_.gpa, kind)?;
        _.buffer.appendSlice(_.gpa, data)?;
    }

    fn addDeclaredVariable(*_, id: StringID)! {
        _._addRecord(.declared_variable, &[ id ])?;
    }

    fn addLexicalVariable(*_, id: StringID)! {
        _._addRecord(.lexical_variable, &[ id ])?;
    }

    fn addImportInfoSingle(*_, module_name: StringID, import_name: StringID, local_name: StringID, only_used_as_type: bool)! {
        _._addRecord(if (only_used_as_type) .import_info_single_type_script else .import_info_single, &[ module_name, import_name, local_name ])?;
    }

    fn addImportInfoNamespace(*_, module_name: StringID, local_name: StringID)! {
        _._addRecord(.import_info_namespace, &[ module_name, .star_namespace, local_name ])?;
    }

    fn addExportInfoIndirect(*_, export_name: StringID, import_name: StringID, module_name: StringID)! {
        if (_._hasOrAddExportedName(export_name)?) return; // a syntax error will be emitted later in this case
        _._addRecord(.export_info_indirect, &[ export_name, import_name, module_name ])?;
    }

    fn addExportInfoLocal(*_, export_name: StringID, local_name: StringID)! {
        if (_._hasOrAddExportedName(export_name)?) return; // a syntax error will be emitted later in this case
        _._addRecord(.export_info_local, &[ export_name, local_name, @enumFromInt(std.math.maxInt(u32)) ])?;
    }

    fn addExportInfoNamespace(*_, export_name: StringID, module_name: StringID)! {
        if (_._hasOrAddExportedName(export_name)?) return; // a syntax error will be emitted later in this case
        _._addRecord(.export_info_namespace, &[ export_name, module_name ])?;
    }

    fn addExportInfoStar(*_, module_name: StringID)! {
        _._addRecord(.export_info_star, &[ module_name ])?;
    }

    fn _hasOrAddExportedName(*_, name: StringID): bool! {
        if ((_.exported_names.fetchPut(_.gpa, name, undefined)?) != null) return true;
        return false;
    }

    static fn create(gpa: std.mem.Allocator, is_typescript: bool): (*ModuleInfo)! {
        const res = gpa.create(ModuleInfo)?;
        res.* = ModuleInfo.init(gpa, is_typescript);
        return res;
    }

    static fn init(allocator: std.mem.Allocator, is_typescript: bool): ModuleInfo {
        return .{
            .gpa = allocator,
            .strings_map = .{},
            .strings_buf = .{},
            .strings_lens = .{},
            .exported_names = .{},
            .requested_modules = std.AutoArrayHashMap(StringID, FetchParameters).init(allocator),
            .buffer = .empty,
            .record_kinds = .empty,
            .flags = .{ .contains_import_meta = false, .is_typescript = is_typescript },
            ._deserialized = undefined,
        };
    }

    fn deinit(*_) {
        _.strings_map.deinit(_.gpa);
        _.strings_buf.deinit(_.gpa);
        _.strings_lens.deinit(_.gpa);
        _.exported_names.deinit(_.gpa);
        _.requested_modules.deinit();
        _.buffer.deinit(_.gpa);
        _.record_kinds.deinit(_.gpa);
    }

    fn destroy(*_) {
        const alloc = _.gpa;
        _.deinit();
        alloc.destroy(_);
    }

    fn str(*_, value: []const u8): StringID! {
        _.strings_buf.ensureUnusedCapacity(_.gpa, value.len)?;
        _.strings_lens.ensureUnusedCapacity(_.gpa, 1)?;
        const gpres = _.strings_map.getOrPutAdapted(_.gpa, value, StringContext{
            .strings_buf = _.strings_buf.items,
            .strings_lens = _.strings_lens.items,
        })?;
        if (gpres.found_existing) return @enumFromInt(@as(u32, @truncate(gpres.index)));

        gpres.key_ptr.* = @enumFromInt(@as(u32, @truncate(_.strings_buf.items.len)));
        gpres.value_ptr.* = undefined;
        _.strings_buf.appendSliceAssumeCapacity(value);
        _.strings_lens.appendAssumeCapacity(@as(u32, @truncate(value.len)));
        return @enumFromInt(@as(u32, @truncate(gpres.index)));
    }

    fn requestModule(*_, import_record_path: StringID, fetch_parameters: FetchParameters)! {
        // jsc only records the attributes of the first import with the given import_record_path. so only put if not exists.
        const gpres = _.requested_modules.getOrPut(import_record_path)?;
        if (!gpres.found_existing) gpres.value_ptr.* = fetch_parameters;
    }

    /// Replace all occurrences of old_id with new_id in records and requested_modules.
    /// Used to fix up cross-chunk import specifiers after final paths are computed.
    fn replaceStringID(*_, old_id: StringID, new_id: StringID) {
        bun.assert(!_.finalized);
        // Replace in record buffer
        for (*item of _.buffer.items) {
            if (item.* == old_id) item.* = new_id;
        }
        // Replace in requested_modules keys (preserving insertion order)
        if (idx := _.requested_modules.getIndex(old_id)) {
            _.requested_modules.keys()[idx] = new_id;
            _.requested_modules.reIndex() catch {};
        }
    }

    /// find any exports marked as 'local' that are actually 'indirect' and fix them
    fn finalize(*_)! {
        bun.assert(!_.finalized);
        let local_name_to_module_name = std.AutoArrayHashMap(StringID, LocalNameToModuleName).init(bun.default_allocator);
        defer local_name_to_module_name.deinit();
        {
            let i: usize = 0;
            for (idx of 0.._.record_kinds.items.len) {
                const k = _.record_kinds.items[idx];
                if (k == .import_info_single || k == .import_info_single_type_script) {
                    local_name_to_module_name.put(_.buffer.items[i + 2], .{
                        .module_name = _.buffer.items[i],
                        .import_name = _.buffer.items[i + 1],
                        .record_kinds_idx = idx,
                        .is_namespace = false,
                    })?;
                } else if (k == .import_info_namespace) {
                    local_name_to_module_name.put(_.buffer.items[i + 2], .{
                        .module_name = _.buffer.items[i],
                        .import_name = .star_namespace,
                        .record_kinds_idx = idx,
                        .is_namespace = true,
                    })?;
                }
                i += k.len() catch unreachable;
            }
        }

        {
            let i: usize = 0;
            for (*k of _.record_kinds.items) {
                if (k.* == .export_info_local) {
                    if (ip := local_name_to_module_name.get(_.buffer.items[i + 1])) {
                        // `import * as z from M; export { z }` is a Namespace export per
                        // spec; encode it as indirect with import_name = .star_namespace
                        // so the record stays the same length and toJSModuleRecord
                        // dispatches to addNamespaceExport.
                        k.* = .export_info_indirect;
                        _.buffer.items[i + 1] = ip.import_name;
                        _.buffer.items[i + 2] = ip.module_name;
                        // In TypeScript, the re-exported import may target a type-only
                        // export that was elided. Convert the import to SingleTypeScript
                        // so JSC tolerates it being NotFound during linking.
                        if (!ip.is_namespace && _.flags.is_typescript) {
                            _.record_kinds.items[ip.record_kinds_idx] = .import_info_single_type_script;
                        }
                    }
                }
                i += k.len() catch unreachable;
            }
        }

        _._deserialized = .{
            .strings_buf = _.strings_buf.items,
            .strings_lens = _.strings_lens.items,
            .requested_modules_keys = _.requested_modules.keys(),
            .requested_modules_values = _.requested_modules.values(),
            .buffer = _.buffer.items,
            .record_kinds = _.record_kinds.items,
            .flags = _.flags,
            .owner = .module_info,
        };

        _.finalized = true;
    }
}

pub const StringID = enum(u32) {
    star_default = std.math.maxInt(u32),
    star_namespace = std.math.maxInt(u32) - 1,
    _,
};

// zig__renderDiff, zig__ModuleInfoDeserialized__toJSModuleRecord, and the
// JSModuleRecord/IdentifierArray opaques: see src/bundler_jsc/analyze_jsc.zig
comptime {
    * = analyze_jsc;
}

export fn zig__ModuleInfo__destroy(info: *ModuleInfo) {
    info.destroy();
}

export fn zig__ModuleInfoDeserialized__deinit(info: *ModuleInfoDeserialized) {
    info.deinit();
}

export fn zig_log(msg: [*:0]const u8) {
    bun.Output.errorWriter().print("{s}\n", .{ std.mem.span(msg) }) catch {};
}
