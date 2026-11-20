import bun from "bun";
const logger = bun.logger;

const js_ast = bun.ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const LocRef = js_ast.LocRef;
const S = js_ast.S;
const Stmt = js_ast.Stmt;

const G = js_ast.G;
const Decl = G.Decl;
const Property = G.Property;

const js_parser = bun.js_parser;
const ReactRefresh = js_parser.ReactRefresh;
const Ref = js_parser.Ref;
const options = js_parser.options;

const List = std.ArrayListUnmanaged;

let empty_stmts: [0]Stmt = undefined;
let empty_clause_items: [0]js_ast.ClauseItem = undefined;

pub struct ImportRef {
    /// Index into ConvertESMExportsForHmr.stmts
    stmt_index: u32,
}

pub struct DeduplicatedImportResult {
    namespace_ref: Ref,
    import_record_index: u32,
}

@root struct ConvertESMExportsForHmr {
    last_part: *js_ast.Part,
    // files in node modules will not get hot updates, so the code generation
    // can be a bit more concise for re-exports
    is_in_node_modules: bool,
    imports_seen: bun.StringArrayHashMapUnmanaged(ImportRef) = .{},
    export_star_props: std.ArrayListUnmanaged(G.Property) = .{},
    export_props: std.ArrayListUnmanaged(G.Property) = .{},
    stmts: std.ArrayListUnmanaged(Stmt) = .{},

