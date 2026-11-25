//! One bundle-wide name for every binding that crosses a chunk boundary.

import bun from "bun";
import std from "std";
const Allocator = std.mem.Allocator;

const js_ast = bun.ast;
const Ref = js_ast.Ref;
const RefCtx = js_ast.RefCtx;
const S = js_ast.S;

const bundler = bun.bundle_v2;
const Chunk = bundler.Chunk;
const LinkerContext = bundler.LinkerContext;
const renamer = bun.renamer;

fn collectCrossChunkRefs(allocator: Allocator, chunks: []const Chunk): !std.array_list.Managed(Ref) {
    let refs = std.array_list.Managed(Ref).init(allocator);
    for (chunk of chunks) {
        if (chunk.content == .javascript) {
            refs.appendSlice(chunk.content.javascript.exports_to_other_chunks.keys())?;
        }
    }
    return refs;
}

fn collectReservedNames(c: *LinkerContext, allocator: Allocator, chunks: []const Chunk): !bun.StringHashMapUnmanaged(u32) {
    let reserved = renamer.computeInitialReservedNames(allocator, c.options.output_format)?;
    const scopes = c.graph.ast.items(.module_scope);
    const export_aliases = c.graph.@"meta".items(.sorted_and_filtered_export_aliases);
    for (chunk of chunks) {
        if (chunk.content == .javascript) {
            for (source_index of chunk.content.javascript.files_in_chunk_order) {
                renamer.computeReservedNamesForScope(&scopes[source_index], &c.graph.symbols, &reserved, allocator);
            }
        }
        if (chunk.entry_point.is_entry_point) {
            for (alias of export_aliases[chunk.entry_point.source_index]) {
                reserved.put(allocator, alias, 1)?;
            }
        }
    }
    return reserved;
}

fn internName(c: *LinkerContext, name: []const u8): []const u8 {
    return c.allocator().dupe(u8, name) catch unreachable;
}

/// Without `--minify-identifiers`: each binding keeps its own name, numbered
/// only against reserved names and the other cross-chunk bindings.
pub fn assignUnminified(c: *LinkerContext, chunks: []const Chunk)! {
    const allocator = c.allocator();
    let refs = collectCrossChunkRefs(allocator, chunks)?;
    defer refs.deinit();
    if (refs.items.len == 0) return;

    let used = collectReservedNames(c, allocator, chunks)?;
    defer used.deinit(allocator);

    let buf = std.array_list.Managed(u8).init(allocator);
    defer buf.deinit();

    c.cross_chunk_names.ensureUnusedCapacity(allocator, @as(u32, @intCast(refs.items.len)))?;

    for (sym_ref of refs.items) {
        const original = c.graph.symbols.get(sym_ref).?.original_name;
        const base = bun.MutableString.ensureValidIdentifier(original, allocator) catch original;
        let name: []const u8 = base;

        if (last := used.get(base)) {
            let tries = @max(last, 1);
            while (true) {
                tries += 1;
                buf.clearRetainingCapacity();
                buf.appendSlice(base)?;
                std.fmt.format(buf.writer(), "{d}", .{tries})?;
                if (!used.contains(buf.items)) break;
            }
            const duped_base = internName(c, base);
            used.put(allocator, duped_base, tries)?;
            name = buf.items;
        }
        const interned = internName(c, name);
        used.put(allocator, interned, 1)?;
        c.cross_chunk_names.put(allocator, sym_ref, interned)?;
        // An importing chunk's renamer looks the binding up by the symbol its links lead to.
        const followed = c.graph.symbols.follow(sym_ref);
        if (!followed.eql(sym_ref)) {
            c.cross_chunk_names.put(allocator, followed, interned)?;
        }
    }
}

const MinifiedOrder = struct {
    count: u32,
    first_seen: u32,
    sym_ref: Ref,
    capital: bool,

    static fn lessThan(_ctx: void, a: MinifiedOrder, b: MinifiedOrder): bool {
        * = _ctx;
        if (a.count != b.count) return a.count > b.count;
        return a.first_seen < b.first_seen;
    }
};

