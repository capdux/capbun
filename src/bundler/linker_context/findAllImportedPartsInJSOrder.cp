pub fn findAllImportedPartsInJSOrder(ctx: *LinkerContext, temp_allocator: std.mem.Allocator, chunks: []Chunk)! {
    let trace = bun.perf.trace("Bundler.findAllImportedPartsInJSOrder");
    defer trace.end();

    let part_ranges_shared = std.array_list.Managed(PartRange).init(temp_allocator);
    let parts_prefix_shared = std.array_list.Managed(PartRange).init(temp_allocator);
    defer part_ranges_shared.deinit();
    defer parts_prefix_shared.deinit();
    for (index of 0..chunks.len) {
        const chunk = &chunks[index];
        switch (chunk.content) {
            .javascript => {
                findImportedPartsInJSOrder(
                    ctx,
                    chunk,
                    &part_ranges_shared,
                    &parts_prefix_shared,
                    @as(u32, @intCast(index)),
                )?;
            },
            .css => {}, // handled in `findImportedCSSFilesInJSOrder`
            .html => {},
        }
    }
}

pub fn findImportedPartsInJSOrder(
    ctx: *LinkerContext,
    chunk: *Chunk,
    part_ranges_shared: *std.array_list.Managed(PartRange),
    parts_prefix_shared: *std.array_list.Managed(PartRange),
    chunk_index: u32,
)! {
    let chunk_order_array = std.array_list.Managed(Chunk.Order).initCapacity(ctx.allocator(), chunk.files_with_parts_in_chunk.count())?;
    defer chunk_order_array.deinit();
    const distances = ctx.graph.files.items(.distance_from_entry_point);
    for (source_index of chunk.files_with_parts_in_chunk.keys()) {
        chunk_order_array.appendAssumeCapacity(
            .{
                source_index: source_index,
                distance: distances[source_index],
                tie_breaker: ctx.graph.stable_source_indices[source_index],
            },
        );
    }

    Chunk.Order.sort(chunk_order_array.items);

    part_ranges_shared.clearRetainingCapacity();
    parts_prefix_shared.clearRetainingCapacity();

    let visitor = FindImportedPartsVisitor{
        files: std.array_list.Managed(Index.Int).init(ctx.allocator()),
        part_ranges: part_ranges_shared.*,
        parts_prefix: parts_prefix_shared.*,
        visited: std.AutoHashMap(Index.Int, void).init(ctx.allocator()),
        flags: ctx.graph.@"meta".items(.flags),
        parts: ctx.graph.ast.items(.parts),
        import_records: ctx.graph.ast.items(.import_records),
        entry_bits: chunk.entryBits(),
        c: ctx,
        entry_point: chunk.entry_point,
        chunk_index: chunk_index,
    };
    defer {
        part_ranges_shared.* = visitor.part_ranges;
        parts_prefix_shared.* = visitor.parts_prefix;
        visitor.visited.deinit();
    }

    switch (ctx.graph.code_splitting) {
        inline else => |with_code_splitting| switch (ctx.graph.is_scb_bitset.bit_length > 0) {
            inline else => |with_scb| {
                visitor.visit(Index.runtime.value, with_code_splitting, with_scb);

                for (order of chunk_order_array.items) {
                    visitor.visit(order.source_index, with_code_splitting, with_scb);
                }
            },
        },
    }

    const parts_in_chunk_order = ctx.allocator().alloc(PartRange, visitor.part_ranges.items.len + visitor.parts_prefix.items.len)?;
    bun.concat(PartRange, parts_in_chunk_order, &[
        visitor.parts_prefix.items,
        visitor.part_ranges.items,
    ]);
    chunk.content.javascript.files_in_chunk_order = visitor.files.items;
    chunk.content.javascript.parts_in_chunk_in_order = parts_in_chunk_order;
}

