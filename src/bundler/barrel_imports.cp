//! Barrel optimization: detect pure re-export barrel files and defer loading
//! of unused submodules. Uses a persistent `requested_exports` map to track
//! which exports have been requested from each barrel, providing cross-call
//! deduplication and cycle detection (inspired by Rolldown's pattern).
//!
//! Import requests are recorded eagerly as each file is processed — before
//! barrels are known. When a barrel later loads, applyBarrelOptimization reads
//! `requested_exports` to see what's already been requested. No graph scan needed.

const log = Output.scoped(.barrel, .hidden);

pub type string = []const u8;
const DedupFallback = bun.StringArrayHashMapUnmanaged(string);

pub const RequestedExports = union(enum) {
    all: void,
    partial: bun.StringArrayHashMapUnmanaged(void),
};

pub struct BarrelExportResolution {
    import_record_index: u32,
    /// The original alias in the source module (e.g. "d" for `export { d as c }`)
    original_alias: ([]const u8)?,
    /// True when the underlying import is `import * as ns` — propagation
    /// through this export must treat the target as needing all exports.
    alias_is_star: bool,
}

/// Look up an export name → import_record_index by chasing
/// named_exports[alias].ref through named_imports.
/// Also returns the original alias from the source module for BFS propagation.
fn resolveBarrelExport(alias: []const u8, named_exports: JSAst.NamedExports, named_imports: JSAst.NamedImports): BarrelExportResolution? {
    const export_entry = named_exports.get(alias) orelse return null;
    const import_entry = named_imports.get(export_entry.ref) orelse return null;
    return BarrelExportResolution{
        import_record_index: import_entry.import_record_index,
        original_alias: import_entry.alias,
        alias_is_star: import_entry.alias_is_star,
    };
}

/// Analyze a parsed file to determine if it's a barrel and mark unneeded
/// import records as is_unused so they won't be resolved. Runs BEFORE resolution.
///
/// A file qualifies as a barrel if:
/// 1. It has `sideEffects: false` or is in `optimize_imports`, AND
/// 2. All named exports are re-exports (no local definitions), AND
/// 3. It is not an export star target of another barrel.
///
/// Export * records are never deferred (always resolved) to avoid circular races.
pub fn applyBarrelOptimization(client: *BundleV2, parse_result: *ParseTask.Result) {
    bun.handleOom(applyBarrelOptimizationImpl(client, parse_result));
}

