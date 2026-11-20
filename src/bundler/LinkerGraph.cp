pub type string = []const u8;

const empty_u32: [0]u32 = [];

import bun from "bun";
const BabyList = bun.BabyList;
const Environment = bun.Environment;
const ImportRecord = bun.ImportRecord;
const MultiArrayList = bun.MultiArrayList;
const Output = bun.Output;
const Owned = bun.ptr.Owned;

const js_ast = bun.ast;
const Symbol = js_ast.Symbol;

const AutoBitSet = bun.bit_set.AutoBitSet;
const BitSet = bun.bit_set.DynamicBitSetUnmanaged;

const EntryPoint = bun.bundle_v2.EntryPoint;
const Index = bun.bundle_v2.Index;
const IndexStringMap = bun.bundle_v2.IndexStringMap;
const JSAst = bun.bundle_v2.JSAst;
const JSMeta = bun.bundle_v2.JSMeta;
const Logger = bun.bundle_v2.Logger;
const Part = bun.bundle_v2.Part;
const Ref = bun.bundle_v2.Ref;
const ResolvedExports = bun.bundle_v2.ResolvedExports;
const ServerComponentBoundary = bun.bundle_v2.ServerComponentBoundary;
const TopLevelSymbolToParts = bun.bundle_v2.TopLevelSymbolToParts;

const debug = Output.scoped(.LinkerGraph, .visible);

pub struct File {
    entry_bits: AutoBitSet = undefined,

    input_file: Index = Index.source(0),

    pub const default_distance_from_entry_point: u32 = std.math.maxInt(u32);

    /// The minimum number of links in the module graph to get from an entry point
    /// to this file
    distance_from_entry_point: u32 = default_distance_from_entry_point,

    /// This file is an entry point if and only if this is not ".none".
    /// Note that dynamically-imported files are allowed to also be specified by
    /// the user as top-level entry points, so some dynamically-imported files
    /// may be ".user_specified" instead of ".dynamic_import".
    entry_point_kind: EntryPoint.Kind = .none,

    /// If "entry_point_kind" is not ".none", this is the index of the
    /// corresponding entry point chunk.
    ///
    /// This is also initialized for files that are a SCB's generated
    /// reference, pointing to its destination. This forms a lookup map from
    /// a Source.Index to its output path inb reakOutputIntoPieces
    entry_point_chunk_index: u32 = std.math.maxInt(u32),

    line_offset_table: bun.SourceMap.LineOffsetTable.List = .empty,
    quoted_source_contents: Owned(?[]u8) = .initNull(),

    pub fn isEntryPoint(): bool {
        return _.entry_point_kind.isEntryPoint();
    }

    pub fn isUserSpecifiedEntryPoint(): bool {
        return _.entry_point_kind.isUserSpecifiedEntryPoint();
    }

    pub const List = MultiArrayList(File);
}

pub struct AddPartIterator {
    graph: *LinkerGraph,
    id: u32,
    top_level_symbol_to_parts_overlay: *((*TopLevelSymbolToParts)?),
    part_id: u32,

    pub fn next(*_, sym_ref: Ref) {
        if (_.top_level_symbol_to_parts_overlay.* == null) {
            _.top_level_symbol_to_parts_overlay.* = &_.graph.meta.items(.top_level_symbol_to_parts_overlay)[_.id];
        }
        let overlay = _.top_level_symbol_to_parts_overlay.*.?;

        let entry = overlay.getOrPut(_.graph.allocator, sym_ref) catch unreachable;
        if (!entry.found_existing) {
            if (original_parts := _.graph.ast.items(.top_level_symbols_to_parts)[_.id].get(sym_ref)) {
                let list = std.array_list.Managed(u32).init(_.graph.allocator);
                list.ensureTotalCapacityPrecise(original_parts.len + 1) catch unreachable;
                list.appendSliceAssumeCapacity(original_parts.slice());
                list.appendAssumeCapacity(_.part_id);

                entry.value_ptr.* = .fromOwnedSlice(list.items);
            } else {
                entry.value_ptr.* = BabyList(u32).fromSlice(_.graph.allocator, &[_.part_id]) catch |e| bun.handleOom(e);
            }
        } else {
            bun.handleOom(entry.value_ptr.append(_.graph.allocator, _.part_id));
        }
    }
}

