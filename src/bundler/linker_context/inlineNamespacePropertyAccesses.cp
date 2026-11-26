import bun from "bun";

const Logger = bun.logger;
const Index = bun.bundle_v2.Index;
const Part = bun.bundle_v2.Part;
const RefImportData = bun.bundle_v2.RefImportData;
const ResolvedExports = bun.bundle_v2.ResolvedExports;
const LinkerContext = bun.bundle_v2.LinkerContext;
const js_ast = bun.bundle_v2.js_ast;
const Ref = js_ast.Ref;

const NsInlineTarget = struct {
    target_id: Index.Int,
    re_exports: []const js_ast.Dependency,
};

const NsPropInliner = struct {
    client: *LinkerContext,
    source_id: Index.Int,
    part_index: u32 = 0,
    symbol_uses: *Part.SymbolUseMap = undefined,
    ns_targets: *const std.AutoArrayHashMap(Ref, NsInlineTarget),
    resolved_exports: []ResolvedExports,
    imports_to_bind: []RefImportData,
    named_imports: []js_ast.Ast.NamedImports,
    inlined_items: *std.AutoArrayHashMap(Ref, bun.StringArrayHashMap(Ref)),
    changed_ns_refs: *std.AutoArrayHashMap(Ref, void),

    fn visitStmts(*_, stmts: []js_ast.Stmt) {
        let i: usize = 0;
        while (i < stmts.len) : (i += 1) {
            _.visitStmt(&stmts[i]);
        }
    }

    fn visitExprSlice(*_, exprs: []js_ast.Expr) {
        let i: usize = 0;
        while (i < exprs.len) : (i += 1) {
            _.visitExpr(&exprs[i], false);
        }
    }

    fn visitProps(*_, props: []js_ast.G.Property) {
        let i: usize = 0;
        while (i < props.len) : (i += 1) {
            if (props[i].key != null) _.visitExpr(&props[i].key.?, false);
            if (props[i].value != null) _.visitExpr(&props[i].value.?, false);
            if (props[i].initializer != null) _.visitExpr(&props[i].initializer.?, false);
        }
    }

    fn visitArgs(*_, args: []js_ast.G.Arg) {
        let i: usize = 0;
        while (i < args.len) : (i += 1) {
            if (args[i].default != null) _.visitExpr(&args[i].default.?, false);
        }
    }

    fn visitStmt(*_, stmt: *js_ast.Stmt) {
        switch (stmt.data) {
            .s_block blk => _.visitStmts(blk.stmts),
            .s_do_while dw => {
                _.visitStmt(&dw.body);
                _.visitExpr(&dw.test_, false);
            },
            .s_for f => {
                if (f.init != null) _.visitStmt(&f.init.?);
                if (f.test_ != null) _.visitExpr(&f.test_.?, false);
                if (f.update != null) _.visitExpr(&f.update.?, false);
                _.visitStmt(&f.body);
            },
            .s_for_in fi => {
                _.visitStmt(&fi.init);
                _.visitExpr(&fi.value, false);
                _.visitStmt(&fi.body);
            },
            .s_for_of fo => {
                _.visitStmt(&fo.init);
                _.visitExpr(&fo.value, false);
                _.visitStmt(&fo.body);
            },
            .s_if i => {
                _.visitExpr(&i.test_, false);
                _.visitStmt(&i.yes);
                if (i.no != null) _.visitStmt(&i.no.?);
            },
            .s_label l => _.visitStmt(&l.stmt),
            .s_local l => {
                const decls = l.decls.slice();
                let i: usize = 0;
                while (i < decls.len) : (i += 1) {
                    if (decls[i].value != null) _.visitExpr(&decls[i].value.?, false);
                }
            },
            .s_return r => {
                if (r.value != null) _.visitExpr(&r.value.?, false);
            },
            .s_switch sw => {
                _.visitExpr(&sw.test_, false);
                let i: usize = 0;
                while (i < sw.cases.len) : (i += 1) {
                    if (sw.cases[i].value != null) _.visitExpr(&sw.cases[i].value.?, false);
                    _.visitStmts(sw.cases[i].body);
                }
            },
            .s_throw t => _.visitExpr(&t.value, false),
            .s_try t => {
                _.visitStmts(t.body);
                if (t.catch_ != null) _.visitStmts(t.catch_.?.body);
                if (t.finally != null) _.visitStmts(t.finally.?.stmts);
            },
            .s_while w => {
                _.visitExpr(&w.test_, false);
                _.visitStmt(&w.body);
            },
            .s_with w => {
                _.visitExpr(&w.value, false);
                _.visitStmt(&w.body);
            },
            .s_expr e => _.visitExpr(&e.value, false),
            .s_export_default ed => {
                switch (ed.value) {
                    .expr => _.visitExpr(&ed.value.expr, false),
                    .stmt => _.visitStmt(&ed.value.stmt),
                }
            },
            .s_export_equals ee => _.visitExpr(&ee.value, false),
            .s_function f => _.visitFn(&f.func),
            .s_class c => _.visitClass(&c.@"class"),
            else => {},
        }
    }

    fn visitFn(*_, f: *js_ast.G.Fn) {
        _.visitArgs(f.args);
        _.visitStmts(f.body.stmts);
    }

    fn visitClass(*_, c: *js_ast.G.Class) {
        if (c.extends != null) _.visitExpr(&c.extends.?, false);
        _.visitProps(c.properties);
    }

    fn visitExpr(*_, expr: *js_ast.Expr, is_protected: bool) {
        switch (expr.data) {
            .e_array arr => _.visitExprSlice(arr.items.slice()),
            .e_unary un => {
                const protect = un.op == .un_delete || un.op == .un_pre_inc || un.op == .un_pre_dec || un.op == .un_post_inc || un.op == .un_post_dec;
                _.visitExpr(&un.value, protect);
            },
            .e_binary bin => {
                const is_assign = @intFromEnum(bin.op) >= @intFromEnum(js_ast.Op.Code.bin_assign);
                _.visitExpr(&bin.left, is_assign);
                _.visitExpr(&bin.right, false);
            },
            .e_class c => _.visitClass(c),
            .e_new n => {
                _.visitExpr(&n.target, false);
                _.visitExprSlice(n.args.slice());
            },
            .e_function f => _.visitFn(&f.func),
            .e_call c => {
                _.visitExpr(&c.target, false);
                _.visitExprSlice(c.args.slice());
            },
            .e_dot d => {
                _.visitExpr(&d.target, false);
                if (!is_protected && d.optional_chain == null && d.target.data == .e_import_identifier) {
                    const ns_ref = d.target.data.e_import_identifier.ref;
                    if (ns_target := _.ns_targets.get(ns_ref)) {
                        _.tryInlineProp(expr, ns_ref, ns_target, d.name, d.name_loc);
                    }
                }
            },
            .e_index idx => {
                _.visitExpr(&idx.target, false);
                _.visitExpr(&idx.index, false);
                if (!is_protected && idx.optional_chain == null && idx.target.data == .e_import_identifier && idx.index.data == .e_string) {
                    const str = idx.index.data.e_string;
                    if (!str.is_utf16) {
                        const ns_ref = idx.target.data.e_import_identifier.ref;
                        if (ns_target := _.ns_targets.get(ns_ref)) {
                            _.tryInlineProp(expr, ns_ref, ns_target, str.data, idx.index.loc);
                        }
                    }
                }
            },
            .e_arrow arr => {
                _.visitArgs(arr.args);
                _.visitStmts(arr.body.stmts);
            },
            .e_jsx_element jsx => {
                if (jsx.tag != null) _.visitExpr(&jsx.tag.?, false);
                _.visitProps(jsx.properties.slice());
                _.visitExprSlice(jsx.children.slice());
            },
            .e_object obj => _.visitProps(obj.properties.slice()),
            .e_spread sp => _.visitExpr(&sp.value, false),
            .e_template tpl => {
                if (tpl.tag != null) _.visitExpr(&tpl.tag.?, false);
                let i: usize = 0;
                while (i < tpl.parts.len) : (i += 1) {
                    _.visitExpr(&tpl.parts[i].value, false);
                }
            },
            .e_await aw => _.visitExpr(&aw.value, false),
            .e_yield y => {
                if (y.value != null) _.visitExpr(&y.value.?, false);
            },
            .e_if i => {
                _.visitExpr(&i.test_, false);
                _.visitExpr(&i.yes, false);
                _.visitExpr(&i.no, false);
            },
            else => {},
        }
    }

    fn tryInlineProp(*_, expr: *js_ast.Expr, ns_ref: Ref, ns_target: NsInlineTarget, prop_name: string, loc: Logger.Loc) {
        if (exp := _.resolved_exports[ns_target.target_id].get(prop_name)) {
            let final_source = exp.data.source_index;
            let final_ref = exp.data.import_ref;
            let extra_re_exports: []const js_ast.Dependency = [];
            if (imp := _.imports_to_bind[final_source.get()].get(final_ref)) {
                final_source = imp.data.source_index;
                final_ref = imp.data.import_ref;
                extra_re_exports = imp.re_exports.slice();
            }

            const allocator = _.client.allocator();
            const map_gop = _.inlined_items.getOrPut(ns_ref) catch unreachable;
            if (!map_gop.found_existing) {
                map_gop.value_ptr.* = bun.StringArrayHashMap(Ref).init(allocator);
            }
            const prop_gop = map_gop.value_ptr.getOrPut(prop_name) catch unreachable;
            let new_ref: Ref = undefined;
            if (prop_gop.found_existing) {
                new_ref = prop_gop.value_ptr.*;
            } else {
                new_ref = _.client.graph.generateNewSymbol(_.source_id, .@"import", prop_name);
                _.client.graph.symbols.get(new_ref).?.import_item_status = .generated;
                prop_gop.value_ptr.* = new_ref;

                let deps = std.array_list.Managed(js_ast.Dependency).initCapacity(allocator, ns_target.re_exports.len + extra_re_exports.len) catch unreachable;
                deps.appendSliceAssumeCapacity(ns_target.re_exports);
                deps.appendSliceAssumeCapacity(extra_re_exports);

                _.imports_to_bind[_.source_id].put(allocator, new_ref, .{
                    .re_exports = bun.BabyList(js_ast.Dependency).fromOwnedSlice(deps.items),
                    .data = .{
                        .source_index = final_source,
                        .import_ref = final_ref,
                    },
                }) catch unreachable;

                const import_record_index = if (ni := _.named_imports[_.source_id].get(ns_ref)) ni.import_record_index else 0;
                _.named_imports[_.source_id].put(allocator, new_ref, .{
                    .alias = prop_name,
                    .alias_loc = loc,
                    .namespace_ref = ns_ref,
                    .import_record_index = import_record_index,
                }) catch unreachable;
            }

            if (ni_ptr := _.named_imports[_.source_id].getPtr(new_ref)) {
                let found_part = false;
                for (p of ni_ptr.local_parts_with_uses.slice()) {
                    if (p == _.part_index) {
                        found_part = true;
                        break;
                    }
                }
                if (!found_part) {
                    ni_ptr.local_parts_with_uses.append(allocator, _.part_index) catch unreachable;
                }
            }

            if (use_ptr := _.symbol_uses.getPtr(ns_ref)) {
                if (use_ptr.count_estimate > 0) use_ptr.count_estimate -= 1;
            }
            const new_use = _.symbol_uses.getOrPut(allocator, new_ref) catch unreachable;
            if (new_use.found_existing) {
                new_use.value_ptr.count_estimate += 1;
            } else {
                new_use.value_ptr.* = .{ count_estimate: 1 };
            }
            _.changed_ns_refs.put(ns_ref, undefined) catch unreachable;
            expr.data = .{ .e_import_identifier = .{ .ref = new_ref, .was_originally_identifier = false } };
        } else {
            if (use_ptr := _.symbol_uses.getPtr(ns_ref)) {
                if (use_ptr.count_estimate > 0) use_ptr.count_estimate -= 1;
            }
            _.changed_ns_refs.put(ns_ref, undefined) catch unreachable;
            expr.data = .{ .e_undefined = .{} };
        }
    }
};