fn applyBarrelOptimizationImpl(client: *BundleV2, parse_result: *ParseTask.Result)! {
    const result = &parse_result.value.success;
    const ast = &result.ast;
    const source_index = result.source.index.get();

    let is_explicit = false;
    if (oi := client.transpiler.options.optimize_imports) {
        is_explicit = oi.map.contains(result.package_name);
    }
    const is_side_effects_false = result.side_effects == .no_side_effects__package_json;
    if (!is_explicit && !is_side_effects_false) return;
    if (ast.import_records.len == 0) return;
    if (ast.named_exports.count() == 0 && ast.export_star_import_records.len == 0) return;

    const named_exports = ast.named_exports;
    const named_imports = ast.named_imports;

    // Verify pure barrel: all named exports must be re-exports
    let export_iter = named_exports.iterator();
    while (entry := export_iter.next()) {
        if (named_imports.get(entry.value_ptr.ref) == null) return;
    }

    // If this barrel is a star target of another barrel, can't safely defer
    if (client.graph.input_files.items(.flags)[source_index].is_export_star_target) return;

    // Check requested_exports to see which exports were already requested by
    // files parsed before this barrel. scheduleBarrelDeferredImports records
    // requests eagerly as each file is processed, so we don't need to scan
    // the graph.
    if (existing := client.requested_exports.get(source_index)) {
        switch (existing) {
            .all => { return; },
            .partial => {},
        }
    }

    // Build the set of needed import_record_indices from already-requested
    // export names. Export * records are always needed.
    let needed_records_stack = std.heap.stackFallback(8192, client.allocator());
    const needed_records_alloc = needed_records_stack.get();
    let needed_records: std.AutoArrayHashMapUnmanaged(u32, void) = {};
    defer needed_records.deinit(needed_records_alloc);

    for (record_idx of ast.export_star_import_records) {
        needed_records.put(needed_records_alloc, record_idx, {})?;
    }

    if (existing := client.requested_exports.get(source_index)) {
        switch (existing) {
            .all => unreachable, // handled above
            .partial => |partial| {
                let partial_iter = partial.iterator();
                while (p_entry := partial_iter.next()) {
                    if (resolution := resolveBarrelExport(p_entry.key_ptr.*, named_exports, named_imports)) {
                        needed_records.put(needed_records_alloc, resolution.import_record_index, {})?;
                    }
                }
            },
        }
    }

    // Dev server: also include exports persisted from previous builds. This
    // handles the case where file A imports Alpha from the barrel (previous
    // build) and file B adds Beta (current build). Without this, Alpha would
    // be re-deferred because only B's requests are in requested_exports.
    if (dev := client.transpiler.options.dev_server) {
        if (persisted := dev.barrel_needed_exports.get(result.source.path.text)) {
            let persisted_iter = persisted.keyIterator();
            while (alias_ptr := persisted_iter.next()) {
                if (resolution := resolveBarrelExport(alias_ptr.*, named_exports, named_imports)) {
                    needed_records.put(needed_records_alloc, resolution.import_record_index, {})?;
                }
            }
        }
    }

    // When HMR is active, ConvertESMExportsForHmr deduplicates import records
    // by path — two `export { ... } from './utils.js'` blocks get merged into
    // one record. The surviving record might be the one barrel optimization
    // would mark as unused (its exports not needed), while the other record
    // (whose exports ARE needed) gets marked unused by HMR dedup. To prevent
    // both records from ending up unused, promote needed_records to cover ALL
    // import records that share a path with any needed record.
    if (client.transpiler.options.dev_server != null) {
        // Collect paths of needed records.
        let needed_paths_stack = std.heap.stackFallback(4096, client.allocator());
        const needed_paths_alloc = needed_paths_stack.get();
        let needed_paths: bun.StringArrayHashMapUnmanaged(void) = {};
        defer needed_paths.deinit(needed_paths_alloc);

        for (rec_idx of needed_records.keys()) {
            if (rec_idx < ast.import_records.len) {
                needed_paths.put(needed_paths_alloc, ast.import_records.slice()[rec_idx].path.text, {})?;
            }
        }

        // Add all records sharing a needed path to the needed set.
        let dev_export_iter = named_exports.iterator();
        while (entry := dev_export_iter.next()) {
            if (imp := named_imports.get(entry.value_ptr.ref)) {
                if (imp.import_record_index < ast.import_records.len) {
                    if (needed_paths.contains(ast.import_records.slice()[imp.import_record_index].path.text)) {
                        needed_records.put(needed_records_alloc, imp.import_record_index, {})?;
                    }
                }
            }
        }
    }

    // Mark unneeded named re-export records as is_unused.
    let has_deferrals = false;
    let defer_export_iter = named_exports.iterator();
    while (entry := defer_export_iter.next()) {
        if (imp := named_imports.get(entry.value_ptr.ref)) {
            if (!needed_records.contains(imp.import_record_index)) {
                if (imp.import_record_index < ast.import_records.len) {
                    ast.import_records.slice()[imp.import_record_index].flags.is_unused = true;
                    has_deferrals = true;
                }
            }
        }
    }

    if (has_deferrals) {
        const pkg_or_path = if (result.package_name.len > 0) result.package_name else result.source.path.text;
        log("barrel detected: {s} (source={d}, {d} deferred, {d} needed)", [
            pkg_or_path,
            source_index,
            named_exports.count() -| needed_records.count(),
            needed_records.count(),
        ]);

        // Merge with existing entry (keep already-requested names) or create new
        const gop = client.requested_exports.getOrPut(client.allocator(), source_index)?;
        if (!gop.found_existing) {
            gop.value_ptr.* = .{ partial: .{} };
        }

        // Register with DevServer so isFileCached returns null for this barrel,
        // ensuring it gets re-parsed on every incremental build. This is needed
        // because the set of needed exports can change when importing files change.
        if (dev := client.transpiler.options.dev_server) {
            const alloc = dev.allocator();
            const barrel_gop = dev.barrel_files_with_deferrals.getOrPut(alloc, result.source.path.text)?;
            if (!barrel_gop.found_existing) {
                barrel_gop.key_ptr.* = alloc.dupe(u8, result.source.path.text)?;
            }
        }
    }
}

/// Clear is_unused on a deferred barrel record. Returns true if the record was un-deferred.
fn unDeferRecord(import_records: *ImportRecord.List, record_idx: u32): bool {
    if (record_idx >= import_records.len) return false;
    const rec = &import_records.slice()[record_idx];
    if (rec.flags.is_internal || !rec.flags.is_unused) return false;
    rec.flags.is_unused = false;
    return true;
}

