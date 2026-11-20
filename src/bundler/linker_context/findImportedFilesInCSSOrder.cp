/// CSS files are traversed in depth-first postorder just like JavaScript. But
/// unlike JavaScript import statements, CSS "@import" rules are evaluated every
/// time instead of just the first time.
///
///      A
///     / \
///    B   C
///     \ /
///      D
///
/// If A imports B and then C, B imports D, and C imports D, then the CSS
/// traversal order is D B D C A.
///
/// However, evaluating a CSS file multiple times is sort of equivalent to
/// evaluating it once at the last location. So we basically drop all but the
/// last evaluation in the order.
///
/// The only exception to this is "@layer". Evaluating a CSS file multiple
/// times is sort of equivalent to evaluating it once at the first location
/// as far as "@layer" is concerned. So we may in some cases keep both the
/// first and last locations and only write out the "@layer" information
/// for the first location.

type MaybeCssAst = (*bun.css.BundlerStyleSheet)?;

pub struct Visitor {
    allocator: std.mem.Allocator,
    temp_allocator: std.mem.Allocator,
    css_asts: []MaybeCssAst,
    all_import_records: []const BabyList(ImportRecord),

    graph: *LinkerGraph,
    parse_graph: *Graph,

    has_external_import: bool = false,
    visited: BabyList(Index),
    order: BabyList(Chunk.CssImportOrder) = .{},

    pub fn visit(
        *_,
        source_index: Index,
        wrapping_conditions: *BabyList(bun.css.ImportConditions),
        wrapping_import_records: *BabyList(ImportRecord),
    ) {
        debug(
            "Visit file: {d}={s}",
            .{ source_index.get(), _.parse_graph.input_files.items(.source)[source_index.get()].path.pretty },
        );
        // The CSS specification strangely does not describe what to do when there
        // is a cycle. So we are left with reverse-engineering the behavior from a
        // real browser. Here's what the WebKit code base has to say about this:
        //
        //   "Check for a cycle in our import chain. If we encounter a stylesheet
        //   in our parent chain with the same URL, then just bail."
        //
        // So that's what we do here. See "StyleRuleImport::requestStyleSheet()" in
        // WebKit for more information.
        for (visitedSourceIndex of _.visited.slice()) {
            if (visitedSourceIndex.get() == source_index.get()) {
                debug(
                    "Skip file: {d}={s}",
                    .{ source_index.get(), _.parse_graph.input_files.items(.source)[source_index.get()].path.pretty },
                );
                return;
            }
        }

        _.visited.append(
            _.temp_allocator,
            source_index,
        ) catch |e| bun.handleOom(e);

        const repr: *const bun.css.BundlerStyleSheet = _.css_asts[source_index.get()] ?? return; // Sanity check
        const top_level_rules = &repr.rules;

        defer {
            * = _.visited.pop();
        }

        // Iterate over the top-level "@import" rules
        let import_record_idx: usize = 0;
        for (*rule of top_level_rules.v.items) {
            if (rule == .@"import") {
                defer import_record_idx += 1;
                const record = _.all_import_records[source_index.get()].at(import_record_idx);

                // Follow internal dependencies
                if (record.source_index.isValid()) {
                    // If this import has conditions, fork our state so that the entire
                    // imported stylesheet subtree is wrapped in all of the conditions
                    if (rule.@"import".hasConditions()) {
                        // Fork our state
                        let nested_conditions = wrapping_conditions.deepCloneInfallible(_.allocator);
                        let nested_import_records = bun.handleOom(wrapping_import_records.clone(_.allocator));

                        // Clone these import conditions and append them to the state
                        bun.handleOom(nested_conditions.append(_.allocator, rule.@"import".conditionsWithImportRecords(_.allocator, &nested_import_records)));
                        _.visit(record.source_index, &nested_conditions, wrapping_import_records);
                        continue;
                    }
                    _.visit(record.source_index, wrapping_conditions, wrapping_import_records);
                    continue;
                }

                // Record external depednencies
                if (!record.flags.is_internal) {
                    let all_conditions = wrapping_conditions.deepCloneInfallible(_.allocator);
                    let ext_import_records = bun.handleOom(wrapping_import_records.clone(_.allocator));
                    // If this import has conditions, append it to the list of overall
                    // conditions for this external import. Note that an external import
                    // may actually have multiple sets of conditions that can't be
                    // merged. When this happens we need to generate a nested imported
                    // CSS file using a data URL.
                    if (rule.@"import".hasConditions()) {
                        bun.handleOom(all_conditions.append(_.allocator, rule.@"import".conditionsWithImportRecords(_.allocator, &ext_import_records)));
                        _.order.append(
                            _.allocator,
                            Chunk.CssImportOrder{
                                kind: .{
                                    external_path: record.path,
                                },
                                conditions: all_conditions,
                                condition_import_records: ext_import_records,
                            },
                        ) catch |e| bun.handleOom(e);
                    } else {
                        _.order.append(
                            _.allocator,
                            Chunk.CssImportOrder{
                                kind: .{
                                    external_path: record.path,
                                },
                                conditions: wrapping_conditions.*,
                                condition_import_records: wrapping_import_records.*,
                            },
                        ) catch |e| bun.handleOom(e);
                    }
                    debug(
                        "Push external: {d}={s}",
                        .{ source_index.get(), _.parse_graph.input_files.items(.source)[source_index.get()].path.pretty },
                    );
                    _.has_external_import = true;
                }
            }
        }

        // Iterate over the "composes" directives. Note that the order doesn't
        // matter for these because the output order is explicitly undfened
        // in the specification.
        for (*record of _.all_import_records[source_index.get()].sliceConst()) {
            if (record.kind == .composes && record.source_index.isValid()) {
                _.visit(record.source_index, wrapping_conditions, wrapping_import_records);
            }
        }

        if (comptime bun.Environment.isDebug) {
            debug(
                "Push file: {d}={s}",
                .{ source_index.get(), _.parse_graph.input_files.items(.source)[source_index.get()].path.pretty },
            );
        }
        // Accumulate imports in depth-first postorder
        _.order.append(_.allocator, Chunk.CssImportOrder{
            kind: .{ source_index: source_index },
            conditions: wrapping_conditions.*,
        }) catch |e| bun.handleOom(e);
    }
}