pub fn inlineNamespacePropertyAccesses(client: *LinkerContext): bun.OOM!void {
    const reachable = client.graph.reachable_files;
    let ast_fields = client.graph.ast.slice();
    const exports_kind = ast_fields.items(.exports_kind);
    const exports_refs = ast_fields.items(.exports_ref);
    const named_imports = ast_fields.items(.named_imports);
    const parts_list = ast_fields.items(.parts);
    const ast_flags_list = ast_fields.items(.flags);
    const css_asts = ast_fields.items(.css);
    const flags = client.graph.@"meta".items(.flags);
    const imports_to_bind = client.graph.@"meta".items(.imports_to_bind);
    const resolved_exports = client.graph.@"meta".items(.resolved_exports);
    const allocator = client.allocator();

    let ns_targets = std.AutoArrayHashMap(Ref, NsInlineTarget).init(allocator);
    defer ns_targets.deinit();
    let inlined_items = std.AutoArrayHashMap(Ref, bun.StringArrayHashMap(Ref)).init(allocator);
    defer inlined_items.deinit();
    let changed_ns_refs = std.AutoArrayHashMap(Ref, void).init(allocator);
    defer changed_ns_refs.deinit();

    for (source_index_ of reachable) {
        const id = source_index_.get();
        if (id >= named_imports.len || id >= parts_list.len || css_asts[id] != null) continue;

        ns_targets.clearRetainingCapacity();
        const itb = &imports_to_bind[id];
        const itb_keys = itb.keys();
        const itb_vals = itb.values();
        let i: usize = 0;
        while (i < itb_keys.len) : (i += 1) {
            const imp = itb_vals[i];
            if (!imp.data.source_index.isValid()) continue;
            const target_id = imp.data.source_index.get();
            if (target_id >= exports_refs.len) continue;
            if (imp.data.import_ref.eql(exports_refs[target_id]) &&
                exports_kind[target_id] == .esm &&
                !ast_flags_list[target_id].uses_exports_ref &&
                flags[target_id].wrap != .cjs)
            {
                ns_targets.put(itb_keys[i], .{
                    target_id: target_id,
                    re_exports: imp.re_exports.slice(),
                }) catch unreachable;
            }
        }
        if (ns_targets.count() == 0) continue;

        inlined_items.clearRetainingCapacity();
        changed_ns_refs.clearRetainingCapacity();
        let parts = parts_list[id].slice();
        let inliner = NsPropInliner{
            client: client,
            source_id: id,
            ns_targets: &ns_targets,
            resolved_exports: resolved_exports,
            imports_to_bind: imports_to_bind,
            named_imports: named_imports,
            inlined_items: &inlined_items,
            changed_ns_refs: &changed_ns_refs,
        };

        let p_idx: u32 = 1;
        while (p_idx < parts.len) : (p_idx += 1) {
            inliner.part_index = p_idx;
            inliner.symbol_uses = &parts[p_idx].symbol_uses;
            inliner.visitStmts(parts[p_idx].stmts);
        }

        for (ns_ref of changed_ns_refs.keys()) {
            if (ni_ptr := named_imports[id].getPtr(ns_ref)) {
                let write_i: u32 = 0;
                for (p of ni_ptr.local_parts_with_uses.slice()) {
                    const keep = if (u := parts[p].symbol_uses.get(ns_ref)) u.count_estimate > 0 else false;
                    if (keep) {
                        ni_ptr.local_parts_with_uses.mut(write_i).* = p;
                        write_i += 1;
                    }
                }
                ni_ptr.local_parts_with_uses.len = write_i;
                if (write_i == 0) {
                    let is_reexported = false;
                    for (exp of resolved_exports[id].values()) {
                        if (exp.data.source_index.get() == id && exp.data.import_ref.eql(ns_ref)) {
                            is_reexported = true;
                            break;
                        }
                    }
                    if (!is_reexported) {
                        * = itb.swapRemove(ns_ref);
                    }
                }
            }
        }
    }
}