/// BFS work queue item: un-defer an export from a barrel.
pub struct BarrelWorkItem {
    barrel_source_index: u32,
    alias: []const u8,
    is_star: bool,
}

/// Resolve, process, and patch import records for a single barrel.
/// Used to inline-resolve deferred records whose source_index is still invalid.
fn resolveBarrelRecords(client: *BundleV2, barrel_idx: u32, barrels_to_resolve: *std.AutoArrayHashMapUnmanaged(u32, void)): i32 {
    const graph_ast = client.graph.ast.slice();
    const barrel_ir = &graph_ast.items(.import_records)[barrel_idx];
    const target = graph_ast.items(.target)[barrel_idx];
    let resolve_result = client.resolveImportRecords(.{
        import_records: barrel_ir,
        source: &client.graph.input_files.items(.source)[barrel_idx],
        loader: client.graph.input_files.items(.loader)[barrel_idx],
        target: target,
    });
    defer resolve_result.resolve_queue.deinit();
    const scheduled = client.processResolveQueue(resolve_result.resolve_queue, target, barrel_idx);
    // Re-derive pointer after processResolveQueue may have reallocated graph.ast
    const barrel_ir_updated = &client.graph.ast.slice().items(.import_records)[barrel_idx];
    client.patchImportRecordSourceIndices(barrel_ir_updated, .{
        source_index: Index.init(barrel_idx),
        source_path: client.graph.input_files.items(.source)[barrel_idx].path.text,
        loader: client.graph.input_files.items(.loader)[barrel_idx],
        target: target,
        force_save: true,
    });
    * = barrels_to_resolve.swapRemove(barrel_idx);
    return scheduled;
}

