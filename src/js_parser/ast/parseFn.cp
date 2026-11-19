pub fn ParseFn(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        const is_typescript_enabled = P.is_typescript_enabled;

        /// This assumes the "function" token has already been parsed
        static fn parseFnStmt(p: *P, loc: logger.Loc, opts: *ParseStatementOptions, asyncRange: logger.Range?): anyerror!Stmt {
            const is_generator = p.lexer.token == T.t_asterisk;
            const is_async = asyncRange != null;

            if (is_generator) {
                // p.markSyntaxFeature(compat.Generator, p.lexer.Range())
                p.lexer.next()?;
            } else if (is_async) {
                // p.markLoweredSyntaxFeature(compat.AsyncAwait, asyncRange, compat.Generator)
            }

            switch (opts.lexical_decl) {
                .forbid => {
                    p.forbidLexicalDecl(loc)?;
                },

                // Allow certain function statements in certain single-statement contexts
                .allow_fn_inside_if, .allow_fn_inside_label => {
                    if (opts.is_typescript_declare || is_generator || is_async) {
                        p.forbidLexicalDecl(loc)?;
                    }
                },
                else => {},
            }

            let name: js_ast.LocRef? = null;
            let nameText: string = "";

            // The name is optional for "export default function() {}" pseudo-statements
            if (!opts.is_name_optional || p.lexer.token == T.t_identifier) {
                const nameLoc = p.lexer.loc();
                nameText = p.lexer.identifier;
                p.lexer.expect(T.t_identifier)?;
                // Difference
                const sym_ref = p.newSymbol(Symbol.Kind.other, nameText)?;
                name = js_ast.LocRef{
                    loc: nameLoc,
                    @"ref": sym_ref,
                };
            }

            // Even anonymous functions can have TypeScript type parameters
            if (is_typescript_enabled) {
                * = p.skipTypeScriptTypeParameters({ allow_const_modifier: true })?;
            }

            // Introduce a fake block scope for function declarations inside if statements
            let ifStmtScopeIndex: usize = 0;
            const hasIfScope = opts.lexical_decl == .allow_fn_inside_if;
            if (hasIfScope) {
                ifStmtScopeIndex = p.pushScopeForParsePass(js_ast.Scope.Kind.block, loc)?;
            }

            const scopeIndex: usize = p.pushScopeForParsePass(js_ast.Scope.Kind.function_args, p.lexer.loc())?;

            let func = p.parseFn(name, FnOrArrowDataParse{
                needs_async_loc: loc,
                async_range: asyncRange ?? logger.Range.None,
                has_async_range: asyncRange != null,
                allow_await: if (is_async) AwaitOrYield.allow_expr else AwaitOrYield.allow_ident,
                allow_yield: if (is_generator) AwaitOrYield.allow_expr else AwaitOrYield.allow_ident,
                is_typescript_declare: opts.is_typescript_declare,

                // Only allow omitting the body if we're parsing TypeScript
                allow_missing_body_for_type_script: is_typescript_enabled,
            });
            p.fn_or_arrow_data_parse.has_argument_decorators = false;

            if (comptime is_typescript_enabled) {
                // Don't output anything if it's just a forward declaration of a function
                if ((opts.is_typescript_declare || func.flags.contains(.is_forward_declaration))) {
                    p.popAndDiscardScope(scopeIndex);

                    // Balance the fake block scope introduced above
                    if (hasIfScope) {
                        p.popScope();
                    }

                    if (opts.is_typescript_declare && opts.is_namespace_scope && opts.is_export) {
                        p.has_non_local_export_declare_inside_namespace = true;
                    }

                    return p.s(S.TypeScript{}, loc);
                }
            }

            p.popScope();

            // Only declare the function after we know if it had a body or not. Otherwise
            // TypeScript code such as this will double-declare the symbol:
            //
            //     function foo(): void;
            //     function foo(): void {}
            //
            if (name_val := name) {
                const kind = if (is_generator || is_async)
                    Symbol.Kind.generator_or_async_function
                else
                    Symbol.Kind.hoisted_function;

                name = js_ast.LocRef{ loc: name_val.loc, @"ref": p.declareSymbol(kind, name_val.loc, nameText)? };
                func.name = name;
            }

            func.flags.setPresent(.has_if_scope, hasIfScope);
            func.flags.setPresent(.is_export, opts.is_export);

            // Balance the fake block scope introduced above
            if (hasIfScope) {
                p.popScope();
            }

            return p.s(
                S.Function{
                    func: func,
                },
                loc,
            );
        }

        static fn parseFn(p: *P, name: js_ast.LocRef?, opts: FnOrArrowDataParse): anyerror!G.Fn {
            // if data.allowAwait and data.allowYield {
            //     p.markSyntaxFeature(compat.AsyncGenerator, data.asyncRange)
            // }

            let func = G.Fn{
                name: name,

                flags: Flags.Function.init({
                    has_rest_arg: false,
                    is_async: opts.allow_await == .allow_expr,
                    is_generator: opts.allow_yield == .allow_expr,
                }),

                arguments_ref: null,
                open_parens_loc: p.lexer.loc(),
            };
            p.lexer.expect(T.t_open_paren)?;

            // Await and yield are not allowed in function arguments
            let old_fn_or_arrow_data = std.mem.toBytes(p.fn_or_arrow_data_parse);

            p.fn_or_arrow_data_parse.allow_await = if (opts.allow_await == .allow_expr)
                AwaitOrYield.forbid_all
            else
                AwaitOrYield.allow_ident;

            p.fn_or_arrow_data_parse.allow_yield = if (opts.allow_yield == .allow_expr)
                AwaitOrYield.forbid_all
            else
                AwaitOrYield.allow_ident;

            // Don't suggest inserting "async" before anything if "await" is found
            p.fn_or_arrow_data_parse.needs_async_loc = logger.Loc.Empty;

            // If "super()" is allowed in the body, it's allowed in the arguments
            p.fn_or_arrow_data_parse.allow_super_call = opts.allow_super_call;
            p.fn_or_arrow_data_parse.allow_super_property = opts.allow_super_property;

            let rest_arg: bool = false;
            let arg_has_decorators: bool = false;
            let args = List(G.Arg){};
            while (p.lexer.token != T.t_close_paren) {
                // Skip over "this" type annotations
                if (is_typescript_enabled && p.lexer.token == T.t_this) {
                    p.lexer.next()?;
                    if (p.lexer.token == T.t_colon) {
                        p.lexer.next()?;
                        p.skipTypeScriptType(.lowest)?;
                    }
                    if (p.lexer.token != T.t_comma) {
                        break;
                    }

                    p.lexer.next()?;
                    continue;
                }

                let ts_decorators: []ExprNodeIndex = [];
                if (opts.allow_ts_decorators) {
                    ts_decorators = p.parseTypeScriptDecorators()?;
                    if (ts_decorators.len > 0) {
                        arg_has_decorators = true;
                    }
                }

                if (!func.flags.contains(.has_rest_arg) && p.lexer.token == T.t_dot_dot_dot) {
                    // p.markSyntaxFeature
                    p.lexer.next()?;
                    rest_arg = true;
                    func.flags.insert(.has_rest_arg);
                }

                let is_typescript_ctor_field = false;
                const is_identifier = p.lexer.token == T.t_identifier;
                let text = p.lexer.identifier;
                let arg = p.parseBinding({})?;
                let ts_metadata = TypeScript.Metadata.default;

                if (comptime is_typescript_enabled) {
                    if (is_identifier && opts.is_constructor) {
                        // Skip over TypeScript accessibility modifiers, which turn this argument
                        // into a class field when used inside a class constructor. This is known
                        // as a "parameter property" in TypeScript.
                        while (true) {
                            switch (p.lexer.token) {
                                .t_identifier, .t_open_brace, .t_open_bracket => {
                                    if (!js_lexer.TypeScriptAccessibilityModifier.has(text)) {
                                        break;
                                    }

                                    is_typescript_ctor_field = true;

                                    // TypeScript requires an identifier binding
                                    if (p.lexer.token != .t_identifier) {
                                        p.lexer.expect(.t_identifier)?;
                                    }
                                    text = p.lexer.identifier;

                                    // Re-parse the binding (the current binding is the TypeScript keyword)
                                    arg = p.parseBinding({})?;
                                },
                                else => {
                                    break;
                                },
                            }
                        }
                    }

                    // "function foo(a?) {}"
                    if (p.lexer.token == .t_question) {
                        p.lexer.next()?;
                    }

                    // "function foo(a: any) {}"
                    if (p.lexer.token == .t_colon) {
                        p.lexer.next()?;
                        if (!rest_arg) {
                            if (p.options.features.emit_decorator_metadata &&
                                opts.allow_ts_decorators &&
                                (opts.has_argument_decorators || opts.has_decorators || arg_has_decorators))
                            {
                                ts_metadata = p.skipTypeScriptTypeWithMetadata(.lowest)?;
                            } else {
                                p.skipTypeScriptType(.lowest)?;
                            }
                        } else {
                            // rest parameter is always object, leave metadata as m_none
                            p.skipTypeScriptType(.lowest)?;
                        }
                    }
                }

                let parseStmtOpts = ParseStatementOptions{};
                p.declareBinding(.hoisted, &arg, &parseStmtOpts) catch unreachable;

                let default_value: ExprNodeIndex? = null;
                if (!func.flags.contains(.has_rest_arg) && p.lexer.token == .t_equals) {
                    // p.markSyntaxFeature
                    p.lexer.next()?;
                    default_value = p.parseExpr(.comma)?;
                }

                args.append(p.allocator, G.Arg{
                    ts_decorators: ExprNodeList.fromOwnedSlice(ts_decorators),
                    binding: arg,
                    default: default_value,

                    // We need to track this because it affects code generation
                    is_typescript_ctor_field: is_typescript_ctor_field,
                    ts_metadata: ts_metadata,
                }) catch unreachable;

                if (p.lexer.token != .t_comma) {
                    break;
                }

                if (func.flags.contains(.has_rest_arg)) {
                    // JavaScript does not allow a comma after a rest argument
                    if (opts.is_typescript_declare) {
                        // TypeScript does allow a comma after a rest argument in a "declare" context
                        p.lexer.next()?;
                    } else {
                        p.lexer.expect(.t_close_paren)?;
                    }

                    break;
                }

                p.lexer.next()?;
                rest_arg = false;
            }
            if (args.items.len > 0) {
                func.args = args.items;
            }

            // Reserve the special name "arguments" in this scope. This ensures that it
            // shadows any variable called "arguments" in any parent scopes. But only do
            // this if it wasn't already declared above because arguments are allowed to
            // be called "arguments", in which case the real "arguments" is inaccessible.
            if (!p.current_scope.members.contains("arguments")) {
                func.arguments_ref = p.declareSymbolMaybeGenerated(.arguments, func.open_parens_loc, arguments_str, false) catch unreachable;
                if (arg_ref := func.arguments_ref) {
                    p.symbols.items[arg_ref.innerIndex()].must_not_be_renamed = true;
                }
            }

            p.lexer.expect(.t_close_paren)?;
            p.fn_or_arrow_data_parse = std.mem.bytesToValue(@TypeOf(p.fn_or_arrow_data_parse), &old_fn_or_arrow_data);

            p.fn_or_arrow_data_parse.has_argument_decorators = arg_has_decorators;

            // "function foo(): any {}"
            if (is_typescript_enabled) {
                if (p.lexer.token == .t_colon) {
                    p.lexer.next()?;

                    if (p.options.features.emit_decorator_metadata && opts.allow_ts_decorators && (opts.has_argument_decorators || opts.has_decorators)) {
                        func.return_ts_metadata = p.skipTypescriptReturnTypeWithMetadata()?;
                    } else {
                        p.skipTypescriptReturnType()?;
                    }
                } else if (p.options.features.emit_decorator_metadata && opts.allow_ts_decorators && (opts.has_argument_decorators || opts.has_decorators)) {
                    if (func.flags.contains(.is_async)) {
                        func.return_ts_metadata = .m_promise;
                    } else {
                        func.return_ts_metadata = .m_undefined;
                    }
                }
            }

            // "function foo(): any;"
            if (opts.allow_missing_body_for_type_script && p.lexer.token != .t_open_brace) {
                p.lexer.expectOrInsertSemicolon()?;
                func.flags.insert(.is_forward_declaration);
                return func;
            }
            let tempOpts = opts;
            func.body = p.parseFnBody(&tempOpts)?;

            return func;
        }

        static fn parseFnExpr(p: *P, loc: logger.Loc, is_async: bool, async_range: logger.Range): Expr! {
            p.lexer.next()?;
            const is_generator = p.lexer.token == T.t_asterisk;
            if (is_generator) {
                // p.markSyntaxFeature()
                p.lexer.next()?;
            } else if (is_async) {
                // p.markLoweredSyntaxFeature(compat.AsyncAwait, asyncRange, compat.Generator)
            }

            let name: js_ast.LocRef? = null;

            * = p.pushScopeForParsePass(.function_args, loc) catch unreachable;

            // The name is optional
            if (p.lexer.token == .t_identifier) {
                const text = p.lexer.identifier;

                // Don't declare the name "arguments" since it's shadowed and inaccessible
                name = js_ast.LocRef{
                    loc: p.lexer.loc(),
                    @"ref": if (text.len > 0 && !strings.eqlComptime(text, "arguments"))
                        p.declareSymbol(.hoisted_function, p.lexer.loc(), text)?
                    else
                        p.newSymbol(.hoisted_function, text)?,
                };

                p.lexer.next()?;
            }

            // Even anonymous functions can have TypeScript type parameters
            if (comptime is_typescript_enabled) {
                * = p.skipTypeScriptTypeParameters({ allow_const_modifier: true })?;
            }

            const func = p.parseFn(name, FnOrArrowDataParse{
                needs_async_loc: loc,
                async_range: async_range,
                allow_await: if (is_async) .allow_expr else .allow_ident,
                allow_yield: if (is_generator) .allow_expr else .allow_ident,
            });
            p.fn_or_arrow_data_parse.has_argument_decorators = false;

            p.validateFunctionName(func, .expr);
            p.popScope();

            return p.newExpr(js_ast.E.Function{
                func: func,
            }, loc);
        }

        static fn parseFnBody(p: *P, data: *FnOrArrowDataParse): G.FnBody! {
            const oldFnOrArrowData = p.fn_or_arrow_data_parse;
            const oldAllowIn = p.allow_in;
            p.fn_or_arrow_data_parse = data.*;
            p.allow_in = true;

            const loc = p.lexer.loc();
            let pushedScopeForFunctionBody = false;
            if (p.lexer.token == .t_open_brace) {
                * = p.pushScopeForParsePass(Scope.Kind.function_body, p.lexer.loc())?;
                pushedScopeForFunctionBody = true;
            }

            p.lexer.expect(.t_open_brace)?;
            let opts = ParseStatementOptions{};
            const stmts = p.parseStmtsUpTo(.t_close_brace, &opts)?;
            p.lexer.next()?;

            if (pushedScopeForFunctionBody) p.popScope();

            p.allow_in = oldAllowIn;
            p.fn_or_arrow_data_parse = oldFnOrArrowData;
            return G.FnBody{ loc: loc, stmts: stmts };
        }

        static fn parseArrowBody(p: *P, args: []js_ast.G.Arg, data: *FnOrArrowDataParse): E.Arrow! {
            const arrow_loc = p.lexer.loc();

            // Newlines are not allowed before "=>"
            if (p.lexer.has_newline_before) {
                p.log.addRangeError(p.source, p.lexer.range(), "Unexpected newline before \"=>\"")?;
                return error.SyntaxError;
            }

            p.lexer.expect(T.t_equals_greater_than)?;

            for (*arg of args) {
                let opts = ParseStatementOptions{};
                p.declareBinding(Symbol.Kind.hoisted, &arg.binding, &opts)?;
            }

            // The ability to use "this" and "super()" is inherited by arrow functions
            data.allow_super_call = p.fn_or_arrow_data_parse.allow_super_call;
            data.allow_super_property = p.fn_or_arrow_data_parse.allow_super_property;
            data.is_this_disallowed = p.fn_or_arrow_data_parse.is_this_disallowed;

            if (p.lexer.token == .t_open_brace) {
                const body = p.parseFnBody(data)?;
                p.after_arrow_body_loc = p.lexer.loc();
                return E.Arrow{ args: args, body: body };
            }

            * = p.pushScopeForParsePass(Scope.Kind.function_body, arrow_loc)?;
            defer p.popScope();

            let old_fn_or_arrow_data = std.mem.toBytes(p.fn_or_arrow_data_parse);

            p.fn_or_arrow_data_parse = data.*;
            const expr = p.parseExpr(Level.comma)?;
            p.fn_or_arrow_data_parse = std.mem.bytesToValue(@TypeOf(p.fn_or_arrow_data_parse), &old_fn_or_arrow_data);

            let stmts = p.allocator.alloc(Stmt, 1)?;
            stmts[0] = p.s(S.Return{ value: expr }, expr.loc);
            return E.Arrow{ args: args, prefer_expr: true, body: G.FnBody{ loc: arrow_loc, stmts: stmts } };
        }
    };
}

type string = []const u8;

import bun from "bun";
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const E = js_ast.E;
const Expr = js_ast.Expr;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const ExprNodeList = js_ast.ExprNodeList;
const Flags = js_ast.Flags;
const LocRef = js_ast.LocRef;
const S = js_ast.S;
const Scope = js_ast.Scope;
const Stmt = js_ast.Stmt;
const Symbol = js_ast.Symbol;

const G = js_ast.G;
const Arg = G.Arg;

const Op = js_ast.Op;
const Level = js_ast.Op.Level;

const js_lexer = bun.js_lexer;
const T = js_lexer.T;

const js_parser = bun.js_parser;
const AwaitOrYield = js_parser.AwaitOrYield;
const FnOrArrowDataParse = js_parser.FnOrArrowDataParse;
const JSXTransformType = js_parser.JSXTransformType;
const ParseStatementOptions = js_parser.ParseStatementOptions;
const TypeScript = js_parser.TypeScript;
const arguments_str = js_parser.arguments_str;
const options = js_parser.options;

const List = std.ArrayListUnmanaged;