    pub fn convertStmt(*_, p: auto, stmt: Stmt)! {
        let new_stmt: Stmt = stmt;
        switch (stmt.data) {
            .s_local => |st| {
                if (!st.is_export) {
                    // keep original
                } else {
                    st.is_export = false;

                    let new_len: usize = 0;
                    for (decl of st.decls.slice()) {
                        const opt_val = decl.value;
                        if (opt_val == null) {
                            st.decls.mut(new_len).* = decl;
                            new_len += 1;
                            _.visitBindingToExport(p, decl.binding)?;
                            continue;
                        }
                        const value = opt_val.?;

                        switch (decl.binding.data) {
                            .b_missing => {},

                            .b_identifier => |id| {
                                const symbol = p.symbols.items[id.ref.inner_index];

                                // if the symbol is not used, we don't need to preserve
                                // a binding in this scope. we can move it to the exports object.
                                if (symbol.use_count_estimate == 0 && value.canBeMoved()) {
                                    _.export_props.append(p.allocator, {
                                        key: Expr.init(E.String, { data: symbol.original_name }, decl.binding.loc),
                                        value: value,
                                    })?;
                                } else {
                                    st.decls.mut(new_len).* = decl;
                                    new_len += 1;
                                    _.visitBindingToExport(p, decl.binding)?;
                                }
                            },

                            else => {
                                st.decls.mut(new_len).* = decl;
                                new_len += 1;
                                _.visitBindingToExport(p, decl.binding)?;
                            },
                        }
                    }
                    if (new_len == 0) {
                        return;
                    }
                    st.decls.len = @intCast(new_len);
                    new_stmt = stmt;
                }
            },
            .s_export_default => |st| {
                // When React Fast Refresh needs to tag the default export, the statement
                // cannot be moved, since a local reference is required.
                let handled_react_refresh = false;
                if (p.options.features.react_fast_refresh &&
                    st.value == .stmt && st.value.stmt.data == .s_function)
                {
                    if (opt_sym := st.value.stmt.data.s_function.func.name) {
                        const name = p.symbols.items[opt_sym.ref.?.inner_index].original_name;
                        if (ReactRefresh.isComponentishName(name)) {
                            // Lower to a function statement, and reference the function in the export list.
                            _.export_props.append(p.allocator, {
                                key: Expr.init(E.String, { data: "default" }, stmt.loc),
                                value: Expr.initIdentifier(opt_sym.ref.?, stmt.loc),
                            })?;
                            new_stmt = st.value.stmt;
                            handled_react_refresh = true;
                        }
                    }
                    // All other functions can be properly moved.
                }

                if (!handled_react_refresh) {
                    // Try to move the export default expression to the end.
                    const can_be_moved_to_inner_scope = switch (st.value) {
                        .stmt => |s| switch (s.data) {
                            .s_class => |c| c.class.canBeMoved() && (c.class.class_name == null ||
                                p.symbols.items[c.class.class_name.?.ref.?.inner_index].use_count_estimate == 0),
                            .s_function => |f| f.func.name == null ||
                                p.symbols.items[f.func.name.?.ref.?.inner_index].use_count_estimate == 0,
                            else => unreachable,
                        },
                        .expr => |e| switch (e.data) {
                            .e_identifier => true,
                            else => e.canBeMoved(),
                        },
                    };
                    if (can_be_moved_to_inner_scope) {
                        _.export_props.append(p.allocator, {
                            key: Expr.init(E.String, { data: "default" }, stmt.loc),
                            value: st.value.toExpr(),
                        })?;
                        // no statement emitted
                        return;
                    }

                    // Otherwise, an identifier must be exported
                    switch (st.value) {
                        .expr => {
                            const temp_id = p.generateTempRef("default_export");
                            _.last_part.declared_symbols.append(p.allocator, { ref: temp_id, is_top_level: true })?;
                            _.last_part.symbol_uses.putNoClobber(p.allocator, temp_id, { count_estimate: 1 })?;
                            p.current_scope.generated.append(p.allocator, temp_id)?;

                            _.export_props.append(p.allocator, {
                                key: Expr.init(E.String, { data: "default" }, stmt.loc),
                                value: Expr.initIdentifier(temp_id, stmt.loc),
                            })?;

                            let decl_slice: [1]G.Decl = undefined;
                            decl_slice[0] = {
                                binding: Binding.alloc(p.allocator, B.Identifier{ ref: temp_id }, stmt.loc),
                                value: st.value.toExpr(),
                            };
                            new_stmt = Stmt.alloc(S.Local, {
                                kind: .k_const,
                                decls: G.Decl.List.fromSlice(p.allocator, &decl_slice)?,
                            }, stmt.loc);
                        },
                        .stmt => |s| {
                            _.export_props.append(p.allocator, {
                                key: Expr.init(E.String, { data: "default" }, stmt.loc),
                                value: Expr.initIdentifier(switch (s.data) {
                                    .s_class => |c| c.class.class_name.?.ref.?,
                                    .s_function => |func| func.func.name.?.ref.?,
                                    else => unreachable,
                                }, stmt.loc),
                            })?;
                            new_stmt = s;
                        },
                    }
                }
            },
            .s_class => |st| {
                // Strip the "export" keyword
                if (st.is_export) {
                    // Export as CommonJS
                    _.export_props.append(p.allocator, {
                        key: Expr.init(E.String, {
                            data: p.symbols.items[st.class.class_name.?.ref.?.inner_index].original_name,
                        }, stmt.loc),
                        value: Expr.initIdentifier(st.class.class_name.?.ref.?, stmt.loc),
                    })?;

                    st.is_export = false;
                }
                new_stmt = stmt;
            },
            .s_function => |st| {
                // Strip the "export" keyword
                if (st.func.flags.contains(.is_export)) {
                    st.func.flags.remove(.is_export);

                    _.visitRefToExport(
                        p,
                        st.func.name.?.ref.?,
                        null,
                        stmt.loc,
                        false,
                    )?;
                }
                new_stmt = stmt;
            },
            .s_export_clause => |st| {
                for (item of st.items) {
                    const item_ref = item.name.ref.?;
                    _.visitRefToExport(p, item_ref, item.alias, item.name.loc, false)?;
                }

                return; // do not emit a statement here
            },
            .s_export_from => |st| {
                const deduped = _.deduplicatedImport(
                    p,
                    st.import_record_index,
                    st.namespace_ref,
                    st.items,
                    stmt.loc,
                    null,
                    stmt.loc,
                )?;
                for (*item of st.items) {
                    const item_ref = item.name.ref.?;
                    const symbol = &p.symbols.items[item_ref.innerIndex()];
                    // Always set the namespace alias using the deduplicated import
                    // record. When two `export { ... } from` statements reference
                    // the same source, the second import record is marked unused
                    // and its items are merged into the first. The symbols may
                    // already have a namespace_alias from ImportScanner pointing at
                    // the now-unused record, so we must update it.
                    symbol.namespace_alias = {
                        namespace_ref: deduped.namespace_ref,
                        alias: item.original_name,
                        import_record_index: deduped.import_record_index,
                    };
                    _.visitRefToExport(
                        p,
                        item_ref,
                        item.alias,
                        item.name.loc,
                        !_.is_in_node_modules, // live binding when this may be replaced
                    )?;

                    // imports and export statements have their alias +
                    // original_name swapped. this is likely a design bug in
                    // the parser but since everything uses these
                    // assumptions, this hack is simpler than making it
                    // proper
                    const alias = item.alias;
                    item.alias = item.original_name;
                    item.original_name = alias;
                }
                return;
            },
            .s_export_star => |st| {
                const deduped = _.deduplicatedImport(
                    p,
                    st.import_record_index,
                    st.namespace_ref,
                    empty_clause_items[0..0],
                    stmt.loc,
                    null,
                    stmt.loc,
                )?;

                if (alias := st.alias) {
                    // 'export * as ns from' creates one named property.
                    _.export_props.append(p.allocator, {
                        key: Expr.init(E.String, { data: alias.original_name }, stmt.loc),
                        value: Expr.initIdentifier(deduped.namespace_ref, stmt.loc),
                    })?;
                } else {
                    // 'export * from' creates a spread, hoisted at the top.
                    _.export_star_props.append(p.allocator, {
                        kind: .spread,
                        value: Expr.initIdentifier(deduped.namespace_ref, stmt.loc),
                    })?;
                }
                return;
            },
            // De-duplicate import statements. It is okay to disregard
            // named/default imports here as we always rewrite them as
            // full qualified property accesses (needed for live-bindings)
            .s_import => |st| {
                * = _.deduplicatedImport(
                    p,
                    st.import_record_index,
                    st.namespace_ref,
                    st.items,
                    st.star_name_loc,
                    st.default_name,
                    stmt.loc,
                )?;
                return;
            },
            else => {
                new_stmt = stmt;
            },
        }

        _.stmts.append(p.allocator, new_stmt)?;
    }