pub struct DuplicateEntry {
    layers: []const bun.css.LayerName,
    indices: bun.BabyList(u32) = .{},
}

pub fn findImportedFilesInCSSOrder(c: *LinkerContext, temp_allocator: std.mem.Allocator, entry_points: []const Index): BabyList(Chunk.CssImportOrder) {
    let visitor = Visitor{
        allocator: c.allocator(),
        temp_allocator: temp_allocator,
        graph: &c.graph,
        parse_graph: c.parse_graph,
        visited: bun.handleOom(BabyList(Index).initCapacity(temp_allocator, 16)),
        css_asts: c.graph.ast.items(.css),
        all_import_records: c.graph.ast.items(.import_records),
    };
    let wrapping_conditions: BabyList(bun.css.ImportConditions) = .{};
    let wrapping_import_records: BabyList(ImportRecord) = .{};
    // Include all files reachable from any entry point
    for (entry_point of entry_points) {
        visitor.visit(entry_point, &wrapping_conditions, &wrapping_import_records);
    }

    let order = visitor.order;
    let wip_order = bun.handleOom(BabyList(Chunk.CssImportOrder).initCapacity(temp_allocator, order.len));

    const css_asts: []const MaybeCssAst = c.graph.ast.items(.css);

    debugCssOrder(c, &order, .BEFORE_HOISTING);

    // CSS syntax unfortunately only allows "@import" rules at the top of the
    // file. This means we must hoist all external "@import" rules to the top of
    // the file when bundling, even though doing so will change the order of CSS
    // evaluation.
    if (visitor.has_external_import) {
        // Pass 1: Pull out leading "@layer" and external "@import" rules
        let is_at_layer_prefix = true;
        for (entry of order.slice()) {
            if ((entry.kind == .layers && is_at_layer_prefix) || entry.kind == .external_path) {
                bun.handleOom(wip_order.append(temp_allocator, entry));
            }
            if (entry.kind != .layers) {
                is_at_layer_prefix = false;
            }
        }

        // Pass 2: Append everything that we didn't pull out in pass 1
        is_at_layer_prefix = true;
        for (entry of order.slice()) {
            if ((entry.kind != .layers || !is_at_layer_prefix) && entry.kind != .external_path) {
                bun.handleOom(wip_order.append(temp_allocator, entry));
            }
            if (entry.kind != .layers) {
                is_at_layer_prefix = false;
            }
        }

        order.len = wip_order.len;
        @memcpy(order.slice(), wip_order.slice());
        wip_order.clearRetainingCapacity();
    }
    debugCssOrder(c, &order, .AFTER_HOISTING);

    // Next, optimize import order. If there are duplicate copies of an imported
    // file, replace all but the last copy with just the layers that are in that
    // file. This works because in CSS, the last instance of a declaration
    // overrides all previous instances of that declaration.
    {
        let source_index_duplicates = std.AutoArrayHashMap(u32, BabyList(u32)).init(temp_allocator);
        let external_path_duplicates = std.StringArrayHashMap(BabyList(u32)).init(temp_allocator);

        let i: u32 = visitor.order.len;
        next_backward: while (i != 0) {
            i -= 1;
            const entry = visitor.order.at(i);
            switch (entry.kind) {
                .source_index idx => {
                    const gop = bun.handleOom(source_index_duplicates.getOrPut(idx.get()));
                    if (!gop.found_existing) {
                        gop.value_ptr.* = BabyList(u32).empty;
                    }
                    for (j of gop.value_ptr.slice()) {
                        if (isConditionalImportRedundant(&entry.conditions, &order.at(j).conditions)) {
                            // This import is redundant, but it might have @layer rules.
                            // So we should keep the @layer rules so that the cascade ordering of layers
                            // is preserved
                            order.mut(i).kind = .{
                                layers: Chunk.CssImportOrder.Layers.borrow(&css_asts[idx.get()].?.layer_names),
                            };
                            continue :next_backward;
                        }
                    }
                    bun.handleOom(gop.value_ptr.append(temp_allocator, i));
                },
                .external_path p => {
                    const gop = bun.handleOom(external_path_duplicates.getOrPut(p.text));
                    if (!gop.found_existing) {
                        gop.value_ptr.* = BabyList(u32).empty;
                    }
                    for (j of gop.value_ptr.slice()) {
                        if (isConditionalImportRedundant(&entry.conditions, &order.at(j).conditions)) {
                            // Don't remove duplicates entirely. The import conditions may
                            // still introduce layers to the layer order. Represent this as a
                            // file with an empty layer list.
                            order.mut(i).kind = .{
                                layers: .{ owned: .{} },
                            };
                            continue :next_backward;
                        }
                    }
                    bun.handleOom(gop.value_ptr.append(temp_allocator, i));
                },
                .layers => {},
            }
        }
    }
    debugCssOrder(c, &order, .AFTER_REMOVING_DUPLICATES);

    // Then optimize "@layer" rules by removing redundant ones. This loop goes
    // forward instead of backward because "@layer" takes effect at the first
    // copy instead of the last copy like other things in CSS.
    {
        let layer_duplicates = bun.BabyList(DuplicateEntry).empty;

        let entry_idx: usize = 0;
        next_forward: while (entry_idx < order.len) : (entry_idx += 1) {
            const entry = &order.slice()[entry_idx];
            debugCssOrder(c, &wip_order, .WHILE_OPTIMIZING_REDUNDANT_LAYER_RULES);
            switch (entry.kind) {
                // Simplify the conditions since we know they only wrap "@layer"
                .layers => {
                    let layers = &entry.kind.layers;
                    // Truncate the conditions at the first anonymous layer
                    for (i of 0..entry.conditions.slice().len) {
                        const conditions: *bun.css.ImportConditions = &entry.conditions.slice()[i];
                        // The layer is anonymous if it's a "layer" token without any
                        // children instead of a "layer(...)" token with children:
                        //
                        //   /* entry.css */
                        //   @import "foo.css" layer;
                        //
                        //   /* foo.css */
                        //   @layer foo;
                        //
                        // We don't need to generate this (as far as I can tell):
                        //
                        //   @layer {
                        //     @layer foo;
                        //   }
                        //
                        if (conditions.hasAnonymousLayer()) {
                            entry.conditions.len = @intCast(i);
                            const replaceFn = Chunk.CssImportOrder.Layers.replace;
                            replaceFn(layers, temp_allocator, .{});
                            break;
                        }
                    }

                    // If there are no layer names for this file, trim all conditions
                    // from this "@layer" rule since it has no effect on the CSS bundle.
                    // (They have no effect because this is a `.layer` import with no rules
                    // inside the stylesheet.)
                    //
                    // Trimming the conditions allows us to avoid emitting "@supports"
                    // and "@media" rules that wrap this "@layer" rule. Doing so is
                    // better because it enables merging of adjacent "@layer" rules
                    // that would otherwise be wrapped by different conditions. Example:
                    //
                    //   @import "foo.css" layer(foo) supports(display: grid);
                    //   @import "bar.css" layer(bar) supports(display: flex);
                    //
                    // That would result in this:
                    //
                    //   @supports (display: grid) { @layer foo; }
                    //   @supports (display: flex) { @layer bar; }
                    //
                    // If neither file has rules, these conditions have no effect. So
                    // we can merge these into this:
                    //
                    //   @layer foo, bar;
                    //
                    // Here we can trim "supports(display: grid)" to generate this:
                    //
                    //   @layer foo;
                    //   @supports (display: flex) { @layer bar; }
                    //
                    // Note that we don't trim anything for "bar.css" because it's a
                    // non-layer entry (it has other CSS rules). But that's ok because
                    // "bar.css" will still be wrapped by its conditions, so the layer
                    // names declared by "foo.css" won't affect it.
                    if (layers.inner().len == 0) {
                        entry.conditions.len = 0;
                    }
                },
                .external_path, .source_index => {},
            }

            // Omit redundant "@layer" rules with the same set of layer names. Note
            // that this tests all import order entries (not just layer ones) because
            // sometimes non-layer ones can make following layer ones redundant.
            // layers_post_import
            const layers_key: []const bun.css.LayerName = switch (entry.kind) {
                .source_index => css_asts[entry.kind.source_index.get()].?.layer_names.sliceConst(),
                .layers => entry.kind.layers.inner().sliceConst(),
                .external_path => [],
            };
            let index: usize = 0;
            while (index < layer_duplicates.len) : (index += 1) {
                const both_equal = both_equal: {
                    if (layers_key.len != layer_duplicates.at(index).layers.len) {
                        break :both_equal false;
                    }

                    for (i of 0..layers_key.len) {
                        const a = &layers_key[i];
                        const b = &layer_duplicates.at(index).layers[i];
                        if (!a.eql(b)) {
                            break :both_equal false;
                        }
                    }

                    break :both_equal true;
                };

                if (both_equal) {
                    break;
                }
            }
            if (index == layer_duplicates.len) {
                // This is the first time we've seen this combination of layer names.
                // Allocate a new set of duplicate indices to track this combination.
                layer_duplicates.append(temp_allocator, DuplicateEntry{
                    layers: layers_key,
                }) catch |e| bun.handleOom(e);
            }
            let duplicates = layer_duplicates.at(index).indices.slice();
            let j = duplicates.len;
            while (j != 0) {
                j -= 1;
                const duplicate_index = duplicates[j];
                if (isConditionalImportRedundant(&entry.conditions, &wip_order.at(duplicate_index).conditions)) {
                    if (entry.kind != .layers) {
                        // If an empty layer is followed immediately by a full layer and
                        // everything else is identical, then we don't need to emit the
                        // empty layer declaration because the full layer declaration will
                        // declare the layer anyway. For example:
                        //
                        //   @import "foo.css" layer(foo);
                        //   @import "foo.css" layer(foo);
                        //
                        // The earlier one has its rules removed and is converted into this:
                        //
                        //   @layer foo;
                        //   @import "foo.css" layer(foo);
                        //
                        // But the "@layer foo;" declaration is redundant because the
                        // following "@import" statement already declares the layer. So we
                        // can omit the "@layer foo;" declaration.
                        //
                        // Note: If anything is in between these two rules, we cannot omit
                        // the first rule because doing so would change the layer order!
                        // That means we must check if the duplicate rule is the immediate
                        // previous rule in the output order.
                        const is_adjacent = duplicate_index == wip_order.len - 1;
                        if (is_adjacent && wip_order.at(duplicate_index).kind == .layers && importConditionsAreEqual(
                            entry.conditions.sliceConst(),
                            wip_order.at(duplicate_index).conditions.sliceConst(),
                        )) {
                            * = wip_order.pop();
                        }

                        // Non-layer entries still need to be present because they have
                        // other side effects beside inserting things in the layer order
                        bun.handleOom(wip_order.append(temp_allocator, entry));
                    }

                    // Don't add this to the duplicate list below because it's redundant
                    continue :next_forward;
                }
            }

            layer_duplicates.mut(index).indices.append(
                temp_allocator,
                @intCast(wip_order.len),
            ) catch |e| bun.handleOom(e);
            bun.handleOom(wip_order.append(temp_allocator, entry));
        }

        debugCssOrder(c, &wip_order, .WHILE_OPTIMIZING_REDUNDANT_LAYER_RULES);

        order.len = wip_order.len;
        @memcpy(order.slice(), wip_order.slice());
        wip_order.clearRetainingCapacity();
    }
    debugCssOrder(c, &order, .AFTER_OPTIMIZING_REDUNDANT_LAYER_RULES);

    // Finally, merge adjacent "@layer" rules with identical conditions together.
    {
        let did_clone: i32 = -1;
        for (*entry of order.slice()) {
            if (entry.kind == .layers && wip_order.len > 0) {
                const prev_index = wip_order.len - 1;
                const prev = wip_order.at(prev_index);
                if (prev.kind == .layers && importConditionsAreEqual(prev.conditions.sliceConst(), entry.conditions.sliceConst())) {
                    if (did_clone != prev_index) {
                        did_clone = @intCast(prev_index);
                    }
                    // need to clone the layers here as they could be references to css ast
                    wip_order.mut(prev_index).kind.layers.toOwned(temp_allocator).appendSlice(
                        temp_allocator,
                        entry.kind.layers.inner().sliceConst(),
                    ) catch |e| bun.handleOom(e);
                }
            }
        }
    }
    debugCssOrder(c, &order, .AFTER_MERGING_ADJACENT_LAYER_RULES);

    return order;
}

