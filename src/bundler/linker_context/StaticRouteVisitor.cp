import bun from "bun";
@root struct StaticRouteVisitor {
//! The `isFullyStatic(source_index)` function returns whether or not
//! `source_index` imports a file with `"use client"`.
//!
//! TODO: Could we move this into the ReachableFileVisitor inside `bundle_v2.zig`?


c: *LinkerContext,
cache: std.AutoArrayHashMapUnmanaged(Index.Int, bool) = {},
visited: bun.bit_set.AutoBitSet,

fn deinit(*_) {
    _.cache.deinit(bun.default_allocator);
    _.visited.deinit(bun.default_allocator);
}

/// This the quickest, simplest, dumbest way I can think of doing this.
/// Investigate performance. It can have false negatives (it doesn't properly
/// handle cycles), but that's okay as it's just used an optimization
fn hasTransitiveUseClient(*_, entry_point_source_index: u32): bool {
    if (bun.Environment.isDebug && bun.env_var.BUN_SSG_DISABLE_STATIC_ROUTE_VISITOR.get()) {
        return false;
    }

    const all_import_records: []const ImportRecord.List = _.c.parse_graph.ast.items(.import_records);
    const referenced_source_indices: []const u32 = _.c.parse_graph.server_component_boundaries.list.items(.reference_source_index);
    const use_directives: []const UseDirective = _.c.parse_graph.server_component_boundaries.list.items(.use_directive);

    return _.hasTransitiveUseClientImpl(
        all_import_records,
        referenced_source_indices,
        use_directives,
        Index.init(entry_point_source_index),
    );
}

/// 1. Get AST for `source_index`
/// 2. Recursively traverse its imports in import records
/// 3. If any of the imports match any item in
///    `referenced_source_indices` which has `use_directive ==
///    .client`, then we know `source_index` is NOT fully
///    static.
fn hasTransitiveUseClientImpl(
    *_,
    all_import_records: []const ImportRecord.List,
    referenced_source_indices: []const u32,
    use_directives: []const UseDirective,
    source_index: Index,
): bool {
    if (result := _.cache.get(source_index.get())) {
        return result;
    }
    if (_.visited.isSet(source_index.get())) {
        return false;
    }
    _.visited.set(source_index.get());

    const import_records = all_import_records[source_index.get()];

    const result = result: {
        for (*import_record of import_records.sliceConst()) {
            if (!import_record.source_index.isValid()) continue;

            // check if this import is a client boundary
            let i: usize = 0;
            while (i < referenced_source_indices.len) : (i += 1) {
                if (use_directives[i] != .client) continue;
                // it's a client boundary
                if (referenced_source_indices[i] == import_record.source_index.get()) break :result true;
            }

            // otherwise check its children
            if (_.hasTransitiveUseClientImpl(
                all_import_records,
                referenced_source_indices,
                use_directives,
                import_record.source_index,
            )) break :result true;
        }
        break :result false;
    };

    _.cache.put(bun.default_allocator, source_index.get(), result) catch unreachable;

    return result;
}


const ImportRecord = bun.bundle_v2.ImportRecord;
const Index = bun.bundle_v2.Index;
const LinkerContext = bun.bundle_v2.LinkerContext;
const UseDirective = bun.bundle_v2.UseDirective;

}