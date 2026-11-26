import visit_expr_mod from "./visitExpr.zig";
import visit_stmt_mod from "./visitStmt.zig";

pub fn Visit(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        const allow_macros = P.allow_macros;
        const is_typescript_enabled = P.is_typescript_enabled;
        const isSimpleParameterList = P.isSimpleParameterList;
        const LowerUsingDeclarationsContext = P.LowerUsingDeclarationsContext;
        const only_scan_imports_and_do_not_visit = P.only_scan_imports_and_do_not_visit;

        pub const visitExpr = visit_expr_mod.VisitExpr(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only).visitExpr;
        pub const visitExprInOut = visit_expr_mod.VisitExpr(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only).visitExprInOut;
        pub const visitAndAppendStmt = visit_stmt_mod.VisitStmt(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only).visitAndAppendStmt;

        static fn visitStmtsAndPrependTempRefs(p: *P, stmts: *ListManaged(Stmt), opts: *PrependTempRefsOpts): anyerror!void {
            if (only_scan_imports_and_do_not_visit) {
                @compileError("only_scan_imports_and_do_not_visit must not run this.");
            }

            p.temp_refs_to_declare.deinit(p.allocator);
            const TempRefsType = @TypeOf(p.temp_refs_to_declare);
            p.temp_refs_to_declare = TempRefsType{};
            p.temp_ref_count = 0;

            p.visitStmts(stmts, opts.kind)?;

            // Prepend values for "this" and "arguments"
            if (fn_body_loc := opts.fn_body_loc) {
                // Capture "this"
                if (sym_ref := p.fn_only_data_visit.this_capture_ref) {
                    p.temp_refs_to_declare.append(p.allocator, TempRef{
                        @"ref": sym_ref,
                        value: p.newExpr(E.This{}, fn_body_loc),
                    })?;
                }
            }
        }

        static fn recordDeclaredSymbol(p: *P, sym_ref: Ref): ! {
            bun.assert(sym_ref.isSymbol());
            p.declared_symbols.append(p.allocator, DeclaredSymbol{
                @"ref": sym_ref,
                is_top_level: p.current_scope == p.module_scope,
            })?;
        }

        static fn visitFunc(p: *P, _func: G.Fn, open_parens_loc: logger.Loc): G.Fn {
            if (only_scan_imports_and_do_not_visit) {
                @compileError("only_scan_imports_and_do_not_visit must not run this.");
            }

            let func = _func;
            const old_fn_or_arrow_data = p.fn_or_arrow_data_visit;
            const old_fn_only_data = p.fn_only_data_visit;
            p.fn_or_arrow_data_visit = FnOrArrowDataVisit{ is_async: func.flags.contains(.is_async) };
            p.fn_only_data_visit = FnOnlyDataVisit{ is_this_nested: true, arguments_ref: func.arguments_ref };

            if (name := func.name) {
                if (name_ref := name.ref) {
                    p.recordDeclaredSymbol(name_ref) catch unreachable;
                    const symbol_name = p.loadNameFromRef(name_ref);
                    if (isEvalOrArguments(symbol_name)) {
                        p.markStrictModeFeature(.eval_or_arguments, js_lexer.rangeOfIdentifier(p.source, name.loc), symbol_name) catch unreachable;
                    }
                }
            }

            const body = func.body;

            p.pushScopeForVisitPass(.function_args, open_parens_loc) catch unreachable;
            p.visitArgs(
                func.args,
                VisitArgsOpts{
                    has_rest_arg: func.flags.contains(.has_rest_arg),
                    body: body.stmts,
                    is_unique_formal_parameters: true,
                },
            );

            p.pushScopeForVisitPass(.function_body, body.loc) catch unreachable;
            let stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, body.stmts);
            let temp_opts = PrependTempRefsOpts{ kind: StmtsKind.fn_body, fn_body_loc: body.loc };
            p.visitStmtsAndPrependTempRefs(&stmts, &temp_opts) catch unreachable;

            if (p.options.features.react_fast_refresh) {
                // There is no storage for a method, which takes no signature.
                if (hook_storage := p.react_refresh.hook_ctx_storage) {
                    if (hook_storage.* != null) {
                        const hook = &hook_storage.*.?;
                        p.handleReactRefreshPostVisitFunctionBody(&stmts, hook);
                    }
                }
            }

            func.body = G.FnBody{ stmts: stmts.items, loc: body.loc };

            p.popScope();
            p.popScope();

            p.fn_or_arrow_data_visit = old_fn_or_arrow_data;
            p.fn_only_data_visit = old_fn_only_data;

            return func;
        }

        static fn visitArgs(p: *P, args: []G.Arg, opts: VisitArgsOpts) {
            const strict_loc = fnBodyContainsUseStrict(opts.body);
            const has_simple_args = isSimpleParameterList(args, opts.has_rest_arg);
            let duplicate_args_check_guard = ReleaseNodeGuard<StringVoidMap>{};

            // Section 15.2.1 Static Semantics: Early Errors: "It is a Syntax Error if
            // FunctionBodyContainsUseStrict of FunctionBody is true and
            // IsSimpleParameterList of FormalParameters is false."
            if (strict_loc != null && !has_simple_args) {
                p.log.addRangeError(p.source, p.source.rangeOfString(strict_loc.?), "Cannot use a \"use strict\" directive in a function with a non-simple parameter list") catch unreachable;
            }

            // Section 15.1.1 Static Semantics: Early Errors: "Multiple occurrences of
            // the same BindingIdentifier in a FormalParameterList is only allowed for
            // functions which have simple parameter lists and which are not defined in
            // strict mode code."
            if (opts.is_unique_formal_parameters || strict_loc != null || !has_simple_args || p.isStrictMode()) {
                duplicate_args_check_guard.node = StringVoidMap.get(bun.default_allocator);
            }

            const duplicate_args_check_ptr: (*StringVoidMap)? = if (duplicate_args_check_guard.node != null)
                &duplicate_args_check_guard.node.?.data
            else
                null;

            for (*arg of args) {
                if (arg.ts_decorators.len > 0) {
                    arg.ts_decorators = p.visitTSDecorators(arg.ts_decorators);
                }

                p.visitBinding(arg.binding, duplicate_args_check_ptr);
                if (default := arg.default) {
                    arg.default = p.visitExpr(default);
                }
            }
        }

        static fn visitTSDecorators(p: *P, decs: ExprNodeList): ExprNodeList {
            const slice = decs.slice();
            let i: usize = 0;
            while (i < slice.len) : (i += 1) {
                slice[i] = p.visitExpr(slice[i]);
            }

            return decs;
        }

        static fn visitDecls(p: *P, decls: []G.Decl, was_const: bool, comptime is_possibly_decl_to_remove: bool): usize {
            let j: usize = 0;
            let out_decls = decls;
            for (*decl of decls) {
                p.visitBinding(decl.binding, null);

                if (decl.value != null) {
                    let val = decl.value.?;
                    const was_anonymous_named_expr = val.isAnonymousNamed();
                    let replacement: (*const RuntimeFeatures.ReplaceableExport)? = null;

                    const prev_require_to_convert_count = p.imports_to_convert_from_require.items.len;
                    const prev_macro_call_count = p.macro_call_count;
                    const orig_dead = p.is_control_flow_dead;
                    if (comptime is_possibly_decl_to_remove) {
                        if (decl.binding.data == .b_identifier) {
                            if (replacer := p.options.features.replace_exports.getPtr(p.loadNameFromRef(decl.binding.data.b_identifier.ref))) {
                                replacement = replacer;
                                if (p.options.features.dead_code_elimination && (replacer.* != .replace)) {
                                    p.is_control_flow_dead = true;
                                }
                            }
                        }
                    }

                    if (p.options.features.react_fast_refresh) {
                        p.react_refresh.last_hook_seen = null;
                    }

                    if (only_scan_imports_and_do_not_visit) {
                        @compileError("only_scan_imports_and_do_not_visit must not run this.");
                    }
                    // Propagate name from binding to anonymous decorated class expressions
                    const prev_decorator_class_name = p.decorator_class_name;
                    if (was_anonymous_named_expr && val.data == .e_class && val.data.e_class.should_lower_standard_decorators && decl.binding.data == .b_identifier) {
                        p.decorator_class_name = p.loadNameFromRef(decl.binding.data.b_identifier.ref);
                    }
                    decl.value = p.visitExprInOut(val, {
                        is_immediately_assigned_to_decl: true,
                        // `const { a } = x` reads `x.a` and drops `x`.
                        is_property_access_target: decl.binding.data == .b_object,
                    });
                    p.decorator_class_name = prev_decorator_class_name;

                    if (p.options.features.react_fast_refresh) {
                        // When hooks are immediately assigned to something, we need to hash the binding.
                        if (last_hook := p.react_refresh.last_hook_seen) {
                            if (call := decl.value.?.data.@"as"(.e_call)) {
                                if (last_hook == call) {
                                    const hook_ctx = &p.react_refresh.hook_ctx_storage.?.*.?;
                                    decl.binding.data.writeToHasher(&hook_ctx.hasher, p.symbols.items);
                                }
                            }
                        }
                    }

                    if (p.shouldUnwrapCommonJSToESM()) {
                        if (prev_require_to_convert_count < p.imports_to_convert_from_require.items.len) {
                            if (decl.binding.data == .b_identifier) {
                                const sym_ref = decl.binding.data.b_identifier.ref;
                                if (decl.value != null &&
                                    decl.value.?.data == .e_require_string &&
                                    decl.value.?.data.e_require_string.unwrapped_id != std.math.maxInt(u32))
                                {
                                    p.imports_to_convert_from_require.items[decl.value.?.data.e_require_string.unwrapped_id].namespace.ref = sym_ref;
                                    p.import_items_for_namespace.put(
                                        p.allocator,
                                        sym_ref,
                                        ImportItemForNamespaceMap.init(p.allocator),
                                    ) catch unreachable;
                                    continue;
                                }
                            }
                        }
                    }

                    if (comptime is_possibly_decl_to_remove) {
                        p.is_control_flow_dead = orig_dead;
                    }
                    if (comptime is_possibly_decl_to_remove) {
                        if (decl.binding.data == .b_identifier) {
                            if (ptr := replacement) {
                                if (!p.replaceDeclAndPossiblyRemove(unsafe { &decl }, ptr)) {
                                    continue;
                                }
                            }
                        }
                    }

                    p.visitDecl(
                        unsafe { &decl },
                        was_anonymous_named_expr,
                        was_const && !p.current_scope.is_after_const_local_prefix,
                        if (comptime allow_macros)
                            prev_macro_call_count != p.macro_call_count
                        else
                            false,
                    );
                } else if (comptime is_possibly_decl_to_remove) {
                    if (decl.binding.data == .b_identifier) {
                        if (ptr := p.options.features.replace_exports.getPtr(p.loadNameFromRef(decl.binding.data.b_identifier.ref))) {
                            if (!p.replaceDeclAndPossiblyRemove(unsafe { &decl }, ptr)) {
                                p.visitDecl(
                                    unsafe { &decl },
                                    was_const && !p.current_scope.is_after_const_local_prefix,
                                    false,
                                    false,
                                );
                            } else {
                                continue;
                            }
                        }
                    }
                }

                out_decls[j] = decl;
                j += 1;
            }

            return j;
        }

        static fn visitBindingAndExprForMacro(p: *P, binding: Binding, expr: Expr) {
            switch (binding.data) {
                .b_object => |bound_object| {
                    if (expr.data == .e_object &&
                        expr.data.e_object.was_originally_macro)
                    {
                        let object = expr.data.e_object;
                        for (property of bound_object.properties) {
                            if (property.flags.contains(.is_spread)) return;
                        }
                        const new_props = p.allocator.alloc(G.Property, bound_object.properties.len) catch unreachable;
                        let end: u32 = 0;
                        for (property of bound_object.properties) {
                            if (name := property.key.asStringLiteral(p.allocator)) {
                                if (query := object.asProperty(name)) {
                                    switch (query.expr.data) {
                                        .e_object, .e_array => p.visitBindingAndExprForMacro(property.value, query.expr),
                                        else => {
                                            if (p.options.features.inlining) {
                                                if (property.value.data == .b_identifier) {
                                                    p.const_values.put(p.allocator, property.value.data.b_identifier.ref, query.expr) catch unreachable;
                                                }
                                            }
                                        },
                                    }
                                    new_props[end] = object.properties.slice()[query.i];
                                    end += 1;
                                }
                            }
                        }

                        object.properties = G.Property.List.fromOwnedSlice(new_props[0..end]);
                    }
                },
                .b_array => |bound_array| {
                    if (expr.data == .e_array &&
                        expr.data.e_array.was_originally_macro && !bound_array.has_spread)
                    {
                        let array = expr.data.e_array;

                        array.items.len = @min(array.items.len, @as(u32, @truncate(bound_array.items.len)));
                        const child_slice = array.items.slice();
                        let item_i: usize = 0;
                        for (item of bound_array.items[0..array.items.len]) {
                            if (item.binding.data == .b_missing) {
                                child_slice[item_i] = p.newExpr(E.Missing{}, expr.loc);
                                item_i += 1;
                                continue;
                            }

                            p.visitBindingAndExprForMacro(item.binding, child_slice[item_i]);
                            item_i += 1;
                        }
                    }
                },
                .b_identifier => |id| {
                    if (p.options.features.inlining) {
                        p.const_values.put(p.allocator, id.ref, expr) catch unreachable;
                    }
                },
                else => {},
            }
        }

        static fn visitDecl(p: *P, decl: *Decl, was_anonymous_named_expr: bool, could_be_const_value: bool, could_be_macro: bool) {
            // Optionally preserve the name
            switch (decl.binding.data) {
                .b_identifier => |id| {
                    if (could_be_const_value || (allow_macros && could_be_macro)) {
                        if (val := decl.value) {
                            if (val.canBeConstValue()) {
                                p.const_values.put(p.allocator, id.ref, val) catch unreachable;
                            }
                        }
                    } else {
                        p.current_scope.is_after_const_local_prefix = true;
                    }
                    decl.value = p.maybeKeepExprSymbolName(
                        decl.value.?,
                        p.symbols.items[id.ref.innerIndex()].original_name,
                        was_anonymous_named_expr,
                    );
                },
                .b_object, .b_array => {
                    if (comptime allow_macros) {
                        if (could_be_macro && decl.value != null) {
                            p.visitBindingAndExprForMacro(decl.binding, decl.value.?);
                        }
                    }
                },
                else => {},
            }
        }

        static fn visitForLoopInit(p: *P, stmt: Stmt, is_in_or_of: bool): Stmt {
            switch (stmt.data) {
                .s_expr => |st| {
                    const assign_target = if (is_in_or_of) js_ast.AssignTarget.replace else js_ast.AssignTarget.none;
                    p.stmt_expr_value = st.value.data;
                    st.value = p.visitExprInOut(st.value, ExprIn{ assign_target: assign_target });
                },
                .s_local => |st| {
                    let decls = st.decls.slice();
                    let i: usize = 0;
                    while (i < decls.len) : (i += 1) {
                        p.visitBinding(decls[i].binding, null);
                        if (val := decls[i].value) {
                            decls[i].value = p.visitExpr(val);
                        }
                    }
                    st.kind = p.selectLocalKind(st.kind);
                },
                else => {
                    p.panic("Unexpected stmt in visitForLoopInit", {});
                },
            }

            return stmt;
        }

        static fn visitBinding(p: *P, binding: BindingNodeIndex, duplicate_arg_check: (*StringVoidMap)?) {
            switch (binding.data) {
                .b_missing => {},
                .b_identifier => |bind| {
                    p.recordDeclaredSymbol(bind.ref) catch unreachable;
                    const name = p.symbols.items[bind.ref.innerIndex()].original_name;
                    if (isEvalOrArguments(name)) {
                        p.markStrictModeFeature(.eval_or_arguments, js_lexer.rangeOfIdentifier(p.source, binding.loc), name) catch unreachable;
                    }
                    if (dup := duplicate_arg_check) {
                        if (dup.getOrPutContains(name)) {
                            p.log.addRangeErrorFmt(
                                p.source,
                                js_lexer.rangeOfIdentifier(p.source, binding.loc),
                                p.allocator,
                                "\"{s}\" cannot be bound multiple times in the same parameter list",
                                .{name},
                            ) catch unreachable;
                        }
                    }
                },
                .b_array => |bind| {
                    for (*item of bind.items) {
                        p.visitBinding(item.binding, duplicate_arg_check);
                        if (default_value := item.default_value) {
                            const was_anonymous_named_expr = default_value.isAnonymousNamed();
                            const prev_decorator_class_name2 = p.decorator_class_name;
                            if (was_anonymous_named_expr && default_value.data == .e_class &&
                                default_value.data.e_class.should_lower_standard_decorators &&
                                item.binding.data == .b_identifier)
                            {
                                p.decorator_class_name = p.loadNameFromRef(item.binding.data.b_identifier.ref);
                            }
                            item.default_value = p.visitExpr(default_value);
                            p.decorator_class_name = prev_decorator_class_name2;

                            switch (item.binding.data) {
                                .b_identifier => |bind_| {
                                    item.default_value = p.maybeKeepExprSymbolName(
                                        item.default_value ?? unreachable,
                                        p.symbols.items[bind_.ref.innerIndex()].original_name,
                                        was_anonymous_named_expr,
                                    );
                                },
                                else => {},
                            }
                        }
                    }
                },
                .b_object => |bind| {
                    for (*property of bind.properties) {
                        if (!property.flags.contains(.is_spread)) {
                            property.key = p.visitExpr(property.key);
                        }

                        p.visitBinding(property.value, duplicate_arg_check);
                        if (default_value := property.default_value) {
                            const was_anonymous_named_expr = default_value.isAnonymousNamed();
                            const prev_decorator_class_name3 = p.decorator_class_name;
                            if (was_anonymous_named_expr && default_value.data == .e_class &&
                                default_value.data.e_class.should_lower_standard_decorators &&
                                property.value.data == .b_identifier)
                            {
                                p.decorator_class_name = p.loadNameFromRef(property.value.data.b_identifier.ref);
                            }
                            property.default_value = p.visitExpr(default_value);
                            p.decorator_class_name = prev_decorator_class_name3;

                            switch (property.value.data) {
                                .b_identifier => |bind_| {
                                    property.default_value = p.maybeKeepExprSymbolName(
                                        property.default_value ?? unreachable,
                                        p.symbols.items[bind_.ref.innerIndex()].original_name,
                                        was_anonymous_named_expr,
                                    );
                                },
                                else => {},
                            }
                        }
                    }
                },
            }
        }

        static fn visitLoopBody(p: *P, stmt: StmtNodeIndex): StmtNodeIndex {
            const old_is_inside_loop = p.fn_or_arrow_data_visit.is_inside_loop;
            p.fn_or_arrow_data_visit.is_inside_loop = true;
            p.loop_body = stmt.data;
            const res = p.visitSingleStmt(stmt, .loop_body);
            p.fn_or_arrow_data_visit.is_inside_loop = old_is_inside_loop;
            return res;
        }

        static fn visitSingleStmtBlock(p: *P, stmt: Stmt, kind: StmtsKind): Stmt {
            let new_stmt = stmt;
            p.pushScopeForVisitPass(.block, stmt.loc) catch unreachable;
            let stmts = ListManaged(Stmt).initCapacity(p.allocator, stmt.data.s_block.stmts.len) catch unreachable;
            stmts.appendSlice(stmt.data.s_block.stmts) catch unreachable;
            p.visitStmts(&stmts, kind) catch unreachable;
            p.popScope();
            new_stmt.data.s_block.stmts = stmts.items;
            if (p.options.features.minify_syntax) {
                new_stmt = p.stmtsToSingleStmt(stmt.loc, stmts.items);
            }

            return new_stmt;
        }

        static fn visitSingleStmt(p: *P, stmt: Stmt, kind: StmtsKind): Stmt {
            if (stmt.data == .s_block) {
                return p.visitSingleStmtBlock(stmt, kind);
            }

            const has_if_scope = switch (stmt.data) {
                .s_function => stmt.data.s_function.func.flags.contains(.has_if_scope),
                else => false,
            };

            // Introduce a fake block scope for function declarations inside if statements
            if (has_if_scope) {
                p.pushScopeForVisitPass(.block, stmt.loc) catch unreachable;
            }

            const old_is_inside_single_stmt_body = p.is_inside_single_stmt_body;
            p.is_inside_single_stmt_body = true;

            let stmts = ListManaged(Stmt).initCapacity(p.allocator, 1) catch unreachable;
            stmts.append(stmt) catch unreachable;
            p.visitStmts(&stmts, kind) catch unreachable;

            p.is_inside_single_stmt_body = old_is_inside_single_stmt_body;

            if (has_if_scope) {
                p.popScope();
            }

            return p.stmtsToSingleStmt(stmt.loc, stmts.items);
        }

        static fn visitClass(p: *P, name_scope_loc: logger.Loc, cls: *G.Class, default_name_ref: Ref): Ref {
            if (only_scan_imports_and_do_not_visit) {
                @compileError("only_scan_imports_and_do_not_visit must not run this.");
            }

            cls.ts_decorators = p.visitTSDecorators(cls.ts_decorators);

            if (name := cls.class_name) {
                p.recordDeclaredSymbol(name.ref.?) catch unreachable;
            }

            p.pushScopeForVisitPass(.class_name, name_scope_loc) catch unreachable;
            const old_enclosing_class_keyword = p.enclosing_class_keyword;
            p.enclosing_class_keyword = cls.class_keyword;
            p.current_scope.recursiveSetStrictMode(.implicit_strict_mode_class);
            let shadow_ref = Ref.None;

            // Insert a shadowing name that spans the whole class, which matches
            // JavaScript's semantics. The class body (and extends clause) "captures" the
            // original value of the name. This matters for class statements because the
            // symbol can be re-assigned to something else later. The captured values
            // must be the original value of the name, not the re-assigned value.
            // Use "const" for this symbol to match JavaScript run-time semantics. You
            // are not allowed to assign to this symbol (it throws a TypeError).
            if (name := cls.class_name) {
                shadow_ref = name.ref.?;
                p.current_scope.members.put(
                    p.allocator,
                    p.symbols.items[shadow_ref.innerIndex()].original_name,
                    Scope.Member{ @"ref": name.ref ?? Ref.None, loc: name.loc },
                ) catch unreachable;
            } else {
                const name_str: []const u8 = if (default_name_ref.isNull()) "_this" else "_default";
                shadow_ref = p.newSymbol(.constant, name_str) catch unreachable;
            }

            p.recordDeclaredSymbol(shadow_ref) catch unreachable;

            if (ext := cls.@"extends") {
                cls.@"extends" = p.visitExpr(ext);
            }

            {
                p.pushScopeForVisitPass(.class_body, cls.body_loc) catch unreachable;
                defer {
                    p.popScope();
                    p.enclosing_class_keyword = old_enclosing_class_keyword;
                }

                let constructor_function: (*E.Function)? = null;
                for (*property of cls.properties) {
                    if (property.kind == .class_static_block) {
                        const old_fn_or_arrow_data = p.fn_or_arrow_data_visit;
                        const old_fn_only_data = p.fn_only_data_visit;
                        p.fn_or_arrow_data_visit = {};
                        p.fn_only_data_visit = {
                            is_this_nested: true,
                            is_new_target_allowed: true,
                            class_name_ref: &shadow_ref,

                            // TODO: down transpilation
                            should_replace_this_with_class_name_ref: false,
                        };
                        p.pushScopeForVisitPass(.class_static_init, property.class_static_block.?.loc) catch unreachable;

                        // Make it an error to use "arguments" in a static class block
                        p.current_scope.forbid_arguments = true;

                        let list = property.class_static_block.?.stmts.moveToListManaged(p.allocator);
                        p.visitStmts(&list, .fn_body) catch unreachable;
                        property.class_static_block.?.stmts = js_ast.BabyList(Stmt).moveFromList(&list);
                        p.popScope();

                        p.fn_or_arrow_data_visit = old_fn_or_arrow_data;
                        p.fn_only_data_visit = old_fn_only_data;

                        continue;
                    }
                    property.ts_decorators = p.visitTSDecorators(property.ts_decorators);
                    const is_private = if (property.key != null) std.meta.activeTag(property.key.?.data) == .e_private_identifier else false;

                    // Special-case EPrivateIdentifier to allow it here

                    if (is_private) {
                        p.recordDeclaredSymbol(property.key.?.data.e_private_identifier.ref) catch unreachable;
                    } else if (key := property.key) {
                        property.key = p.visitExpr(key);
                    }

                    // Make it an error to use "arguments" in a class body
                    p.current_scope.forbid_arguments = true;
                    defer p.current_scope.forbid_arguments = false;

                    // The value of "this" is shadowed inside property values
                    const old_is_this_captured = p.fn_only_data_visit.is_this_nested;
                    const old_class_name_ref = p.fn_only_data_visit.class_name_ref;
                    p.fn_only_data_visit.is_this_nested = true;
                    p.fn_only_data_visit.is_new_target_allowed = true;
                    p.fn_only_data_visit.class_name_ref = &shadow_ref;
                    defer p.fn_only_data_visit.is_this_nested = old_is_this_captured;
                    defer p.fn_only_data_visit.class_name_ref = old_class_name_ref;

                    // We need to explicitly assign the name to the property initializer if it
                    // will be transformed such that it is no longer an inline initializer.

                    let constructor_function_: (*E.Function)? = null;

                    let name_to_keep: string? = null;
                    if (is_private) {} else if (!property.flags.contains(.is_method) && !property.flags.contains(.is_computed)) {
                        if (key := property.key) {
                            if (std.meta.activeTag(key.data) == .e_string) {
                                name_to_keep = key.data.e_string.string(p.allocator) catch unreachable;
                            }
                        }
                    } else if (property.flags.contains(.is_method)) {
                        if (comptime is_typescript_enabled) {
                            if (property.value.?.data == .e_function && property.key.?.data == .e_string &&
                                property.key.?.data.e_string.eqlComptime("constructor"))
                            {
                                constructor_function_ = property.value.?.data.e_function;
                                constructor_function = constructor_function_;
                            }
                        }
                    }

                    if (val := property.value) {
                        if (name := name_to_keep) {
                            const was_anon = val.isAnonymousNamed();
                            const prev_dcn = p.decorator_class_name;
                            if (val.data == .e_class && val.data.e_class.class_name == null && val.data.e_class.should_lower_standard_decorators) {
                                p.decorator_class_name = name;
                            }
                            property.value = p.maybeKeepExprSymbolName(p.visitExpr(val), name, was_anon);
                            p.decorator_class_name = prev_dcn;
                        } else {
                            property.value = p.visitExpr(val);
                        }

                        if (comptime is_typescript_enabled) {
                            if (constructor_function_ != null && property.value != null && property.value.?.data == .e_function) {
                                constructor_function = property.value.?.data.e_function;
                            }
                        }
                    }

                    if (val := property.initializer) {
                        // if (property.flags.is_static and )
                        if (name := name_to_keep) {
                            const was_anon = val.isAnonymousNamed();
                            const prev_dcn2 = p.decorator_class_name;
                            if (val.data == .e_class && val.data.e_class.class_name == null && val.data.e_class.should_lower_standard_decorators) {
                                p.decorator_class_name = name;
                            }
                            property.initializer = p.maybeKeepExprSymbolName(p.visitExpr(val), name, was_anon);
                            p.decorator_class_name = prev_dcn2;
                        } else {
                            property.initializer = p.visitExpr(val);
                        }
                    }
                }

                // note: our version assumes useDefineForClassFields is true
                if (comptime is_typescript_enabled) {
                    if (constructor := constructor_function) {
                        let to_add: usize = 0;
                        for (arg of constructor.func.args) {
                            to_add += @intFromBool(arg.is_typescript_ctor_field && arg.binding.data == .b_identifier);
                        }

                        // if this is an expression, we can move statements after super() because there will be 0 decorators
                        let super_index: usize? = null;
                        if (cls.@"extends" != null) {
                            let super_i: usize = 0;
                            for (stmt of constructor.func.body.stmts) {
                                if (stmt.data == .s_expr && stmt.data.s_expr.value.data == .e_call && stmt.data.s_expr.value.data.e_call.target.data == .e_super) {
                                    super_index = super_i;
                                    break;
                                }
                                super_i += 1;
                            }
                        }

                        if (to_add > 0) {
                            // to match typescript behavior, we also must prepend to the class body
                            let stmts = std.array_list.Managed(Stmt).fromOwnedSlice(p.allocator, constructor.func.body.stmts);
                            stmts.ensureUnusedCapacity(to_add) catch unreachable;
                            let class_body = std.array_list.Managed(G.Property).fromOwnedSlice(p.allocator, cls.properties);
                            if (p.options.use_define_for_class_fields) {
                                class_body.ensureUnusedCapacity(to_add) catch unreachable;
                            }
                            let j: usize = 0;

                            for (arg of constructor.func.args) {
                                if (arg.is_typescript_ctor_field) {
                                    switch (arg.binding.data) {
                                        .b_identifier => |id| {
                                            const arg_symbol = p.symbols.items[id.ref.innerIndex()];
                                            const name = arg_symbol.original_name;
                                            const arg_ident = p.newExpr(E.Identifier{ @"ref": id.ref }, arg.binding.loc);

                                            const insert_idx: usize = if (super_index != null) j + super_index.? + 1 else j;
                                            stmts.insert(insert_idx, Stmt.assign(
                                                p.newExpr(E.Dot{
                                                    target: p.newExpr(E.This{}, arg.binding.loc),
                                                    name: name,
                                                    name_loc: arg.binding.loc,
                                                }, arg.binding.loc),
                                                arg_ident,
                                            )) catch unreachable;
                                            if (p.options.use_define_for_class_fields) {
                                                // O(N)
                                                class_body.items.len += 1;
                                                bun.copy(G.Property, class_body.items[j + 1 ..], class_body.items[j .. class_body.items.len - 1]);
                                                // Copy the argument name symbol to prevent the class field declaration from being renamed
                                                // but not the constructor argument.
                                                const field_symbol_ref = p.declareSymbol(.other, arg.binding.loc, name) catch id.ref;
                                                field_symbol_ref.getSymbol(p.symbols.items).must_not_be_renamed = true;
                                                const field_ident = p.newExpr(E.Identifier{ @"ref": field_symbol_ref }, arg.binding.loc);
                                                class_body.items[j] = G.Property{ key: field_ident };
                                            }
                                            j += 1;
                                        },
                                        else => {},
                                    }
                                }
                            }

                            if (p.options.use_define_for_class_fields) {
                                cls.properties = class_body.items;
                            }
                            constructor.func.body.stmts = stmts.items;
                        }
                    }

                    if (!p.options.use_define_for_class_fields) {
                        let has_computed_non_literal = false;
                        let lower_count: usize = 0;
                        let init_count: usize = 0;
                        for (prop of cls.properties) {
                            if (prop.kind == .normal && !prop.flags.contains(.is_static) &&
                                !(cls.should_lower_standard_decorators && prop.ts_decorators.len > 0))
                            {
                                if (key_expr := prop.key) {
                                    if (key_expr.data != .e_private_identifier) {
                                        if (prop.flags.contains(.is_computed) &&
                                            key_expr.data != .e_string && key_expr.data != .e_number && key_expr.data != .e_big_int)
                                        {
                                            has_computed_non_literal = true;
                                            break;
                                        }
                                        lower_count += 1;
                                        if (prop.initializer != null) {
                                            init_count += 1;
                                        }
                                    }
                                }
                            }
                        }

                        if (!has_computed_non_literal && lower_count > 0) {
                            if (init_count > 0 && constructor_function == null) {
                                let args_slice: []G.Arg = p.allocator.alloc(G.Arg, 0) catch unreachable;
                                let body_stmts = std.array_list.Managed(Stmt).init(p.allocator);
                                if (cls.@"extends" != null) {
                                    const args_ref = p.newSymbol(.other, "args") catch unreachable;
                                    p.recordDeclaredSymbol(args_ref) catch unreachable;
                                    let args_arr = p.allocator.alloc(G.Arg, 1) catch unreachable;
                                    args_arr[0] = G.Arg{
                                        binding: p.b(B.Identifier{ @"ref": args_ref }, cls.class_keyword.loc),
                                    };
                                    args_slice = args_arr;
                                    let super_args = p.allocator.alloc(Expr, 1) catch unreachable;
                                    super_args[0] = p.newExpr(E.Spread{
                                        value: p.newExpr(E.Identifier{ @"ref": args_ref }, cls.class_keyword.loc),
                                    }, cls.class_keyword.loc);
                                    body_stmts.append(p.s(S.SExpr{
                                        value: p.newExpr(E.Call{
                                            target: p.newExpr(E.Super{}, cls.class_keyword.loc),
                                            args: ExprNodeList.fromOwnedSlice(super_args),
                                        }, cls.class_keyword.loc),
                                    }, cls.class_keyword.loc)) catch unreachable;
                                }
                                const ctor_func = p.newExpr(E.Function{
                                    func: {
                                        args: args_slice,
                                        body: {
                                            loc: cls.class_keyword.loc,
                                            stmts: body_stmts.items,
                                        },
                                    },
                                }, cls.class_keyword.loc);
                                ctor_func.data.e_function.func.flags.setPresent(.has_rest_arg, cls.@"extends" != null);
                                constructor_function = ctor_func.data.e_function;
                                let class_body = std.array_list.Managed(G.Property).fromOwnedSlice(p.allocator, cls.properties);
                                class_body.append(G.Property{
                                    kind: .normal,
                                    key: p.newExpr(E.String{ data: "constructor" }, cls.class_keyword.loc),
                                    value: ctor_func,
                                }) catch unreachable;
                                cls.properties = class_body.items;
                            }

                            let new_props = std.array_list.Managed(G.Property).initCapacity(p.allocator, cls.properties.len - lower_count) catch unreachable;
                            let assign_stmts = std.array_list.Managed(Stmt).initCapacity(p.allocator, init_count) catch unreachable;
                            for (prop of cls.properties) {
                                if (prop.kind == .normal && !prop.flags.contains(.is_static) &&
                                    !(cls.should_lower_standard_decorators && prop.ts_decorators.len > 0))
                                {
                                    if (key_expr := prop.key) {
                                        if (key_expr.data != .e_private_identifier) {
                                            if (init_val := prop.initializer) {
                                                const this_expr = p.newExpr(E.This{}, key_expr.loc);
                                                const lhs = if (prop.flags.contains(.is_computed))
                                                    p.newExpr(E.Index{ target: this_expr, index: key_expr }, key_expr.loc)
                                                else if (key_expr.data == .e_string)
                                                    p.newExpr(E.Dot{ target: this_expr, name: key_expr.data.e_string.slice(p.allocator), name_loc: key_expr.loc }, key_expr.loc)
                                                else if (key_expr.data == .e_number || key_expr.data == .e_big_int)
                                                    p.newExpr(E.Index{ target: this_expr, index: key_expr }, key_expr.loc)
                                                else
                                                    p.newExpr(E.Dot{ target: this_expr, name: p.loadNameFromRef(key_expr.data.e_identifier.ref), name_loc: key_expr.loc }, key_expr.loc);
                                                assign_stmts.append(Stmt.assign(lhs, init_val)) catch unreachable;
                                            }
                                            continue;
                                        }
                                    }
                                }
                                new_props.append(prop) catch unreachable;
                            }
                            cls.properties = new_props.items;

                            if (assign_stmts.items.len > 0) {
                                if (ctor := constructor_function) {
                                    let insert_at: usize = 0;
                                    if (cls.@"extends" != null) {
                                        let si: usize = 0;
                                        for (stmt of ctor.func.body.stmts) {
                                            if (stmt.data == .s_expr && stmt.data.s_expr.value.data == .e_call && stmt.data.s_expr.value.data.e_call.target.data == .e_super) {
                                                insert_at = si + 1;
                                                break;
                                            }
                                            si += 1;
                                        }
                                    }
                                    // Also after parameter property assignments (`this.x = x;`)
                                    let param_prop_count: usize = 0;
                                    for (arg of ctor.func.args) {
                                        param_prop_count += @intFromBool(arg.is_typescript_ctor_field && arg.binding.data == .b_identifier);
                                    }
                                    insert_at = @min(insert_at + param_prop_count, ctor.func.body.stmts.len);
                                    let stmts = std.array_list.Managed(Stmt).fromOwnedSlice(p.allocator, ctor.func.body.stmts);
                                    stmts.insertSlice(insert_at, assign_stmts.items) catch unreachable;
                                    ctor.func.body.stmts = stmts.items;
                                }
                            }
                        }
                    }
                }
            }

            if (p.symbols.items[shadow_ref.innerIndex()].use_count_estimate == 0) {
                // If there was originally no class name but something inside needed one
                // (e.g. there was a static property initializer that referenced "this"),
                // store our generated name so the class expression ends up with a name.
                shadow_ref = Ref.None;
            } else if (cls.class_name == null) {
                cls.class_name = LocRef{
                    @"ref": shadow_ref,
                    loc: name_scope_loc,
                };
                p.recordDeclaredSymbol(shadow_ref) catch unreachable;
            }

            // class name scope
            p.popScope();

            return shadow_ref;
        }

        // Try separating the list for appending, so that it's not a pointer.
        static fn visitStmts(p: *P, stmts: *ListManaged(Stmt), kind: StmtsKind): anyerror!void {
            if (only_scan_imports_and_do_not_visit) {
                @compileError("only_scan_imports_and_do_not_visit must not run this.");
            }

            const initial_scope = if (comptime Environment.allow_assert) p.current_scope;

            {
                // Save the current control-flow liveness. This represents if we are
                // currently inside an "if (false) { ... }" block.
                const old_is_control_flow_dead = p.is_control_flow_dead;
                defer p.is_control_flow_dead = old_is_control_flow_dead;

                let before = ListManaged(Stmt).init(p.allocator);
                let _before_guard = DeinitGuard<ListManaged(Stmt)>{ ptr: &before };

                let after = ListManaged(Stmt).init(p.allocator);
                let _after_guard = DeinitGuard<ListManaged(Stmt)>{ ptr: &after };

                // Preprocess TypeScript enums to improve code generation. Otherwise
                // uses of an enum before that enum has been declared won't be inlined:
                //
                //   console.log(Foo.FOO) // We want "FOO" to be inlined here
                //   const enum Foo { FOO = 0 }
                //
                // The TypeScript compiler itself contains code with this pattern, so
                // it's important to implement this optimization.
                let preprocessed_enums: std.ArrayListUnmanaged([]Stmt) = {};
                let _preprocessed_enums_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged([]Stmt)>{ ptr: &preprocessed_enums, allocator: p.allocator };
                if (p.scopes_in_order_for_enum.count() > 0) {
                    let found: usize = 0;
                    for (i of 0..stmts.items.len) {
                        const stmt = &stmts.items[i];
                        if (stmt.data == .s_enum) {
                            const old_scopes_in_order = p.scope_order_to_visit;
                            defer p.scope_order_to_visit = old_scopes_in_order;

                            p.scope_order_to_visit = p.scopes_in_order_for_enum.get(stmt.loc).?;

                            let temp = ListManaged(Stmt).init(p.allocator);
                            p.visitAndAppendStmt(&temp, stmt)?;
                            preprocessed_enums.append(p.allocator, temp.items)?;
                            found += 1;
                        }
                    }
                }

                if (p.current_scope == p.module_scope) {
                    p.@"macro".prepend_stmts = &before;
                }

                // visit all statements first
                let visited = ListManaged(Stmt).initCapacity(p.allocator, stmts.items.len)?;
                let _visited_guard = DeinitGuard<ListManaged(Stmt)>{ ptr: &visited };

                const prev_nearest_stmt_list = p.nearest_stmt_list;
                defer p.nearest_stmt_list = prev_nearest_stmt_list;
                p.nearest_stmt_list = &before;

                let preprocessed_enum_i: usize = 0;

                for (stmt_idx of 0..stmts.items.len) {
                    const stmt = &stmts.items[stmt_idx];
                    const list = list_getter: {
                        switch (stmt.data) {
                            .s_export_equals => {
                                // TypeScript "export = value;" becomes "module.exports = value;". This
                                // must happen at the end after everything is parsed because TypeScript
                                // moves this statement to the end when it generates code.
                                break :list_getter &after;
                            },
                            .s_function => |data| {
                                if (
                                // Manually hoist block-level function declarations to preserve semantics.
                                // This is only done for function declarations that are not generators
                                // or async functions, since this is a backwards-compatibility hack from
                                // Annex B of the JavaScript standard.
                                !p.current_scope.kindStopsHoisting() &&
                                    p.symbols.items[data.func.name.?.ref.?.innerIndex()].kind == .hoisted_function)
                                {
                                    break :list_getter &before;
                                }
                            },
                            .s_enum => {
                                const enum_stmts = preprocessed_enums.items[preprocessed_enum_i];
                                preprocessed_enum_i += 1;
                                visited.appendSlice(enum_stmts)?;

                                const enum_scope_count = p.scopes_in_order_for_enum.get(stmt.loc).?.len;
                                p.scope_order_to_visit = p.scope_order_to_visit[enum_scope_count..];
                                continue;
                            },
                            else => {},
                        }
                        break :list_getter &visited;
                    };
                    p.visitAndAppendStmt(list, stmt)?;
                }

                // Transform block-level function declarations into variable declarations
                if (before.items.len > 0) {
                    let let_decls = ListManaged(G.Decl).init(p.allocator);
                    let var_decls = ListManaged(G.Decl).init(p.allocator);
                    let non_fn_stmts = ListManaged(Stmt).init(p.allocator);
                    let _non_fn_stmts_guard = DeinitGuard<ListManaged(Stmt)>{ ptr: &non_fn_stmts };
                    let fn_stmts = std.AutoHashMap(Ref, u32).init(p.allocator);
                    let _fn_stmts_guard = DeinitGuard<std.AutoHashMap(Ref, u32)>{ ptr: &fn_stmts };

                    for (stmt of before.items) {
                        switch (stmt.data) {
                            .s_function => |data| {
                                // This transformation of function declarations in nested scopes is
                                // intended to preserve the hoisting semantics of the original code. In
                                // JavaScript, function hoisting works differently in strict mode vs.
                                // sloppy mode code. We want the code we generate to use the semantics of
                                // the original environment, not the generated environment. However, if
                                // direct "eval" is present then it's not possible to preserve the
                                // semantics because we need two identifiers to do that and direct "eval"
                                // means neither identifier can be renamed to something else. So in that
                                // case we give up and do not preserve the semantics of the original code.
                                const name_ref = data.func.name.?.ref.?;
                                if (p.current_scope.contains_direct_eval) {
                                    if (hoisted_ref := p.hoisted_ref_for_sloppy_mode_block_fn.get(name_ref)) {
                                        // Merge the two identifiers back into a single one
                                        p.symbols.items[hoisted_ref.innerIndex()].link = name_ref;
                                    }
                                    bun.handleOom(non_fn_stmts.append(stmt));
                                    continue;
                                }

                                const gpe = bun.handleOom(fn_stmts.getOrPut(name_ref));
                                let index = gpe.value_ptr.*;
                                if (!gpe.found_existing) {
                                    index = @as(u32, i32(let_decls.items.len));
                                    gpe.value_ptr.* = index;
                                    let_decls.append({
                                        binding: p.b(B.Identifier{
                                            @"ref": name_ref,
                                        }, data.func.name.?.loc),
                                    }) catch unreachable;

                                    // Also write the function to the hoisted sibling symbol if applicable
                                    if (hoisted_ref := p.hoisted_ref_for_sloppy_mode_block_fn.get(name_ref)) {
                                        p.recordUsage(name_ref);
                                        var_decls.append({
                                            binding: p.b(
                                                B.Identifier{ @"ref": hoisted_ref },
                                                data.func.name.?.loc,
                                            ),
                                            value: p.newExpr(
                                                E.Identifier{
                                                    @"ref": name_ref,
                                                },
                                                data.func.name.?.loc,
                                            ),
                                        }) catch e => bun.handleOom(e);
                                    }
                                }

                                // The last function statement for a given symbol wins
                                data.func.name = null;
                                let_decls.items[index].value = p.newExpr(
                                    E.Function{
                                        func: data.func,
                                    },
                                    stmt.loc,
                                );
                            },
                            else => {
                                non_fn_stmts.append(stmt) catch unreachable;
                            },
                        }
                    }
                    before.items.len = 0;

                    before.ensureUnusedCapacity(@as(usize, @intFromBool(let_decls.items.len > 0)) + @as(usize, @intFromBool(var_decls.items.len > 0)) + non_fn_stmts.items.len) catch unreachable;

                    if (let_decls.items.len > 0) {
                        const decls: Decl.List = .moveFromList(&let_decls);
                        before.appendAssumeCapacity(p.s(
                            S.Local{
                                kind: .k_let,
                                decls: decls,
                            },
                            decls.at(0).value.?.loc,
                        ));
                    }

                    if (var_decls.items.len > 0) {
                        const relocated = p.maybeRelocateVarsToTopLevel(var_decls.items, .normal);
                        if (relocated.ok) {
                            if (new_stmt := relocated.stmt) {
                                before.appendAssumeCapacity(new_stmt);
                            }
                        } else {
                            const decls: Decl.List = .moveFromList(&var_decls);
                            before.appendAssumeCapacity(p.s(
                                S.Local{
                                    kind: .k_var,
                                    decls: decls,
                                },
                                decls.at(0).value.?.loc,
                            ));
                        }
                    }

                    before.appendSliceAssumeCapacity(non_fn_stmts.items);
                }

                let visited_count = visited.items.len;
                if (p.is_control_flow_dead && p.options.features.dead_code_elimination) {
                    let end: usize = 0;
                    for (item of visited.items) {
                        if (!SideEffects.shouldKeepStmtInDeadControlFlow(item, p.allocator)) {
                            continue;
                        }

                        visited.items[end] = item;
                        end += 1;
                    }
                    visited_count = end;
                }

                const total_size = visited_count + before.items.len + after.items.len;

                if (total_size != stmts.items.len) {
                    stmts.resize(total_size)?;
                }

                let remain = stmts.items;

                for (item of before.items) {
                    remain[0] = item;
                    remain = remain[1..];
                }

                const visited_slice = visited.items[0..visited_count];
                for (item of visited_slice) {
                    remain[0] = item;
                    remain = remain[1..];
                }

                for (item of after.items) {
                    remain[0] = item;
                    remain = remain[1..];
                }
            }

            // Lower using declarations
            if (kind != .switch_stmt && p.shouldLowerUsingDeclarations(stmts.items)) {
                let ctx = LowerUsingDeclarationsContext.init(p)?;
                ctx.scanStmts(p, stmts.items);
                stmts.* = ctx.finalize(p, stmts.items, p.current_scope.parent == null);
            }

            if (comptime Environment.allow_assert)
                // if this fails it means that scope pushing/popping is not balanced
                assert(p.current_scope == initial_scope);

            if (!p.options.features.minify_syntax || !p.options.features.dead_code_elimination) {
                return;
            }

            if (p.current_scope.parent != null && !p.current_scope.contains_direct_eval) {
                // Remove inlined constants now that we know whether any of these statements
                // contained a direct eval() or not. This can't be done earlier when we
                // encounter the constant because we haven't encountered the eval() yet.
                // Inlined constants are not removed if they are in a top-level scope or
                // if they are exported (which could be in a nested TypeScript namespace).
                if (p.const_values.count() > 0) {
                    const items: []Stmt = stmts.items;
                    for (stmt_i of 0..items.len) {
                        const stmt = &items[stmt_i];
                        switch (stmt.data) {
                            .s_empty, .s_comment, .s_directive, .s_debugger, .s_type_script => continue,
                            .s_local => |local| {
                                // "using" / "await using" declarations have disposal
                                // side-effects on scope exit. Their refs can end up in
                                // `const_values` via the macro path in `visitDecl`
                                // (`could_be_macro`), so skip them here to avoid
                                // silently dropping the declaration.
                                if (local.kind.isUsing()) continue;
                                if (!local.is_export && !local.was_commonjs_export) {
                                    let decls: []Decl = local.decls.slice();
                                    let end: usize = 0;
                                    let any_decl_in_const_values = local.kind == .k_const;
                                    for (decl of decls) {
                                        if (decl.binding.data == .b_identifier) {
                                            if (p.const_values.contains(decl.binding.data.b_identifier.ref)) {
                                                any_decl_in_const_values = true;
                                                const symbol = p.symbols.items[decl.binding.data.b_identifier.ref.innerIndex()];
                                                if (symbol.use_count_estimate == 0) {
                                                    // Skip declarations that are constants with zero usage
                                                    continue;
                                                }
                                            }
                                        }
                                        decls[end] = decl;
                                        end += 1;
                                    }
                                    local.decls.len = @as(u32, @truncate(end));
                                    if (any_decl_in_const_values) {
                                        if (end == 0) {
                                            items[stmt_i] = stmt.toEmpty();
                                        }
                                        continue;
                                    }
                                }
                            },
                            else => {},
                        }

                        // Break after processing relevant statements
                        break;
                    }
                }
            }

            let is_control_flow_dead = false;

            let output = ListManaged(Stmt).initCapacity(p.allocator, stmts.items.len) catch unreachable;

            const dead_code_elimination = p.options.features.dead_code_elimination;
            for (stmt of stmts.items) {
                if (is_control_flow_dead && dead_code_elimination &&
                    !SideEffects.shouldKeepStmtInDeadControlFlow(stmt, p.allocator))
                {
                    // Strip unnecessary statements if the control flow is dead here
                    continue;
                }

                // Inline single-use variable declarations where possible:
                //
                //   // Before
                //   let x = fn();
                //   return x.y();
                //
                //   // After
                //   return fn().y();
                //
                // The declaration must not be exported. We can't just check for the
                // "export" keyword because something might do "export {id};" later on.
                // Instead we just ignore all top-level declarations for now. That means
                // this optimization currently only applies in nested scopes.
                //
                // Ignore declarations if the scope is shadowed by a direct "eval" call.
                // The eval'd code may indirectly reference this symbol and the actual
                // use count may be greater than 1.
                if (p.current_scope != p.module_scope && !p.current_scope.contains_direct_eval) {
                    // Keep inlining variables until a failure or until there are none left.
                    // That handles cases like this:
                    //
                    //   // Before
                    //   let x = fn();
                    //   let y = x.prop;
                    //   return y;
                    //
                    //   // After
                    //   return fn().prop;
                    //
                    inner: while (output.items.len > 0) {
                        // Ignore "var" declarations since those have function-level scope and
                        // we may not have visited all of their uses yet by this point. We
                        // should have visited all the uses of "let" and "const" declarations
                        // by now since they are scoped to this block which we just finished
                        // visiting.
                        const prev_statement = &output.items[output.items.len - 1];
                        switch (prev_statement.data) {
                            .s_local => {
                                let local = prev_statement.data.s_local;
                                // "using" / "await using" declarations have disposal
                                // side-effects on scope exit, so they must not be
                                // removed by inlining their initializer into the use.
                                if (local.decls.len == 0 || local.kind == .k_var || local.kind.isUsing() || local.is_export) {
                                    break;
                                }

                                const last: *Decl = local.decls.last().?;
                                // The variable must be initialized, since we will be substituting
                                // the value into the usage.
                                if (last.value == null)
                                    break;

                                // The binding must be an identifier that is only used once.
                                // Ignore destructuring bindings since that's not the simple case.
                                // Destructuring bindings could potentially execute side-effecting
                                // code which would invalidate reordering.

                                switch (last.binding.data) {
                                    .b_identifier => |ident| {
                                        const id = ident.ref;

                                        const symbol: *const Symbol = &p.symbols.items[id.innerIndex()];

                                        // Try to substitute the identifier with the initializer. This will
                                        // fail if something with side effects is in between the declaration
                                        // and the usage.
                                        if (symbol.use_count_estimate == 1) {
                                            if (p.substituteSingleUseSymbolInStmt(stmt, id, last.value.?)) {
                                                switch (local.decls.len) {
                                                    1 => {
                                                        local.decls.len = 0;
                                                        output.items.len -= 1;
                                                        continue :inner;
                                                    },
                                                    else => {
                                                        local.decls.len -= 1;
                                                        continue :inner;
                                                    },
                                                }
                                            }
                                        }
                                    },
                                    else => {},
                                }
                            },
                            else => {},
                        }
                        break;
                    }
                }

                // don't merge super calls to ensure they are called before "this" is accessed
                if (stmt.isSuperCall()) {
                    output.append(stmt) catch unreachable;
                    continue;
                }

                // The following calls to `joinWithComma` are only enabled during bundling. We do this
                // to avoid changing line numbers too much for source maps

                switch (stmt.data) {
                    .s_empty => continue,

                    // skip directives for now
                    .s_directive => continue,

                    .s_local => |local| {
                        // Merge adjacent local statements
                        if (output.items.len > 0) {
                            let prev_stmt = &output.items[output.items.len - 1];
                            if (prev_stmt.data == .s_local &&
                                local.canMergeWith(prev_stmt.data.s_local))
                            {
                                prev_stmt.data.s_local.decls.appendSlice(
                                    p.allocator,
                                    local.decls.slice(),
                                ) catch e => bun.handleOom(e);
                                continue;
                            }
                        }
                    },

                    .s_expr => |s_expr| {
                        // Merge adjacent expression statements
                        if (output.items.len > 0) {
                            let prev_stmt = &output.items[output.items.len - 1];
                            if (prev_stmt.data == .s_expr && !prev_stmt.isSuperCall() && p.options.runtimeMergeAdjacentExpressionStatements()) {
                                prev_stmt.data.s_expr.does_not_affect_tree_shaking = prev_stmt.data.s_expr.does_not_affect_tree_shaking &&
                                    s_expr.does_not_affect_tree_shaking;
                                prev_stmt.data.s_expr.value = prev_stmt.data.s_expr.value.joinWithComma(
                                    s_expr.value,
                                    p.allocator,
                                );
                                continue;
                            } else if
                            //
                            // Input:
                            //      var f;
                            //      f = 123;
                            // Output:
                            //      var f = 123;
                            //
                            // This doesn't handle every case. Only the very simple one.
                            (prev_stmt.data == .s_local &&
                                s_expr.value.data == .e_binary &&
                                prev_stmt.data.s_local.decls.len == 1 &&
                                s_expr.value.data.e_binary.op == .bin_assign &&
                                // we can only do this with var because var is hoisted
                                // the statement we are merging into may use the statement before its defined.
                                prev_stmt.data.s_local.kind == .k_var)
                            {
                                let prev_local = prev_stmt.data.s_local;
                                const bin_assign = s_expr.value.data.e_binary;

                                if (bin_assign.left.data == .e_identifier) {
                                    let decl = &prev_local.decls.slice()[0];
                                    if (decl.binding.data == .b_identifier &&
                                        decl.binding.data.b_identifier.ref.eql(bin_assign.left.data.e_identifier.ref) &&
                                        // If the value was assigned, we shouldn't merge it incase it was used in the current statement
                                        // https://github.com/oven-sh/bun/issues/2948
                                        // We don't have a more granular way to check symbol usage so this is the best we can do
                                        decl.value == null)
                                    {
                                        decl.value = bin_assign.right;
                                        p.ignoreUsage(bin_assign.left.data.e_identifier.ref);
                                        continue;
                                    }
                                }
                            }
                        }
                    },
                    .s_switch => |s_switch| {
                        // Absorb a previous expression statement
                        if (output.items.len > 0 && p.options.runtimeMergeAdjacentExpressionStatements()) {
                            let prev_stmt = &output.items[output.items.len - 1];
                            if (prev_stmt.data == .s_expr && !prev_stmt.isSuperCall()) {
                                s_switch.test_ = prev_stmt.data.s_expr.value.joinWithComma(s_switch.test_, p.allocator);
                                output.items.len -= 1;
                            }
                        }
                    },
                    .s_if => |s_if| {
                        // Absorb a previous expression statement
                        if (output.items.len > 0 && p.options.runtimeMergeAdjacentExpressionStatements()) {
                            let prev_stmt = &output.items[output.items.len - 1];
                            if (prev_stmt.data == .s_expr && !prev_stmt.isSuperCall()) {
                                s_if.test_ = prev_stmt.data.s_expr.value.joinWithComma(s_if.test_, p.allocator);
                                output.items.len -= 1;
                            }
                        }

                        // TODO: optimize jump
                    },

                    .s_return => |ret| {
                        // Merge return statements with the previous expression statement
                        if (output.items.len > 0 && ret.value != null && p.options.runtimeMergeAdjacentExpressionStatements()) {
                            let prev_stmt = &output.items[output.items.len - 1];
                            if (prev_stmt.data == .s_expr && !prev_stmt.isSuperCall()) {
                                ret.value = prev_stmt.data.s_expr.value.joinWithComma(ret.value.?, p.allocator);
                                prev_stmt.* = stmt;
                                continue;
                            }
                        }

                        is_control_flow_dead = true;
                    },

                    .s_break, .s_continue => {
                        is_control_flow_dead = true;
                    },

                    .s_throw => {
                        // Merge throw statements with the previous expression statement
                        if (output.items.len > 0 && p.options.runtimeMergeAdjacentExpressionStatements()) {
                            let prev_stmt = &output.items[output.items.len - 1];
                            if (prev_stmt.data == .s_expr && !prev_stmt.isSuperCall()) {
                                prev_stmt.* = p.s(S.Throw{
                                    value: prev_stmt.data.s_expr.value.joinWithComma(
                                        stmt.data.s_throw.value,
                                        p.allocator,
                                    ),
                                }, stmt.loc);
                                continue;
                            }
                        }

                        is_control_flow_dead = true;
                    },

                    else => {},
                }

                output.append(stmt) catch unreachable;
            }

            stmts.deinit();
            stmts.* = output;
        }
    };
}