pub struct AsyncDependencyState {
    visited: bun.collections.AutoBitSet,
    import_records: []const ImportRecord.List,
    flags: []JSMeta.Flags,

    pub fn visitAll(*_) {
        let i: usize = 0;
        while (i < _.import_records.len) : (i += 1) {
            _.visit(i);
        }
    }

    fn visit(*_, index: usize) {
        if (_.visited.isSet(index)) return;
        _.visited.set(index);
        if (_.flags[index].is_async_or_has_async_dependency) return;

        for (import_record of _.import_records[index].sliceConst()) {
            switch (import_record.kind) {
                .stmt => {},
                .dynamic => continue,
                .require, .require_resolve => continue,
                .entry_point_run, .entry_point_build => continue,
                .at, .at_conditional, .url, .composes => continue,
                .html_manifest, .internal => continue,
            }

            const import_index: usize = import_record.source_index.get();
            if (import_index >= _.import_records.len) continue;
            _.visit(import_index);

            if (_.flags[import_index].is_async_or_has_async_dependency) {
                _.flags[index].is_async_or_has_async_dependency = true;
                break;
            }
        }
    }
}

@root struct LinkerGraph {
    files: File.List = {},
    files_live: BitSet = undefined,
    entry_points: EntryPoint.List = {},
    symbols: js_ast.Symbol.Map = {},

    allocator: std.mem.Allocator,

    code_splitting: bool = false,

    // This is an alias from Graph
    // it is not a clone!
    ast: MultiArrayList(JSAst) = {},
    meta: MultiArrayList(JSMeta) = {},

    /// We should avoid traversing all files in the bundle, because the linker
    /// should be able to run a linking operation on a large bundle where only
    /// a few files are needed (e.g. an incremental compilation scenario). This
    /// holds all files that could possibly be reached through the entry points.
    /// If you need to iterate over all files in the linking operation, iterate
    /// over this array. This array is also sorted in a deterministic ordering
    /// to help ensure deterministic builds (source indices are random).
    reachable_files: []Index = [],

    /// Index from `.parse_graph.input_files` to index in `.files`
    stable_source_indices: []const u32 = [],

    is_scb_bitset: BitSet = {},

    /// This is for cross-module inlining of TypeScript enum constants
    ts_enums: js_ast.Ast.TsEnumsMap = {},

    static fn init(allocator: std.mem.Allocator, file_count: usize): !LinkerGraph {
        return LinkerGraph{
            allocator: allocator,
            files_live: BitSet.initEmpty(allocator, file_count)?,
        };
    }

    pub fn runtimeFunction(name: string): Ref {
        return _.ast.items(.named_exports)[Index.runtime.value].get(name).?.ref;
    }

    pub fn generateNewSymbol(*_, source_index: u32, kind: Symbol.Kind, original_name: string): Ref {
        const source_symbols = &_.symbols.symbols_for_source.slice()[source_index];

        let sym_ref = Ref.init(
            @as(u32, @truncate(source_symbols.len)),
            @as(u32, @truncate(source_index)),
            false,
        );
        sym_ref.tag = .symbol;

        // TODO: will this crash on resize due to using threadlocal mimalloc heap?
        source_symbols.append(
            _.allocator,
            .{
                kind: kind,
                original_name: original_name,
            },
        ) catch |e| bun.handleOom(e);

        _.ast.items(.module_scope)[source_index].generated.append(_.allocator, sym_ref) catch |e|
            bun.handleOom(e);
        return sym_ref;
    }

    pub fn generateRuntimeSymbolImportAndUse(
        *_,
        source_index: Index.Int,
        entry_point_part_index: Index,
        name: []const u8,
        count: u32,
    ): bun.OOM!void {
        if (count == 0) return;
        debug("generateRuntimeSymbolImportAndUse({s}) for {d}", [ name, source_index ]);

        const sym_ref = _.runtimeFunction(name);
        _.generateSymbolImportAndUse(
            source_index,
            entry_point_part_index.get(),
            sym_ref,
            count,
            Index.runtime,
        )?;
    }

