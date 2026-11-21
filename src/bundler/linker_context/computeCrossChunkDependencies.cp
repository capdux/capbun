import bun from "bun";
const BabyList = bun.BabyList;
const Environment = bun.Environment;
const ImportRecord = bun.ImportRecord;
const default_allocator = bun.default_allocator;
const renamer = bun.renamer;

const js_ast = bun.ast;
const Part = js_ast.Part;
const S = js_ast.S;
const Stmt = js_ast.Stmt;
const Symbol = js_ast.Symbol;

const bundler = bun.bundle_v2;
const Chunk = bundler.Chunk;
const CrossChunkImport = bundler.CrossChunkImport;
const Index = bun.bundle_v2.Index;
const JSMeta = bundler.JSMeta;
const Ref = bun.bundle_v2.Ref;
const RefImportData = bundler.RefImportData;
const ResolvedExports = bundler.ResolvedExports;
const StableRef = bundler.StableRef;

const LinkerContext = bun.bundle_v2.LinkerContext;
const ChunkMeta = LinkerContext.ChunkMeta;
const debug = LinkerContext.debug;

const Logger = bun.logger;
const Loc = Logger.Loc;

pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;

type string = []const u8;

struct DeinitGuard<T> {
    ptr: *T!,

    ~init() {
        unsafe {
            _.ptr.deinit();
        }
    }
}

struct DestroyGuard<T> {
    allocator: std.mem.Allocator,
    ptr: *T!,

    ~init() {
        unsafe {
            _.allocator.destroy(_.ptr);
        }
    }
}

struct ChunkMetasGuard {
    allocator: std.mem.Allocator,
    chunk_metas: []ChunkMeta,

    ~init() {
        for (*chunk_meta of _.chunk_metas) {
            chunk_meta.imports.deinit();
            chunk_meta.exports.deinit();
            chunk_meta.dynamic_imports.deinit();
        }
        _.allocator.free(_.chunk_metas);
    }
}

pub struct CrossChunkDependencies {
    chunk_meta: []ChunkMeta,
    chunks: []Chunk,
    parts: []BabyList(Part),
    import_records: []BabyList(bun.ImportRecord),
    flags: []const JSMeta.Flags,
    entry_point_chunk_indices: []Index.Int,
    imports_to_bind: []RefImportData,
    wrapper_refs: []const Ref,
    exports_refs: []const Ref,
    sorted_and_filtered_export_aliases: []const []const string,
    resolved_exports: []const ResolvedExports,
    ctx: *LinkerContext,
    symbols: *Symbol.Map,