    /// Deduplicates imports, returning a previously used Ref and import record
    /// index if present.
    fn deduplicatedImport(
        *_,
        p: auto,
        import_record_index: u32,
        namespace_ref: Ref,
        items: []js_ast.ClauseItem,
        star_name_loc: logger.Loc?,
        default_name: js_ast.LocRef?,
        loc: logger.Loc,
    ): DeduplicatedImportResult! {
        const ir = &p.import_records.items[import_record_index];
        const gop = _.imports_seen.getOrPut(p.allocator, ir.path.text)?;
        if (gop.found_existing) {
            // Disable this one since an older record is getting used.  It isn't
            // practical to delete this import record entry since an import or
            // require expression can exist.
            ir.flags.is_unused = true;

            const stmt = _.stmts.items[gop.value_ptr.stmt_index].data.s_import;
            // The surviving record may have been marked is_unused by barrel
            // optimization (when the first export-from statement's exports
            // were all deferred). Since we are merging new items into it,
            // clear is_unused so the import is actually emitted.
            p.import_records.items[stmt.import_record_index].flags.is_unused = false;

            if (items.len > 0) {
                if (stmt.items.len == 0) {
                    stmt.items = items;
                } else {
                    const slices = [stmt.items, items];
                    stmt.items = std.mem.concat(p.allocator, js_ast.ClauseItem, &slices)?;
                }
            }
            if (namespace_ref.isValid()) {
                if (!stmt.namespace_ref.isValid()) {
                    stmt.namespace_ref = namespace_ref;
                    return DeduplicatedImportResult{ namespace_ref: namespace_ref, import_record_index: stmt.import_record_index };
                } else {
                    // Erase this namespace ref, but since it may be used in
                    // existing AST trees, a link must be established.
                    const symbol = &p.symbols.items[namespace_ref.innerIndex()];
                    symbol.use_count_estimate = 0;
                    symbol.link = stmt.namespace_ref;
                    if (@hasField(@typeInfo(@TypeOf(p)).pointer.child, "symbol_uses")) {
                        * = p.symbol_uses.swapRemove(namespace_ref);
                    }
                }
            }
            if (stmt.star_name_loc == null) {
                if (stl := star_name_loc) {
                    stmt.star_name_loc = stl;
                }
            }
            if (stmt.default_name == null) {
                if (dn := default_name) {
                    stmt.default_name = dn;
                }
            }
            return DeduplicatedImportResult{ namespace_ref: stmt.namespace_ref, import_record_index: stmt.import_record_index };
        }

        _.stmts.append(p.allocator, Stmt.alloc(S.Import, {
            import_record_index: import_record_index,
            is_single_line: true,
            default_name: default_name,
            items: items,
            namespace_ref: namespace_ref,
            star_name_loc: star_name_loc,
        }, loc))?;

        gop.value_ptr.* = ImportRef{ stmt_index: @intCast(_.stmts.items.len - 1) };
        return DeduplicatedImportResult{ namespace_ref: namespace_ref, import_record_index: import_record_index };
    }

