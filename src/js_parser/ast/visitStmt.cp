pub fn VisitStmt(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        const createDefaultName = P.createDefaultName;
        const is_typescript_enabled = P.is_typescript_enabled;

        static fn visitAndAppendStmt(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt): anyerror!void {
            // By default any statement ends the const local prefix
            const was_after_after_const_local_prefix = p.current_scope.is_after_const_local_prefix;
            p.current_scope.is_after_const_local_prefix = true;

            switch (std.meta.activeTag(stmt.data)) {
                .s_directive, .s_comment, .s_empty => {
                    p.current_scope.is_after_const_local_prefix = was_after_after_const_local_prefix;
                    stmts.append(stmt.*)?;
                },
                .s_type_script => {
                    p.current_scope.is_after_const_local_prefix = was_after_after_const_local_prefix;
                    return;
                },
                .s_debugger => {
                    p.current_scope.is_after_const_local_prefix = was_after_after_const_local_prefix;
                    if (p.define.drop_debugger) {
                        return;
                    }
                    stmts.append(stmt.*)?;
                },

                inline .s_enum, .s_local => |tag| return @field(visitors, @tagName(tag))(p, stmts, stmt, @field(stmt.data, @tagName(tag)), was_after_after_const_local_prefix),
                inline else => |tag| return @field(visitors, @tagName(tag))(p, stmts, stmt, @field(stmt.data, @tagName(tag))),

                // Only used by the bundler for lazy export ASTs.
                .s_lazy_export => unreachable,
            }
        }

        const visitors = VisitStmtVisitors(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
    };
}