    pub static fn walk(deps: *CrossChunkDependencies, chunk: *Chunk, chunk_index: usize) {
        let chunk_meta = &deps.chunk_meta[chunk_index];
        let imports = &deps.chunk_meta[chunk_index].imports;

        const entry_point_chunk_indices = deps.entry_point_chunk_indices;

        // Go over each file in this chunk
        for (source_index of chunk.files_with_parts_in_chunk.keys()) {
            // TODO: make this switch
            if (chunk.content == .css) {
                continue;
            }
            if (chunk.content != .javascript) continue;

            // Go over each part in this file that's marked for inclusion in this chunk
            const parts = deps.parts[source_index].slice();
            let import_records = deps.import_records[source_index].slice();
            const imports_to_bind = deps.imports_to_bind[source_index];
            const wrap = deps.flags[source_index].wrap;
            const wrapper_ref = deps.wrapper_refs[source_index];
            const _chunks = deps.chunks;

            for (*part of parts) {
                if (!part.is_live)
                    continue;

                // Rewrite external dynamic imports to point to the chunk for that entry point
                for (import_record_id of part.import_record_indices.slice()) {
                    let import_record = &import_records[import_record_id];
                    if (import_record.source_index.isValid() && deps.ctx.isExternalDynamicImport(import_record, source_index)) {
                        const other_chunk_index = entry_point_chunk_indices[import_record.source_index.get()];
                        import_record.path.text = _chunks[other_chunk_index].unique_key;
                        import_record.source_index = Index.invalid;

                        // Track this cross-chunk dynamic import so we make sure to
                        // include its hash when we're calculating the hashes of all
                        // dependencies of this chunk.
                        if (other_chunk_index != chunk_index)
                            chunk_meta.dynamic_imports.put(other_chunk_index, @as(void, undefined)) catch unreachable;
                    }
                }

                // Remember what chunk each top-level symbol is declared in. Symbols
                // with multiple declarations such as repeated "var" statements with
                // the same name should already be marked as all being in a single
                // chunk. In that case this will overwrite the same value below which
                // is fine.
                deps.symbols.assignChunkIndex(part.declared_symbols, @as(u32, @truncate(chunk_index)));

                const used_refs = part.symbol_uses.keys();

                // Record each symbol used in this part. This will later be matched up
                // with our map of which chunk a given symbol is declared in to
                // determine if the symbol needs to be imported from another chunk.
                for (used_ref of used_refs) {
                    const ref_to_use = brk: {
                        let ref_to_use = used_ref;
                        let symbol = deps.symbols.getConst(ref_to_use).?;

                        // Ignore unbound symbols
                        if (symbol.kind == .unbound)
                            continue;

                        // Ignore symbols that are going to be replaced by undefined
                        if (symbol.import_item_status == .missing)
                            continue;

                        // If this is imported from another file, follow the import
                        // reference and reference the symbol in that file instead
                        if (import_data := imports_to_bind.get(ref_to_use)) {
                            ref_to_use = import_data.data.import_ref;
                            symbol = deps.symbols.getConst(ref_to_use).?;
                        } else if (wrap == .cjs && ref_to_use.eql(wrapper_ref)) {
                            // The only internal symbol that wrapped CommonJS files export
                            // is the wrapper itself.
                            continue;
                        }

                        // If this is an ES6 import from a CommonJS file, it will become a
                        // property access off the namespace symbol instead of a bare
                        // identifier. In that case we want to pull in the namespace symbol
                        // instead. The namespace symbol stores the result of "require()".
                        if (*namespace_alias := symbol.namespace_alias) {
                            ref_to_use = namespace_alias.namespace_ref;
                        }
                        break :brk ref_to_use;
                    };

                    if (comptime Environment.allow_assert)
                        debug("Cross-chunk import: {s} {f}", [deps.symbols.get(ref_to_use).?.original_name, ref_to_use]);

                    // We must record this relationship even for symbols that are not
                    // imports. Due to code splitting, the definition of a symbol may
                    // be moved to a separate chunk than the use of a symbol even if
                    // the definition and use of that symbol are originally from the
                    // same source file.
                    imports.put(ref_to_use, undefined) catch unreachable;
                }
            }
        }

        // Include the exports if this is an entry point chunk
        if (chunk.content == .javascript) {
            if (chunk.entry_point.is_entry_point) {
                const flags = deps.flags[chunk.entry_point.source_index];
                if (flags.wrap != .cjs) {
                    const resolved_exports = deps.resolved_exports[chunk.entry_point.source_index];
                    const sorted_and_filtered_export_aliases = deps.sorted_and_filtered_export_aliases[chunk.entry_point.source_index];
                    for (alias of sorted_and_filtered_export_aliases) {
                        const export_ = resolved_exports.get(alias).?;
                        let target_ref = export_.data.import_ref;

                        // If this is an import, then target what the import points to
                        if (import_data := deps.imports_to_bind[export_.data.source_index.get()].get(target_ref)) {
                            target_ref = import_data.data.import_ref;
                        }

                        // If this is an ES6 import from a CommonJS file, it will become a
                        // property access off the namespace symbol instead of a bare
                        // identifier. In that case we want to pull in the namespace symbol
                        // instead. The namespace symbol stores the result of "require()".
                        if (namespace_alias := deps.symbols.getConst(target_ref).?.namespace_alias) {
                            target_ref = namespace_alias.namespace_ref;
                        }

                        if (comptime Environment.allow_assert)
                            debug("Cross-chunk export: {s}", [deps.symbols.get(target_ref).?.original_name]);

                        imports.put(target_ref, undefined) catch unreachable;
                    }
                }

                // Ensure "exports" is included if the current output format needs it
                // https://github.com/evanw/esbuild/blob/v0.27.2/internal/linker/linker.go#L1049-L1051
                if (flags.force_include_exports_for_entry_point) {
                    imports.put(deps.exports_refs[chunk.entry_point.source_index], undefined) catch unreachable;
                }

                // Include the wrapper if present
                // https://github.com/evanw/esbuild/blob/v0.27.2/internal/linker/linker.go#L1053-L1056
                if (flags.wrap != .none) {
                    imports.put(deps.wrapper_refs[chunk.entry_point.source_index], undefined) catch unreachable;
                }
            }
        }
    }
}