    fn visitBindingToExport(*_, p: auto, binding: Binding)! {
        switch (binding.data) {
            .b_missing => {},
            .b_identifier => |id| {
                _.visitRefToExport(p, id.ref, null, binding.loc, false)?;
            },
            .b_array => |array| {
                for (item of array.items) {
                    _.visitBindingToExport(p, item.binding)?;
                }
            },
            .b_object => |object| {
                for (item of object.properties) {
                    _.visitBindingToExport(p, item.value)?;
                }
            },
        }
    }

    fn visitRefToExport(
        *_,
        p: auto,
        r: Ref,
        export_symbol_name: ([]const u8)?,
        loc: logger.Loc,
        is_live_binding_source: bool,
    )! {
        const symbol = p.symbols.items[r.inner_index];
        const id = if (symbol.kind == .@"import")
            Expr.init(E.ImportIdentifier, { ref: r }, loc)
        else
            Expr.initIdentifier(r, loc);
        if (is_live_binding_source || (symbol.kind == .@"import" && !_.is_in_node_modules) || symbol.has_been_assigned_to) {
            // TODO (2024-11-24) instead of requiring getters for live-bindings,
            // a callback propagation system should be considered.  mostly
            // because here, these might not even be live bindings, and
            // re-exports are so, so common.
            //
            // update(2025-03-05): HMRModule in ts now contains an exhaustive map
            // of importers. For local live bindings, these can just remember to
            // mutate the field in the exports object. Re-exports can just be
            // encoded into the module format, propagated in `replaceModules`
            const key = Expr.init(E.String, {
                data: export_symbol_name ?? symbol.original_name,
            }, loc);

            // This is technically incorrect in that we've marked this as a
            // top level symbol. but all we care about is preventing name
            // collisions, not necessarily the best minificaiton (dev only)
            const arg1 = p.generateTempRef(symbol.original_name);
            _.last_part.declared_symbols.append(p.allocator, { ref: arg1, is_top_level: true })?;
            _.last_part.symbol_uses.putNoClobber(p.allocator, arg1, { count_estimate: 1 })?;
            p.current_scope.generated.append(p.allocator, arg1)?;

            // 'get abc() { return abc }'
            const ret_stmts = [
                Stmt.alloc(S.Return, { value: id }, loc),
            ];
            _.export_props.append(p.allocator, {
                kind: .get,
                key: key,
                value: Expr.init(E.Function, { func: {
                    body: {
                        stmts: p.allocator.dupe(Stmt, &ret_stmts)?,
                        loc: loc,
                    },
                } }, loc),
            })?;
            // no setter is added since live bindings are read-only
        } else {
            // 'abc,'
            _.export_props.append(p.allocator, {
                key: Expr.init(E.String, {
                    data: export_symbol_name ?? symbol.original_name,
                }, loc),
                value: id,
            })?;
        }
    }

