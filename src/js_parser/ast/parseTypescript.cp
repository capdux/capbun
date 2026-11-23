pub fn ParseTypescript(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        const is_typescript_enabled = P.is_typescript_enabled;

        static fn parseTypeScriptDecorators(p: *P): ![]ExprNodeIndex {
            if (!is_typescript_enabled && !p.options.features.standard_decorators) {
                return [];
            }

            let decorators = ListManaged(ExprNodeIndex).init(p.allocator);
            while (p.lexer.token == T.t_at) {
                p.lexer.next()?;

                if (p.options.features.standard_decorators) {
                    // TC39 standard decorator grammar:
                    //   @Identifier
                    //   @Identifier.member
                    //   @Identifier.member(args)
                    //   @(Expression)
                    decorators.ensureUnusedCapacity(1)?;
                    decorators.unusedCapacitySlice()[0] = p.parseStandardDecorator()?;
                    decorators.items.len += 1;
                } else {
                    // Parse a new/call expression with "exprFlagTSDecorator" so we ignore
                    // EIndex expressions, since they may be part of a computed property:
                    //
                    //   class Foo {
                    //     @foo ['computed']() {}
                    //   }
                    //
                    // This matches the behavior of the TypeScript compiler.
                    decorators.ensureUnusedCapacity(1)?;
                    const slot = unsafe { &decorators.unusedCapacitySlice()[0] };
                    p.parseExprWithFlags(.new, Expr.EFlags.ts_decorator, slot)?;
                    decorators.items.len += 1;
                }
            }

            return decorators.items;
        }

        /// Parse a standard (TC39) decorator expression following the `@` token.
        ///
        /// DecoratorExpression:
        ///   @ IdentifierReference
        ///   @ DecoratorMemberExpression
        ///   @ DecoratorCallExpression
        ///   @ DecoratorParenthesizedExpression
        static fn parseStandardDecorator(p: *P): !ExprNodeIndex {
            const loc = p.lexer.loc();

            // @(Expression) — parenthesized, any expression allowed
            if (p.lexer.token == .t_open_paren) {
                p.lexer.next()?;
                const expr = p.parseExpr(.lowest)?;
                p.lexer.expect(.t_close_paren)?;
                return expr;
            }

            // Must start with an identifier
            if (p.lexer.token != .t_identifier) {
                p.lexer.expect(.t_identifier)?;
                return error.SyntaxError;
            }

            let expr = p.newExpr(E.Identifier{ @"ref": p.storeNameInRef(p.lexer.identifier)? }, loc);
            p.lexer.next()?;

            // Skip TypeScript type arguments after the identifier (e.g., @foo<T>)
            if (is_typescript_enabled) {
                * = p.skipTypeScriptTypeArguments(false)?;
            }

            // DecoratorMemberExpression: Identifier (.Identifier)*
            while (p.lexer.token == .t_dot || p.lexer.token == .t_question_dot) {
                // Forbid optional chaining in decorators
                if (p.lexer.token == .t_question_dot) {
                    p.log.addError(p.source, p.lexer.loc(), "Optional chaining is not allowed in decorator expressions")?;
                    return error.SyntaxError;
                }

                p.lexer.next()?;

                if (!p.lexer.isIdentifierOrKeyword()) {
                    p.lexer.expect(.t_identifier)?;
                    return error.SyntaxError;
                }

                const name = p.lexer.identifier;
                const name_loc = p.lexer.loc();
                p.lexer.next()?;

                expr = p.newExpr(E.Dot{ target: expr, name: name, name_loc: name_loc }, loc);

                // Skip TypeScript type arguments after member access (e.g., @foo.bar<T>)
                if (is_typescript_enabled) {
                    * = p.skipTypeScriptTypeArguments(false)?;
                }
            }

            // DecoratorCallExpression: DecoratorMemberExpression Arguments
            // Only a single call is allowed, no chaining after the call
            if (p.lexer.token == .t_open_paren) {
                const args = p.parseCallArgs()?;
                expr = p.newExpr(E.Call{
                    target: expr,
                    args: args.list,
                    close_paren_loc: args.loc,
                }, loc);
            }

            return expr;
        }

        static fn parseTypeScriptNamespaceStmt(p: *P, loc: logger.Loc, opts: *ParseStatementOptions): anyerror!Stmt {
            // "namespace foo {}";
            const name_loc = p.lexer.loc();
            const name_text = p.lexer.identifier;
            p.lexer.next()?;

            // Generate the namespace object
            const ts_namespace = p.getOrCreateExportedNamespaceMembers(name_text, opts.is_export, false);
            const exported_members = ts_namespace.exported_members;
            const ns_member_data = js_ast.TSNamespaceMember.Data{ namespace: exported_members };

            // Declare the namespace and create the scope
            let name = LocRef{ loc: name_loc, @"ref": null };
            const scope_index = p.pushScopeForParsePass(.entry, loc)?;
            p.current_scope.ts_namespace = ts_namespace;

            const old_has_non_local_export_declare_inside_namespace = p.has_non_local_export_declare_inside_namespace;
            p.has_non_local_export_declare_inside_namespace = false;

            // Parse the statements inside the namespace
            let stmts: ListManaged(Stmt) = ListManaged(Stmt).init(p.allocator);
            if (p.lexer.token == .t_dot) {
                const dot_loc = p.lexer.loc();
                p.lexer.next()?;

                let _opts = ParseStatementOptions{
                    is_export: true,
                    is_namespace_scope: true,
                    is_typescript_declare: opts.is_typescript_declare,
                };
                stmts.append(p.parseTypeScriptNamespaceStmt(dot_loc, &_opts)?) catch unreachable;
            } else if (opts.is_typescript_declare && p.lexer.token != .t_open_brace) {
                p.lexer.expectOrInsertSemicolon()?;
            } else {
                p.lexer.expect(.t_open_brace)?;
                let _opts = ParseStatementOptions{
                    is_namespace_scope: true,
                    is_typescript_declare: opts.is_typescript_declare,
                };
                stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, p.parseStmtsUpTo(.t_close_brace, &_opts)?);
                p.lexer.next()?;
            }
            const has_non_local_export_declare_inside_namespace = p.has_non_local_export_declare_inside_namespace;
            p.has_non_local_export_declare_inside_namespace = old_has_non_local_export_declare_inside_namespace;

            // Add any exported members from this namespace's body as members of the
            // associated namespace object.
            for (stmt of stmts.items) {
                switch (stmt.data) {
                    .s_function func => {
                        if (func.func.flags.contains(.is_export)) {
                            if (locref_opt := func.func.name) {
                                if (ref_opt := locref_opt.ref) {
                                    const fn_name = p.symbols.items[ref_opt.inner_index].original_name;
                                    exported_members.put(p.allocator, fn_name, {
                                        loc: locref_opt.loc,
                                        data: .property,
                                    })?;
                                    p.ref_to_ts_namespace_member.put(
                                        p.allocator,
                                        ref_opt,
                                        .property,
                                    )?;
                                }
                            }
                        }
                    },
                    .s_class cls => {
                        if (cls.is_export) {
                            if (locref_opt := cls.class.class_name) {
                                if (ref_opt := locref_opt.ref) {
                                    const class_name = p.symbols.items[ref_opt.inner_index].original_name;
                                    exported_members.put(p.allocator, class_name, {
                                        loc: locref_opt.loc,
                                        data: .property,
                                    })?;
                                    p.ref_to_ts_namespace_member.put(
                                        p.allocator,
                                        ref_opt,
                                        .property,
                                    )?;
                                }
                            }
                        }
                    },
                    .s_namespace ns => {
                        if (ns.is_export) {
                            if (ref_opt := ns.name.ref) {
                                if (member_data := p.ref_to_ts_namespace_member.get(ref_opt)) {
                                    exported_members.put(
                                        p.allocator,
                                        p.symbols.items[ref_opt.inner_index].original_name,
                                        {
                                            data: member_data,
                                            loc: ns.name.loc,
                                        },
                                    )?;
                                    p.ref_to_ts_namespace_member.put(
                                        p.allocator,
                                        ref_opt,
                                        member_data,
                                    )?;
                                }
                            }
                        }
                    },
                    .s_enum ns => {
                        if (ns.is_export) {
                            if (ref_opt := ns.name.ref) {
                                if (member_data := p.ref_to_ts_namespace_member.get(ref_opt)) {
                                    exported_members.put(
                                        p.allocator,
                                        p.symbols.items[ref_opt.inner_index].original_name,
                                        {
                                            data: member_data,
                                            loc: ns.name.loc,
                                        },
                                    )?;
                                    p.ref_to_ts_namespace_member.put(
                                        p.allocator,
                                        ref_opt,
                                        member_data,
                                    )?;
                                }
                            }
                        }
                    },
                    .s_local local => {
                        if (local.is_export) {
                            for (decl of local.decls.slice()) {
                                p.defineExportedNamespaceBinding(
                                    exported_members,
                                    decl.binding,
                                )?;
                            }
                        }
                    },
                    else => {},
                }
            }

            // Import assignments may be only used in type expressions, not value
            // expressions. If this is the case, the TypeScript compiler removes
            // them entirely from the output. That can cause the namespace itself
            // to be considered empty and thus be removed.
            let import_equal_count: usize = 0;
            for (stmt of stmts.items) {
                switch (stmt.data) {
                    .s_local local => {
                        if (local.was_ts_import_equals && !local.is_export) {
                            import_equal_count += 1;
                        }
                    },
                    else => {},
                }
            }

            // TypeScript omits namespaces without values. These namespaces
            // are only allowed to be used in type expressions. They are
            // allowed to be exported, but can also only be used in type
            // expressions when imported. So we shouldn't count them as a
            // real export either.
            //
            // TypeScript also strangely counts namespaces containing only
            // "export declare" statements as non-empty even though "declare"
            // statements are only type annotations. We cannot omit the namespace
            // in that case. See https://github.com/evanw/esbuild/issues/1158.
            if ((stmts.items.len == import_equal_count && !has_non_local_export_declare_inside_namespace) || opts.is_typescript_declare) {
                p.popAndDiscardScope(scope_index);
                if (opts.is_module_scope) {
                    p.local_type_names.put(p.allocator, name_text, true) catch unreachable;
                }
                return p.s(S.TypeScript{}, loc);
            }

            let arg_ref = Ref.None;
            if (!opts.is_typescript_declare) {
                // Avoid a collision with the namespace closure argument variable if the
                // namespace exports a symbol with the same name as the namespace itself:
                //
                //   namespace foo {
                //     export let foo = 123
                //     console.log(foo)
                //   }
                //
                // TypeScript generates the following code in this case:
                //
                //   var foo;
                //   (function (foo_1) {
                //     foo_1.foo = 123;
                //     console.log(foo_1.foo);
                //   })(foo || (foo = {}));
                //
                if (p.current_scope.members.contains(name_text)) {
                    // Add a "_" to make tests easier to read, since non-bundler tests don't
                    // run the renamer. For external-facing things the renamer will avoid
                    // collisions automatically so this isn't important for correctness.
                    arg_ref = p.newSymbol(.hoisted, strings.cat(p.allocator, "_", name_text) catch unreachable) catch unreachable;
                    bun.handleOom(p.current_scope.generated.append(p.allocator, arg_ref));
                } else {
                    arg_ref = p.newSymbol(.hoisted, name_text) catch unreachable;
                }
                ts_namespace.arg_ref = arg_ref;
            }
            p.popScope();

            if (!opts.is_typescript_declare) {
                const sym_ref = p.declareSymbol(.ts_namespace, name_loc, name_text)?;
                name.ref = sym_ref;
                p.ref_to_ts_namespace_member.put(p.allocator, sym_ref, ns_member_data)?;
            }

            return p.s(S.Namespace{
                name: name,
                arg: arg_ref,
                stmts: stmts.items,
                is_export: opts.is_export,
            }, loc);
        }

        static fn parseTypeScriptImportEqualsStmt(p: *P, loc: logger.Loc, opts: *ParseStatementOptions, default_name_loc: logger.Loc, default_name: string): anyerror!Stmt {
            p.lexer.expect(.t_equals)?;

            const kind = S.Local.Kind.k_const;
            const name = p.lexer.identifier;
            const target = p.newExpr(E.Identifier{ @"ref": p.storeNameInRef(name) catch unreachable }, p.lexer.loc());
            let value = target;
            p.lexer.expect(.t_identifier)?;

            if (strings.eqlComptime(name, "require") && p.lexer.token == .t_open_paren) {
                // "import ns = require('x')"
                p.lexer.next()?;
                const path = p.newExpr(p.lexer.toEString()?, p.lexer.loc());
                p.lexer.expect(.t_string_literal)?;
                p.lexer.expect(.t_close_paren)?;
                if (!opts.is_typescript_declare) {
                    const args = ExprNodeList.initOne(p.allocator, path)?;
                    value = p.newExpr(E.Call{ target: target, close_paren_loc: p.lexer.loc(), args: args }, loc);
                }
            } else {
                // "import Foo = Bar"
                // "import Foo = Bar.Baz"
                let prev_value = value;
                while (p.lexer.token == .t_dot) : (prev_value = value) {
                    p.lexer.next()?;
                    value = p.newExpr(E.Dot{ target: prev_value, name: p.lexer.identifier, name_loc: p.lexer.loc() }, loc);
                    p.lexer.expect(.t_identifier)?;
                }
            }

            p.lexer.expectOrInsertSemicolon()?;

            if (opts.is_typescript_declare) {
                // "import type foo = require('bar');"
                // "import type foo = bar.baz;"
                return p.s(S.TypeScript{}, loc);
            }

            const sym_ref = p.declareSymbol(.constant, default_name_loc, default_name) catch unreachable;
            let decls = p.allocator.alloc(Decl, 1) catch unreachable;
            decls[0] = Decl{
                binding: p.b(B.Identifier{ @"ref": sym_ref }, default_name_loc),
                value: value,
            };
            return p.s(S.Local{
                kind: kind,
                decls: Decl.List.fromOwnedSlice(decls),
                is_export: opts.is_export,
                was_ts_import_equals: true,
            }, loc);
        }

        static fn parseTypescriptEnumStmt(p: *P, loc: logger.Loc, opts: *ParseStatementOptions): anyerror!Stmt {
            p.lexer.expect(.t_enum)?;
            const name_loc = p.lexer.loc();
            const name_text = p.lexer.identifier;
            p.lexer.expect(.t_identifier)?;
            let name = LocRef{ loc: name_loc, @"ref": Ref.None };

            // Generate the namespace object
            let arg_ref: Ref = undefined;
            const ts_namespace = p.getOrCreateExportedNamespaceMembers(name_text, opts.is_export, true);
            const exported_members = ts_namespace.exported_members;
            const enum_member_data = js_ast.TSNamespaceMember.Data{ namespace: exported_members };

            // Declare the enum and create the scope
            const scope_index = p.scopes_in_order.items.len;
            if (!opts.is_typescript_declare) {
                const enum_ref = p.declareSymbol(.ts_enum, name_loc, name_text)?;
                name.ref = enum_ref;
                * = p.pushScopeForParsePass(.entry, loc)?;
                p.current_scope.ts_namespace = ts_namespace;
                bun.handleOom(p.ref_to_ts_namespace_member.putNoClobber(p.allocator, enum_ref, enum_member_data));
            }

            p.lexer.expect(.t_open_brace)?;

            // Parse the body
            let values = std.array_list.Managed(js_ast.EnumValue).init(p.allocator);
            while (p.lexer.token != .t_close_brace) {
                let value = js_ast.EnumValue{ loc: p.lexer.loc(), @"ref": Ref.None, name: undefined, value: null };
                let needs_symbol = false;

                // Parse the name
                if (p.lexer.token == .t_string_literal) {
                    const utf8_str = p.lexer.toUTF8EString()?;
                    value.name = utf8_str.slice8();
                    needs_symbol = js_lexer.isIdentifier(value.name);
                } else if (p.lexer.isIdentifierOrKeyword()) {
                    value.name = p.lexer.identifier;
                    needs_symbol = true;
                } else {
                    p.lexer.expect(.t_identifier)?;
                    // error early, name is still `undefined`
                    return error.SyntaxError;
                }
                p.lexer.next()?;

                // Identifiers can be referenced by other values
                if (!opts.is_typescript_declare && needs_symbol) {
                    value.ref = p.declareSymbol(.other, value.loc, value.name)?;
                }

                // Parse the initializer
                if (p.lexer.token == .t_equals) {
                    p.lexer.next()?;
                    value.value = p.parseExpr(.comma)?;
                }

                values.append(value) catch unreachable;

                exported_members.put(p.allocator, value.name, {
                    loc: value.loc,
                    data: .enum_property,
                }) catch e => bun.handleOom(e);

                if (p.lexer.token != .t_comma && p.lexer.token != .t_semicolon) {
                    break;
                }

                p.lexer.next()?;
            }

            if (!opts.is_typescript_declare) {
                // Avoid a collision with the enum closure argument variable if the
                // enum exports a symbol with the same name as the enum itself:
                //
                //   enum foo {
                //     foo = 123,
                //     bar = foo,
                //   }
                //
                // TypeScript generates the following code in this case:
                //
                //   var foo;
                //   (function (foo) {
                //     foo[foo["foo"] = 123] = "foo";
                //     foo[foo["bar"] = 123] = "bar";
                //   })(foo || (foo = {}));
                //
                // Whereas in this case:
                //
                //   enum foo {
                //     bar = foo as any,
                //   }
                //
                // TypeScript generates the following code:
                //
                //   var foo;
                //   (function (foo) {
                //     foo[foo["bar"] = foo] = "bar";
                //   })(foo || (foo = {}));
                if (p.current_scope.members.contains(name_text)) {
                    // Add a "_" to make tests easier to read, since non-bundler tests don't
                    // run the renamer. For external-facing things the renamer will avoid
                    // collisions automatically so this isn't important for correctness.
                    arg_ref = p.newSymbol(.hoisted, strings.cat(p.allocator, "_", name_text) catch unreachable) catch unreachable;
                    bun.handleOom(p.current_scope.generated.append(p.allocator, arg_ref));
                } else {
                    arg_ref = p.declareSymbol(.hoisted, name_loc, name_text) catch unreachable;
                }
                bun.handleOom(p.ref_to_ts_namespace_member.put(p.allocator, arg_ref, enum_member_data));
                ts_namespace.arg_ref = arg_ref;

                p.popScope();
            }

            p.lexer.expect(.t_close_brace)?;

            if (opts.is_typescript_declare) {
                if (opts.is_namespace_scope && opts.is_export) {
                    p.has_non_local_export_declare_inside_namespace = true;
                }

                return p.s(S.TypeScript{}, loc);
            }

            // Save these for when we do out-of-order enum visiting
            //
            // Make a copy of "scopesInOrder" instead of a slice or index since
            // the original array may be flattened in the future by
            // "popAndFlattenScope"
            p.scopes_in_order_for_enum.putNoClobber(
                p.allocator,
                loc,
                scope_order_clone: {
                    let count: usize = 0;
                    for (i of p.scopes_in_order.items[scope_index..]) {
                        if (i != null) count += 1;
                    }

                    const items = bun.handleOom(p.allocator.alloc(ScopeOrder, count));
                    let i: usize = 0;
                    for (item of p.scopes_in_order.items[scope_index..]) {
                        items[i] = item ?? continue;
                        i += 1;
                    }
                    break :scope_order_clone items;
                },
            ) catch e => bun.handleOom(e);

            return p.s(S.Enum{
                name: name,
                arg: arg_ref,
                values: values.items,
                is_export: opts.is_export,
            }, loc);
        }
    };
}

type string = []const u8;

import bun from "bun";
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const B = js_ast.B;
const E = js_ast.E;
const Expr = js_ast.Expr;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const ExprNodeList = js_ast.ExprNodeList;
const LocRef = js_ast.LocRef;
const S = js_ast.S;
const Stmt = js_ast.Stmt;

const G = js_ast.G;
const Decl = G.Decl;

const js_lexer = bun.js_lexer;
const T = js_lexer.T;

const js_parser = bun.js_parser;
const JSXTransformType = js_parser.JSXTransformType;
const ParseStatementOptions = js_parser.ParseStatementOptions;
const Ref = js_parser.Ref;
const ScopeOrder = js_parser.ScopeOrder;
const TypeScript = js_parser.TypeScript;

const List = std.ArrayListUnmanaged;
const ListManaged = std.array_list.Managed;