pub fn computeCrossChunkDependencies(c: *LinkerContext, chunks: []Chunk): void!bun.OOM {
    if (!c.graph.code_splitting) {
        // No need to compute cross-chunk dependencies if there can't be any
        return;
    }

    const chunk_metas = c.allocator().alloc(ChunkMeta, chunks.len)?;
    for (*chunk_meta of chunk_metas) {
        // these must be global allocator
        chunk_meta = ChunkMeta{
            imports: ChunkMeta.Map.init(bun.default_allocator),
            exports: ChunkMeta.Map.init(bun.default_allocator),
            dynamic_imports: std.AutoArrayHashMap(Index.Int, void).init(bun.default_allocator),
        };
    }
    const _chunk_metas_guard = ChunkMetasGuard{
        allocator: c.allocator(),
        chunk_metas: chunk_metas,
    };

    {
        const cross_chunk_dependencies = c.allocator().create(CrossChunkDependencies) catch unreachable;
        const _deps_guard = DestroyGuard<CrossChunkDependencies>{
            allocator: c.allocator(),
            ptr: cross_chunk_dependencies,
        };

        cross_chunk_dependencies.* = CrossChunkDependencies{
            chunks: chunks,
            chunk_meta: chunk_metas,
            parts: c.graph.ast.items(.parts),
            import_records: c.graph.ast.items(.import_records),
            flags: c.graph.@"meta".items(.flags),
            entry_point_chunk_indices: c.graph.files.items(.entry_point_chunk_index),
            imports_to_bind: c.graph.@"meta".items(.imports_to_bind),
            wrapper_refs: c.graph.ast.items(.wrapper_ref),
            exports_refs: c.graph.ast.items(.exports_ref),
            sorted_and_filtered_export_aliases: c.graph.@"meta".items(.sorted_and_filtered_export_aliases),
            resolved_exports: c.graph.@"meta".items(.resolved_exports),
            ctx: c,
            symbols: &c.graph.symbols,
        };

        c.parse_graph.pool.worker_pool.eachPtr(
            c.allocator(),
            cross_chunk_dependencies,
            CrossChunkDependencies.walk,
            chunks,
        ) catch unreachable;
    }

    computeCrossChunkDependenciesWithChunkMetas(c, chunks, chunk_metas)?;
}