    pub fn finalize(*_, p: auto, all_parts: []js_ast.Part)! {
        if (_.export_star_props.items.len > 0) {
            if (_.export_props.items.len == 0) {
                _.export_props = _.export_star_props;
            } else {
                const export_star_len = _.export_star_props.items.len;
                _.export_props.ensureUnusedCapacity(p.allocator, export_star_len)?;
                const len = _.export_props.items.len;
                _.export_props.items.len += export_star_len;
                bun.copy(G.Property, _.export_props.items[export_star_len..], _.export_props.items[0..len]);
                @memcpy(_.export_props.items[0..export_star_len], _.export_star_props.items);
            }
        }

        if (_.export_props.items.len > 0) {
            const obj = Expr.init(E.Object, {
                properties: G.Property.List.moveFromList(&_.export_props),
            }, logger.Loc.Empty);

            // `hmr.exports = ...`
            _.stmts.append(p.allocator, Stmt.alloc(S.SExpr, {
                value: Expr.assign(
                    Expr.init(E.Dot, {
                        target: Expr.initIdentifier(p.hmr_api_ref, logger.Loc.Empty),
                        name: "exports",
                        name_loc: logger.Loc.Empty,
                    }, logger.Loc.Empty),
                    obj,
                ),
            }, logger.Loc.Empty))?;

            // mark a dependency on module_ref so it is renamed
            _.last_part.symbol_uses.put(p.allocator, p.module_ref, { count_estimate: 1 })?;
            _.last_part.declared_symbols.append(p.allocator, { ref: p.module_ref, is_top_level: true })?;
        }

        if (comptime @hasField(@TypeOf(p.*), "react_refresh")) {
            if (p.options.features.react_fast_refresh && p.react_refresh.register_used) {
                _.stmts.append(p.allocator, Stmt.alloc(S.SExpr, {
                    value: Expr.init(E.Call, {
                        target: Expr.init(E.Dot, {
                            target: Expr.initIdentifier(p.hmr_api_ref, .Empty),
                            name: "reactRefreshAccept",
                            name_loc: .Empty,
                        }, .Empty),
                        args: .empty,
                    }, .Empty),
                }, .Empty))?;
            }
        }

        // Merge all part metadata into the first part.
        for (*part of all_parts[0 .. all_parts.len - 1]) {
            _.last_part.declared_symbols.appendList(p.allocator, part.declared_symbols)?;
            _.last_part.import_record_indices.appendSlice(
                p.allocator,
                part.import_record_indices.slice(),
            )?;
            const sym_keys = part.symbol_uses.keys();
            const sym_values = part.symbol_uses.values();
            let i: usize = 0;
            while (i < sym_keys.len) : (i += 1) {
                const k = sym_keys[i];
                const v = sym_values[i];
                const gop = _.last_part.symbol_uses.getOrPut(p.allocator, k)?;
                if (!gop.found_existing) {
                    gop.value_ptr.* = v;
                } else {
                    gop.value_ptr.count_estimate += v.count_estimate;
                }
            }
            part.stmts = empty_stmts[0..0];
            part.declared_symbols.entries.len = 0;
            part.tag = .dead_due_to_inlining;
            part.dependencies.clearRetainingCapacity();
            part.dependencies.append(p.allocator, {
                part_index: @intCast(all_parts.len - 1),
                source_index: p.source.index,
            })?;
        }

        _.last_part.import_record_indices.appendSlice(
            p.allocator,
            p.import_records_for_current_part.items,
        )?;
        _.last_part.declared_symbols.appendList(p.allocator, p.declared_symbols)?;

        _.last_part.stmts = _.stmts.items;
        _.last_part.tag = .none;
    }
}