pub fn fnBodyContainsUseStrict(body: []Stmt): logger.Loc? {
    for (stmt of body) {
        // "use strict" has to appear at the top of the function body
        // but we can allow comments
        switch (stmt.data) {
            .s_comment => {
                continue;
            },
            .s_directive => |dir| {
                if (strings.eqlComptime(dir.value, "use strict")) {
                    return stmt.loc;
                }
            },
            .s_empty => {},
            else => return null,
        }
    }

    return null;
}

type string = []const u8;

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;
const js_lexer = bun.js_lexer;
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const BindingNodeIndex = js_ast.BindingNodeIndex;
const DeclaredSymbol = js_ast.DeclaredSymbol;
const E = js_ast.E;
const Expr = js_ast.Expr;
const ExprNodeList = js_ast.ExprNodeList;
const LocRef = js_ast.LocRef;
const S = js_ast.S;
const Scope = js_ast.Scope;
const Stmt = js_ast.Stmt;
const StmtNodeIndex = js_ast.StmtNodeIndex;
const Symbol = js_ast.Symbol;

const G = js_ast.G;
const Arg = G.Arg;
const Decl = G.Decl;
const Property = G.Property;

const js_parser = bun.js_parser;
const ExprIn = js_parser.ExprIn;
const FnOnlyDataVisit = js_parser.FnOnlyDataVisit;
const FnOrArrowDataVisit = js_parser.FnOrArrowDataVisit;
const ImportItemForNamespaceMap = js_parser.ImportItemForNamespaceMap;
const JSXTransformType = js_parser.JSXTransformType;
const PrependTempRefsOpts = js_parser.PrependTempRefsOpts;
const Ref = js_parser.Ref;
const RuntimeFeatures = js_parser.RuntimeFeatures;
const SideEffects = js_parser.SideEffects;
const StmtsKind = js_parser.StmtsKind;
const StringVoidMap = js_parser.StringVoidMap;
const TempRef = js_parser.TempRef;
const TypeScript = js_parser.TypeScript;
const VisitArgsOpts = js_parser.VisitArgsOpts;
const isEvalOrArguments = js_parser.isEvalOrArguments;
const options = js_parser.options;

const AutoHashMap = std.AutoHashMap;
const List = std.ArrayListUnmanaged;
const ListManaged = std.array_list.Managed;

struct DeinitGuard<T: type> {
    ptr: (*T)? = null,

    ~init() {
        if (ptr := _.ptr) {
            ptr.deinit();
        }
    }
}

struct DeinitWithAllocGuard<T: type> {
    ptr: (*T)? = null,
    allocator: std.mem.Allocator,

    ~init() {
        if (ptr := _.ptr) {
            ptr.deinit(_.allocator);
        }
    }
}

struct ReleaseNodeGuard<T: type> {
    node: (*T.Node)? = null,

    ~init() {
        if (n := _.node) {
            const release_fn = T.release;
            release_fn(n);
        }
    }
}