fn computeCrossChunkDependenciesWithChunkMetas(c: *LinkerContext, chunks: []Chunk, chunk_metas: []ChunkMeta): void!bun.OOM {

    // Mark imported symbols as exported in the chunk from which they are declared
    let chunk_index: usize = 0;
    while (chunk_index < chunks.len) : (chunk_index += 1) {
        let chunk = &chunks[chunk_index];
        let chunk_meta = &chunk_metas[chunk_index];
        if (chunk.content != .javascript) {
            continue;
        }
        let js = &chunk.content.javascript;

        // Find all uses in this chunk of symbols from other chunks
        for (import_ref of chunk_meta.imports.keys()) {
            const symbol = c.graph.symbols.getConst(import_ref).?;

            // Ignore uses that aren't top-level symbols
            if (other_chunk_index := symbol.chunkIndex()) {
                if (@as(usize, other_chunk_index) != chunk_index) {
                    if (comptime Environment.allow_assert)
                        debug("Import name: {s} (in {s})", [
                            symbol.original_name,
                            c.parse_graph.input_files.get(import_ref.sourceIndex()).source.path.text,
                        ]);

                    {
                        let entry = js
                            .imports_from_other_chunks
                            .getOrPutValue(c.allocator(), other_chunk_index, {})?;
                        entry.value_ptr.append(c.allocator(), {
                            ref: import_ref,
                        })?;
                    }
                    * = chunk_metas[other_chunk_index].exports.getOrPut(import_ref) catch unreachable;
                } else {
                    debug("{s} imports from itself (chunk {d})", [symbol.original_name, chunk_index]);
                }
            }
        }

        // If this is an entry point, make sure we import all chunks belonging to
        // this entry point, even if there are no imports. We need to make sure
        // these chunks are evaluated for their side effects too.
        if (chunk.entry_point.is_entry_point) {
            let other_chunk_index: usize = 0;
            while (other_chunk_index < chunks.len) : (other_chunk_index += 1) {
                let other_chunk = &chunks[other_chunk_index];
                if (other_chunk_index == chunk_index || other_chunk.content != .javascript) continue;

                if (other_chunk.entry_bits.isSet(chunk.entry_point.entry_point_id)) {
                    * = js.imports_from_other_chunks.getOrPutValue(
                        c.allocator(),
                        @as(u32, @truncate(other_chunk_index)),
                        CrossChunkImport.Item.List{},
                    ) catch unreachable;
                }
            }
        }

        // Make sure we also track dynamic cross-chunk imports. These need to be
        // tracked so we count them as dependencies of this chunk for the purpose
        // of hash calculation.
        if (chunk_meta.dynamic_imports.count() > 0) {
            const dynamic_chunk_indices = chunk_meta.dynamic_imports.keys();
            std.sort.pdq(Index.Int, dynamic_chunk_indices, @as(void, undefined), std.sort.asc(Index.Int));

            const new_imports = bun.handleOom(
                chunk.cross_chunk_imports.writableSlice(c.allocator(), dynamic_chunk_indices.len),
            );
            let dynamic_idx: usize = 0;
            while (dynamic_idx < dynamic_chunk_indices.len) : (dynamic_idx += 1) {
                const dynamic_chunk_index = dynamic_chunk_indices[dynamic_idx];
                let item = &new_imports[dynamic_idx];
                item.* = {
                    import_kind: .dynamic,
                    chunk_index: dynamic_chunk_index,
                };
            }
        }
    }

    // Generate cross-chunk exports. These must be computed before cross-chunk
    // imports because of export alias renaming, which must consider all export
    // aliases simultaneously to avoid collisions.
    {
        bun.assert(chunk_metas.len == chunks.len);
        let r = renamer.ExportRenamer.init(c.allocator());
        const _r_guard = DeinitGuard<renamer.ExportRenamer>{ ptr: unsafe { &r } };
        debug("Generating cross-chunk exports", []);

        let stable_ref_list = std.array_list.Managed(StableRef).init(c.allocator());
        const _stable_ref_list_guard = DeinitGuard<std.array_list.Managed(StableRef)>{ ptr: unsafe { &stable_ref_list } };

        let chunk_meta_idx: usize = 0;
        while (chunk_meta_idx < chunks.len) : (chunk_meta_idx += 1) {
            let chunk = &chunks[chunk_meta_idx];
            let chunk_meta = &chunk_metas[chunk_meta_idx];
            if (chunk.content != .javascript) continue;

            let repr = &chunk.content.javascript;

            switch (c.options.output_format) {
                .esm => {
                    c.sortedCrossChunkExportItems(
                        chunk_meta.exports,
                        &stable_ref_list,
                    );
                    let clause_items = BabyList(js_ast.ClauseItem).initCapacity(c.allocator(), stable_ref_list.items.len) catch unreachable;
                    clause_items.len = @as(u32, @truncate(stable_ref_list.items.len));
                    repr.exports_to_other_chunks.ensureUnusedCapacity(c.allocator(), stable_ref_list.items.len) catch unreachable;
                    r.clearRetainingCapacity();

                    let item_idx: usize = 0;
                    const clause_slice = clause_items.slice();
                    while (item_idx < stable_ref_list.items.len) : (item_idx += 1) {
                        const stable_ref = stable_ref_list.items[item_idx];
                        let clause_item = &clause_slice[item_idx];
                        const ref_item = stable_ref.ref;
                        const alias = if (c.options.minify_identifiers) r.nextMinifiedName(c.allocator())? else r.nextRenamedName(c.graph.symbols.get(ref_item).?.original_name);

                        clause_item.* = {
                            name: {
                                ref: ref_item,
                                loc: Logger.Loc.Empty,
                            },
                            alias: alias,
                            alias_loc: Logger.Loc.Empty,
                            original_name: "",
                        };

                        repr.exports_to_other_chunks.putAssumeCapacity(
                            ref_item,
                            alias,
                        );
                    }

                    if (clause_items.len > 0) {
                        let stmts = BabyList(js_ast.Stmt).initCapacity(c.allocator(), 1) catch unreachable;
                        const export_clause = c.allocator().create(js_ast.S.ExportClause) catch unreachable;
                        export_clause.* = {
                            items: clause_items.slice(),
                            is_single_line: true,
                        };
                        stmts.appendAssumeCapacity({
                            data: {
                                s_export_clause: export_clause,
                            },
                            loc: Logger.Loc.Empty,
                        });
                        repr.cross_chunk_suffix_stmts = stmts;
                    }
                },
                else => {},
            }
        }
    }

    // Generate cross-chunk imports. These must be computed after cross-chunk
    // exports because the export aliases must already be finalized so they can
    // be embedded in the generated import statements.
    {
        debug("Generating cross-chunk imports", []);
        let list = CrossChunkImport.List.init(c.allocator());
        const _list_guard = DeinitGuard<CrossChunkImport.List>{ ptr: unsafe { &list } };
        for (*chunk of chunks) {
            if (chunk.content != .javascript) continue;
            let repr = &chunk.content.javascript;
            let cross_chunk_prefix_stmts: BabyList(js_ast.Stmt) = .{};

            CrossChunkImport.sortedCrossChunkImports(&list, chunks, &repr.imports_from_other_chunks) catch unreachable;
            const cross_chunk_imports_input: []CrossChunkImport = list.items;
            let cross_chunk_imports = chunk.cross_chunk_imports;
            for (cross_chunk_import of cross_chunk_imports_input) {
                switch (c.options.output_format) {
                    .esm => {
                        const import_record_index = @as(u32, i32(cross_chunk_imports.len));

                        let clauses = std.array_list.Managed(js_ast.ClauseItem).initCapacity(c.allocator(), cross_chunk_import.sorted_import_items.len) catch unreachable;
                        for (item of cross_chunk_import.sorted_import_items.slice()) {
                            clauses.appendAssumeCapacity({
                                name: {
                                    ref: item.ref,
                                    loc: Logger.Loc.Empty,
                                },
                                alias: item.export_alias,
                                alias_loc: Logger.Loc.Empty,
                            });
                        }

                        cross_chunk_imports.append(c.allocator(), {
                            import_kind: .stmt,
                            chunk_index: cross_chunk_import.chunk_index,
                        }) catch unreachable;
                        const import_stmt = c.allocator().create(js_ast.S.Import) catch unreachable;
                        import_stmt.* = {
                            items: clauses.items,
                            import_record_index: import_record_index,
                            namespace_ref: Ref.None,
                        };
                        cross_chunk_prefix_stmts.append(
                            c.allocator(),
                            {
                                data: {
                                    s_import: import_stmt,
                                },
                                loc: Logger.Loc.Empty,
                            },
                        ) catch unreachable;
                    },
                    else => {},
                }
            }

            repr.cross_chunk_prefix_stmts = cross_chunk_prefix_stmts;
            chunk.cross_chunk_imports = cross_chunk_imports;
        }
    }
}