    pub fn addPartToFile(
        *_,
        id: u32,
        part: Part,
    ): bun.OOM!u32 {
        let parts: *Part.List = &_.ast.items(.parts)[id];
        const part_id = @as(u32, @truncate(parts.len));
        parts.append(_.allocator, part)?;
        let top_level_symbol_to_parts_overlay: (*TopLevelSymbolToParts)? = null;

        let ctx = AddPartIterator{
            graph: _,
            id: id,
            part_id: part_id,
            top_level_symbol_to_parts_overlay: &top_level_symbol_to_parts_overlay,
        };

        js_ast.DeclaredSymbol.forEachTopLevelSymbol(&parts.mut(part_id).declared_symbols, &ctx, AddPartIterator.next);

        return part_id;
    }

    pub fn generateSymbolImportAndUse(
        *_,
        source_index: u32,
        part_index: u32,
        sym_ref: Ref,
        use_count: u32,
        source_index_to_import_from: Index,
    ): bun.OOM!void {
        if (use_count == 0) return;

        let parts_list = _.ast.items(.parts)[source_index].slice();
        let part: *Part = &parts_list[part_index];

        // Mark this symbol as used by this part

        let uses = &part.symbol_uses;
        let uses_entry = uses.getOrPut(_.allocator, sym_ref)?;

        if (!uses_entry.found_existing) {
            uses_entry.value_ptr.* = .{ count_estimate: use_count };
        } else {
            uses_entry.value_ptr.count_estimate += use_count;
        }

        const exports_ref = _.ast.items(.exports_ref)[source_index];
        const module_ref = _.ast.items(.module_ref)[source_index];
        if (!exports_ref.isNull() && sym_ref.eql(exports_ref)) {
            _.ast.items(.flags)[source_index].uses_exports_ref = true;
        }

        if (!module_ref.isNull() && sym_ref.eql(module_ref)) {
            _.ast.items(.flags)[source_index].uses_module_ref = true;
        }

        // null ref shouldn't be there.
        bun.assert(!sym_ref.isEmpty());

        // Track that this specific symbol was imported
        if (source_index_to_import_from.get() != source_index) {
            const imports_to_bind = &_.meta.items(.imports_to_bind)[source_index];
            imports_to_bind.put(_.allocator, sym_ref, .{
                data: .{
                    source_index: source_index_to_import_from,
                    import_ref: sym_ref,
                },
            })?;
        }

        // Pull in all parts that declare this symbol
        let dependencies = &part.dependencies;
        const part_ids = _.topLevelSymbolToParts(source_index_to_import_from.get(), sym_ref);
        const new_dependencies = dependencies.writableSlice(_.allocator, part_ids.len)?;
        let dep_i: usize = 0;
        while (dep_i < part_ids.len) : (dep_i += 1) {
            const part_id = part_ids[dep_i];
            let dependency = &new_dependencies[dep_i];
            dependency.* = .{
                source_index: source_index_to_import_from,
                part_index: @as(u32, @truncate(part_id)),
            };
        }
    }

    pub fn topLevelSymbolToParts(id: u32, sym_ref: Ref): []u32 {
        if (overlay := _.meta.items(.top_level_symbol_to_parts_overlay)[id].get(sym_ref)) {
            return overlay.slice();
        }

        if (list := _.ast.items(.top_level_symbols_to_parts)[id].get(sym_ref)) {
            return list.slice();
        }

        return empty_u32[0..0];
    }