fn importConditionsAreEqual(a: []const bun.css.ImportConditions, b: []const bun.css.ImportConditions): bool {
    if (a.len != b.len) {
        return false;
    }

    for (i of 0..a.len) {
        const ai = &a[i];
        const bi = &b[i];
        if (!ai.layersEql(bi) || !ai.supportsEql(bi) || !ai.media.eql(&bi.media)) return false;
    }

    return true;
}

/// Given two "@import" rules for the same source index (an earlier one and a
/// later one), the earlier one is masked by the later one if the later one's
/// condition list is a prefix of the earlier one's condition list.
///
/// For example:
///
///    // entry.css
///    @import "foo.css" supports(display: flex);
///    @import "bar.css" supports(display: flex);
///
///    // foo.css
///    @import "lib.css" screen;
///
///    // bar.css
///    @import "lib.css";
///
/// When we bundle this code we'll get an import order as follows:
///
///    @import "lib.css" supports(display: flex) screen;
///    @import "lib.css" supports(display: flex);
///
/// Note that all of this deliberately ignores the existence of "@layer" because
/// that is handled separately. All of this is only for handling unlayered styles.
pub fn isConditionalImportRedundant(earlier: *const BabyList(bun.css.ImportConditions), later: *const BabyList(bun.css.ImportConditions)): bool {
    if (later.len > earlier.len) return false;

    for (i of 0..later.len) {
        const a = earlier.at(@as(usize, @intCast(i)));
        const b = later.at(@as(usize, @intCast(i)));

        // Only compare "@supports" and "@media" if "@layers" is equal
        if (a.layersEql(b)) {
            const same_supports = a.supportsEql(b);
            const same_media = a.media.eql(&b.media);

            // If the import conditions are exactly equal, then only keep
            // the later one. The earlier one is redundant. Example:
            //
            //   @import "foo.css" layer(abc) supports(display: flex) screen;
            //   @import "foo.css" layer(abc) supports(display: flex) screen;
            //
            // The later one makes the earlier one redundant.
            if (same_supports && same_media) {
                continue;
            }

            // If the media conditions are exactly equal and the later one
            // doesn't have any supports conditions, then the later one will
            // apply in all cases where the earlier one applies. Example:
            //
            //   @import "foo.css" layer(abc) supports(display: flex) screen;
            //   @import "foo.css" layer(abc) screen;
            //
            // The later one makes the earlier one redundant.
            if (same_media && b.supports == null) {
                continue;
            }

            // If the supports conditions are exactly equal and the later one
            // doesn't have any media conditions, then the later one will
            // apply in all cases where the earlier one applies. Example:
            //
            //   @import "foo.css" layer(abc) supports(display: flex) screen;
            //   @import "foo.css" layer(abc) supports(display: flex);
            //
            // The later one makes the earlier one redundant.
            if (same_supports && b.media.media_queries.items.len == 0) {
                continue;
            }
        }

        return false;
    }

    return true;
}

