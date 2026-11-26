import bun from "bun";

pub const ThreadPool = bun.bundle_v2.ThreadPool;

type string = []const u8;

const Environment = bun.Environment;
const options = bun.options;
const strings = bun.strings;

const ImportData = bun.bundle_v2.ImportData;
const Index = bun.bundle_v2.Index;
const LinkerContext = bun.bundle_v2.LinkerContext;
const Part = bun.bundle_v2.Part;
const RefImportData = bun.bundle_v2.RefImportData;
const ResolvedExports = bun.bundle_v2.ResolvedExports;

const js_ast = bun.bundle_v2.js_ast;
const B = js_ast.B;
const Dependency = js_ast.Dependency;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const Ref = bun.bundle_v2.js_ast.Ref;
const S = js_ast.S;
const Stmt = js_ast.Stmt;

const Logger = bun.logger;
const Loc = Logger.Loc;

pub struct EndGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.end(); }
    }
}

pub struct UngetGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.unget(); }
    }
}

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct DoneGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.done(); }
    }
}

/// Step 5: Create namespace exports for every file. This is always necessary
/// for CommonJS files, and is also necessary for other files if they are
/// imported using an import star statement.
pub fn doStep5(c: *LinkerContext, source_index_: Index, _: usize) {
    const source_index = source_index_.get();
    let trace = bun.perf.trace("Bundler.CreateNamespaceExports");
    const trace_guard = EndGuard<@TypeOf(trace)>{ ptr: unsafe { &trace } };
    * = trace_guard;

    const id = source_index;
    if (id >= c.graph.meta.len) return;

    const worker: *ThreadPool.Worker = ThreadPool.Worker.get(@fieldParentPtr("linker", c));
    const worker_guard = UngetGuard<ThreadPool.Worker>{ ptr: unsafe { worker as *ThreadPool.Worker! } };
    * = worker_guard;

    // we must use this allocator here
    const allocator = worker.allocator;

    const resolved_exports: *ResolvedExports = &c.graph.meta.items(.resolved_exports)[id];

    // Now that all exports have been resolved, sort and filter them to create
    // something we can iterate over later.
    let aliases = std.array_list.Managed(string).initCapacity(allocator, resolved_exports.count()) catch unreachable;
    let alias_iter = resolved_exports.iterator();
    const imports_to_bind = c.graph.meta.items(.imports_to_bind);
    const probably_typescript_type = c.graph.meta.items(.probably_typescript_type);

    // counting in here saves us an extra pass through the array
    let re_exports_count: usize = 0;

    next_alias: while (entry := alias_iter.next()) {
        let export_ = entry.value_ptr.*;
        const alias = entry.key_ptr.*;
        const this_id = export_.data.source_index.get();
        let inner_count: usize = 0;
        // Re-exporting multiple symbols with the same name causes an ambiguous
        // export. These names cannot be used and should not end up in generated code.
        if (export_.potentially_ambiguous_export_star_refs.len > 0) {
            const main = imports_to_bind[this_id].get(export_.data.import_ref) ?? ImportData{ data: export_.data };
            for (ambig of export_.potentially_ambiguous_export_star_refs.slice()) {
                const _id = ambig.data.source_index.get();
                let ambig_ref = ambig.data.import_ref;
                if (bound := imports_to_bind[_id].get(ambig.data.import_ref)) {
                    ambig_ref = bound.data.import_ref;
                }
                if (!main.data.import_ref.eql(ambig_ref)) {
                    continue :next_alias;
                }
                inner_count += @as(usize, ambig.re_exports.len);
            }
        }

        // Ignore re-exported imports in TypeScript files that failed to be
        // resolved. These are probably just type-only imports so the best thing to
        // do is to silently omit them from the export list.
        if (probably_typescript_type[this_id].contains(export_.data.import_ref)) {
            continue;
        }
        re_exports_count += inner_count;

        aliases.appendAssumeCapacity(alias);
    }
    // TODO: can this be u32 instead of a string?
    // if yes, we could just move all the hidden exports to the end of the array
    // and only store a count instead of an array
    strings.sortAsc(aliases.items);
    const export_aliases = aliases.toOwnedSlice() catch unreachable;
    c.graph.meta.items(.sorted_and_filtered_export_aliases)[id] = export_aliases;

    // Export creation uses "sortedAndFilteredExportAliases" so this must
    // come second after we fill in that array
    createExportsForFile(
        c,
        allocator,
        id,
        resolved_exports,
        imports_to_bind,
        export_aliases,
        re_exports_count,
    );

    // Each part tracks the other parts it depends on within this file
    let local_dependencies = std.AutoHashMap(u32, u32).init(allocator);
    const local_deps_guard = DeinitGuard<@TypeOf(local_dependencies)>{ ptr: unsafe { &local_dependencies } };
    * = local_deps_guard;

    const parts_slice: []Part = c.graph.ast.items(.parts)[id].slice();
    const named_imports: *js_ast.Ast.NamedImports = &c.graph.ast.items(.named_imports)[id];

    const our_imports_to_bind = imports_to_bind[id];
    for (part_index of 0..parts_slice.len) {
        let part = &parts_slice[part_index];
        // Previously owned by `c.allocator()`, which is a `MimallocArena` (from
        // `BundleV2.graph.heap`).
        part.dependencies.transferOwnership(&worker.heap);

        // Now that all files have been parsed, determine which property
        // accesses off of imported symbols are inlined enum values and
        // which ones aren't
        const prop_keys = part.import_symbol_property_uses.keys();
        const prop_vals = part.import_symbol_property_uses.values();
        for (k_idx of 0..prop_keys.len) {
            const sym_ref = prop_keys[k_idx];
            const properties = prop_vals[k_idx];
            // The parser drops the entry if a replaced macro call was the last plain use.
            let use = (part.symbol_uses.getOrPutValue(allocator, sym_ref, .{}) catch e => bun.handleOom(e)).value_ptr;

            // Rare path: this import is a TypeScript enum
            if (import_data := our_imports_to_bind.get(sym_ref)) {
                const import_ref = import_data.data.import_ref;
                if (symbol := c.graph.symbols.get(import_ref)) {
                    if (symbol.kind == .ts_enum) {
                        if (enum_data := c.graph.ts_enums.get(import_ref)) {
                            let found_non_inlined_enum = false;

                            let it = properties.iterator();
                            while (next := it.next()) {
                                const name = next.key_ptr.*;
                                const prop_use = next.value_ptr;

                                if (enum_data.get(name) == null) {
                                    found_non_inlined_enum = true;
                                    use.count_estimate += prop_use.count_estimate;
                                }
                            }

                            if (!found_non_inlined_enum) {
                                if (use.count_estimate == 0) {
                                    * = part.symbol_uses.swapRemove(sym_ref);
                                }
                                continue;
                            }
                        }
                    }
                }
            }

            // Common path: this import isn't a TypeScript enum
            let it = properties.valueIterator();
            while (prop_use := it.next()) {
                use.count_estimate += prop_use.count_estimate;
            }
        }

        // Now that we know this, we can determine cross-part dependencies
        const sym_keys = part.symbol_uses.keys();
        for (j of 0..sym_keys.len) {
            const sym_ref = sym_keys[j];
            if (comptime Environment.allow_assert) {
                bun.assert(part.symbol_uses.values()[j].count_estimate > 0);
            }

            const other_parts = c.topLevelSymbolsToParts(id, sym_ref);

            for (other_part_index of other_parts) {
                let local = local_dependencies.getOrPut(other_part_index) catch unreachable;
                if (!local.found_existing || local.value_ptr.* != part_index) {
                    local.value_ptr.* = @as(u32, @intCast(part_index));
                    // note: if we crash on append, it is due to threadlocal heaps in mimalloc
                    part.dependencies.append(
                        allocator,
                        .{
                            source_index: Index.source(source_index),
                            part_index: other_part_index,
                        },
                    ) catch unreachable;
                }
            }

            // Also map from imports to parts that use them
            if (existing := named_imports.getPtr(sym_ref)) {
                bun.handleOom(existing.local_parts_with_uses.append(allocator, @intCast(part_index)));
            }
        }
    }
}