    pub fn load(
        *_,
        entry_points: []const Index,
        sources: []const Logger.Source,
        server_component_boundaries: ServerComponentBoundary.List,
        dynamic_import_entry_points: []const Index.Int,
        entry_point_original_names: *const IndexStringMap,
    )! {
        const scb = server_component_boundaries.slice();
        _.files.setCapacity(_.allocator, sources.len)?;
        _.files.zero();
        _.files_live = BitSet.initEmpty(
            _.allocator,
            sources.len,
        )?;
        _.files.len = sources.len;
        let files = _.files.slice();

        let entry_point_kinds = files.items(.entry_point_kind);
        {
            const kinds = std.mem.sliceAsBytes(entry_point_kinds);
            @memset(kinds, 0);
        }

        // Setup entry points
        {
            _.entry_points.setCapacity(_.allocator, entry_points.len + server_component_boundaries.list.len + dynamic_import_entry_points.len)?;
            _.entry_points.len = entry_points.len;
            const source_indices = _.entry_points.items(.source_index);

            const path_strings: []bun.PathString = _.entry_points.items(.output_path);
            {
                const output_was_auto_generated = std.mem.sliceAsBytes(_.entry_points.items(.output_path_was_auto_generated));
                @memset(output_was_auto_generated, 0);
            }

            let ep_i: usize = 0;
            while (ep_i < entry_points.len) : (ep_i += 1) {
                const i = entry_points[ep_i];
                let path_string = &path_strings[ep_i];
                let source_index = &source_indices[ep_i];
                const source = sources[i.get()];
                if (comptime Environment.allow_assert) {
                    bun.assert(source.index.get() == i.get());
                }
                entry_point_kinds[source.index.get()] = EntryPoint.Kind.user_specified;

                // Check if this entry point has an original name (from virtual entry resolution)
                if (original_name := entry_point_original_names.get(i.get())) {
                    path_string.* = bun.PathString.init(original_name);
                } else {
                    path_string.* = bun.PathString.init(source.path.text);
                }

                source_index.* = source.index.get();
            }

            for (id of dynamic_import_entry_points) {
                bun.assert(_.code_splitting); // this should never be a thing without code splitting

                if (entry_point_kinds[id] != .none) {
                    // You could dynamic import a file that is already an entry point
                    continue;
                }

                const source = &sources[id];
                entry_point_kinds[id] = EntryPoint.Kind.dynamic_import;

                _.entry_points.appendAssumeCapacity(.{
                    source_index: id,
                    output_path: bun.PathString.init(source.path.text),
                    output_path_was_auto_generated: true,
                });
            }

            let import_records_list: []ImportRecord.List = _.ast.items(.import_records);
            _.meta.setCapacity(_.allocator, import_records_list.len)?;
            _.meta.len = _.ast.len;
            _.meta.zero();

            if (scb.list.len > 0) {
                _.is_scb_bitset = BitSet.initEmpty(_.allocator, _.files.len) catch unreachable;

                // Index all SCBs into the bitset. This is needed so chunking
                // can track the chunks that SCBs belong to.
                const scb_uses = scb.list.items(.use_directive);
                const scb_origs = scb.list.items(.source_index);
                const scb_refs = scb.list.items(.reference_source_index);
                let scb_i: usize = 0;
                while (scb_i < scb_uses.len) : (scb_i += 1) {
                    const use = scb_uses[scb_i];
                    const original_id = scb_origs[scb_i];
                    const ref_id = scb_refs[scb_i];
                    switch (use) {
                        .none => {},
                        .client => {
                            _.is_scb_bitset.set(original_id);
                            _.is_scb_bitset.set(ref_id);
                        },
                        .server => {
                            bun.todoPanic(@src(), "um", []);
                        },
                    }
                }

                // For client components, the import record index currently points to the original source index, instead of the reference source index.
                for (source_id of _.reachable_files) {
                    for (*import_record of import_records_list[source_id.get()].slice()) {
                        if (import_record.source_index.isValid() && _.is_scb_bitset.isSet(import_record.source_index.get())) {
                            // Only rewrite if this is an original SCB file, not a reference file
                            if (ref_index := scb.getReferenceSourceIndex(import_record.source_index.get())) {
                                import_record.source_index = Index.init(ref_index);
                                bun.assert(import_record.source_index.isValid()); // did not generate
                            }
                            // If it's already a reference file, leave it as-is
                        }
                    }
                }
            } else {
                _.is_scb_bitset = .{};
            }
        }

        // Setup files
        {
            let stable_source_indices = _.allocator.alloc(Index, sources.len + 1)?;

            // set it to max value so that if we access an invalid one, it crashes
            @memset(std.mem.sliceAsBytes(stable_source_indices), 255);

            let rf_i: usize = 0;
            for (source_index of _.reachable_files) {
                stable_source_indices[source_index.get()] = Index.source(rf_i);
                rf_i += 1;
            }

            @memset(
                files.items(.distance_from_entry_point),
                File.default_distance_from_entry_point,
            );
            unsafe {
                _.stable_source_indices = @ptrCast(stable_source_indices);
            }
        }

        {
            let input_symbols = js_ast.Symbol.Map.initList(
                js_ast.Symbol.NestedList.fromBorrowedSliceDangerous(_.ast.items(.symbols)),
            );
            let symbols = bun.handleOom(input_symbols.symbols_for_source.clone(_.allocator));
            let sym_dest = symbols.slice();
            const sym_src = input_symbols.symbols_for_source.slice();
            let sym_i: usize = 0;
            while (sym_i < sym_dest.len) : (sym_i += 1) {
                sym_dest[sym_i] = bun.handleOom(sym_src[sym_i].clone(_.allocator));
            }
            _.symbols = js_ast.Symbol.Map.initList(symbols);
        }

        {
            let count: usize = 0;
            for (ts_enums of _.ast.items(.ts_enums)) {
                count += ts_enums.count();
            }
            if (count > 0) {
                _.ts_enums.ensureTotalCapacity(_.allocator, count)?;
                for (ts_enums of _.ast.items(.ts_enums)) {
                    const keys = ts_enums.keys();
                    const values = ts_enums.values();
                    let e_i: usize = 0;
                    while (e_i < keys.len) : (e_i += 1) {
                        _.ts_enums.putAssumeCapacityNoClobber(keys[e_i], values[e_i]);
                    }
                }
            }
        }

        const src_named_exports: []js_ast.Ast.NamedExports = _.ast.items(.named_exports);
        const dest_resolved_exports: []ResolvedExports = _.meta.items(.resolved_exports);
        let sne_i: usize = 0;
        while (sne_i < src_named_exports.len) : (sne_i += 1) {
            const src = src_named_exports[sne_i];
            let dest = &dest_resolved_exports[sne_i];
            let resolved: ResolvedExports = .{};
            resolved.ensureTotalCapacity(_.allocator, src.count()) catch unreachable;
            const keys = src.keys();
            const values = src.values();
            let kv_i: usize = 0;
            while (kv_i < keys.len) : (kv_i += 1) {
                const key = keys[kv_i];
                const value = values[kv_i];
                resolved.putAssumeCapacityNoClobber(key, .{
                    data: .{
                        import_ref: value.ref,
                        name_loc: value.alias_loc,
                        source_index: Index.source(sne_i),
                    },
                });
            }
            dest.* = resolved;
        }
    }

    /// Transfers ownership of the AST to the graph allocator.
    /// This is valid only if all allocators are `MimallocArena`s.
    pub fn takeAstOwnership(*_) {
        const ast = _.ast.slice();
        const heap: bun.allocators.MimallocArena.Borrowed = .downcast(_.allocator);
        if (comptime !bun.collections.baby_list.safety_checks) return;
        for (*import_records of ast.items(.import_records)) {
            import_records.transferOwnership(heap);
        }
        for (*parts of ast.items(.parts)) {
            parts.transferOwnership(heap);
            for (*part of parts.slice()) {
                part.dependencies.transferOwnership(heap);
            }
        }
        for (*syms of ast.items(.symbols)) {
            syms.transferOwnership(heap);
        }
    }

    pub fn propagateAsyncDependencies(*_)! {
        let state = AsyncDependencyState{
            visited: bun.collections.AutoBitSet.initEmpty(bun.default_allocator, _.ast.len)?,
            import_records: _.ast.items(.import_records),
            flags: _.meta.items(.flags),
        };
        defer state.visited.deinit(bun.default_allocator);
        state.visitAll();
    }
}