fn VisitStmtVisitors(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        const createDefaultName = P.createDefaultName;
        const is_typescript_enabled = P.is_typescript_enabled;
        static fn s_import(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Import)! {
            p.recordDeclaredSymbol(data.namespace_ref)?;

            if (default_name := data.default_name) {
                p.recordDeclaredSymbol(default_name.ref.?)?;
            }

            if (data.items.len > 0) {
                for (*item of data.items) {
                    p.recordDeclaredSymbol(item.name.ref.?)?;
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_export_clause(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.ExportClause)! {
            // "export {foo}"
            let end: usize = 0;
            let any_replaced = false;
            if (p.options.features.replace_exports.count() > 0) {
                for (*item of data.items) {
                    const name = p.loadNameFromRef(item.name.ref.?);

                    const symbol = p.findSymbol(item.alias_loc, name)?;
                    const sym_ref = symbol.ref;

                    if (entry := p.options.features.replace_exports.getPtr(name)) {
                        if (entry.* != .replace) p.ignoreUsage(symbol.ref);
                        * = p.injectReplacementExport(stmts, symbol.ref, stmt.loc, entry);
                        any_replaced = true;
                        continue;
                    }

                    if (p.symbols.items[sym_ref.innerIndex()].kind == .unbound) {
                        // Silently strip exports of non-local symbols in TypeScript, since
                        // those likely correspond to type-only exports. But report exports of
                        // non-local symbols as errors in JavaScript.
                        if (!is_typescript_enabled) {
                            const r = js_lexer.rangeOfIdentifier(p.source, item.name.loc);
                            p.log.addRangeErrorFmt(p.source, r, p.allocator, "\"{s}\" is not declared in this file", .{name})?;
                        }
                        continue;
                    }

                    item.name.ref = sym_ref;
                    data.items[end] = item;
                    end += 1;
                }
            } else {
                for (*item of data.items) {
                    const name = p.loadNameFromRef(item.name.ref.?);
                    const symbol = p.findSymbol(item.alias_loc, name)?;
                    const sym_ref = symbol.ref;

                    if (p.symbols.items[sym_ref.innerIndex()].kind == .unbound) {
                        // Silently strip exports of non-local symbols in TypeScript, since
                        // those likely correspond to type-only exports. But report exports of
                        // non-local symbols as errors in JavaScript.
                        if (!is_typescript_enabled) {
                            const r = js_lexer.rangeOfIdentifier(p.source, item.name.loc);
                            p.log.addRangeErrorFmt(p.source, r, p.allocator, "\"{s}\" is not declared in this file", .{name})?;
                            continue;
                        }
                        continue;
                    }

                    item.name.ref = sym_ref;
                    data.items[end] = item;
                    end += 1;
                }
            }

            const remove_for_tree_shaking = any_replaced && end == 0 && data.items.len > 0 && p.options.tree_shaking;
            data.items.len = end;

            if (remove_for_tree_shaking) {
                return;
            }

            stmts.append(stmt.*)?;
        }
        static fn s_export_from(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.ExportFrom)! {

            // "export {foo} from 'path'"
            const name = p.loadNameFromRef(data.namespace_ref);

            data.namespace_ref = p.newSymbol(.other, name)?;
            p.current_scope.generated.append(p.allocator, data.namespace_ref)?;
            p.recordDeclaredSymbol(data.namespace_ref)?;

            if (p.options.features.replace_exports.count() > 0) {
                let j: usize = 0;
                // This is a re-export and the symbols created here are used to reference
                for (item of data.items) {
                    const old_ref = item.name.ref.?;

                    if (p.options.features.replace_exports.count() > 0) {
                        if (entry := p.options.features.replace_exports.getPtr(item.alias)) {
                            * = p.injectReplacementExport(stmts, old_ref, logger.Loc.Empty, entry);

                            continue;
                        }
                    }

                    const _name = p.loadNameFromRef(old_ref);

                    const sym_ref = p.newSymbol(.@"import", _name)?;
                    p.current_scope.generated.append(p.allocator, sym_ref)?;
                    p.recordDeclaredSymbol(sym_ref)?;
                    data.items[j] = item;
                    data.items[j].name.ref = sym_ref;
                    j += 1;
                }

                data.items.len = j;

                if (j == 0 && data.items.len > 0) {
                    return;
                }
            } else {
                // This is a re-export and the symbols created here are used to reference
                for (*item of data.items) {
                    const _name = p.loadNameFromRef(item.name.ref.?);
                    const sym_ref = p.newSymbol(.@"import", _name)?;
                    p.current_scope.generated.append(p.allocator, sym_ref)?;
                    p.recordDeclaredSymbol(sym_ref)?;
                    item.name.ref = sym_ref;
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_export_star(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.ExportStar)! {

            // "export * from 'path'"
            const name = p.loadNameFromRef(data.namespace_ref);
            data.namespace_ref = p.newSymbol(.other, name)?;
            p.current_scope.generated.append(p.allocator, data.namespace_ref)?;
            p.recordDeclaredSymbol(data.namespace_ref)?;

            // "export * as ns from 'path'"
            if (alias := data.alias) {
                if (p.options.features.replace_exports.count() > 0) {
                    if (entry := p.options.features.replace_exports.getPtr(alias.original_name)) {
                        * = p.injectReplacementExport(stmts, p.declareSymbol(.other, logger.Loc.Empty, alias.original_name) catch unreachable, logger.Loc.Empty, entry);
                        return;
                    }
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_export_default(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.ExportDefault)! {
            defer {
                if (sym_ref := data.default_name.ref) {
                    p.recordDeclaredSymbol(sym_ref) catch unreachable;
                }
            }

            let mark_for_replace: bool = false;

            const orig_dead = p.is_control_flow_dead;
            if (p.options.features.replace_exports.count() > 0) {
                if (entry := p.options.features.replace_exports.getPtr("default")) {
                    p.is_control_flow_dead = p.options.features.dead_code_elimination && (entry.* != .replace);
                    mark_for_replace = true;
                }
            }

            defer {
                p.is_control_flow_dead = orig_dead;
            }

            switch (data.value) {
                .expr => |expr| {
                    const was_anonymous_named_expr = expr.isAnonymousNamed();
                    const prev_decorator_class_name = p.decorator_class_name;
                    if (was_anonymous_named_expr && expr.data == .e_class && expr.data.e_class.should_lower_standard_decorators) {
                        p.decorator_class_name = js_ast.ClauseItem.default_alias;
                    }
                    data.value.expr = p.visitExpr(expr);
                    p.decorator_class_name = prev_decorator_class_name;

                    if (p.is_control_flow_dead) {
                        return;
                    }

                    // Optionally preserve the name

                    data.value.expr = p.maybeKeepExprSymbolName(data.value.expr, js_ast.ClauseItem.default_alias, was_anonymous_named_expr);

                    // Discard type-only export default statements
                    if (is_typescript_enabled) {
                        switch (data.value.expr.data) {
                            .e_identifier => |ident| {
                                if (!ident.ref.isSourceContentsSlice()) {
                                    const symbol = p.symbols.items[ident.ref.innerIndex()];
                                    if (symbol.kind == .unbound) {
                                        if (local_type := p.local_type_names.get(symbol.original_name)) {
                                            if (local_type) {
                                                // the name points to a type
                                                // don't try to declare this symbol
                                                data.default_name.ref = null;
                                                return;
                                            }
                                        }
                                    }
                                }
                            },
                            else => {},
                        }
                    }

                    if (data.default_name.ref.?.isSourceContentsSlice()) {
                        data.default_name = createDefaultName(p, data.value.expr.loc) catch unreachable;
                    }

                    if (p.options.features.react_fast_refresh && switch (data.value.expr.data) {
                        .e_arrow => true,
                        .e_call => |call| switch (call.target.data) {
                            .e_identifier => |id| id.ref == p.react_refresh.latest_signature_ref,
                            else => false,
                        },
                        else => false,
                    }) {
                        // declare a temporary ref for this
                        const temp_id = p.generateTempRef("default_export");
                        p.current_scope.generated.append(p.allocator, temp_id)?;

                        try stmts.append(Stmt.alloc(S.Local, {
                            kind: .k_const,
                            decls: try G.Decl.List.fromSlice(p.allocator, &.{
                                {
                                    binding: Binding.alloc(p.allocator, B.Identifier{ ref: temp_id }, stmt.loc),
                                    value: data.value.expr,
                                },
                            }),
                        }, stmt.loc));

                        data.value = { expr: .initIdentifier(temp_id, stmt.loc) };

                        p.emitReactRefreshRegister(stmts, "default", temp_id, .default)?;
                    }

                    if (p.options.features.server_components.wrapsExports()) {
                        data.value.expr = p.wrapValueForServerComponentReference(data.value.expr, "default");
                    }

                    // If there are lowered "using" declarations, change this into a "var"
                    if (p.current_scope.parent == null && p.will_wrap_module_in_try_catch_for_using) {
                        stmts.ensureUnusedCapacity(2)?;

                        const decls = bun.handleOom(p.allocator.alloc(G.Decl, 1));
                        decls[0] = {
                            binding: p.b(B.Identifier{ ref: data.default_name.ref.? }, data.default_name.loc),
                            value: data.value.expr,
                        };
                        stmts.appendAssumeCapacity(p.s(S.Local{
                            decls: G.Decl.List.fromOwnedSlice(decls),
                        }, stmt.loc));
                        const items = bun.handleOom(p.allocator.alloc(js_ast.ClauseItem, 1));
                        items[0] = js_ast.ClauseItem{
                            alias: "default",
                            alias_loc: data.default_name.loc,
                            name: data.default_name,
                        };
                        stmts.appendAssumeCapacity(p.s(S.ExportClause{
                            items: items,
                            is_single_line: false,
                        }, stmt.loc));
                    }

                    if (mark_for_replace) {
                        const entry = p.options.features.replace_exports.getPtr("default").?;
                        if (entry.* == .replace) {
                            data.value.expr = entry.replace;
                        } else {
                            * = p.injectReplacementExport(stmts, Ref.None, logger.Loc.Empty, entry);
                            return;
                        }
                    }
                },

                .stmt => |s2| switch (s2.data) {
                    .s_function => |func| {
                        let name: string = js_ast.ClauseItem.default_alias;
                        if (func_loc := func.func.name) {
                            name = p.loadNameFromRef(func_loc.ref.?);
                        } else {
                            func.func.name = data.default_name;
                        }

                        let react_hook_data: ReactRefresh.HookContext? = null;
                        const prev = p.react_refresh.hook_ctx_storage;
                        defer p.react_refresh.hook_ctx_storage = prev;
                        p.react_refresh.hook_ctx_storage = &react_hook_data;

                        func.func = p.visitFunc(func.func, func.func.open_parens_loc);

                        if (p.is_control_flow_dead) {
                            return;
                        }

                        if (data.default_name.ref.?.isSourceContentsSlice()) {
                            data.default_name = createDefaultName(p, stmt.loc) catch unreachable;
                        }

                        if (*hook := react_hook_data) {
                            bun.handleOom(stmts.append(p.getReactRefreshHookSignalDecl(hook.signature_cb)));

                            data.value = {
                                expr: p.getReactRefreshHookSignalInit(hook, p.newExpr(
                                    E.Function{ func: func.func },
                                    stmt.loc,
                                )),
                            };
                        }

                        if (mark_for_replace) {
                            const entry = p.options.features.replace_exports.getPtr("default").?;
                            if (entry.* == .replace) {
                                data.value = { expr: entry.replace };
                            } else {
                                * = p.injectReplacementExport(stmts, Ref.None, logger.Loc.Empty, entry);
                                return;
                            }
                        }

                        if (p.options.features.react_fast_refresh &&
                            (ReactRefresh.isComponentishName(name) || bun.strings.eqlComptime(name, "default")))
                        {
                            // If server components or react refresh had wrapped the value (convert to .expr)
                            // then a temporary variable must be emitted.
                            //
                            // > export default _s(function App() { ... }, "...")
                            // > $RefreshReg(App, "App.tsx:default")
                            //
                            // > const default_export = _s(function App() { ... }, "...")
                            // > export default default_export;
                            // > $RefreshReg(default_export, "App.tsx:default")
                            const sym_ref = if (data.value == .expr) emit_temp_var: {
                                const ref_to_use = brk: {
                                    if (*loc_ref := func.func.name) {
                                        // Input:
                                        //
                                        //  export default function Foo() {}
                                        //
                                        // Output:
                                        //
                                        //  const Foo = _s(function Foo() {})
                                        //  export default Foo;
                                        if (sym_ref := loc_ref.ref) break :brk sym_ref;
                                    }

                                    const temp_id = p.generateTempRef("default_export");
                                    p.current_scope.generated.append(p.allocator, temp_id)?;
                                    break :brk temp_id;
                                };

                                stmts.append(Stmt.alloc(S.Local, {
                                    kind: .k_const,
                                    decls: try G.Decl.List.fromSlice(p.allocator, &.{
                                        {
                                            binding: Binding.alloc(p.allocator, B.Identifier{ ref: ref_to_use }, stmt.loc),
                                            value: data.value.expr,
                                        },
                                    }),
                                }, stmt.loc)) catch |e| bun.handleOom(e);

                                data.value = { expr: .initIdentifier(ref_to_use, stmt.loc) };

                                break :emit_temp_var ref_to_use;
                            } else data.default_name.ref.?;

                            if (p.options.features.server_components.wrapsExports()) {
                                data.value = { expr: p.wrapValueForServerComponentReference(if (data.value == .expr) data.value.expr else p.newExpr(E.Function{ func: func.func }, stmt.loc), "default") };
                            }

                            stmts.append(stmt.*)?;
                            p.emitReactRefreshRegister(stmts, name, sym_ref, .default)?;
                        } else {
                            if (p.options.features.server_components.wrapsExports()) {
                                data.value = { expr: p.wrapValueForServerComponentReference(p.newExpr(E.Function{ func: func.func }, stmt.loc), "default") };
                            }

                            stmts.append(stmt.*)?;
                        }

                        // if (func.func.name != null and func.func.name.?.ref != null) {
                        //     stmts.append(p.keepStmtSymbolName(func.func.name.?.loc, func.func.name.?.ref.?, name)) catch unreachable;
                        // }
                        return;
                    },
                    .s_class => |cls| {
                        * = p.visitClass(s2.loc, &cls.class, data.default_name.ref.?);

                        if (p.is_control_flow_dead)
                            return;

                        if (mark_for_replace) {
                            const entry = p.options.features.replace_exports.getPtr("default").?;
                            if (entry.* == .replace) {
                                data.value = { expr: entry.replace };
                            } else {
                                * = p.injectReplacementExport(stmts, Ref.None, logger.Loc.Empty, entry);
                                return;
                            }
                        }

                        if (data.default_name.ref.?.isSourceContentsSlice()) {
                            data.default_name = createDefaultName(p, stmt.loc) catch unreachable;
                        }

                        // We only inject a name into classes when there is a decorator
                        if (cls.class.has_decorators) {
                            if (cls.class.class_name == null ||
                                cls.class.class_name.?.ref == null)
                            {
                                cls.class.class_name = data.default_name;
                            }
                        }

                        // Lower the class (handles both TS legacy and standard decorators).
                        // Standard decorator lowering may produce prefix statements
                        // (variable declarations) before the class statement.
                        let class_stmts = p.lowerClass({ stmt: s2 });

                        // Find the s_class statement in the returned list
                        let class_stmt_idx: usize = 0;
                        let idx: usize = 0;
                        while (idx < class_stmts.len) : (idx += 1) {
                            const cs = class_stmts[idx];
                            if (cs.data == .s_class) {
                                class_stmt_idx = idx;
                                break;
                            }
                        }

                        // Emit any prefix statements before the export default
                        stmts.appendSlice(class_stmts[0..class_stmt_idx]) catch {};

                        data.value.stmt = class_stmts[class_stmt_idx];
                        stmts.append(stmt.*) catch {};

                        // Emit any suffix statements after the export default
                        if (class_stmt_idx + 1 < class_stmts.len) {
                            stmts.appendSlice(class_stmts[class_stmt_idx + 1 ..]) catch {};
                        }

                        if (p.options.features.server_components.wrapsExports()) {
                            data.value = { expr: p.wrapValueForServerComponentReference(p.newExpr(cls.class, stmt.loc), "default") };
                        }

                        return;
                    },
                    else => {},
                },
            }

            stmts.append(stmt.*)?;
        }
        static fn s_function(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Function)! {
            // We mark it as dead, but the value may not actually be dead
            // We just want to be sure to not increment the usage counts for anything in the function
            const mark_as_dead = p.options.features.dead_code_elimination && data.func.flags.contains(.is_export) &&
                p.options.features.replace_exports.count() > 0 && p.isExportToEliminate(data.func.name.?.ref.?);
            const original_is_dead = p.is_control_flow_dead;

            if (mark_as_dead) {
                p.is_control_flow_dead = true;
            }
            defer {
                if (mark_as_dead) {
                    p.is_control_flow_dead = original_is_dead;
                }
            }

            let react_hook_data: ReactRefresh.HookContext? = null;
            const prev = p.react_refresh.hook_ctx_storage;
            defer p.react_refresh.hook_ctx_storage = prev;
            p.react_refresh.hook_ctx_storage = &react_hook_data;

            data.func = p.visitFunc(data.func, data.func.open_parens_loc);

            const name_ref = data.func.name.?.ref.?;
            bun.assert(name_ref.tag == .symbol);
            const name_symbol = &p.symbols.items[name_ref.innerIndex()];
            const original_name = name_symbol.original_name;

            // Handle exporting this function from a namespace
            if (data.func.flags.contains(.is_export) && p.enclosing_namespace_arg_ref != null) {
                data.func.flags.remove(.is_export);

                const enclosing_namespace_arg_ref = p.enclosing_namespace_arg_ref ?? bun.outOfMemory();
                bun.handleOom(stmts.ensureUnusedCapacity(3));
                stmts.appendAssumeCapacity(stmt.*);
                stmts.appendAssumeCapacity(Stmt.assign(
                    p.newExpr(E.Dot{
                        target: p.newExpr(E.Identifier{ ref: enclosing_namespace_arg_ref }, stmt.loc),
                        name: original_name,
                        name_loc: data.func.name.?.loc,
                    }, stmt.loc),
                    p.newExpr(E.Identifier{ ref: data.func.name.?.ref.? }, data.func.name.?.loc),
                ));
            } else if (!mark_as_dead) {
                if (name_symbol.remove_overwritten_function_declaration) {
                    return;
                }

                if (p.options.features.server_components.wrapsExports() && data.func.flags.contains(.is_export)) {
                    // Convert this into `export var <name> = registerClientReference(<func>, ...);`
                    const name = data.func.name.?;
                    // From the inner scope, have code reference the wrapped function.
                    data.func.name = null;
                    try stmts.append(p.s(S.Local{
                        kind: .k_var,
                        is_export: true,
                        decls: try G.Decl.List.fromSlice(p.allocator, &.{{
                            binding: p.b(B.Identifier{ ref: name_ref }, name.loc),
                            value: p.wrapValueForServerComponentReference(
                                p.newExpr(E.Function{ func: data.func }, stmt.loc),
                                original_name,
                            ),
                        }}),
                    }, stmt.loc));
                } else {
                    bun.handleOom(stmts.append(stmt.*));
                }
            } else if (mark_as_dead) {
                if (replacement := p.options.features.replace_exports.getPtr(original_name)) {
                    * = p.injectReplacementExport(stmts, name_ref, data.func.name.?.loc, replacement);
                }
            }

            if (p.options.features.react_fast_refresh) {
                if (*hook := react_hook_data) {
                    stmts.append(p.getReactRefreshHookSignalDecl(hook.signature_cb))?;
                    try stmts.append(p.s(S.SExpr{
                        value: p.getReactRefreshHookSignalInit(hook, Expr.initIdentifier(name_ref, logger.Loc.Empty)),
                    }, logger.Loc.Empty));
                }

                if (p.current_scope == p.module_scope) {
                    p.handleReactRefreshRegister(stmts, original_name, name_ref, .named)?;
                }
            }

            return;
        }

        static fn s_class(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Class)! {
            const mark_as_dead = p.options.features.dead_code_elimination && data.is_export &&
                p.options.features.replace_exports.count() > 0 && p.isExportToEliminate(data.class.class_name.?.ref.?);
            const original_is_dead = p.is_control_flow_dead;

            if (mark_as_dead) {
                p.is_control_flow_dead = true;
            }
            defer {
                if (mark_as_dead) {
                    p.is_control_flow_dead = original_is_dead;
                }
            }

            * = p.visitClass(stmt.loc, &data.class, Ref.None);

            // Remove the export flag inside a namespace
            const was_export_inside_namespace = data.is_export && p.enclosing_namespace_arg_ref != null;
            if (was_export_inside_namespace) {
                data.is_export = false;
            }

            const lowered = p.lowerClass(js_ast.StmtOrExpr{ stmt: stmt.* });

            if (!mark_as_dead || was_export_inside_namespace)
                // Lower class field syntax for browsers that don't support it
                stmts.appendSlice(lowered) catch unreachable
            else {
                const sym_ref = data.class.class_name.?.ref.?;
                if (replacement := p.options.features.replace_exports.getPtr(p.loadNameFromRef(sym_ref))) {
                    if (p.injectReplacementExport(stmts, sym_ref, data.class.class_name.?.loc, replacement)) {
                        p.is_control_flow_dead = original_is_dead;
                    }
                }
            }

            // Handle exporting this class from a namespace
            if (was_export_inside_namespace) {
                stmts.append(
                    Stmt.assign(
                        p.newExpr(
                            E.Dot{
                                target: p.newExpr(
                                    E.Identifier{ ref: p.enclosing_namespace_arg_ref.? },
                                    stmt.loc,
                                ),
                                name: p.symbols.items[data.class.class_name.?.ref.?.innerIndex()].original_name,
                                name_loc: data.class.class_name.?.loc,
                            },
                            stmt.loc,
                        ),
                        p.newExpr(
                            E.Identifier{ ref: data.class.class_name.?.ref.? },
                            data.class.class_name.?.loc,
                        ),
                    ),
                ) catch unreachable;
            }

            return;
        }
        static fn s_export_equals(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.ExportEquals)! {
            // "module.exports = value"
            stmts.append(
                Stmt.assign(
                    p.@"module.exports"(stmt.loc),
                    p.visitExpr(data.value),
                ),
            ) catch unreachable;
            p.recordUsage(p.module_ref);
            return;
        }
        static fn s_break(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Break)! {
            if (*label := data.label) {
                const name = p.loadNameFromRef(label.ref ?? p.panicLoc("Expected label to have a ref", {}, label.loc));
                const res = p.findLabelSymbol(label.loc, name);
                if (res.found) {
                    label.ref = res.ref;
                } else {
                    data.label = null;
                }
            } else if (!p.fn_or_arrow_data_visit.is_inside_loop && !p.fn_or_arrow_data_visit.is_inside_switch) {
                const r = js_lexer.rangeOfIdentifier(p.source, stmt.loc);
                p.log.addRangeError(p.source, r, "Cannot use \"break\" here") catch unreachable;
            }

            stmts.append(stmt.*)?;
        }
        static fn s_continue(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Continue)! {
            if (*label := data.label) {
                const name = p.loadNameFromRef(label.ref ?? p.panicLoc("Expected continue label to have a ref", {}, label.loc));
                const res = p.findLabelSymbol(label.loc, name);
                label.ref = res.ref;
                if (res.found && !res.is_loop) {
                    const r = js_lexer.rangeOfIdentifier(p.source, stmt.loc);
                    p.log.addRangeErrorFmt(p.source, r, p.allocator, "Cannot \"continue\" to label {s}", .{name}) catch unreachable;
                }
            } else if (!p.fn_or_arrow_data_visit.is_inside_loop) {
                const r = js_lexer.rangeOfIdentifier(p.source, stmt.loc);
                p.log.addRangeError(p.source, r, "Cannot use \"continue\" here") catch unreachable;
            }

            stmts.append(stmt.*)?;
        }
        static fn s_label(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Label)! {
            p.pushScopeForVisitPass(.label, stmt.loc) catch unreachable;
            const name = p.loadNameFromRef(data.name.ref.?);
            const sym_ref = p.newSymbol(.label, name) catch unreachable;
            data.name.ref = sym_ref;
            p.current_scope.label_ref = sym_ref;
            switch (data.stmt.data) {
                .s_for, .s_for_in, .s_for_of, .s_while, .s_do_while => {
                    p.current_scope.label_stmt_is_loop = true;
                },
                else => {},
            }

            data.stmt = p.visitSingleStmt(data.stmt, StmtsKind.none);
            p.popScope();

            stmts.append(stmt.*)?;
        }
        static fn s_local(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Local, was_after_after_const_local_prefix: bool)! {
            // TODO: Silently remove unsupported top-level "await" in dead code branches
            // (this was from 'await using' syntax)

            // Local statements do not end the const local prefix
            p.current_scope.is_after_const_local_prefix = was_after_after_const_local_prefix;

            const decls_len = if (!(data.is_export && p.options.features.replace_exports.entries.len > 0))
                p.visitDecls(data.decls.slice(), data.kind == .k_const, false)
            else
                p.visitDecls(data.decls.slice(), data.kind == .k_const, true);

            const is_now_dead = data.decls.len > 0 && decls_len == 0;
            if (is_now_dead) {
                return;
            }

            data.decls.len = @as(u32, @truncate(decls_len));

            // Handle being exported inside a namespace
            if (data.is_export && p.enclosing_namespace_arg_ref != null) {
                for (*d of data.decls.slice()) {
                    if (val := d.value) {
                        p.recordUsage((p.enclosing_namespace_arg_ref ?? unreachable));
                        // TODO: is it necessary to lowerAssign? why does esbuild do it _most_ of the time?
                        stmts.append(p.s(S.SExpr{
                            value: Expr.assign(Binding.toExpr(&d.binding, p.to_expr_wrapper_namespace), val),
                        }, stmt.loc)) catch unreachable;
                    }
                }

                return;
            }

            // Optimization: Avoid unnecessary "using" machinery by changing ones
            // initialized to "null" or "undefined" into a normal variable. Note that
            // "await using" still needs the "await", so we can't do it for those.
            if (p.options.features.minify_syntax && data.kind == .k_using) {
                data.kind = .k_let;
                for (*d of data.decls.slice()) {
                    if (val := d.value) {
                        if (val.data != .e_null && val.data != .e_undefined) {
                            data.kind = .k_using;
                            break;
                        }
                    }
                }
            }

            // We must relocate vars in order to safely handle removing if/else depending on NODE_ENV.
            // Edgecase:
            //  `export var` is skipped because it's unnecessary. That *should* be a noop, but it loses the `is_export` flag if we're in HMR.
            const kind = p.selectLocalKind(data.kind);
            if (kind == .k_var && !data.is_export) {
                const relocated = p.maybeRelocateVarsToTopLevel(data.decls.slice(), .normal);
                if (relocated.ok) {
                    if (new_stmt := relocated.stmt) {
                        stmts.append(new_stmt) catch unreachable;
                    }

                    return;
                }
            }

            data.kind = kind;
            stmts.append(stmt.*)?;

            if (p.options.features.react_fast_refresh && p.current_scope == p.module_scope) {
                for (decl of data.decls.slice()) try_register: {
                    if (decl.value == null) break :try_register;
                    const val = decl.value.?;
                    switch (val.data) {
                        // Assigning a component to a local.
                        .e_arrow, .e_function => {},

                        // A wrapped component.
                        .e_call => |call| switch (call.target.data) {
                            .e_identifier => |id| if (id.ref != p.react_refresh.latest_signature_ref)
                                break :try_register,
                            else => break :try_register,
                        },
                        else => break :try_register,
                    }
                    const id = switch (decl.binding.data) {
                        .b_identifier => |id| id.ref,
                        else => break :try_register,
                    };
                    const original_name = p.symbols.items[id.innerIndex()].original_name;
                    p.handleReactRefreshRegister(stmts, original_name, id, .named)?;
                }
            }

            if (data.is_export && p.options.features.server_components.wrapsExports()) {
                for (*decl of data.decls.slice()) try_annotate: {
                    if (decl.value == null) break :try_annotate;
                    const val = decl.value.?;
                    const id = switch (decl.binding.data) {
                        .b_identifier => |id| id.ref,
                        else => break :try_annotate,
                    };
                    const original_name = p.symbols.items[id.innerIndex()].original_name;
                    decl.value = p.wrapValueForServerComponentReference(val, original_name);
                    break :try_annotate;
                }
            }

            return;
        }
        static fn s_expr(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.SExpr)! {
            const should_trim_primitive = p.options.features.dead_code_elimination &&
                (p.options.features.minify_syntax && data.value.isPrimitiveLiteral());
            p.stmt_expr_value = data.value.data;
            defer p.stmt_expr_value = { e_missing: {} };

            const is_top_level = p.current_scope == p.module_scope;
            if (p.shouldUnwrapCommonJSToESM()) {
                p.commonjs_named_exports_needs_conversion = if (is_top_level)
                    std.math.maxInt(u32)
                else
                    p.commonjs_named_exports_needs_conversion;
            }

            data.value = p.visitExpr(data.value);

            if (should_trim_primitive && data.value.isPrimitiveLiteral()) {
                return;
            }

            // simplify unused
            if (simplified := SideEffects.simplifyUnusedExpr(p, data.value)) {
                data.value = simplified;
            } else {
                return;
            }

            if (p.shouldUnwrapCommonJSToESM()) {
                if (is_top_level) {
                    if (data.value.data == .e_binary) {
                        const to_convert = p.commonjs_named_exports_needs_conversion;
                        if (to_convert != std.math.maxInt(u32)) {
                            p.commonjs_named_exports_needs_conversion = std.math.maxInt(u32);
                            convert: {
                                const bin: *E.Binary = data.value.data.e_binary;
                                if (bin.op == .bin_assign && bin.left.data == .e_commonjs_export_identifier) {
                                    let last = &p.commonjs_named_exports.values()[to_convert];
                                    if (!last.needs_decl) break :convert;
                                    last.needs_decl = false;

                                    let decls = p.allocator.alloc(Decl, 1) catch unreachable;
                                    const sym_ref = bin.left.data.e_commonjs_export_identifier.ref;
                                    decls[0] = {
                                        binding: p.b(B.Identifier{ ref: sym_ref }, bin.left.loc),
                                        value: bin.right,
                                    };
                                    // we have to ensure these are known to be top-level
                                    p.declared_symbols.append(p.allocator, {
                                        ref: sym_ref,
                                        is_top_level: true,
                                    }) catch unreachable;
                                    p.esm_export_keyword.loc = stmt.loc;
                                    p.esm_export_keyword.len = 5;
                                    p.had_commonjs_named_exports_this_visit = true;
                                    let clause_items = p.allocator.alloc(js_ast.ClauseItem, 1) catch unreachable;
                                    clause_items[0] = js_ast.ClauseItem{
                                        // We want the generated name to not conflict
                                        alias: p.commonjs_named_exports.keys()[to_convert],
                                        alias_loc: bin.left.loc,
                                        name: {
                                            ref: sym_ref,
                                            loc: last.loc_ref.loc,
                                        },
                                    };
                                    const cjs_stmts = [
                                        p.s(
                                            S.Local{
                                                kind: .k_var,
                                                is_export: false,
                                                was_commonjs_export: true,
                                                decls: G.Decl.List.fromOwnedSlice(decls),
                                            },
                                            stmt.loc,
                                        ),
                                        p.s(
                                            S.ExportClause{
                                                items: clause_items,
                                                is_single_line: true,
                                            },
                                            stmt.loc,
                                        ),
                                    ];
                                    stmts.appendSlice(&cjs_stmts) catch unreachable;

                                    return;
                                }
                            }
                        } else if (p.commonjs_replacement_stmts.len > 0) {
                            if (stmts.items.len == 0) {
                                stmts.items = p.commonjs_replacement_stmts;
                                stmts.capacity = p.commonjs_replacement_stmts.len;
                                p.commonjs_replacement_stmts.len = 0;
                            } else {
                                stmts.appendSlice(p.commonjs_replacement_stmts) catch unreachable;
                                p.commonjs_replacement_stmts.len = 0;
                            }

                            return;
                        }
                    }
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_throw(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Throw)! {
            data.value = p.visitExpr(data.value);
            stmts.append(stmt.*)?;
        }
        static fn s_return(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Return)! {
            // Forbid top-level return inside modules with ECMAScript-style exports
            if (p.fn_or_arrow_data_visit.is_outside_fn_or_arrow) {
                const where = where: {
                    if (p.esm_export_keyword.len > 0) {
                        break :where p.esm_export_keyword;
                    } else if (p.top_level_await_keyword.len > 0) {
                        break :where p.top_level_await_keyword;
                    } else {
                        break :where logger.Range.None;
                    }
                };

                if (where.len > 0) {
                    p.log.addRangeError(p.source, where, "Top-level return cannot be used inside an ECMAScript module") catch unreachable;
                }
            }

            if (val := data.value) {
                data.value = p.visitExpr(val);

                // "return undefined;" can safely just always be "return;"
                if (data.value != null && std.meta.activeTag(data.value.?.data) == .e_undefined) {
                    // Returning undefined is implicit
                    data.value = null;
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_block(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Block)! {
            {
                p.pushScopeForVisitPass(.block, stmt.loc) catch unreachable;

                // Pass the "is loop body" status on to the direct children of a block used
                // as a loop body. This is used to enable optimizations specific to the
                // topmost scope in a loop body block.
                const kind = if (std.@"meta".eql(p.loop_body, stmt.data)) StmtsKind.loop_body else StmtsKind.none;
                let _stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, data.stmts);
                p.visitStmts(&_stmts, kind) catch unreachable;
                data.stmts = _stmts.items;
                p.popScope();
            }

            if (p.options.features.minify_syntax) {
                // // trim empty statements
                if (data.stmts.len == 0) {
                    stmts.append(Stmt{ data: Prefill.Data.SEmpty, loc: stmt.loc }) catch unreachable;
                    return;
                } else if (data.stmts.len == 1 && !statementCaresAboutScope(data.stmts[0])) {
                    // Unwrap blocks containing a single statement
                    stmts.append(data.stmts[0]) catch unreachable;
                    return;
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_with(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.With)! {
            data.value = p.visitExpr(data.value);

            p.pushScopeForVisitPass(.with, data.body_loc) catch unreachable;

            // This can be many different kinds of statements.
            // example code:
            //
            //      with(this.document.defaultView || Object.create(null))
            //         with(this.document)
            //           with(this.form)
            //             with(this.element)
            //
            data.body = p.visitSingleStmt(data.body, StmtsKind.none);

            p.popScope();
            stmts.append(stmt.*)?;
        }
        static fn s_while(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.While)! {
            data.test_ = p.visitExpr(data.test_);
            data.body = p.visitLoopBody(data.body);

            data.test_ = SideEffects.simplifyBoolean(p, data.test_);
            const result = SideEffects.toBoolean(p, data.test_.data);
            if (result.ok && result.side_effects == .no_side_effects) {
                data.test_ = p.newExpr(E.Boolean{ value: result.value }, data.test_.loc);
            }

            stmts.append(stmt.*)?;
        }
        static fn s_do_while(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.DoWhile)! {
            data.body = p.visitLoopBody(data.body);
            data.test_ = p.visitExpr(data.test_);

            data.test_ = SideEffects.simplifyBoolean(p, data.test_);
            stmts.append(stmt.*)?;
        }
        static fn s_if(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.If)! {
            const prev_in_branch = p.in_branch_condition;
            p.in_branch_condition = true;
            data.test_ = p.visitExpr(data.test_);
            p.in_branch_condition = prev_in_branch;

            if (p.options.features.minify_syntax) {
                data.test_ = SideEffects.simplifyBoolean(p, data.test_);
            }

            const effects = SideEffects.toBoolean(p, data.test_.data);
            if (effects.ok && !effects.value) {
                const old = p.is_control_flow_dead;
                p.is_control_flow_dead = true;
                data.yes = p.visitSingleStmt(data.yes, StmtsKind.none);
                p.is_control_flow_dead = old;
            } else {
                data.yes = p.visitSingleStmt(data.yes, StmtsKind.none);
            }

            // The "else" clause is optional
            if (no := data.no) {
                if (effects.ok && effects.value) {
                    const old = p.is_control_flow_dead;
                    p.is_control_flow_dead = true;
                    defer p.is_control_flow_dead = old;
                    data.no = p.visitSingleStmt(no, .none);
                } else {
                    data.no = p.visitSingleStmt(no, .none);
                }

                // Trim an "else" clause whose body was emptied by dead-code
                // elimination. This avoids emitting `else {}` for e.g.
                // `if (true) { A } else { B }` where B was pruned. Gated on
                // the union of DCE and minify_syntax so the pre-existing
                // `deadCodeElimination: false, minify: { syntax: true }`
                // configuration (which already dropped the `else {}`)
                // doesn't regress to `else ;`.
                if (p.options.features.dead_code_elimination || p.options.features.minify_syntax) {
                    if (no_stmt := data.no) {
                        const no_is_empty = switch (no_stmt.data) {
                            .s_empty => true,
                            .s_block => |block| block.stmts.len == 0,
                            else => false,
                        };
                        if (no_is_empty) {
                            data.no = null;
                        }
                    }
                }
            }

            if (p.options.features.minify_syntax) {
                if (effects.ok) {
                    if (effects.value) {
                        if (data.no == null || !SideEffects.shouldKeepStmtInDeadControlFlow(data.no.?, p.allocator)) {
                            if (effects.side_effects == .could_have_side_effects) {
                                // Keep the condition if it could have side effects (but is still known to be truthy)
                                if (test_ := SideEffects.simplifyUnusedExpr(p, data.test_)) {
                                    stmts.append(p.s(S.SExpr{ value: test_ }, test_.loc)) catch unreachable;
                                }
                            }

                            return p.appendIfBodyPreservingScope(stmts, data.yes)?;
                        } else {
                            // We have to keep the "no" branch
                        }
                    } else {
                        // The test is falsy
                        if (!SideEffects.shouldKeepStmtInDeadControlFlow(data.yes, p.allocator)) {
                            if (effects.side_effects == .could_have_side_effects) {
                                // Keep the condition if it could have side effects (but is still known to be truthy)
                                if (test_ := SideEffects.simplifyUnusedExpr(p, data.test_)) {
                                    stmts.append(p.s(S.SExpr{ value: test_ }, test_.loc)) catch unreachable;
                                }
                            }

                            if (data.no == null) {
                                return;
                            }

                            return p.appendIfBodyPreservingScope(stmts, data.no.?)?;
                        }
                    }
                }

                // TODO: more if statement syntax minification
                const can_remove_test = p.exprCanBeRemovedIfUnused(&data.test_);
                switch (data.yes.data) {
                    .s_expr => |yes_expr| {
                        if (yes_expr.value.isMissing()) {
                            if (data.no == null) {
                                if (can_remove_test) {
                                    return;
                                }
                            } else if (data.no.?.isMissingExpr() && can_remove_test) {
                                return;
                            }
                        }
                    },
                    .s_empty => {
                        if (data.no == null) {
                            if (can_remove_test) {
                                return;
                            }
                        } else if (data.no.?.isMissingExpr() && can_remove_test) {
                            return;
                        }
                    },
                    else => {},
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_for(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.For)! {
            p.pushScopeForVisitPass(.block, stmt.loc) catch unreachable;

            if (initst := data.init) {
                data.init = p.visitForLoopInit(initst, false);
            }

            if (test_ := data.test_) {
                data.test_ = SideEffects.simplifyBoolean(p, p.visitExpr(test_));

                const result = SideEffects.toBoolean(p, data.test_.?.data);
                if (result.ok && result.value && result.side_effects == .no_side_effects) {
                    data.test_ = null;
                }
            }

            if (update := data.update) {
                data.update = p.visitExpr(update);
            }

            data.body = p.visitLoopBody(data.body);

            if (for_init := data.init) {
                if (for_init.data == .s_local) {
                    // Potentially relocate "var" declarations to the top level. Note that this
                    // must be done inside the scope of the for loop or they won't be relocated.
                    if (for_init.data.s_local.kind == .k_var) {
                        const relocate = p.maybeRelocateVarsToTopLevel(for_init.data.s_local.decls.slice(), .normal);
                        if (relocated := relocate.stmt) {
                            data.init = relocated;
                        }
                    }
                }
            }

            p.popScope();

            stmts.append(stmt.*)?;
        }
        static fn s_for_in(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.ForIn)! {
            {
                p.pushScopeForVisitPass(.block, stmt.loc) catch unreachable;
                defer p.popScope();
                * = p.visitForLoopInit(data.init, true);
                data.value = p.visitExpr(data.value);
                data.body = p.visitLoopBody(data.body);

                // Check for a variable initializer
                if (data.init.data == .s_local && data.init.data.s_local.kind == .k_var) {
                    // Lower for-in variable initializers in case the output is used in strict mode
                    let local = data.init.data.s_local;
                    if (local.decls.len == 1) {
                        let decl: *G.Decl = &local.decls.ptr[0];
                        if (decl.binding.data == .b_identifier) {
                            if (val := decl.value) {
                                stmts.append(
                                    Stmt.assign(
                                        Expr.initIdentifier(decl.binding.data.b_identifier.ref, decl.binding.loc),
                                        val,
                                    ),
                                ) catch unreachable;
                                decl.value = null;
                            }
                        }
                    }

                    const relocate = p.maybeRelocateVarsToTopLevel(data.init.data.s_local.decls.slice(), RelocateVars.Mode.for_in_or_for_of);
                    if (relocated_stmt := relocate.stmt) {
                        data.init = relocated_stmt;
                    }
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_for_of(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.ForOf)! {
            p.pushScopeForVisitPass(.block, stmt.loc) catch unreachable;
            defer p.popScope();
            * = p.visitForLoopInit(data.init, true);
            data.value = p.visitExpr(data.value);
            data.body = p.visitLoopBody(data.body);

            if (data.init.data == .s_local) {
                if (data.init.data.s_local.kind == .k_var) {
                    const relocate = p.maybeRelocateVarsToTopLevel(data.init.data.s_local.decls.slice(), RelocateVars.Mode.for_in_or_for_of);
                    if (relocated_stmt := relocate.stmt) {
                        data.init = relocated_stmt;
                    }
                }

                // Handle "for (using x of y)" and "for (await using x of y)"
                if (data.init.data == .s_local && data.init.data.s_local.kind.isUsing() && p.options.features.lower_using) {
                    // fn lowerUsingDeclarationInForOf()
                    const loc = data.init.loc;
                    const init2 = data.init.data.s_local;
                    const binding = init2.decls.at(0).binding;
                    let id = binding.data.b_identifier;
                    const temp_ref = p.generateTempRef(p.symbols.items[id.ref.inner_index].original_name);

                    const first = p.s(S.Local{
                        kind: init2.kind,
                        decls: bindings: {
                            const decls = bun.handleOom(p.allocator.alloc(G.Decl, 1));
                            decls[0] = {
                                binding: p.b(B.Identifier{ ref: id.ref }, loc),
                                value: p.newExpr(E.Identifier{ ref: temp_ref }, loc),
                            };
                            break :bindings G.Decl.List.fromOwnedSlice(decls);
                        },
                    }, loc);

                    const length = if (data.body.data == .s_block) data.body.data.s_block.stmts.len else 1;
                    let statements = bun.handleOom(p.allocator.alloc(Stmt, 1 + length));
                    statements[0] = first;
                    if (data.body.data == .s_block) {
                        @memcpy(statements[1..], data.body.data.s_block.stmts);
                    } else {
                        statements[1] = data.body;
                    }

                    let ctx = P.LowerUsingDeclarationsContext.init(p)?;
                    ctx.scanStmts(p, statements);
                    const visited_stmts = ctx.finalize(p, statements, p.will_wrap_module_in_try_catch_for_using && p.current_scope.parent == null);
                    if (data.body.data == .s_block) {
                        data.body.data.s_block.stmts = visited_stmts.items;
                    } else {
                        data.body = p.s(S.Block{
                            stmts: visited_stmts.items,
                        }, loc);
                    }
                    id.ref = temp_ref;
                    init2.kind = .k_const;
                }
            }

            stmts.append(stmt.*)?;
        }
        static fn s_try(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Try)! {
            p.pushScopeForVisitPass(.block, stmt.loc) catch unreachable;
            {
                let _stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, data.body);
                p.fn_or_arrow_data_visit.try_body_count += 1;
                p.visitStmts(&_stmts, StmtsKind.none) catch unreachable;
                p.fn_or_arrow_data_visit.try_body_count -= 1;
                data.body = _stmts.items;
            }
            p.popScope();

            if (*catch_ := data.catch_) {
                p.pushScopeForVisitPass(.catch_binding, catch_.loc) catch unreachable;
                {
                    if (catch_binding := catch_.binding) {
                        p.visitBinding(catch_binding, null);
                    }
                    let _stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, catch_.body);
                    p.pushScopeForVisitPass(.block, catch_.body_loc) catch unreachable;
                    p.visitStmts(&_stmts, StmtsKind.none) catch unreachable;
                    p.popScope();
                    catch_.body = _stmts.items;
                }
                p.popScope();
            }

            if (*finally := data.finally) {
                p.pushScopeForVisitPass(.block, finally.loc) catch unreachable;
                {
                    let _stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, finally.stmts);
                    p.visitStmts(&_stmts, StmtsKind.none) catch unreachable;
                    finally.stmts = _stmts.items;
                }
                p.popScope();
            }

            stmts.append(stmt.*)?;
        }
        static fn s_switch(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Switch)! {
            data.test_ = p.visitExpr(data.test_);
            {
                p.pushScopeForVisitPass(.block, data.body_loc) catch unreachable;
                defer p.popScope();
                const old_is_inside_Swsitch = p.fn_or_arrow_data_visit.is_inside_switch;
                p.fn_or_arrow_data_visit.is_inside_switch = true;
                defer p.fn_or_arrow_data_visit.is_inside_switch = old_is_inside_Swsitch;
                let i: usize = 0;
                while (i < data.cases.len) : (i += 1) {
                    const case = data.cases[i];
                    if (val := case.value) {
                        data.cases[i].value = p.visitExpr(val);
                        // TODO: error messages
                        // Check("case", *c.Value, c.Value.Loc)
                        //                 p.warnAboutTypeofAndString(s.Test, *c.Value)
                    }
                    let _stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, case.body);
                    p.visitStmts(&_stmts, StmtsKind.none) catch unreachable;
                    data.cases[i].body = _stmts.items;
                }
            }
            // TODO: duplicate case checker

            stmts.append(stmt.*)?;
        }

        static fn s_enum(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Enum, was_after_after_const_local_prefix: bool)! {

            // Do not end the const local prefix after TypeScript enums. We process
            // them first within their scope so that they are inlined into all code in
            // that scope. We don't want that to cause the const local prefix to end.
            p.current_scope.is_after_const_local_prefix = was_after_after_const_local_prefix;

            // Track cross-module enum constants during bundling. This
            // part of the code is different from esbuilt in that we are
            // only storing a list of enum indexes. At the time of
            // referencing, `esbuild` builds a separate hash map of hash
            // maps. We are avoiding that to reduce memory usage, since
            // enum inlining already uses alot of hash maps.
            if (p.current_scope == p.module_scope && p.options.bundle) {
                p.top_level_enums.append(p.allocator, data.name.ref.?)?;
            }

            p.recordDeclaredSymbol(data.name.ref.?)?;
            p.pushScopeForVisitPass(.entry, stmt.loc)?;
            defer p.popScope();
            p.recordDeclaredSymbol(data.arg)?;

            const allocator = p.allocator;
            // Scan ahead for any variables inside this namespace. This must be done
            // ahead of time before visiting any statements inside the namespace
            // because we may end up visiting the uses before the declarations.
            // We need to convert the uses into property accesses on the namespace.
            for (value of data.values) {
                if (value.ref.isValid()) {
                    p.is_exported_inside_namespace.put(allocator, value.ref, data.arg)?;
                }
            }

            // Values without initializers are initialized to one more than the
            // previous value if the previous value is numeric. Otherwise values
            // without initializers are initialized to undefined.
            let next_numeric_value: f64? = 0.0;

            let value_exprs = ListManaged(Expr).initCapacity(allocator, data.values.len)?;

            let all_values_are_pure = true;

            const exported_members = p.current_scope.ts_namespace.?.exported_members;

            // We normally don't fold numeric constants because they might increase code
            // size, but it's important to fold numeric constants inside enums since
            // that's what the TypeScript compiler does.
            const old_should_fold_typescript_constant_expressions = p.should_fold_typescript_constant_expressions;
            p.should_fold_typescript_constant_expressions = true;

            // Create an assignment for each enum value
            for (*value of data.values) {
                const name = value.name;

                let has_string_value = false;
                if (enum_value := value.value) {
                    next_numeric_value = null;

                    const visited = p.visitExpr(enum_value);

                    // "See through" any wrapped comments
                    const underlying_value = if (visited.data == .e_inlined_enum)
                        visited.data.e_inlined_enum.value
                    else
                        visited;
                    value.value = underlying_value;

                    switch (underlying_value.data) {
                        .e_number => |num| {
                            exported_members.getPtr(name).?.data = { enum_number: num.value };

                            p.ref_to_ts_namespace_member.put(
                                p.allocator,
                                value.ref,
                                { enum_number: num.value },
                            ) catch |e| bun.handleOom(e);

                            next_numeric_value = num.value + 1.0;
                        },
                        .e_string => |str| {
                            has_string_value = true;

                            exported_members.getPtr(name).?.data = { enum_string: str };

                            p.ref_to_ts_namespace_member.put(
                                p.allocator,
                                value.ref,
                                { enum_string: str },
                            ) catch |e| bun.handleOom(e);
                        },
                        else => {
                            if (visited.knownPrimitive() == .@"string") {
                                has_string_value = true;
                            }

                            if (!p.exprCanBeRemovedIfUnused(&visited)) {
                                all_values_are_pure = false;
                            }
                        },
                    }
                } else if (num := next_numeric_value) {
                    value.value = p.newExpr(E.Number{ value: num }, value.loc);

                    next_numeric_value = num + 1;

                    exported_members.getPtr(name).?.data = { enum_number: num };

                    p.ref_to_ts_namespace_member.put(
                        p.allocator,
                        value.ref,
                        { enum_number: num },
                    ) catch |e| bun.handleOom(e);
                } else {
                    value.value = p.newExpr(E.Undefined{}, value.loc);
                }

                const is_assign_target = p.options.features.minify_syntax && bun.js_lexer.isIdentifier(value.name);

                const name_as_e_string = if (!is_assign_target || !has_string_value)
                    p.newExpr(value.nameAsEString(allocator), value.loc)
                else
                    null;

                const assign_target = if (is_assign_target)
                    // "Enum.Name = value"
                    Expr.assign(
                        p.newExpr(E.Dot{
                            target: p.newExpr(
                                E.Identifier{ ref: data.arg },
                                value.loc,
                            ),
                            name: value.name,
                            name_loc: value.loc,
                        }, value.loc),
                        value.value.?,
                    )
                else
                    // "Enum['Name'] = value"
                    Expr.assign(
                        p.newExpr(E.Index{
                            target: p.newExpr(
                                E.Identifier{ ref: data.arg },
                                value.loc,
                            ),
                            index: name_as_e_string.?,
                        }, value.loc),
                        value.value.?,
                    );

                p.recordUsage(data.arg);

                // String-valued enums do not form a two-way map
                if (has_string_value) {
                    bun.handleOom(value_exprs.append(assign_target));
                } else {
                    // "Enum[assignTarget] = 'Name'"
                    value_exprs.append(
                        Expr.assign(
                            p.newExpr(E.Index{
                                target: p.newExpr(
                                    E.Identifier{ ref: data.arg },
                                    value.loc,
                                ),
                                index: assign_target,
                            }, value.loc),
                            name_as_e_string.?,
                        ),
                    ) catch |e| bun.handleOom(e);
                    p.recordUsage(data.arg);
                }
            }

            p.should_fold_typescript_constant_expressions = old_should_fold_typescript_constant_expressions;

            let value_stmts = ListManaged(Stmt).initCapacity(allocator, value_exprs.items.len) catch unreachable;
            // Generate statements from expressions
            for (expr of value_exprs.items) {
                value_stmts.appendAssumeCapacity(p.s(S.SExpr{ value: expr }, expr.loc));
            }
            value_exprs.deinit();
            try p.generateClosureForTypeScriptNamespaceOrEnum(
                stmts,
                stmt.loc,
                data.is_export,
                data.name.loc,
                data.name.ref.?,
                data.arg,
                value_stmts.items,
                all_values_are_pure,
            );
            return;
        }
        static fn s_namespace(p: *P, stmts: *ListManaged(Stmt), stmt: *Stmt, data: *S.Namespace)! {
            p.recordDeclaredSymbol(data.name.ref.?) catch unreachable;

            // Scan ahead for any variables inside this namespace. This must be done
            // ahead of time before visiting any statements inside the namespace
            // because we may end up visiting the uses before the declarations.
            // We need to convert the uses into property accesses on the namespace.
            for (child_stmt of data.stmts) {
                switch (child_stmt.data) {
                    .s_local => |local| {
                        if (local.is_export) {
                            p.markExportedDeclsInsideNamespace(data.arg, local.decls.slice());
                        }
                    },
                    else => {},
                }
            }

            let prepend_temp_refs = PrependTempRefsOpts{ kind: StmtsKind.fn_body };
            let prepend_list = ListManaged(Stmt).fromOwnedSlice(p.allocator, data.stmts);

            const old_enclosing_namespace_arg_ref = p.enclosing_namespace_arg_ref;
            p.enclosing_namespace_arg_ref = data.arg;
            p.pushScopeForVisitPass(.entry, stmt.loc) catch unreachable;
            p.recordDeclaredSymbol(data.arg) catch unreachable;
            p.visitStmtsAndPrependTempRefs(&prepend_list, &prepend_temp_refs)?;
            p.popScope();
            p.enclosing_namespace_arg_ref = old_enclosing_namespace_arg_ref;

            try p.generateClosureForTypeScriptNamespaceOrEnum(
                stmts,
                stmt.loc,
                data.is_export,
                data.name.loc,
                data.name.ref.?,
                data.arg,
                prepend_list.items,
                false,
            );
            return;
        }
    };
}

type string = []const u8;

import bun from "bun";
const assert = bun.assert;
const js_lexer = bun.js_lexer;
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const S = js_ast.S;
const Stmt = js_ast.Stmt;

const G = js_ast.G;
const Decl = G.Decl;

const js_parser = bun.js_parser;
const JSXTransformType = js_parser.JSXTransformType;
const Prefill = js_parser.Prefill;
const PrependTempRefsOpts = js_parser.PrependTempRefsOpts;
const ReactRefresh = js_parser.ReactRefresh;
const Ref = js_parser.Ref;
const RelocateVars = js_parser.RelocateVars;
const SideEffects = js_parser.SideEffects;
const StmtsKind = js_parser.StmtsKind;
const options = js_parser.options;
const statementCaresAboutScope = js_parser.statementCaresAboutScope;

const List = std.ArrayListUnmanaged;
const ListManaged = std.array_list.Managed;