pub fn createExportsForFile(
    c: *LinkerContext,
    allocator: std.mem.Allocator,
    id: u32,
    resolved_exports: *ResolvedExports,
    imports_to_bind: []RefImportData,
    export_aliases: []const string,
    re_exports_count: usize,
) {
    ////////////////////////////////////////////////////////////////////////////////
    // WARNING: This method is run in parallel over all files. Do not mutate data
    // for other files within this method or you will create a data race.
    ////////////////////////////////////////////////////////////////////////////////

    Stmt.Disabler.disable();
    defer Stmt.Disabler.enable();
    Expr.Disabler.disable();
    defer Expr.Disabler.enable();

    // 1 property per export
    let properties = bun.handleOom(std.array_list.Managed(js_ast.G.Property)
        .initCapacity(allocator, export_aliases.len));

    let ns_export_symbol_uses: Part.SymbolUseMap = .{};
    bun.handleOom(ns_export_symbol_uses.ensureTotalCapacity(allocator, export_aliases.len));

    const initial_flags = c.graph.meta.items(.flags)[id];
    const needs_exports_variable = initial_flags.needs_exports_variable;
    const force_include_exports_for_entry_point = c.options.output_format == .cjs && initial_flags.force_include_exports_for_entry_point;

    const stmts_count =
        export_aliases.len +
        @as(usize, @intFromBool(export_aliases.len > 0)) +
        @as(usize, @intFromBool(needs_exports_variable)) +
        @as(usize, @intFromBool(force_include_exports_for_entry_point));

    let stmts = bun.handleOom(js_ast.Stmt.Batcher.init(allocator, stmts_count));
    const stmts_guard = DoneGuard<js_ast.Stmt.Batcher>{ ptr: unsafe { &stmts } };
    * = stmts_guard;
    const loc = Logger.Loc.Empty;

    let ns_export_dependencies = bun.handleOom(std.array_list.Managed(js_ast.Dependency).initCapacity(allocator, re_exports_count));
    for (alias of export_aliases) {
        let exp = resolved_exports.getPtr(alias).?.*;

        // If this is an export of an import, reference the symbol that the import
        // was eventually resolved to. We need to do this because imports have
        // already been resolved by this point, so we can't generate a new import
        // and have that be resolved later.
        if (import_data := imports_to_bind[exp.data.source_index.get()].get(exp.data.import_ref)) {
            exp.data.import_ref = import_data.data.import_ref;
            exp.data.source_index = import_data.data.source_index;
            bun.handleOom(ns_export_dependencies.appendSlice(import_data.re_exports.slice()));
        }

        // Exports of imports need EImportIdentifier in case they need to be re-
        // written to a property access later on
        // note: this is stack allocated
        const value: js_ast.Expr = brk: {
            if (symbol := c.graph.symbols.getConst(exp.data.import_ref)) {
                if (symbol.namespace_alias != null) {
                    break :brk js_ast.Expr.init(
                        js_ast.E.ImportIdentifier,
                        js_ast.E.ImportIdentifier{
                            ref: exp.data.import_ref,
                        },
                        loc,
                    );
                }
            }

            break :brk js_ast.Expr.init(
                js_ast.E.Identifier,
                js_ast.E.Identifier{
                    ref: exp.data.import_ref,
                },
                loc,
            );
        };

        const fn_body = js_ast.G.FnBody{
            stmts: stmts.eat1(
                js_ast.Stmt.allocate(
                    allocator,
                    js_ast.S.Return,
                    .{ value: value },
                    loc,
                ),
            ),
            loc: loc,
        };
        properties.appendAssumeCapacity(.{
            key: js_ast.Expr.allocate(
                allocator,
                js_ast.E.String,
                .{
                    data: alias,
                },
                loc,
            ),
            value: js_ast.Expr.allocate(
                allocator,
                js_ast.E.Arrow,
                .{ prefer_expr: true, body: fn_body },
                loc,
            ),
        });
        ns_export_symbol_uses.putAssumeCapacity(exp.data.import_ref, .{ count_estimate: 1 });

        // Make sure the part that declares the export is included
        const parts = c.topLevelSymbolsToParts(exp.data.source_index.get(), exp.data.import_ref);
        ns_export_dependencies.ensureUnusedCapacity(parts.len) catch unreachable;
        let unused_deps = ns_export_dependencies.unusedCapacitySlice()[0..parts.len];
        for (p_idx of 0..parts.len) {
            unused_deps[p_idx] = .{
                source_index: exp.data.source_index,
                part_index: parts[p_idx],
            };
        }
        ns_export_dependencies.items.len += parts.len;
    }

    let declared_symbols = js_ast.DeclaredSymbol.List{};
    const exports_ref = c.graph.ast.items(.exports_ref)[id];
    const all_export_stmts: []js_ast.Stmt = stmts.head[0 .. @as(usize, @intFromBool(needs_exports_variable)) +
        @as(usize, @intFromBool(properties.items.len > 0) +
            @as(usize, @intFromBool(force_include_exports_for_entry_point)))];
    stmts.head = stmts.head[all_export_stmts.len..];
    let remaining_stmts = all_export_stmts;
    defer bun.assert(remaining_stmts.len == 0); // all must be used

    // Prefix this part with exports declaration if this isn't a CommonJS entry point
    if (needs_exports_variable) {
        c.graph.ast.items(.flags)[id].has_esm_namespace = true;
        let decls = allocator.alloc(js_ast.G.Decl, 1) catch unreachable;
        decls[0] = .{
            binding: js_ast.Binding.alloc(
                allocator,
                js_ast.B.Identifier{
                    ref: exports_ref,
                },
                loc,
            ),
            value: js_ast.Expr.allocate(allocator, js_ast.E.Object, .{}, loc),
        };
        remaining_stmts[0] = js_ast.Stmt.allocate(
            allocator,
            js_ast.S.Local,
            .{
                decls: G.Decl.List.fromOwnedSlice(decls),
            },
            loc,
        );
        remaining_stmts = remaining_stmts[1..];
        declared_symbols.append(allocator, .{ ref: exports_ref, is_top_level: true }) catch unreachable;
    }

    // "__export(exports, { foo: () => foo })"
    let export_ref = Ref.None;
    if (properties.items.len > 0) {
        export_ref = c.runtimeFunction("__export");
        let args = allocator.alloc(js_ast.Expr, 2) catch unreachable;
        args[0] = js_ast.Expr.initIdentifier(exports_ref, loc);
        args[1] = js_ast.Expr.allocate(
            allocator,
            js_ast.E.Object,
            .{ properties: .moveFromList(&properties) },
            loc,
        );
        remaining_stmts[0] = js_ast.Stmt.allocate(
            allocator,
            js_ast.S.SExpr,
            .{
                value: js_ast.Expr.allocate(
                    allocator,
                    js_ast.E.Call,
                    .{
                        target: js_ast.Expr.initIdentifier(export_ref, loc),
                        args: js_ast.ExprNodeList.fromOwnedSlice(args),
                    },
                    loc,
                ),
            },
            loc,
        );
        remaining_stmts = remaining_stmts[1..];
        // Make sure this file depends on the "__export" symbol
        const parts = c.topLevelSymbolsToPartsForRuntime(export_ref);
        ns_export_dependencies.ensureUnusedCapacity(parts.len) catch unreachable;
        for (part_index of parts) {
            ns_export_dependencies.appendAssumeCapacity(
                .{ source_index: Index.runtime, part_index: part_index },
            );
        }

        // Make sure the CommonJS closure, if there is one, includes "exports"
        c.graph.ast.items(.flags)[id].uses_exports_ref = true;
    }

    // Decorate "module.exports" with the "__esModule" flag to indicate that
    // we used to be an ES module. This is done by wrapping the exports object
    // instead of by mutating the exports object because other modules in the
    // bundle (including the entry point module) may do "import * as" to get
    // access to the exports object and should NOT see the "__esModule" flag.
    if (force_include_exports_for_entry_point) {
        const toCommonJSRef = c.runtimeFunction("__toCommonJS");

        let call_args = allocator.alloc(js_ast.Expr, 1) catch unreachable;
        call_args[0] = Expr.initIdentifier(exports_ref, Loc.Empty);
        remaining_stmts[0] = js_ast.Stmt.assign(
            Expr.allocate(
                allocator,
                E.Dot,
                E.Dot{
                    name: "exports",
                    name_loc: Loc.Empty,
                    target: Expr.initIdentifier(c.unbound_module_ref, Loc.Empty),
                },
                Loc.Empty,
            ),
            Expr.allocate(
                allocator,
                E.Call,
                E.Call{
                    target: Expr.initIdentifier(toCommonJSRef, Loc.Empty),
                    args: js_ast.ExprNodeList.fromOwnedSlice(call_args),
                },
                Loc.Empty,
            ),
        );
        remaining_stmts = remaining_stmts[1..];
    }

    // No need to generate a part if it'll be empty
    if (all_export_stmts.len > 0) {
        let empty_stmts: [0]js_ast.Stmt = undefined;
        c.graph.ast.items(.parts)[id].slice()[js_ast.namespace_export_part_index] = .{
            stmts: if (c.options.output_format != .internal_bake_dev) all_export_stmts else empty_stmts[0..0],
            symbol_uses: ns_export_symbol_uses,
            dependencies: js_ast.Dependency.List.moveFromList(&ns_export_dependencies),
            declared_symbols: declared_symbols,

            // This can be removed if nothing uses it
            can_be_removed_if_unused: true,

            // Make sure this is trimmed if unused even if tree shaking is disabled
            force_tree_shaking: true,
        };

        // Pull in the "__export" symbol if it was used
        if (export_ref.isValid()) {
            c.graph.meta.items(.flags)[id].needs_export_symbol_from_runtime = true;
        }
    }
}