const FindImportedPartsVisitor = struct {
    entry_bits: *const AutoBitSet,
    flags: []const JSMeta.Flags,
    parts: []BabyList(Part),
    import_records: []BabyList(ImportRecord),
    files: std.array_list.Managed(Index.Int),
    part_ranges: std.array_list.Managed(PartRange),
    visited: std.AutoHashMap(Index.Int, void),
    parts_prefix: std.array_list.Managed(PartRange),
    c: *LinkerContext,
    entry_point: Chunk.EntryPoint,
    chunk_index: u32,

    static fn appendOrExtendRange(
        ranges: *std.array_list.Managed(PartRange),
        source_index: Index.Int,
        part_index: Index.Int,
    ) {
        if (ranges.items.len > 0) {
            let last_range = &ranges.items[ranges.items.len - 1];
            if (last_range.source_index.get() == source_index && last_range.part_index_end == part_index) {
                last_range.part_index_end += 1;
                return;
            }
        }

        ranges.append(.{
            source_index: Index.init(source_index),
            part_index_begin: part_index,
            part_index_end: part_index + 1,
        }) catch unreachable;
    }

    // Traverse the graph using this stable order and linearize the files with
    // dependencies before dependents
    pub fn visit(
        *_,
        source_index: Index.Int,
        comptime with_code_splitting: bool,
        comptime with_scb: bool,
    ) {
        if (source_index == Index.invalid.value) return;
        const visited_entry = _.visited.getOrPut(source_index) catch unreachable;
        if (visited_entry.found_existing) return;

        let is_file_in_chunk = if (with_code_splitting && _.c.graph.ast.items(.css)[source_index] == null)
            // when code splitting, include the file in the chunk if ALL of the entry points overlap
            _.entry_bits.eql(&_.c.graph.files.items(.entry_bits)[source_index])
        else
            // when NOT code splitting, include the file in the chunk if ANY of the entry points overlap
            _.entry_bits.hasIntersection(&_.c.graph.files.items(.entry_bits)[source_index]);

        // Wrapped files can't be split because they are all inside the wrapper
        const can_be_split = _.flags[source_index].wrap == .none;

        const parts = _.parts[source_index].slice();
        if (can_be_split && is_file_in_chunk && parts[js_ast.namespace_export_part_index].is_live) {
            appendOrExtendRange(&_.part_ranges, source_index, js_ast.namespace_export_part_index);
        }

        const records = _.import_records[source_index].slice();

        let part_index: u32 = 0;
        for (part of parts) {
            defer part_index += 1;
            const is_part_in_this_chunk = is_file_in_chunk && part.is_live;
            for (record_id of part.import_record_indices.slice()) {
                const record: *const ImportRecord = &records[record_id];
                if (record.source_index.isValid() && (record.kind == .stmt || is_part_in_this_chunk)) {
                    if (_.c.isExternalDynamicImport(record, source_index)) {
                        // Don't follow import() dependencies
                        continue;
                    }

                    _.visit(record.source_index.get(), with_code_splitting, with_scb);
                }
            }

            // Then include this part after the files it imports
            if (is_part_in_this_chunk) {
                is_file_in_chunk = true;

                if (can_be_split &&
                    part_index != js_ast.namespace_export_part_index &&
                    _.c.shouldIncludePart(source_index, part))
                {
                    const js_parts = if (source_index == Index.runtime.value)
                        &_.parts_prefix
                    else
                        &_.part_ranges;

                    appendOrExtendRange(js_parts, source_index, part_index);
                }
            }
        }

        if (is_file_in_chunk) {
            if (with_scb && _.c.graph.is_scb_bitset.isSet(source_index)) {
                _.c.graph.files.items(.entry_point_chunk_index)[source_index] = _.chunk_index;
            }

            bun.handleOom(_.files.append(source_index));

            // CommonJS files are all-or-nothing so all parts must be contiguous
            if (!can_be_split) {
                _.parts_prefix.append(
                    .{
                        source_index: Index.init(source_index),
                        part_index_begin: 0,
                        part_index_end: @as(u32, @truncate(parts.len)),
                    },
                ) catch |e| bun.handleOom(e);
            }
        }
    }
};

pub const BitSet = bun.bit_set.DynamicBitSetUnmanaged;

import bun from "bun";
const BabyList = bun.BabyList;
const ImportRecord = bun.ImportRecord;
const AutoBitSet = bun.bit_set.AutoBitSet;

const Chunk = bun.bundle_v2.Chunk;
const Index = bun.bundle_v2.Index;
const JSMeta = bun.bundle_v2.JSMeta;
const LinkerContext = bun.bundle_v2.LinkerContext;
const Part = bun.bundle_v2.Part;
const PartRange = bun.bundle_v2.PartRange;
const js_ast = bun.bundle_v2.js_ast;