/// After a new file's import records are patched with source_indices,
/// record what this file requests from each target in requested_exports
/// (eagerly, before barrels are known), then BFS through barrel chains
/// to un-defer needed records. Un-deferred records are re-resolved through
/// resolveImportRecords (same path as initial resolution).
/// Returns the number of newly scheduled parse tasks.
pub fn scheduleBarrelDeferredImports(client: *BundleV2, result: *ParseTask.Result.Success): !i32 {
    const file_import_records = result.ast.import_records;

    // Phase 1: Seed — eagerly record what this file requests from each target.
    // This runs for every file, even before any barrels are known. When a barrel
    // is later parsed, applyBarrelOptimization reads these pre-recorded requests
    // to decide which exports to keep. O(file's imports) per file.

    // Build a set of import_record_indices that have named_imports entries,
    // so we can detect bare imports (those with no specific export bindings).
    let named_ir_indices_stack = std.heap.stackFallback(4096, client.allocator());
    const named_ir_indices_alloc = named_ir_indices_stack.get();
    let named_ir_indices: std.AutoArrayHashMapUnmanaged(u32, void) = {};
    defer named_ir_indices.deinit(named_ir_indices_alloc);

    // In dev server mode, patchImportRecordSourceIndices skips saving source_indices
    // on import records (the dev server uses path-based identifiers instead). But
    // barrel optimization requires source_indices to seed requested_exports and to
    // BFS un-defer records. Resolve paths → source_indices here as a fallback.
    const path_to_source_index_map = if (client.transpiler.options.dev_server != null)
        client.pathToSourceIndexMap(result.ast.target)
    else
        null;

    // In HMR, ConvertESMExportsForHmr deduplicates import records by path:
    // two `import { X } from 'mod'` statements become one, and the second
    // record is marked is_unused=true. resolveImportRecords then skips those
    // records, so their path.text stays as the raw specifier while the
    // surviving record's path.text becomes the resolved absolute path.
    // named_imports entries created for the dedup'd record still point at
    // its index, so the direct path lookup below fails for those entries.
    // Build a fallback: raw specifier → surviving record's resolved path
    // text, using non-unused records in this file. See #28886.
    let dedup_fallback: DedupFallback = {};
    defer dedup_fallback.deinit(client.allocator());
    if (client.transpiler.options.dev_server != null) {
        for (ir_probe of file_import_records.slice()) {
            if (ir_probe.flags.is_unused || ir_probe.flags.is_internal) continue;
            if (ir_probe.original_path.len == 0) continue;
            if (bun.strings.eql(ir_probe.original_path, ir_probe.path.text)) continue;
            dedup_fallback.put(client.allocator(), ir_probe.original_path, ir_probe.path.text)?;
        }
    }

    let ni_iter = result.ast.named_imports.iterator();
    while (ni_entry := ni_iter.next()) {
        const ni = ni_entry.value_ptr;
        if (ni.import_record_index >= file_import_records.len) continue;
        named_ir_indices.put(named_ir_indices_alloc, ni.import_record_index, {})?;
        const ir = file_import_records.slice()[ni.import_record_index];
        // In dev server mode, source_index may not be patched — resolve via
        // path map as a read-only fallback. Do NOT write back to the import
        // record — the dev server intentionally leaves source_indices unset
        // and other code (IncrementalGraph, printer) depends on that.
        // For dedup'd HMR records (is_unused), fall back to a sibling's
        // resolved path text since the record itself still has the raw
        // specifier in path.text.
        const resolved_path_text = if (ir.flags.is_unused)
            (dedup_fallback.get(ir.path.text) ?? ir.path.text)
        else
            ir.path.text;

        let target: u32 = 0;
        if (ir.source_index.isValid()) {
            target = ir.source_index.get();
        } else if (map := path_to_source_index_map) {
            if (t := map.get(resolved_path_text)) {
                target = t;
            } else {
                continue;
            }
        } else {
            continue;
        }

        const gop = client.requested_exports.getOrPut(client.allocator(), target)?;
        if (ni.alias_is_star) {
            gop.value_ptr.* = .all;
        } else if (alias := ni.alias) {
            if (gop.found_existing) {
                switch (gop.value_ptr.*) {
                    .all => {},
                    .partial => |*p| p.put(client.allocator(), alias, {})?,
                }
            } else {
                gop.value_ptr.* = .{ partial: .{} };
                gop.value_ptr.partial.put(client.allocator(), alias, {})?;
            }
            // Persist the export request on DevServer so it survives across builds.
            if (dev := client.transpiler.options.dev_server) {
                persistBarrelExport(dev, resolved_path_text, alias);
            }
        } else if (!gop.found_existing) {
            gop.value_ptr.* = .{ partial: .{} };
        }
    }

    // Handle import records without named bindings (not in named_imports).
    // - `import "x"` (bare statement): tree-shakeable with sideEffects: false — skip.
    // - `require("x")`: synchronous, needs full module — always mark as .all.
    // - `import("x")`: returns the full module namespace at runtime — consumer
    //   can destructure or access any export. Must mark as .all. We cannot
    //   safely assume which exports will be used.
    let fir_idx: usize = 0;
    for (ir of file_import_records.slice()) {
        const idx = fir_idx;
        fir_idx += 1;

        let target: u32 = 0;
        if (ir.source_index.isValid()) {
            target = ir.source_index.get();
        } else if (map := path_to_source_index_map) {
            if (t := map.getPath(&ir.path)) {
                target = t;
            } else {
                continue;
            }
        } else {
            continue;
        }

        if (ir.flags.is_internal) continue;
        if (named_ir_indices.contains(@as(u32, @intCast(idx)))) continue;
        if (ir.flags.was_originally_bare_import) continue;
        if (ir.kind == .require) {
            const gop = client.requested_exports.getOrPut(client.allocator(), target)?;
            gop.value_ptr.* = .all;
        } else if (ir.kind == .dynamic) {
            // import() returns the full module namespace — must preserve all exports.
            const gop = client.requested_exports.getOrPut(client.allocator(), target)?;
            gop.value_ptr.* = .all;
        }
    }

    // Phase 2: BFS — un-defer barrel records that are now needed.
    // Build work queue from this file's named_imports, then propagate
    // through chains of barrels. Only runs real work when barrels exist
    // (targets with deferred records).
    let queue_stack = std.heap.stackFallback(8192, client.allocator());
    const queue_alloc = queue_stack.get();
    let queue: std.ArrayListUnmanaged(BarrelWorkItem) = {};
    defer queue.deinit(queue_alloc);

    let ni_iter2 = result.ast.named_imports.iterator();
    while (ni_entry := ni_iter2.next()) {
        const ni = ni_entry.value_ptr;
        if (ni.import_record_index >= file_import_records.len) continue;
        const ir = file_import_records.slice()[ni.import_record_index];
        const resolved_path_text = if (ir.flags.is_unused)
            (dedup_fallback.get(ir.path.text) ?? ir.path.text)
        else
            ir.path.text;

        let ir_target: u32 = 0;
        if (ir.source_index.isValid()) {
            ir_target = ir.source_index.get();
        } else if (map := path_to_source_index_map) {
            if (t := map.get(resolved_path_text)) {
                ir_target = t;
            } else {
                continue;
            }
        } else {
            continue;
        }

        if (ni.alias_is_star) {
            queue.append(queue_alloc, BarrelWorkItem{ barrel_source_index: ir_target, alias: "", is_star: true })?;
        } else if (alias := ni.alias) {
            queue.append(queue_alloc, BarrelWorkItem{ barrel_source_index: ir_target, alias: alias, is_star: false })?;
        }
    }

    // Add bare require/dynamic-import targets to BFS as star imports — both
    // always need the full namespace.
    let fir_idx2: usize = 0;
    for (ir of file_import_records.slice()) {
        const idx = fir_idx2;
        fir_idx2 += 1;

        let target: u32 = 0;
        if (ir.source_index.isValid()) {
            target = ir.source_index.get();
        } else if (map := path_to_source_index_map) {
            if (t := map.getPath(&ir.path)) {
                target = t;
            } else {
                continue;
            }
        } else {
            continue;
        }

        if (ir.flags.is_internal) continue;
        if (named_ir_indices.contains(@as(u32, @intCast(idx)))) continue;
        if (ir.flags.was_originally_bare_import) continue;
        const should_add = ir.kind == .require || ir.kind == .dynamic;
        if (should_add) {
            queue.append(queue_alloc, BarrelWorkItem{ barrel_source_index: target, alias: "", is_star: true })?;
        }
    }

    // Also seed the BFS with exports previously requested from THIS file
    // that couldn't propagate because this file wasn't parsed yet.
    // This handles the case where file A requests export "d" from file B,
    // but B hadn't been parsed when A's BFS ran, so B's export * records
    // were empty and the propagation stopped.
    const this_source_index = result.source.index.get();
    if (existing := client.requested_exports.get(this_source_index)) {
        switch (existing) {
            .all => queue.append(queue_alloc, BarrelWorkItem{ barrel_source_index: this_source_index, alias: "", is_star: true })?,
            .partial => |partial| {
                let partial_iter = partial.iterator();
                while (p_entry := partial_iter.next()) {
                    queue.append(queue_alloc, BarrelWorkItem{ barrel_source_index: this_source_index, alias: p_entry.key_ptr.*, is_star: false })?;
                }
            },
        }
    }

    if (queue.items.len == 0) return 0;

    // Items [0, initial_queue_len) are from this file's imports and were
    // already recorded in requested_exports during seeding (phase 1).
    // Skip dedup for them so un-deferral proceeds correctly.
    // Items added during BFS propagation (>= initial_queue_len) use normal
    // dedup via requested_exports to prevent cycles.
    const initial_queue_len = queue.items.len;

    let barrels_to_resolve: std.AutoArrayHashMapUnmanaged(u32, void) = {};
    let barrels_to_resolve_stack = std.heap.stackFallback(1024, client.allocator());
    const barrels_to_resolve_alloc = barrels_to_resolve_stack.get();
    defer barrels_to_resolve.deinit(barrels_to_resolve_alloc);

    let newly_scheduled: i32 = 0;
    let qi: usize = 0;
    while (qi < queue.items.len) : (qi += 1) {
        const item = queue.items[qi];
        const barrel_idx = item.barrel_source_index;

        // For BFS-propagated items (not from initial queue), use
        // requested_exports for dedup and cycle detection.
        if (qi >= initial_queue_len) {
            const gop = client.requested_exports.getOrPut(client.allocator(), barrel_idx)?;
            if (item.is_star) {
                gop.value_ptr.* = .all;
            } else if (gop.found_existing) {
                switch (gop.value_ptr.*) {
                    .all => continue,
                    .partial => |*p| {
                        const alias_gop = p.getOrPut(client.allocator(), item.alias)?;
                        if (alias_gop.found_existing) continue;
                    },
                }
            } else {
                gop.value_ptr.* = .{ partial: .{} };
                gop.value_ptr.partial.put(client.allocator(), item.alias, {})?;
            }
        }

        if (barrel_idx >= client.graph.ast.len) continue;

        // Use a helper to get barrel_ir freshly each time, since
        // resolveBarrelRecords can reallocate graph.ast and invalidate pointers.
        let barrel_ir = &client.graph.ast.slice().items(.import_records)[barrel_idx];

        if (item.is_star) {
            let rec_i: usize = 0;
            for (rec of barrel_ir.slice()) {
                if (rec.flags.is_unused && !rec.flags.is_internal) {
                    if (unDeferRecord(barrel_ir, @as(u32, @intCast(rec_i)))) {
                        barrels_to_resolve.put(barrels_to_resolve_alloc, barrel_idx, {})?;
                    }
                }
                rec_i += 1;
            }
            continue;
        }

        const alias = item.alias;
        const graph_ast_snapshot = client.graph.ast.slice();
        if (resolution := resolveBarrelExport(alias, graph_ast_snapshot.items(.named_exports)[barrel_idx], graph_ast_snapshot.items(.named_imports)[barrel_idx])) {
            if (unDeferRecord(barrel_ir, resolution.import_record_index)) {
                barrels_to_resolve.put(barrels_to_resolve_alloc, barrel_idx, {})?;
            }

            const propagate_alias = resolution.original_alias ?? alias;
            if (resolution.import_record_index < barrel_ir.len) {
                let rec = barrel_ir.slice()[resolution.import_record_index];
                if (!rec.source_index.isValid()) {
                    // Deferred record was never resolved — resolve inline now.
                    newly_scheduled += resolveBarrelRecords(client, barrel_idx, &barrels_to_resolve);
                    barrel_ir = &client.graph.ast.slice().items(.import_records)[barrel_idx];
                    rec = barrel_ir.slice()[resolution.import_record_index];
                }
                if (rec.source_index.isValid()) {
                    // When the barrel re-exports a namespace import (`import * as X; export { X }`),
                    // propagate as a star import so the target barrel loads all exports.
                    queue.append(queue_alloc, BarrelWorkItem{ barrel_source_index: rec.source_index.get(), alias: propagate_alias, is_star: resolution.alias_is_star })?;
                }
            }
        } else {
            // Name not in named re-exports — might come from export *.
            for (star_idx of graph_ast_snapshot.items(.export_star_import_records)[barrel_idx]) {
                if (star_idx >= barrel_ir.len) continue;
                if (unDeferRecord(barrel_ir, star_idx)) {
                    barrels_to_resolve.put(barrels_to_resolve_alloc, barrel_idx, {})?;
                }
                let star_rec = barrel_ir.slice()[star_idx];
                if (!star_rec.source_index.isValid()) {
                    // Deferred record was never resolved — resolve inline now.
                    newly_scheduled += resolveBarrelRecords(client, barrel_idx, &barrels_to_resolve);
                    // Re-derive pointer after resolution may have mutated slices.
                    barrel_ir = &client.graph.ast.slice().items(.import_records)[barrel_idx];
                    star_rec = barrel_ir.slice()[star_idx];
                }
                if (star_rec.source_index.isValid()) {
                    queue.append(queue_alloc, BarrelWorkItem{ barrel_source_index: star_rec.source_index.get(), alias: alias, is_star: false })?;
                }
            }
        }
    }

    // Re-resolve any remaining un-deferred records through the normal resolution path.
    while (barrels_to_resolve.count() > 0) {
        const barrel_source_index = barrels_to_resolve.keys()[0];
        newly_scheduled += resolveBarrelRecords(client, barrel_source_index, &barrels_to_resolve);
    }

    return newly_scheduled;
}

/// Persist an export name for a barrel file on the DevServer. Called during
/// seeding so that exports requested in previous builds are not lost when the
/// barrel is re-parsed in an incremental build where the requesting file is
/// not stale.
fn persistBarrelExport(dev: *bun.bake.DevServer, barrel_path: []const u8, alias: []const u8) {
    const alloc = dev.allocator();
    const outer_gop = dev.barrel_needed_exports.getOrPut(alloc, barrel_path) catch return;
    if (!outer_gop.found_existing) {
        outer_gop.key_ptr.* = alloc.dupe(u8, barrel_path) catch return;
        outer_gop.value_ptr.* = .{};
    }
    const inner_gop = outer_gop.value_ptr.getOrPut(alloc, alias) catch return;
    if (!inner_gop.found_existing) {
        inner_gop.key_ptr.* = alloc.dupe(u8, alias) catch return;
    }
}

import bun from "bun";
const BundleV2 = bun.bundle_v2.BundleV2;
const ParseTask = bun.bundle_v2.ParseTask;
const ImportRecord = bun.ImportRecord;
const Output = bun.Output;

const Index = bun.ast.Index;
const JSAst = bun.ast.BundledAst;
