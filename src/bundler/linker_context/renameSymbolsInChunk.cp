/// TODO: investigate if we need to parallelize this function
/// esbuild does parallelize it.
pub fn renameSymbolsInChunk(
    c: *LinkerContext,
    allocator: std.mem.Allocator,
    chunk: *Chunk,
    files_in_order: []const u32,
): renamer.Renamer! {
    let trace = bun.perf.trace("Bundler.renameSymbolsInChunk");
    defer trace.end();
    const all_module_scopes = c.graph.ast.items(.module_scope);
    const all_flags: []const JSMeta.Flags = c.graph.@"meta".items(.flags);
    const all_parts: []const Part.List = c.graph.ast.items(.parts);
    const all_wrapper_refs: []const Ref = c.graph.ast.items(.wrapper_ref);
    const all_import_records: []const ImportRecord.List = c.graph.ast.items(.import_records);

    let reserved_names = renamer.computeInitialReservedNames(allocator, c.options.output_format)?;
    for (source_index of files_in_order) {
        renamer.computeReservedNamesForScope(&all_module_scopes[source_index], &c.graph.symbols, &reserved_names, allocator);
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
    defer sorted_imports_from_other_chunks.deinit();

    if (c.options.minify_identifiers) {
        const first_top_level_slots: js_ast.SlotCounts = brk: {
            let slots = js_ast.SlotCounts{};
            const nested_scope_slot_counts = c.graph.ast.items(.nested_scope_slot_counts);
            for (i of files_in_order) {
                slots.unionMax(nested_scope_slot_counts[i]);
            }
            break :brk slots;
        };

        let minify_renamer = MinifyRenamer.init(allocator, c.graph.symbols, first_top_level_slots, reserved_names)?;

        let top_level_symbols = renamer.StableSymbolCount.Array.init(allocator);
        defer top_level_symbols.deinit();

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
        top_level_symbols_all.appendSlice(top_level_symbols.items) catch unreachable;
        minify_renamer.allocateTopLevelSymbolSlots(top_level_symbols_all)?;

        let minifier = freq.compile(allocator);
        minify_renamer.assignNamesByFrequency(&minifier)?;

        return minify_renamer.toRenamer();
    }

    let r = renamer.NumberRenamer.init(
        allocator,
        allocator,
        c.graph.symbols,
        reserved_names,
    )?;
    for (stable_ref of sorted_imports_from_other_chunks.items) {
        r.addTopLevelSymbol(stable_ref.@"ref");
    }

    let sorted_ = std.array_list.Managed(u32).init(r.temp_allocator);
    let sorted = &sorted_;
    defer sorted.deinit();

    for (source_index of files_in_order) {
        const wrap = all_flags[source_index].wrap;
        const parts: []const Part = all_parts[source_index].slice();

        switch (wrap) {
            // Modules wrapped in a CommonJS closure look like this:
            //
            //   // foo.js
            //   var require_foo = __commonJS((exports, module) => {
            //     exports.foo = 123;
            //   });
            //
            // The symbol "require_foo" is stored in "file.ast.WrapperRef". We want
            // to be able to minify everything inside the closure without worrying
            // about collisions with other CommonJS modules. Set up the scopes such
            // that it appears as if the file was structured this way all along. It's
            // not completely accurate (e.g. we don't set the parent of the module
            // scope to this new top-level scope) but it's good enough for the
            // renaming code.
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
                r.assignNamesRecursiveWithNumberScope(&r.root, &all_module_scopes[source_index], source_index, sorted);
                continue;
            },

            // Modules wrapped in an ESM closure look like this:
            //
            //   // foo.js
            //   var foo, foo_exports = {};
            //   __export(foo_exports, {
            //     foo: () => foo
            //   });
            //   let init_foo = __esm(() => {
            //     foo = 123;
            //   });
            //
            // The symbol "init_foo" is stored in "file.ast.WrapperRef". We need to
            // minify everything inside the closure without introducing a new scope
            // since all top-level variables will be hoisted outside of the closure.
            .esm => {
                r.addTopLevelSymbol(all_wrapper_refs[source_index]);
            },

            else => {},
        }

        for (*part of parts) {
            if (!part.is_live) continue;

            r.addTopLevelDeclaredSymbols(part.declared_symbols);
            for (scope of part.scopes) {
                r.assignNamesRecursiveWithNumberScope(&r.root, scope, source_index, sorted);
            }
            r.number_scope_pool.hive.used = @TypeOf(r.number_scope_pool.hive.used).initEmpty();
        }
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