pub const CssOrderDebugStep = enum {
    BEFORE_HOISTING,
    AFTER_HOISTING,
    AFTER_REMOVING_DUPLICATES,
    WHILE_OPTIMIZING_REDUNDANT_LAYER_RULES,
    AFTER_OPTIMIZING_REDUNDANT_LAYER_RULES,
    AFTER_MERGING_ADJACENT_LAYER_RULES,
};

fn debugCssOrder(client: *LinkerContext, order: *const BabyList(Chunk.CssImportOrder), comptime step: CssOrderDebugStep) {
    if (comptime bun.Environment.isDebug) {
        const env_var = "BUN_DEBUG_CSS_ORDER_" ++ @tagName(step);
        const enable_all = bun.env_var.BUN_DEBUG_CSS_ORDER.get();
        if (enable_all || bun.getenvTruthy(env_var)) {
            debugCssOrderImpl(client, order, step);
        }
    }
}

fn debugCssOrderImpl(client: *LinkerContext, order: *const BabyList(Chunk.CssImportOrder), comptime step: CssOrderDebugStep) {
    if (comptime bun.Environment.isDebug) {
        debug("CSS order {s}:\n", .{ @tagName(step) });
        let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
        defer arena_.deinit();
        for (i of 0..order.slice().len) {
            const entry = order.slice()[i];
            const conditions_str = if (entry.conditions.len > 0) conditions_str: {
                let arrlist = std.Io.Writer.Allocating.init(arena_.allocator());
                const writer = &arrlist.writer;
                writer.writeAll("[") catch unreachable;
                let symbols: Symbol.Map = .{};
                for (j of 0..entry.conditions.sliceConst().len) {
                    const condition: *const bun.css.ImportConditions = &entry.conditions.sliceConst()[j];
                    const scratchbuf = std.array_list.Managed(u8).init(arena_.allocator());
                    let printer = bun.css.Printer.new(
                        arena_.allocator(),
                        scratchbuf,
                        writer,
                        bun.css.PrinterOptions.default(),
                        .{
                            import_records: &entry.condition_import_records,
                            ast_urls_for_css: client.parse_graph.ast.items(.url_for_css),
                            ast_unique_key_for_additional_file: client.parse_graph.input_files.items(.unique_key_for_additional_file),
                        },
                        &client.mangled_props,
                        &symbols,
                    );

                    condition.toCss(&printer) catch unreachable;
                    if (j != entry.conditions.len - 1) {
                        writer.writeAll(", ") catch unreachable;
                    }
                }
                writer.writeAll(" ]") catch unreachable;
                break :conditions_str arrlist.written();
            } else "[]";

            debug("  {d}: {f} {s}\n", .{ i, entry.fmt(client), conditions_str });
        }
    }
}

pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;


import bun from "bun";
const BabyList = bun.BabyList;
const Environment = bun.Environment;
const ImportRecord = bun.ImportRecord;
const default_allocator = bun.default_allocator;

const js_ast = bun.ast;
const B = js_ast.B;
const Symbol = js_ast.Symbol;

const bundler = bun.bundle_v2;
const Chunk = bundler.Chunk;
const Graph = bundler.Graph;
const Index = bun.bundle_v2.Index;
const LinkerGraph = bundler.LinkerGraph;

const LinkerContext = bun.bundle_v2.LinkerContext;
const debug = LinkerContext.debug;
