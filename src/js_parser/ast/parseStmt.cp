pub fn ParseStmt(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        const createDefaultName = P.createDefaultName;
        const extractDeclsForBinding = P.extractDeclsForBinding;
        const is_typescript_enabled = P.is_typescript_enabled;
        const track_symbol_usage_during_parse_pass = P.track_symbol_usage_during_parse_pass;

        static fn t_semicolon(p: *P): anyerror!Stmt {
            p.lexer.next()?;
            return Stmt.empty();
        }

        static fn t_export(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            const previous_export_keyword = p.esm_export_keyword;
            if (opts.is_module_scope) {
                p.esm_export_keyword = p.lexer.range();
            } else if (!opts.is_namespace_scope) {
                p.lexer.unexpected()?;
                return error.SyntaxError;
            }
            p.lexer.next()?;

            // TypeScript decorators only work on class declarations
            // "@decorator export class Foo {}"
            // "@decorator export abstract class Foo {}"
            // "@decorator export default class Foo {}"
            // "@decorator export default abstract class Foo {}"
            // "@decorator export declare class Foo {}"
            // "@decorator export declare abstract class Foo {}"
            if (opts.ts_decorators != null && p.lexer.token != js_lexer.T.t_class &&
                p.lexer.token != js_lexer.T.t_default &&
                !p.lexer.isContextualKeyword("abstract") &&
                !p.lexer.isContextualKeyword("declare"))
            {
                p.lexer.expected(js_lexer.T.t_class)?;
            }

            switch (p.lexer.token) {
                T.t_class, T.t_const, T.t_function, T.t_var => {
                    opts.is_export = true;
                    return p.parseStmt(opts);
                },

                T.t_import => {
                    // "export import foo = bar"
                    if (is_typescript_enabled && (opts.is_module_scope || opts.is_namespace_scope)) {
                        opts.is_export = true;
                        return p.parseStmt(opts);
                    }

                    p.lexer.unexpected()?;
                    return error.SyntaxError;
                },

                T.t_enum => {
                    if (!is_typescript_enabled) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }

                    opts.is_export = true;
                    return p.parseStmt(opts);
                },

                T.t_identifier => {
                    if (p.lexer.isContextualKeyword("let")) {
                        opts.is_export = true;
                        return p.parseStmt(opts);
                    }

                    if (comptime is_typescript_enabled) {
                        if (opts.is_typescript_declare && p.lexer.isContextualKeyword("as")) {
                            // "export as namespace ns;"
                            p.lexer.next()?;
                            p.lexer.expectContextualKeyword("namespace")?;
                            p.lexer.expect(T.t_identifier)?;
                            p.lexer.expectOrInsertSemicolon()?;

                            return p.s(S.TypeScript{}, loc);
                        }
                    }

                    if (p.lexer.isContextualKeyword("async")) {
                        const asyncRange = p.lexer.range();
                        p.lexer.next()?;
                        if (p.lexer.has_newline_before) {
                            p.log.addRangeError(p.source, asyncRange, "Unexpected newline after \"async\"")?;
                        }

                        p.lexer.expect(T.t_function)?;
                        opts.is_export = true;
                        return p.parseFnStmt(loc, opts, asyncRange)?;
                    }

                    if (is_typescript_enabled) {
                        if (ident := TypeScript.Identifier.forStr(p.lexer.identifier)) {
                            switch (ident) {
                                .s_type => {
                                    // "export type foo = ..."
                                    const type_range = p.lexer.range();
                                    p.lexer.next()?;
                                    if (p.lexer.has_newline_before) {
                                        p.log.addErrorFmt(p.source, type_range.end(), p.allocator, "Unexpected newline after \"type\"", {})?;
                                        return error.SyntaxError;
                                    }
                                    let skipper = ParseStatementOptions{ is_module_scope: opts.is_module_scope, is_export: true };
                                    p.skipTypeScriptTypeStmt(&skipper)?;
                                    return p.s(S.TypeScript{}, loc);
                                },
                                .s_namespace, .s_abstract, .s_module, .s_interface => {
                                    // "export namespace Foo {}"
                                    // "export abstract class Foo {}"
                                    // "export module Foo {}"
                                    // "export interface Foo {}"
                                    opts.is_export = true;
                                    return p.parseStmt(opts)?;
                                },
                                .s_declare => {
                                    // "export declare class Foo {}"
                                    opts.is_export = true;
                                    opts.lexical_decl = .allow_all;
                                    opts.is_typescript_declare = true;
                                    return p.parseStmt(opts)?;
                                },
                            }
                        }
                    }

                    p.lexer.unexpected()?;
                    return error.SyntaxError;
                },

                T.t_default => {
                    if (!opts.is_module_scope && (!opts.is_namespace_scope || !opts.is_typescript_declare)) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }

                    const defaultLoc = p.lexer.loc();
                    p.lexer.next()?;

                    // TypeScript decorators only work on class declarations
                    // "@decorator export default class Foo {}"
                    // "@decorator export default abstract class Foo {}"
                    if (opts.ts_decorators != null && p.lexer.token != T.t_class && !p.lexer.isContextualKeyword("abstract")) {
                        p.lexer.expected(T.t_class)?;
                    }

                    if (p.lexer.isContextualKeyword("async")) {
                        const async_range = p.lexer.range();
                        p.lexer.next()?;
                        if (p.lexer.token == T.t_function && !p.lexer.has_newline_before) {
                            p.lexer.next()?;
                            let stmtOpts = ParseStatementOptions{
                                is_name_optional: true,
                                lexical_decl: .allow_all,
                            };
                            const stmt = p.parseFnStmt(loc, &stmtOpts, async_range)?;
                            if (std.meta.activeTag(stmt.data) == .s_type_script) {
                                // This was just a type annotation
                                return stmt;
                            }

                            let defaultName: js_ast.LocRef = undefined;
                            if (name := stmt.data.s_function.func.name) {
                                defaultName = js_ast.LocRef{ loc: name.loc, @"ref": name.ref };
                            } else {
                                defaultName = p.createDefaultName(defaultLoc)?;
                            }

                            const value = js_ast.StmtOrExpr{ stmt: stmt };
                            return p.s(S.ExportDefault{ default_name: defaultName, value: value }, loc);
                        }

                        const defaultName = createDefaultName(p, loc)?;

                        let expr = p.parseAsyncPrefixExpr(async_range, Level.comma)?;
                        p.parseSuffix(&expr, Level.comma, null, Expr.EFlags.none)?;
                        p.lexer.expectOrInsertSemicolon()?;
                        const value = js_ast.StmtOrExpr{ expr: expr };
                        p.has_export_default = true;
                        return p.s(S.ExportDefault{ default_name: defaultName, value: value }, loc);
                    }

                    if (p.lexer.token == .t_function || p.lexer.token == .t_class || p.lexer.isContextualKeyword("interface")) {
                        let _opts = ParseStatementOptions{
                            ts_decorators: opts.ts_decorators,
                            is_name_optional: true,
                            lexical_decl: .allow_all,
                        };
                        const stmt = p.parseStmt(&_opts)?;

                        const default_name: js_ast.LocRef = default_name_getter: {
                            switch (stmt.data) {
                                // This was just a type annotation
                                .s_type_script => {
                                    return stmt;
                                },

                                .s_function => |func_container| {
                                    if (name := func_container.func.name) {
                                        break :default_name_getter LocRef{ loc: name.loc, @"ref": name.ref };
                                    }
                                },
                                .s_class => |cls| {
                                    if (name := cls.@"class".class_name) {
                                        break :default_name_getter LocRef{ loc: name.loc, @"ref": name.ref };
                                    }
                                },
                                else => {},
                            }

                            break :default_name_getter createDefaultName(p, defaultLoc) catch unreachable;
                        };
                        p.has_export_default = true;
                        p.has_es_module_syntax = true;
                        return p.s(
                            S.ExportDefault{ default_name: default_name, value: js_ast.StmtOrExpr{ stmt: stmt } },
                            loc,
                        );
                    }

                    const is_identifier = p.lexer.token == .t_identifier;
                    const name = p.lexer.identifier;
                    const expr = p.parseExpr(.comma)?;

                    // Handle the default export of an abstract class in TypeScript
                    if (is_typescript_enabled && is_identifier && (p.lexer.token == .t_class || opts.ts_decorators != null) && strings.eqlComptime(name, "abstract")) {
                        switch (expr.data) {
                            .e_identifier => {
                                let stmtOpts = ParseStatementOptions{
                                    ts_decorators: opts.ts_decorators,
                                    is_name_optional: true,
                                };
                                const stmt: Stmt = p.parseClassStmt(loc, &stmtOpts)?;

                                // Use the statement name if present, since it's a better name
                                const default_name: js_ast.LocRef = default_name_getter: {
                                    switch (stmt.data) {
                                        // This was just a type annotation
                                        .s_type_script => {
                                            return stmt;
                                        },

                                        .s_function => |func_container| {
                                            if (_name := func_container.func.name) {
                                                break :default_name_getter LocRef{ loc: defaultLoc, @"ref": _name.ref };
                                            }
                                        },
                                        .s_class => |cls| {
                                            if (_name := cls.@"class".class_name) {
                                                break :default_name_getter LocRef{ loc: defaultLoc, @"ref": _name.ref };
                                            }
                                        },
                                        else => {},
                                    }

                                    break :default_name_getter createDefaultName(p, defaultLoc) catch unreachable;
                                };
                                p.has_export_default = true;
                                return p.s(S.ExportDefault{ default_name: default_name, value: js_ast.StmtOrExpr{ stmt: stmt } }, loc);
                            },
                            else => {
                                p.panic("internal error: unexpected", {});
                            },
                        }
                    }

                    p.lexer.expectOrInsertSemicolon()?;

                    // Use the expression name if present, since it's a better name
                    p.has_export_default = true;
                    return p.s(
                        S.ExportDefault{
                            default_name: p.defaultNameForExpr(expr, defaultLoc),
                            value: js_ast.StmtOrExpr{
                                expr: expr,
                            },
                        },
                        loc,
                    );
                },
                T.t_asterisk => {
                    if (!opts.is_module_scope && !(opts.is_namespace_scope || !opts.is_typescript_declare)) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }

                    p.lexer.next()?;
                    let namespace_ref: Ref = Ref.None;
                    let alias: js_ast.G.ExportStarAlias? = null;
                    let path: ParsedPath = undefined;

                    if (p.lexer.isContextualKeyword("as")) {
                        // "export * as ns from 'path'"
                        p.lexer.next()?;
                        const name = p.parseClauseAlias("export")?;
                        namespace_ref = p.storeNameInRef(name)?;
                        alias = G.ExportStarAlias{ loc: p.lexer.loc(), original_name: name };
                        p.lexer.next()?;
                        p.lexer.expectContextualKeyword("from")?;
                        path = p.parsePath()?;
                    } else {
                        // "export * from 'path'"
                        p.lexer.expectContextualKeyword("from")?;
                        path = p.parsePath()?;
                        const name = fs.PathName.init(path.text).nonUniqueNameString(p.allocator)?;
                        namespace_ref = p.storeNameInRef(name)?;
                    }

                    const import_record_index = p.addImportRecord(
                        ImportKind.stmt,
                        path.loc,
                        path.text,
                        // TODO: import assertions
                        // path.assertions
                    );

                    if (path.is_macro) {
                        p.log.addError(p.source, path.loc, "cannot use macro in export statement")?;
                    } else if (path.import_tag != .none) {
                        p.log.addError(p.source, loc, "cannot use export statement with \"type\" attribute")?;
                    }

                    if (comptime track_symbol_usage_during_parse_pass) {
                        // In the scan pass, we need _some_ way of knowing *not* to mark as unused
                        p.import_records.items[import_record_index].flags.calls_runtime_re_export_fn = true;
                    }

                    p.lexer.expectOrInsertSemicolon()?;
                    p.has_es_module_syntax = true;
                    return p.s(S.ExportStar{
                        namespace_ref: namespace_ref,
                        alias: alias,
                        import_record_index: import_record_index,
                    }, loc);
                },
                T.t_open_brace => {
                    if (!opts.is_module_scope && !(opts.is_namespace_scope || !opts.is_typescript_declare)) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }

                    const export_clause = p.parseExportClause()?;
                    if (p.lexer.isContextualKeyword("from")) {
                        p.lexer.expectContextualKeyword("from")?;
                        const parsedPath = p.parsePath()?;

                        p.lexer.expectOrInsertSemicolon()?;

                        if (comptime is_typescript_enabled) {
                            // export {type Foo} from 'bar';
                            // ->
                            // nothing
                            // https://www.typescriptlang.org/play?useDefineForClassFields=true&esModuleInterop=false&declaration=false&target=99&isolatedModules=false&ts=4.5.4#code/KYDwDg9gTgLgBDAnmYcDeAxCEC+cBmUEAtnAOQBGAhlGQNwBQQA
                            if (export_clause.clauses.len == 0 && export_clause.had_type_only_exports) {
                                return p.s(S.TypeScript{}, loc);
                            }
                        }

                        if (parsedPath.is_macro) {
                            p.log.addError(p.source, loc, "export from cannot be used with \"type\": \"macro\"")?;
                        } else if (parsedPath.import_tag != .none) {
                            p.log.addError(p.source, loc, "export from cannot be used with \"type\" attribute")?;
                        }

                        const import_record_index = p.addImportRecord(.stmt, parsedPath.loc, parsedPath.text);
                        const path_name = fs.PathName.init(parsedPath.text);
                        const namespace_ref = p.storeNameInRef(
                            std.fmt.allocPrint(
                                p.allocator,
                                "import_{f}",
                                .{
                                    path_name.fmtIdentifier(),
                                },
                            ) catch |e| bun.handleOom(e),
                        ) catch |e| bun.handleOom(e);

                        if (comptime track_symbol_usage_during_parse_pass) {
                            // In the scan pass, we need _some_ way of knowing *not* to mark as unused
                            p.import_records.items[import_record_index].flags.calls_runtime_re_export_fn = true;
                        }
                        p.current_scope.is_after_const_local_prefix = true;
                        p.has_es_module_syntax = true;
                        return p.s(
                            S.ExportFrom{
                                items: export_clause.clauses,
                                is_single_line: export_clause.is_single_line,
                                namespace_ref: namespace_ref,
                                import_record_index: import_record_index,
                            },
                            loc,
                        );
                    }
                    p.lexer.expectOrInsertSemicolon()?;

                    if (comptime is_typescript_enabled) {
                        // export {type Foo};
                        // ->
                        // nothing
                        // https://www.typescriptlang.org/play?useDefineForClassFields=true&esModuleInterop=false&declaration=false&target=99&isolatedModules=false&ts=4.5.4#code/KYDwDg9gTgLgBDAnmYcDeAxCEC+cBmUEAtnAOQBGAhlGQNwBQQA
                        if (export_clause.clauses.len == 0 && export_clause.had_type_only_exports) {
                            return p.s(S.TypeScript{}, loc);
                        }
                    }
                    p.has_es_module_syntax = true;
                    return p.s(S.ExportClause{
                        items: export_clause.clauses,
                        is_single_line: export_clause.is_single_line,
                    }, loc);
                },
                T.t_equals => {
                    // "export = value;"

                    p.esm_export_keyword = previous_export_keyword; // This wasn't an ESM export statement after all
                    if (is_typescript_enabled) {
                        p.lexer.next()?;
                        const value = p.parseExpr(.lowest)?;
                        p.lexer.expectOrInsertSemicolon()?;
                        return p.s(S.ExportEquals{ value: value }, loc);
                    }
                    p.lexer.unexpected()?;
                    return error.SyntaxError;
                },
                else => {
                    p.lexer.unexpected()?;
                    return error.SyntaxError;
                },
            }
        }

        static fn t_function(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            return p.parseFnStmt(loc, opts, null)?;
        }
        static fn t_enum(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            if (!is_typescript_enabled) {
                p.lexer.unexpected()?;
                return error.SyntaxError;
            }
            return p.parseTypescriptEnumStmt(loc, opts);
        }
        static fn t_at(p: *P, opts: *ParseStatementOptions): anyerror!Stmt {
            // Parse decorators before class statements, which are potentially exported
            if (is_typescript_enabled || p.options.features.standard_decorators) {
                const scope_index = p.scopes_in_order.items.len;
                const ts_decorators = p.parseTypeScriptDecorators()?;

                // If this turns out to be a "declare class" statement, we need to undo the
                // scopes that were potentially pushed while parsing the decorator arguments.
                // That can look like any one of the following:
                //
                //   "@decorator declare class Foo {}"
                //   "@decorator declare abstract class Foo {}"
                //   "@decorator export declare class Foo {}"
                //   "@decorator export declare abstract class Foo {}"
                //
                opts.ts_decorators = DeferredTsDecorators{
                    values: ts_decorators,
                    scope_index: scope_index,
                };

                // "@decorator class Foo {}"
                // "@decorator abstract class Foo {}"
                // "@decorator declare class Foo {}"
                // "@decorator declare abstract class Foo {}"
                // "@decorator export class Foo {}"
                // "@decorator export abstract class Foo {}"
                // "@decorator export declare class Foo {}"
                // "@decorator export declare abstract class Foo {}"
                // "@decorator export default class Foo {}"
                // "@decorator export default abstract class Foo {}"
                if (p.lexer.token != .t_class && p.lexer.token != .t_export &&
                    !(is_typescript_enabled && p.lexer.isContextualKeyword("abstract")) &&
                    !(is_typescript_enabled && p.lexer.isContextualKeyword("declare")))
                {
                    p.lexer.expected(.t_class)?;
                }

                return p.parseStmt(opts);
            }
            // notimpl();

            p.lexer.unexpected()?;
            return error.SyntaxError;
        }
        static fn t_class(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            if (opts.lexical_decl != .allow_all) {
                p.forbidLexicalDecl(loc)?;
            }

            return p.parseClassStmt(loc, opts)?;
        }
        static fn t_var(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            let decls = p.parseAndDeclareDecls(.hoisted, opts)?;
            p.lexer.expectOrInsertSemicolon()?;
            return p.s(S.Local{
                kind: .k_var,
                decls: Decl.List.moveFromList(&decls),
                is_export: opts.is_export,
            }, loc);
        }
        static fn t_const(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            if (opts.lexical_decl != .allow_all) {
                p.forbidLexicalDecl(loc)?;
            }
            // p.markSyntaxFeature(compat.Const, p.lexer.Range())

            p.lexer.next()?;

            if (is_typescript_enabled && p.lexer.token == T.t_enum) {
                return p.parseTypescriptEnumStmt(loc, opts);
            }

            let decls = p.parseAndDeclareDecls(.constant, opts)?;
            p.lexer.expectOrInsertSemicolon()?;

            if (!opts.is_typescript_declare) {
                p.requireInitializers(.k_const, decls.items)?;
            }

            return p.s(S.Local{
                kind: .k_const,
                decls: Decl.List.moveFromList(&decls),
                is_export: opts.is_export,
            }, loc);
        }
        static fn t_if(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            let current_loc = loc;
            let root_if: Stmt? = null;
            let current_if: (*S.If)? = null;

            while (true) {
                p.lexer.next()?;
                p.lexer.expect(.t_open_paren)?;
                const test_ = p.parseExpr(.lowest)?;
                p.lexer.expect(.t_close_paren)?;
                let stmtOpts = ParseStatementOptions{
                    lexical_decl: .allow_fn_inside_if,
                };
                const yes = p.parseStmt(&stmtOpts)?;

                // Create the if node
                const if_stmt = p.s(S.If{
                    test_: test_,
                    yes: yes,
                    no: null,
                }, current_loc);

                // First if statement becomes root
                if (root_if == null) {
                    root_if = if_stmt;
                }

                // Link to previous if statement's else branch
                if (prev_if := current_if) {
                    prev_if.no = if_stmt;
                }

                // Set current if for next iteration
                current_if = if_stmt.data.s_if;

                if (p.lexer.token != .t_else) {
                    return root_if.?;
                }

                p.lexer.next()?;

                // Handle final else
                if (p.lexer.token != .t_if) {
                    stmtOpts = ParseStatementOptions{
                        lexical_decl: .allow_fn_inside_if,
                    };
                    current_if.?.no = p.parseStmt(&stmtOpts)?;
                    return root_if.?;
                }

                // Continue with else if
                current_loc = p.lexer.loc();
            }

            unreachable;
        }
        static fn t_do(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            let stmtOpts = ParseStatementOptions{};
            const body = p.parseStmt(&stmtOpts)?;
            p.lexer.expect(.t_while)?;
            p.lexer.expect(.t_open_paren)?;
            const test_ = p.parseExpr(.lowest)?;
            p.lexer.expect(.t_close_paren)?;

            // This is a weird corner case where automatic semicolon insertion applies
            // even without a newline present
            if (p.lexer.token == .t_semicolon) {
                p.lexer.next()?;
            }
            return p.s(S.DoWhile{ body: body, test_: test_ }, loc);
        }
        static fn t_while(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;

            p.lexer.expect(.t_open_paren)?;
            const test_ = p.parseExpr(.lowest)?;
            p.lexer.expect(.t_close_paren)?;

            let stmtOpts = ParseStatementOptions{};
            const body = p.parseStmt(&stmtOpts)?;

            return p.s(S.While{
                body: body,
                test_: test_,
            }, loc);
        }
        static fn t_with(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            p.lexer.expect(.t_open_paren)?;
            const test_ = p.parseExpr(.lowest)?;
            const body_loc = p.lexer.loc();
            p.lexer.expect(.t_close_paren)?;

            // Push a scope so we make sure to prevent any bare identifiers referenced
            // within the body from being renamed. Renaming them might change the
            // semantics of the code.
            * = p.pushScopeForParsePass(.with, body_loc)?;
            let stmtOpts = ParseStatementOptions{};
            const body = p.parseStmt(&stmtOpts)?;
            p.popScope();

            return p.s(S.With{ body: body, body_loc: body_loc, value: test_ }, loc);
        }
        static fn t_switch(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;

            p.lexer.expect(.t_open_paren)?;
            const test_ = p.parseExpr(.lowest)?;
            p.lexer.expect(.t_close_paren)?;

            const body_loc = p.lexer.loc();
            * = p.pushScopeForParsePass(.block, body_loc)?;
            defer p.popScope();

            p.lexer.expect(.t_open_brace)?;
            let cases = ListManaged(js_ast.Case).init(p.allocator);
            let foundDefault = false;
            let stmtOpts = ParseStatementOptions{ lexical_decl: .allow_all };
            let value: js_ast.Expr? = null;
            while (p.lexer.token != .t_close_brace) {
                let body = StmtList.init(p.allocator);
                value = null;
                if (p.lexer.token == .t_default) {
                    if (foundDefault) {
                        p.log.addRangeError(p.source, p.lexer.range(), "Multiple default clauses are not allowed")?;
                        return error.SyntaxError;
                    }

                    foundDefault = true;
                    p.lexer.next()?;
                    p.lexer.expect(.t_colon)?;
                } else {
                    p.lexer.expect(.t_case)?;
                    value = p.parseExpr(.lowest)?;
                    p.lexer.expect(.t_colon)?;
                }

                caseBody: while (true) {
                    switch (p.lexer.token) {
                        .t_close_brace, .t_case, .t_default => {
                            break :caseBody;
                        },
                        else => {
                            stmtOpts = ParseStatementOptions{ lexical_decl: .allow_all };
                            body.append(p.parseStmt(&stmtOpts)?)?;
                        },
                    }
                }
                cases.append(js_ast.Case{ value: value, body: body.items, loc: logger.Loc.Empty })?;
            }
            p.lexer.expect(.t_close_brace)?;
            return p.s(S.Switch{ test_: test_, body_loc: body_loc, cases: cases.items }, loc);
        }
        static fn t_try(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            const body_loc = p.lexer.loc();
            p.lexer.expect(.t_open_brace)?;
            * = p.pushScopeForParsePass(.block, loc)?;
            let stmt_opts = ParseStatementOptions{};
            const body = p.parseStmtsUpTo(.t_close_brace, &stmt_opts)?;
            p.popScope();
            p.lexer.next()?;

            let catch_: js_ast.Catch? = null;
            let finally: js_ast.Finally? = null;

            if (p.lexer.token == .t_catch) {
                const catch_loc = p.lexer.loc();
                * = p.pushScopeForParsePass(.catch_binding, catch_loc)?;
                p.lexer.next()?;
                let binding: js_ast.Binding? = null;

                // The catch binding is optional, and can be omitted
                if (p.lexer.token != .t_open_brace) {
                    p.lexer.expect(.t_open_paren)?;
                    let value = p.parseBinding({})?;

                    // Skip over types
                    if (is_typescript_enabled && p.lexer.token == .t_colon) {
                        p.lexer.expect(.t_colon)?;
                        p.skipTypeScriptType(.lowest)?;
                    }

                    p.lexer.expect(.t_close_paren)?;

                    // Bare identifiers are a special case
                    let kind = Symbol.Kind.other;
                    switch (value.data) {
                        .b_identifier => {
                            kind = .catch_identifier;
                        },
                        else => {},
                    }
                    p.declareBinding(kind, &value, &stmt_opts)?;
                    binding = value;
                }

                const catch_body_loc = p.lexer.loc();
                p.lexer.expect(.t_open_brace)?;

                * = p.pushScopeForParsePass(.block, catch_body_loc)?;
                const stmts = p.parseStmtsUpTo(.t_close_brace, &stmt_opts)?;
                p.popScope();
                p.lexer.next()?;
                catch_ = js_ast.Catch{
                    loc: catch_loc,
                    binding: binding,
                    body: stmts,
                    body_loc: catch_body_loc,
                };
                p.popScope();
            }

            if (p.lexer.token == .t_finally || catch_ == null) {
                const finally_loc = p.lexer.loc();
                * = p.pushScopeForParsePass(.block, finally_loc)?;
                p.lexer.expect(.t_finally)?;
                p.lexer.expect(.t_open_brace)?;
                const stmts = p.parseStmtsUpTo(.t_close_brace, &stmt_opts)?;
                p.lexer.next()?;
                finally = js_ast.Finally{ loc: finally_loc, stmts: stmts };
                p.popScope();
            }

            return p.s(
                S.Try{ body_loc: body_loc, body: body, catch_: catch_, finally: finally },
                loc,
            );
        }
        static fn t_for(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            * = p.pushScopeForParsePass(.block, loc)?;
            defer p.popScope();

            p.lexer.next()?;

            // "for await (let x of y) {}"
            let isForAwait = p.lexer.isContextualKeyword("await");
            if (isForAwait) {
                const await_range = p.lexer.range();
                if (p.fn_or_arrow_data_parse.allow_await != .allow_expr) {
                    p.log.addRangeError(p.source, await_range, "Cannot use \"await\" outside an async function")?;
                    isForAwait = false;
                } else {
                    // TODO: improve error handling here
                    //                 didGenerateError := p.markSyntaxFeature(compat.ForAwait, awaitRange)
                    if (p.fn_or_arrow_data_parse.is_top_level) {
                        p.top_level_await_keyword = await_range;
                        // p.markSyntaxFeature(compat.TopLevelAwait, awaitRange)
                    }
                }
                p.lexer.next()?;
            }

            p.lexer.expect(.t_open_paren)?;

            let init_: Stmt? = null;
            let test_: Expr? = null;
            let update: Expr? = null;

            // "in" expressions aren't allowed here
            p.allow_in = false;

            let bad_let_range: logger.Range? = null;
            if (p.lexer.isContextualKeyword("let")) {
                bad_let_range = p.lexer.range();
            }

            let decls: G.Decl.List = {};
            const init_loc = p.lexer.loc();
            let is_var = false;
            switch (p.lexer.token) {
                // for (var )
                .t_var => {
                    is_var = true;
                    p.lexer.next()?;
                    let stmtOpts = ParseStatementOptions{};
                    let decls_list = p.parseAndDeclareDecls(.hoisted, &stmtOpts)?;
                    decls = .moveFromList(&decls_list);
                    init_ = p.s(S.Local{ kind: .k_var, decls: decls }, init_loc);
                },
                // for (const )
                .t_const => {
                    p.lexer.next()?;
                    let stmtOpts = ParseStatementOptions{};
                    let decls_list = p.parseAndDeclareDecls(.constant, &stmtOpts)?;
                    decls = .moveFromList(&decls_list);
                    init_ = p.s(S.Local{ kind: .k_const, decls: decls }, init_loc);
                },
                // for (;)
                .t_semicolon => {},
                else => {
                    let stmtOpts = ParseStatementOptions{
                        lexical_decl: .allow_all,
                        is_for_loop_init: true,
                    };

                    const res = p.parseExprOrLetStmt(&stmtOpts)?;
                    switch (res.stmt_or_expr) {
                        .stmt => |stmt| {
                            bad_let_range = null;
                            init_ = stmt;
                        },
                        .expr => |expr| {
                            init_ = p.s(S.SExpr{
                                value: expr,
                            }, init_loc);
                        },
                    }
                },
            }

            // "in" expressions are allowed again
            p.allow_in = true;

            // Detect for-of loops
            if (p.lexer.isContextualKeyword("of") || isForAwait) {
                if (r := bad_let_range) {
                    p.log.addRangeError(p.source, r, "\"let\" must be wrapped in parentheses to be used as an expression here")?;
                    return error.SyntaxError;
                }

                if (isForAwait && !p.lexer.isContextualKeyword("of")) {
                    if (init_ != null) {
                        p.lexer.expectedString("\"of\"")?;
                    } else {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }
                }

                p.forbidInitializers(decls.slice(), "of", false)?;
                p.lexer.next()?;
                const value = p.parseExpr(.comma)?;
                p.lexer.expect(.t_close_paren)?;
                let stmtOpts = ParseStatementOptions{};
                const body = p.parseStmt(&stmtOpts)?;
                return p.s(S.ForOf{ is_await: isForAwait, init: init_ ?? unreachable, value: value, body: body }, loc);
            }

            // Detect for-in loops
            if (p.lexer.token == .t_in) {
                p.forbidInitializers(decls.slice(), "in", is_var)?;
                p.lexer.next()?;
                const value = p.parseExpr(.lowest)?;
                p.lexer.expect(.t_close_paren)?;
                let stmtOpts = ParseStatementOptions{};
                const body = p.parseStmt(&stmtOpts)?;
                return p.s(S.ForIn{ init: init_ ?? unreachable, value: value, body: body }, loc);
            }

            // Only require "const" statement initializers when we know we're a normal for loop
            if (init_stmt := init_) {
                switch (init_stmt.data) {
                    .s_local => {
                        if (init_stmt.data.s_local.kind == .k_const) {
                            p.requireInitializers(.k_const, decls.slice())?;
                        }
                    },
                    else => {},
                }
            }

            p.lexer.expect(.t_semicolon)?;
            if (p.lexer.token != .t_semicolon) {
                test_ = p.parseExpr(.lowest)?;
            }

            p.lexer.expect(.t_semicolon)?;

            if (p.lexer.token != .t_close_paren) {
                update = p.parseExpr(.lowest)?;
            }

            p.lexer.expect(.t_close_paren)?;
            let stmtOpts = ParseStatementOptions{};
            const body = p.parseStmt(&stmtOpts)?;
            return p.s(
                S.For{ init: init_, test_: test_, update: update, body: body },
                loc,
            );
        }
        static fn t_import(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            const previous_import_keyword = p.esm_import_keyword;
            p.esm_import_keyword = p.lexer.range();
            p.lexer.next()?;
            let stmt: S.Import = S.Import{
                namespace_ref: Ref.None,
                import_record_index: std.math.maxInt(u32),
            };
            let was_originally_bare_import = false;

            // "export import foo = bar"
            if ((opts.is_export || (opts.is_namespace_scope && !opts.is_typescript_declare)) && p.lexer.token != .t_identifier) {
                p.lexer.expected(.t_identifier)?;
            }

            switch (p.lexer.token) {
                // "import('path')"
                // "import.meta"
                .t_open_paren, .t_dot => {
                    p.esm_import_keyword = previous_import_keyword; // this wasn't an esm import statement after all
                    let expr = p.parseImportExpr(loc, .lowest)?;
                    p.parseSuffix(&expr, .lowest, null, Expr.EFlags.none)?;
                    p.lexer.expectOrInsertSemicolon()?;
                    return p.s(S.SExpr{
                        value: expr,
                    }, loc);
                },
                .t_string_literal, .t_no_substitution_template_literal => {
                    // "import 'path'"
                    if (!opts.is_module_scope && (!opts.is_namespace_scope || !opts.is_typescript_declare)) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }
                    was_originally_bare_import = true;
                },
                .t_asterisk => {
                    // "import * as ns from 'path'"
                    if (!opts.is_module_scope && (!opts.is_namespace_scope || !opts.is_typescript_declare)) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }

                    p.lexer.next()?;
                    p.lexer.expectContextualKeyword("as")?;
                    stmt = S.Import{
                        namespace_ref: p.storeNameInRef(p.lexer.identifier)?,
                        star_name_loc: p.lexer.loc(),
                        import_record_index: std.math.maxInt(u32),
                    };
                    p.lexer.expect(.t_identifier)?;
                    p.lexer.expectContextualKeyword("from")?;
                },
                .t_open_brace => {
                    // "import {item1, item2} from 'path'"
                    if (!opts.is_module_scope && (!opts.is_namespace_scope || !opts.is_typescript_declare)) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }
                    const importClause = p.parseImportClause()?;
                    if (comptime is_typescript_enabled) {
                        if (importClause.had_type_only_imports && importClause.items.len == 0) {
                            p.lexer.expectContextualKeyword("from")?;
                            * = p.parsePath()?;
                            p.lexer.expectOrInsertSemicolon()?;
                            return p.s(S.TypeScript{}, loc);
                        }
                    }

                    stmt = S.Import{
                        namespace_ref: Ref.None,
                        import_record_index: std.math.maxInt(u32),
                        items: importClause.items,
                        is_single_line: importClause.is_single_line,
                    };
                    p.lexer.expectContextualKeyword("from")?;
                },
                .t_identifier => {
                    // "import defaultItem from 'path'"
                    // "import foo = bar"
                    if (!opts.is_module_scope && (!opts.is_namespace_scope)) {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    }

                    let default_name = p.lexer.identifier;
                    stmt = S.Import{ namespace_ref: Ref.None, import_record_index: std.math.maxInt(u32), default_name: LocRef{
                        loc: p.lexer.loc(),
                        @"ref": p.storeNameInRef(default_name)?,
                    } };
                    p.lexer.next()?;

                    if (comptime is_typescript_enabled) {
                        // Skip over type-only imports
                        if (strings.eqlComptime(default_name, "type")) {
                            switch (p.lexer.token) {
                                .t_identifier => {
                                    if (!strings.eqlComptime(p.lexer.identifier, "from")) {
                                        default_name = p.lexer.identifier;
                                        stmt.default_name.?.loc = p.lexer.loc();
                                        p.lexer.next()?;

                                        if (p.lexer.token == .t_equals) {
                                            // "import type foo = require('bar');"
                                            // "import type foo = bar.baz;"
                                            opts.is_typescript_declare = true;
                                            return p.parseTypeScriptImportEqualsStmt(loc, opts, stmt.default_name.?.loc, default_name)?;
                                        } else {
                                            // "import type foo from 'bar';"
                                            p.lexer.expectContextualKeyword("from")?;
                                            * = p.parsePath()?;
                                            p.lexer.expectOrInsertSemicolon()?;
                                            return p.s(S.TypeScript{}, loc);
                                        }
                                    }
                                },
                                .t_asterisk => {
                                    // "import type * as foo from 'bar';"
                                    p.lexer.next()?;
                                    p.lexer.expectContextualKeyword("as")?;
                                    p.lexer.expect(.t_identifier)?;
                                    p.lexer.expectContextualKeyword("from")?;
                                    * = p.parsePath()?;
                                    p.lexer.expectOrInsertSemicolon()?;
                                    return p.s(S.TypeScript{}, loc);
                                },

                                .t_open_brace => {
                                    // "import type {foo} from 'bar';"
                                    * = p.parseImportClause()?;
                                    p.lexer.expectContextualKeyword("from")?;
                                    * = p.parsePath()?;
                                    p.lexer.expectOrInsertSemicolon()?;
                                    return p.s(S.TypeScript{}, loc);
                                },
                                else => {},
                            }
                        }

                        // Parse TypeScript import assignment statements
                        if (p.lexer.token == .t_equals || opts.is_export || (opts.is_namespace_scope && !opts.is_typescript_declare)) {
                            p.esm_import_keyword = previous_import_keyword; // This wasn't an ESM import statement after all;
                            return p.parseTypeScriptImportEqualsStmt(loc, opts, logger.Loc.Empty, default_name);
                        }
                    }

                    if (p.lexer.token == .t_comma) {
                        p.lexer.next()?;

                        switch (p.lexer.token) {
                            // "import defaultItem, * as ns from 'path'"
                            .t_asterisk => {
                                p.lexer.next()?;
                                p.lexer.expectContextualKeyword("as")?;
                                stmt.namespace_ref = p.storeNameInRef(p.lexer.identifier)?;
                                stmt.star_name_loc = p.lexer.loc();
                                p.lexer.expect(.t_identifier)?;
                            },
                            // "import defaultItem, {item1, item2} from 'path'"
                            .t_open_brace => {
                                const importClause = p.parseImportClause()?;

                                stmt.items = importClause.items;
                                stmt.is_single_line = importClause.is_single_line;
                            },
                            else => {
                                p.lexer.unexpected()?;
                                return error.SyntaxError;
                            },
                        }
                    }

                    p.lexer.expectContextualKeyword("from")?;
                },
                else => {
                    p.lexer.unexpected()?;
                    return error.SyntaxError;
                },
            }

            const path = p.parsePath()?;
            p.lexer.expectOrInsertSemicolon()?;

            return p.processImportStatement(stmt, path, loc, was_originally_bare_import)?;
        }
        static fn t_break(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            const name = p.parseLabelName()?;
            p.lexer.expectOrInsertSemicolon()?;
            return p.s(S.Break{ label: name }, loc);
        }
        static fn t_continue(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            const name = p.parseLabelName()?;
            p.lexer.expectOrInsertSemicolon()?;
            return p.s(S.Continue{ label: name }, loc);
        }
        static fn t_return(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            if (p.fn_or_arrow_data_parse.is_return_disallowed) {
                p.log.addRangeError(p.source, p.lexer.range(), "A return statement cannot be used here")?;
            }
            p.lexer.next()?;
            let value: Expr? = null;
            if ((p.lexer.token != .t_semicolon &&
                !p.lexer.has_newline_before &&
                p.lexer.token != .t_close_brace &&
                p.lexer.token != .t_end_of_file))
            {
                value = p.parseExpr(.lowest)?;
            }
            p.latest_return_had_semicolon = p.lexer.token == .t_semicolon;
            p.lexer.expectOrInsertSemicolon()?;

            return p.s(S.Return{ value: value }, loc);
        }
        static fn t_throw(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            if (p.lexer.has_newline_before) {
                p.log.addError(p.source, logger.Loc{
                    start: loc.start + 5,
                }, "Unexpected newline after \"throw\"")?;
                return error.SyntaxError;
            }
            const expr = p.parseExpr(.lowest)?;
            p.lexer.expectOrInsertSemicolon()?;
            return p.s(S.Throw{ value: expr }, loc);
        }
        static fn t_debugger(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            p.lexer.next()?;
            p.lexer.expectOrInsertSemicolon()?;
            return p.s(S.Debugger{}, loc);
        }
        static fn t_open_brace(p: *P, _opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            * = p.pushScopeForParsePass(.block, loc)?;
            defer p.popScope();
            p.lexer.next()?;
            let stmtOpts = ParseStatementOptions{};
            const stmts = p.parseStmtsUpTo(.t_close_brace, &stmtOpts)?;
            const close_brace_loc = p.lexer.loc();
            p.lexer.next()?;
            return p.s(S.Block{
                stmts: stmts,
                close_brace_loc: close_brace_loc,
            }, loc);
        }

        static fn parseStmtFallthrough(p: *P, opts: *ParseStatementOptions, loc: logger.Loc): anyerror!Stmt {
            const is_identifier = p.lexer.token == .t_identifier;
            const name = p.lexer.identifier;
            // Parse either an async function, an async expression, or a normal expression
            let expr: Expr = Expr{ loc: loc, data: Expr.Data{ e_missing: {} } };
            if (is_identifier && strings.eqlComptime(p.lexer.raw(), "async")) {
                const async_range = p.lexer.range();
                p.lexer.next()?;
                if (p.lexer.token == .t_function && !p.lexer.has_newline_before) {
                    p.lexer.next()?;

                    return p.parseFnStmt(async_range.loc, opts, async_range)?;
                }

                expr = p.parseAsyncPrefixExpr(async_range, .lowest)?;
                p.parseSuffix(&expr, .lowest, null, Expr.EFlags.none)?;
            } else {
                const exprOrLet = p.parseExprOrLetStmt(opts)?;
                switch (exprOrLet.stmt_or_expr) {
                    .stmt => |stmt| {
                        p.lexer.expectOrInsertSemicolon()?;
                        return stmt;
                    },
                    .expr => |_expr| {
                        expr = _expr;
                    },
                }
            }
            if (is_identifier) {
                switch (expr.data) {
                    .e_identifier => |ident| {
                        if (p.lexer.token == .t_colon && !opts.hasDecorators()) {
                            * = p.pushScopeForParsePass(.label, loc)?;
                            defer p.popScope();

                            // Parse a labeled statement
                            p.lexer.next()?;

                            const _name = LocRef{ loc: expr.loc, @"ref": ident.ref };
                            let nestedOpts = ParseStatementOptions{};

                            switch (opts.lexical_decl) {
                                .allow_all, .allow_fn_inside_label => {
                                    nestedOpts.lexical_decl = .allow_fn_inside_label;
                                },
                                else => {},
                            }
                            const stmt = p.parseStmt(&nestedOpts)?;
                            return p.s(S.Label{ name: _name, stmt: stmt }, loc);
                        }
                    },
                    else => {},
                }

                if (is_typescript_enabled) {
                    if (ts_stmt := js_lexer.TypescriptStmtKeyword.List.get(name)) {
                        switch (ts_stmt) {
                            .ts_stmt_type => {
                                if (p.lexer.token == .t_identifier && !p.lexer.has_newline_before) {
                                    // "type Foo = any"
                                    let stmtOpts = ParseStatementOptions{ is_module_scope: opts.is_module_scope };
                                    p.skipTypeScriptTypeStmt(&stmtOpts)?;
                                    return p.s(S.TypeScript{}, loc);
                                }
                            },
                            .ts_stmt_namespace, .ts_stmt_module => {
                                // "namespace Foo {}"
                                // "module Foo {}"
                                // "declare module 'fs' {}"
                                // "declare module 'fs';"
                                if (!p.lexer.has_newline_before &&
                                    (opts.is_module_scope || opts.is_namespace_scope) &&
                                    (p.lexer.token == .t_identifier || (p.lexer.token == .t_string_literal && opts.is_typescript_declare)))
                                {
                                    return p.parseTypeScriptNamespaceStmt(loc, opts);
                                }
                            },
                            .ts_stmt_interface => {
                                // "interface Foo {}"
                                let stmtOpts = ParseStatementOptions{ is_module_scope: opts.is_module_scope };

                                p.skipTypeScriptInterfaceStmt(&stmtOpts)?;
                                return p.s(S.TypeScript{}, loc);
                            },
                            .ts_stmt_abstract => {
                                if (p.lexer.token == .t_class || opts.ts_decorators != null) {
                                    return p.parseClassStmt(loc, opts)?;
                                }
                            },
                            .ts_stmt_global => {
                                // "declare module 'fs' { global { namespace NodeJS {} } }"
                                if (opts.is_namespace_scope && opts.is_typescript_declare && p.lexer.token == .t_open_brace) {
                                    p.lexer.next()?;
                                    * = p.parseStmtsUpTo(.t_close_brace, opts)?;
                                    p.lexer.next()?;
                                    return p.s(S.TypeScript{}, loc);
                                }
                            },
                            .ts_stmt_declare => {
                                opts.lexical_decl = .allow_all;
                                opts.is_typescript_declare = true;

                                // "@decorator declare class Foo {}"
                                // "@decorator declare abstract class Foo {}"
                                if (opts.ts_decorators != null && p.lexer.token != .t_class && !p.lexer.isContextualKeyword("abstract")) {
                                    p.lexer.expected(.t_class)?;
                                }

                                // "declare global { ... }"
                                if (p.lexer.isContextualKeyword("global")) {
                                    p.lexer.next()?;
                                    p.lexer.expect(.t_open_brace)?;
                                    * = p.parseStmtsUpTo(.t_close_brace, opts)?;
                                    p.lexer.next()?;
                                    return p.s(S.TypeScript{}, loc);
                                }

                                // "declare const x: any"
                                const stmt = p.parseStmt(opts)?;
                                if (decs := opts.ts_decorators) {
                                    p.discardScopesUpTo(decs.scope_index);
                                }

                                // Unlike almost all uses of "declare", statements that use
                                // "export declare" with "var/let/const" inside a namespace affect
                                // code generation. They cause any declared bindings to be
                                // considered exports of the namespace. Identifier references to
                                // those names must be converted into property accesses off the
                                // namespace object:
                                //
                                //   namespace ns {
                                //     export declare const x
                                //     export function y() { return x }
                                //   }
                                //
                                //   (ns as any).x = 1
                                //   console.log(ns.y())
                                //
                                // In this example, "return x" must be replaced with "return ns.x".
                                // This is handled by replacing each "export declare" statement
                                // inside a namespace with an "export var" statement containing all
                                // of the declared bindings. That "export var" statement will later
                                // cause identifiers to be transformed into property accesses.
                                if (opts.is_namespace_scope && opts.is_export) {
                                    let decls: G.Decl.List = {};
                                    switch (stmt.data) {
                                        .s_local => |local| {
                                            let _decls = ListManaged(G.Decl).initCapacity(p.allocator, local.decls.len)?;
                                            for (decl of local.decls.slice()) {
                                                extractDeclsForBinding(decl.binding, &_decls)?;
                                            }
                                            decls = .moveFromList(&_decls);
                                        },
                                        else => {},
                                    }

                                    if (decls.len > 0) {
                                        return p.s(S.Local{
                                            kind: .k_var,
                                            is_export: true,
                                            decls: decls,
                                        }, loc);
                                    }
                                }

                                return p.s(S.TypeScript{}, loc);
                            },
                        }
                    }
                }
            }
            // Output.print("\n\nmVALUE {s}:{s}\n", .{ expr, name });
            p.lexer.expectOrInsertSemicolon()?;
            return p.s(S.SExpr{ value: expr }, loc);
        }

        static fn parseStmt(p: *P, opts: *ParseStatementOptions): anyerror!Stmt {
            if (!p.stack_check.isSafeToRecurse()) {
                bun.throwStackOverflow()?;
            }

            return switch (p.lexer.token) {
                .t_semicolon => t_semicolon(p),
                .t_at => t_at(p, opts),

                inline .t_export,
                .t_function,
                .t_enum,
                .t_class,
                .t_var,
                .t_const,
                .t_if,
                .t_do,
                .t_while,
                .t_with,
                .t_switch,
                .t_try,
                .t_for,
                .t_import,
                .t_break,
                .t_continue,
                .t_return,
                .t_throw,
                .t_debugger,
                .t_open_brace => |function| @field(@This(), @tagName(function))(p, opts, p.lexer.loc()),

                else => parseStmtFallthrough(p, opts, p.lexer.loc()),
            };
        }
    };
}

import bun from "bun";
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const Binding = js_ast.Binding;
const Expr = js_ast.Expr;
const LocRef = js_ast.LocRef;
const S = js_ast.S;
const Stmt = js_ast.Stmt;
const Symbol = js_ast.Symbol;

const G = js_ast.G;
const Decl = G.Decl;

const Op = js_ast.Op;
const Level = js_ast.Op.Level;

const js_lexer = bun.js_lexer;
const T = js_lexer.T;

const js_parser = bun.js_parser;
const DeferredTsDecorators = js_parser.DeferredTsDecorators;
const ImportKind = js_parser.ImportKind;
const JSXTransformType = js_parser.JSXTransformType;
const ParseStatementOptions = js_parser.ParseStatementOptions;
const ParsedPath = js_parser.ParsedPath;
const Ref = js_parser.Ref;
const StmtList = js_parser.StmtList;
const TypeScript = js_parser.TypeScript;
const fs = js_parser.fs;

const List = std.ArrayListUnmanaged;
const ListManaged = std.array_list.Managed;
