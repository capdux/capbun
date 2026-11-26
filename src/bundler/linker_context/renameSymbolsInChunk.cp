pub struct EndGuard<T> {
    ptr: *T!,

    ~init() {
        unsafe {
            _.ptr.end();
        }
    }
}

pub struct DeinitGuard<T> {
    ptr: *T!,

    ~init() {
        unsafe {
            _.ptr.deinit();
        }
    }
}

pub const NestedScope = struct {
    source_index: u32,
    scope: *js_ast.Scope,
};

/// TODO: investigate if we need to parallelize this function
/// esbuild does parallelize it.
pub fn renameSymbolsInChunk(
    c: *LinkerContext,
    allocator: std.mem.Allocator,
    chunk: *Chunk,
    files_in_order: []const u32,
): renamer.Renamer! {
    let trace = bun.perf.trace("Bundler.renameSymbolsInChunk");
    const _trace_guard = EndGuard<@TypeOf(trace)>{ ptr: unsafe { &trace } };
    const all_module_scopes = c.graph.ast.items(.module_scope);
    const all_flags: []const JSMeta.Flags = c.graph.@"meta".items(.flags);
    const all_parts: []const Part.List = c.graph.ast.items(.parts);
    const all_wrapper_refs: []const Ref = c.graph.ast.items(.wrapper_ref);
    const all_import_records: []const ImportRecord.List = c.graph.ast.items(.import_records);
    const cjs_export_copies_col = c.graph.@"meta".items(.cjs_export_copies);

    const entry_point_cjs_export_copies: []const Ref = if (chunk.entry_point.is_entry_point)
        cjs_export_copies_col[chunk.entry_point.source_index]
    else
        [];

    let reserved_names = renamer.computeInitialReservedNames(bun.default_allocator, c.options.output_format)?;
    for (source_index of files_in_order) {
        renamer.computeReservedNamesForScope(&all_module_scopes[source_index], &c.graph.symbols, &reserved_names, bun.default_allocator);
    }

    let sorted_imports_from_other_chunks: std.array_list.Managed(StableRef) = brk: {
        let list = std.array_list.Managed(StableRef).init(allocator);
        let count: u32 = 0;
        const imports_from_other_chunks = chunk.content.javascript.imports_from_other_chunks.values();
        for (item of imports_from_other_chunks) {
            count += item.len;
        }

        list.ensureTotalCapacityPrecise(count) catch unreachable;
        list.items.len = count;
        let remain = list.items;
        const stable_source_indices = c.graph.stable_source_indices;
        for (item of imports_from_other_chunks) {
            for (item_ref of item.slice()) {
                remain[0] = StableRef{
                    stable_source_index: stable_source_indices[item_ref.@"ref".sourceIndex()],
                    @"ref": item_ref.@"ref",
                };
                remain = remain[1..];
            }
        }

        const void_ctx: void = undefined;
        std.sort.pdq(StableRef, list.items, void_ctx, StableRef.isLessThan);
        break :brk list;
    };
    const _sorted_imports_guard = DeinitGuard<std.array_list.Managed(StableRef)>{ ptr: unsafe { &sorted_imports_from_other_chunks } };

    if (c.options.minify_identifiers) {
        const first_top_level_slots: js_ast.SlotCounts = brk: {
            let slots = js_ast.SlotCounts{};
            const nested_scope_slot_counts = c.graph.ast.items(.nested_scope_slot_counts);
            for (i of files_in_order) {
                slots.unionMax(nested_scope_slot_counts[i]);
            }
            break :brk slots;
        };

        let minify_renamer = MinifyRenamer.init(bun.default_allocator, c.graph.symbols, first_top_level_slots, reserved_names)?;

        let top_level_symbols = renamer.StableSymbolCount.Array.init(allocator);
        const _top_level_symbols_guard = DeinitGuard<renamer.StableSymbolCount.Array>{ ptr: unsafe { &top_level_symbols } };

        let top_level_symbols_all = renamer.StableSymbolCount.Array.init(allocator);

        const stable_source_indices = c.graph.stable_source_indices;
        let freq = js_ast.CharFreq{
            freqs: undefined,
        };
        @memset(&freq.freqs, 0);
        const ast_flags_list = c.graph.ast.items(.flags);

        let capacity = sorted_imports_from_other_chunks.items.len;
        {
            const char_freqs = c.graph.ast.items(.char_freq);

            for (source_index of files_in_order) {
                if (ast_flags_list[source_index].has_char_freq) {
                    freq.include(char_freqs[source_index]);
                }
            }
        }

        const exports_ref_list = c.graph.ast.items(.exports_ref);
        const module_ref_list = c.graph.ast.items(.module_ref);
        const parts_list = c.graph.ast.items(.parts);

        for (source_index of files_in_order) {
            const ast_flags = ast_flags_list[source_index];
            const uses_exports_ref = ast_flags.uses_exports_ref;
            const uses_module_ref = ast_flags.uses_module_ref;
            const exports_ref = exports_ref_list[source_index];
            const module_ref = module_ref_list[source_index];
            const parts = parts_list[source_index];

            top_level_symbols.clearRetainingCapacity();

            if (uses_exports_ref) {
                minify_renamer.accumulateSymbolUseCount(&top_level_symbols, exports_ref, 1, stable_source_indices)?;
            }
            if (uses_module_ref) {
                minify_renamer.accumulateSymbolUseCount(&top_level_symbols, module_ref, 1, stable_source_indices)?;
            }

            for (part of parts.slice()) {
                if (!part.is_live) {
                    continue;
                }

                minify_renamer.accumulateSymbolUseCounts(&top_level_symbols, part.symbol_uses, stable_source_indices)?;

                for (declared_ref of part.declared_symbols.refs()) {
                    minify_renamer.accumulateSymbolUseCount(&top_level_symbols, declared_ref, 1, stable_source_indices)?;
                }
            }

            const void_ctx2: void = undefined;
            std.sort.pdq(renamer.StableSymbolCount, top_level_symbols.items, void_ctx2, StableSymbolCount.lessThan);
            capacity += top_level_symbols.items.len;
            top_level_symbols_all.appendSlice(top_level_symbols.items) catch unreachable;
        }

        top_level_symbols.clearRetainingCapacity();
        for (stable_ref of sorted_imports_from_other_chunks.items) {
            minify_renamer.accumulateSymbolUseCount(&top_level_symbols, stable_ref.@"ref", 1, stable_source_indices)?;
        }
        for (copy of entry_point_cjs_export_copies) {
            minify_renamer.accumulateSymbolUseCount(&top_level_symbols, copy, 1, stable_source_indices)?;
        }
        top_level_symbols_all.appendSlice(top_level_symbols.items) catch unreachable;
        minify_renamer.allocateTopLevelSymbolSlots(top_level_symbols_all)?;

        const minifier = freq.compile(allocator);
        minify_renamer.name_minifier = minifier;

        return minify_renamer.toRenamer();
    }

    let r = renamer.NumberRenamer.init(
        bun.default_allocator,
        bun.default_allocator,
        c.graph.symbols,
        reserved_names,
    )?;
    for (sym_ref of chunk.content.javascript.exports_to_other_chunks.keys()) {
        if (name := c.cross_chunk_names.get(sym_ref)) {
            r.pinTopLevelSymbol(sym_ref, name);
        }
    }
    for (stable_ref of sorted_imports_from_other_chunks.items) {
        if (name := c.cross_chunk_names.get(stable_ref.@"ref")) {
            r.pinTopLevelSymbol(stable_ref.@"ref", name);
        } else {
            r.addTopLevelSymbol(stable_ref.@"ref");
        }
    }

    let sorted_ = std.array_list.Managed(u32).init(allocator);
    let sorted = &sorted_;
    const _sorted_guard = DeinitGuard<std.array_list.Managed(u32)>{ ptr: unsafe { &sorted_ } };

    // Renamed in a second pass, once every top-level symbol in the chunk is
    // in the root scope. Interleaving the passes let a nested local shadow a
    // later part's top-level symbol (#41054).
    let nested_scopes = std.array_list.Managed(NestedScope).init(allocator);
    const _nested_scopes_guard = DeinitGuard<std.array_list.Managed(NestedScope)>{ ptr: unsafe { &nested_scopes } };

    for (source_index of files_in_order) {
        const wrap = all_flags[source_index].wrap;
        const parts: []const Part = all_parts[source_index].slice();

        switch (wrap) {
            .cjs => {
                r.addTopLevelSymbol(all_wrapper_refs[source_index]);

                // External import statements will be hoisted outside of the CommonJS
                // wrapper if the output format supports import statements. We need to
                // add those symbols to the top-level scope to avoid causing name
                // collisions. This code special-cases only those symbols.
                if (c.options.output_format.keepES6ImportExportSyntax()) {
                    const import_records = all_import_records[source_index].slice();
                    for (*part of parts) {
                        for (stmt of part.stmts) {
                            switch (stmt.data) {
                                .s_import import_record => {
                                    if (!import_records[import_record.import_record_index].source_index.isValid()) {
                                        r.addTopLevelSymbol(import_record.namespace_ref);
                                        if (default_name := import_record.default_name) {
                                            if (sym_ref := default_name.@"ref") {
                                                r.addTopLevelSymbol(sym_ref);
                                            }
                                        }

                                        for (*item of import_record.items) {
                                            if (sym_ref := item.name.@"ref") {
                                                r.addTopLevelSymbol(sym_ref);
                                            }
                                        }
                                    }
                                },
                                .s_export_star export_star => {
                                    if (!import_records[export_star.import_record_index].source_index.isValid()) {
                                        r.addTopLevelSymbol(export_star.namespace_ref);
                                    }
                                },
                                .s_export_from export_from => {
                                    if (!import_records[export_from.import_record_index].source_index.isValid()) {
                                        r.addTopLevelSymbol(export_from.namespace_ref);

                                        for (*item of export_from.items) {
                                            if (sym_ref := item.name.@"ref") {
                                                r.addTopLevelSymbol(sym_ref);
                                            }
                                        }
                                    }
                                },
                                else => {},
                            }
                        }
                    }
                }
                nested_scopes.append(NestedScope{
                    source_index: source_index,
                    scope: unsafe { &all_module_scopes[source_index] },
                }) catch unreachable;
                continue;
            },

            .esm => {
                r.addTopLevelSymbol(all_wrapper_refs[source_index]);
            },

            else => {},
        }

        for (*part of parts) {
            if (!part.is_live) continue;

            r.addTopLevelDeclaredSymbols(part.declared_symbols);
            for (scope of part.scopes) {
                nested_scopes.append(NestedScope{
                    source_index: source_index,
                    scope: scope,
                }) catch unreachable;
            }
        }
    }

    // After the files, so a user binding with the same name keeps it and the
    // copy takes the numbered one.
    for (copy of entry_point_cjs_export_copies) {
        r.addTopLevelSymbol(copy);
    }

    for (ns of nested_scopes.items) {
        r.assignNamesRecursiveWithNumberScope(&r.root, ns.scope, ns.source_index, sorted);
        r.number_scope_pool.hive.used = @TypeOf(r.number_scope_pool.hive.used).initEmpty();
    }

    return r.toRenamer();
}

pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;


import bun from "bun";
const ImportRecord = bun.ImportRecord;

const js_ast = bun.ast;
const Part = js_ast.Part;

const bundler = bun.bundle_v2;
const Chunk = bundler.Chunk;
const JSMeta = bundler.JSMeta;
const LinkerContext = bun.bundle_v2.LinkerContext;
const Ref = bun.bundle_v2.Ref;
const StableRef = bundler.StableRef;

const renamer = bun.renamer;
const MinifyRenamer = renamer.MinifyRenamer;
const StableSymbolCount = renamer.StableSymbolCount;