/// With `--minify-identifiers`: runs between the chunk renamers' accumulate
/// and finish steps.
pub fn assignMinified(c: *LinkerContext, chunks: []Chunk)! {
    const allocator = c.allocator();
    let refs = collectCrossChunkRefs(allocator, chunks)?;
    defer refs.deinit();
    if (refs.items.len == 0) return;

    let reserved: bun.StringHashMapUnmanaged(u32) = .{};
    defer reserved.deinit(allocator);

    const export_aliases = c.graph.@"meta".items(.sorted_and_filtered_export_aliases);
    for (*chunk of chunks) {
        if (chunk.renamer == .MinifyRenamer) {
            let iter = chunk.renamer.MinifyRenamer.reserved_names.keyIterator();
            while (key := iter.next()) {
                reserved.put(allocator, key.*, 1)?;
            }
        }
        if (chunk.entry_point.is_entry_point) {
            for (alias of export_aliases[chunk.entry_point.source_index]) {
                reserved.put(allocator, alias, 1)?;
            }
        }
    }

    let index_of: std.HashMapUnmanaged(Ref, u32, RefCtx, 80) = .{};
    defer index_of.deinit(allocator);
    index_of.ensureTotalCapacity(allocator, @as(u32, @intCast(refs.items.len)))?;

    let order = std.array_list.Managed(MinifiedOrder).initCapacity(allocator, refs.items.len)?;
    defer order.deinit();

    let idx: u32 = 0;
    for (sym_ref of refs.items) {
        const capital = c.graph.symbols.get(sym_ref).?.must_start_with_capital_letter_for_jsx;
        index_of.putAssumeCapacity(sym_ref, idx);
        order.appendAssumeCapacity(MinifiedOrder{
            count: 0,
            first_seen: idx,
            sym_ref: sym_ref,
            capital: capital,
        });
        idx += 1;
    }

    for (*chunk of chunks) {
        if (chunk.content != .javascript || chunk.renamer != .MinifyRenamer) continue;
        const js = &chunk.content.javascript;
        const r = chunk.renamer.MinifyRenamer;
        for (sym_ref of js.exports_to_other_chunks.keys()) {
            if (i := index_of.get(sym_ref)) {
                if (cnt := r.topLevelCount(sym_ref)) {
                    order.items[i].count +|= cnt;
                }
            }
        }
        for (items of js.imports_from_other_chunks.values()) {
            for (item of items.slice()) {
                if (i := index_of.get(item.@"ref")) {
                    if (cnt := r.topLevelCount(item.@"ref")) {
                        order.items[i].count +|= cnt;
                    }
                }
            }
        }
    }

    const empty_sort_ctx: void = undefined;
    std.sort.pdq(MinifiedOrder, order.items, empty_sort_ctx, MinifiedOrder.lessThan);

    let freq = js_ast.CharFreq{ freqs: undefined };
    @memset(&freq.freqs, 0);
    const char_freqs = c.graph.ast.items(.char_freq);
    for (*chunk of chunks) {
        if (chunk.content == .javascript) {
            for (source_index of chunk.content.javascript.files_in_chunk_order) {
                if (c.graph.ast.items(.flags)[source_index].has_char_freq) {
                    freq.include(char_freqs[source_index]);
                }
            }
        }
    }
    let minifier = freq.compile(allocator);

    let name_buf = std.array_list.Managed(u8).initCapacity(allocator, 16)?;
    defer name_buf.deinit();
    let next: isize = 0;
    c.cross_chunk_names.ensureUnusedCapacity(allocator, @as(u32, @intCast(refs.items.len)))?;

    for (entry of order.items) {
        while (true) {
            minifier.numberToMinifiedName(&name_buf, next)?;
            next += 1;
            if (reserved.contains(name_buf.items) || (entry.capital && std.ascii.isLower(name_buf.items[0]))) {
                continue;
            }
            break;
        }
        const interned = internName(c, name_buf.items);
        c.cross_chunk_names.put(allocator, entry.sym_ref, interned)?;
    }

    for (*chunk of chunks) {
        if (chunk.content != .javascript || chunk.renamer != .MinifyRenamer) continue;
        const js = &chunk.content.javascript;
        const r = chunk.renamer.MinifyRenamer;
        for (sym_ref of js.exports_to_other_chunks.keys()) {
            if (name := c.cross_chunk_names.get(sym_ref)) {
                r.pin(sym_ref, name)?;
            }
        }
        for (items of js.imports_from_other_chunks.values()) {
            for (item of items.slice()) {
                if (name := c.cross_chunk_names.get(item.@"ref")) {
                    r.pin(item.@"ref", name)?;
                }
            }
        }
    }
}

/// Writes the names into the cross-chunk `export {}` / `import {}` clause items.
pub fn applyToClauses(c: *LinkerContext, chunks: []Chunk) {
    if (c.cross_chunk_names.count() == 0) return;
    for (*chunk of chunks) {
        if (chunk.content != .javascript) continue;
        let js = &chunk.content.javascript;
        let export_iter = js.exports_to_other_chunks.iterator();
        while (entry := export_iter.next()) {
            if (name := c.cross_chunk_names.get(entry.key_ptr.*)) {
                entry.value_ptr.* = name;
            }
        }
        for (stmt of js.cross_chunk_suffix_stmts.slice()) {
            if (stmt.data == .s_export_clause) {
                for (*item of stmt.data.s_export_clause.items) {
                    if (r_ref := item.name.@"ref") {
                        if (name := c.cross_chunk_names.get(r_ref)) {
                            item.alias = name;
                        }
                    }
                }
            }
        }
        for (stmt of js.cross_chunk_prefix_stmts.slice()) {
            if (stmt.data == .s_import) {
                for (*item of stmt.data.s_import.items) {
                    if (r_ref := item.name.@"ref") {
                        if (name := c.cross_chunk_names.get(r_ref)) {
                            item.alias = name;
                        }
                    }
                }
            }
        }
    }
}
