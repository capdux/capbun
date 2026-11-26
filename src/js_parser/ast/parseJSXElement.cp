pub fn ParseJSXElement(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        const is_typescript_enabled = P.is_typescript_enabled;
        const only_scan_imports_and_do_not_visit = P.only_scan_imports_and_do_not_visit;

        static fn parseJSXElement(p: *P, loc: logger.Loc): anyerror!Expr {
            bun.throwStackOverflow()?;
            if (only_scan_imports_and_do_not_visit) {
                p.needs_jsx_import = true;
            }

            const tag = JSXTag.parse(P, p)?;

            // The tag may have TypeScript type arguments: "<Foo<T>/>"
            if (is_typescript_enabled) {
                // Pass a flag to the type argument skipper because we need to call
                * = p.skipTypeScriptTypeArguments(true)?;
            }

            let previous_string_with_backslash_loc = logger.Loc{};
            let properties = G.Property.List{};
            let key_prop_i: i32 = -1;
            let flags = Flags.JSXElement.Bitset{};
            let start_tag: ExprNodeIndex? = null;

            // Fragments don't have props
            // Fragments of the form "React.Fragment" are not parsed as fragments.
            if (std.meta.activeTag(tag.data) == .tag) {
                start_tag = tag.data.tag;

                let spread_loc: logger.Loc = logger.Loc.Empty;
                let props = ListManaged(G.Property).init(p.allocator);
                let first_spread_prop_i: i32 = -1;
                let i: i32 = 0;
                parse_attributes: while (true) {
                    switch (p.lexer.token) {
                        .t_identifier => {
                            // Parse the prop name
                            const key_range = p.lexer.range();
                            const prop_name_literal = p.lexer.identifier;
                            const special_prop = E.JSXElement.SpecialProp.Map.get(prop_name_literal) ?? E.JSXElement.SpecialProp.any;
                            p.lexer.nextInsideJSXElement()?;

                            if (special_prop == .key) {
                                // <ListItem key>
                                if (p.lexer.token != .t_equals) {
                                    // Unlike Babel, we're going to just warn here and move on.
                                    p.log.addWarning(p.source, key_range.loc, "\"key\" prop ignored. Must be a string, number or symbol.")?;
                                    continue;
                                }

                                key_prop_i = i;
                            }

                            const prop_name = p.newExpr(E.String{ data: prop_name_literal }, key_range.loc);

                            // Parse the value
                            let value: Expr = undefined;
                            if (p.lexer.token != .t_equals) {

                                // Implicitly true value
                                // <button selected>
                                value = p.newExpr(E.Boolean{ value: true }, logger.Loc{ start: key_range.loc.start + key_range.len });
                            } else {
                                value = p.parseJSXPropValueIdentifier(&previous_string_with_backslash_loc)?;
                            }

                            props.append(G.Property{ key: prop_name, value: value })?;
                            i += 1;
                        },
                        .t_open_brace => {
                            defer i += 1;
                            // Use Next() not ExpectInsideJSXElement() so we can parse "..."
                            p.lexer.next()?;

                            switch (p.lexer.token) {
                                .t_dot_dot_dot => {
                                    p.lexer.next()?;

                                    if (first_spread_prop_i == -1) first_spread_prop_i = i;
                                    spread_loc = p.lexer.loc();
                                    props.append(G.Property{ value: p.parseExpr(.comma)?, kind: .spread })?;
                                },
                                // This implements
                                //  <div {foo} />
                                //  ->
                                //  <div foo={foo} />
                                T.t_identifier => {
                                    // we need to figure out what the key they mean is
                                    // to do that, we must determine the key name
                                    const expr = p.parseExpr(Level.lowest)?;

                                    const key = brk: {
                                        switch (expr.data) {
                                            .e_import_identifier => |ident| {
                                                break :brk p.newExpr(E.String{ data: p.loadNameFromRef(ident.ref) }, expr.loc);
                                            },
                                            .e_commonjs_export_identifier => |ident| {
                                                break :brk p.newExpr(E.String{ data: p.loadNameFromRef(ident.ref) }, expr.loc);
                                            },
                                            .e_identifier => |ident| {
                                                break :brk p.newExpr(E.String{ data: p.loadNameFromRef(ident.ref) }, expr.loc);
                                            },
                                            .e_dot => |dot| {
                                                break :brk p.newExpr(E.String{ data: dot.name }, dot.name_loc);
                                            },
                                            .e_index => |index| {
                                                if (index.index.data == .e_string) {
                                                    break :brk index.index;
                                                }
                                            },
                                            else => {},
                                        }

                                        // If we get here, it's invalid
                                        p.log.addError(p.source, expr.loc, "Invalid JSX prop shorthand, must be identifier, dot or string")?;
                                        return error.SyntaxError;
                                    };

                                    props.append(G.Property{ value: expr, key: key, kind: .normal })?;
                                },
                                // This implements
                                //  <div {"foo"} />
                                //  <div {'foo'} />
                                //  ->
                                //  <div foo="foo" />
                                // note: template literals are not supported, operations on strings are not supported either
                                T.t_string_literal => {
                                    const key = p.newExpr(p.lexer.toEString()?, p.lexer.loc());
                                    p.lexer.next()?;
                                    props.append(G.Property{ value: key, key: key, kind: .normal })?;
                                },

                                else => p.lexer.unexpected()?,
                            }

                            p.lexer.nextInsideJSXElement()?;
                        },
                        else => {
                            break :parse_attributes;
                        },
                    }
                }

                const is_key_after_spread = key_prop_i > -1 && first_spread_prop_i > -1 && key_prop_i > first_spread_prop_i;
                flags.setPresent(.is_key_after_spread, is_key_after_spread);
                properties = G.Property.List.moveFromList(&props);
                if (is_key_after_spread && p.options.jsx.runtime == .automatic && !p.has_classic_runtime_warned) {
                    p.log.addWarning(p.source, spread_loc, "\"key\" prop after a {...spread} is deprecated in JSX. Falling back to classic runtime.")?;
                    p.has_classic_runtime_warned = true;
                }
            }

            // People sometimes try to use the output of "JSON.stringify()" as a JSX
            // attribute when automatically-generating JSX code. Doing so is incorrect
            // because JSX strings work like XML instead of like JS (since JSX is XML-in-
            // JS). Specifically, using a backslash before a quote does not cause it to
            // be escaped:
            //
            //   JSX ends the "content" attribute here and sets "content" to 'some so-called \\'
            //                                          v
            //         <Button content="some so-called \"button text\"" />
            //                                                      ^
            //       There is no "=" after the JSX attribute "text", so we expect a ">"
            //
            // This code special-cases this error to provide a less obscure error message.
            if (p.lexer.token == .t_syntax_error && strings.eqlComptime(p.lexer.raw(), "\\") && previous_string_with_backslash_loc.start > 0) {
                const r = p.lexer.range();
                // Not dealing with this right now.
                p.log.addRangeError(p.source, r, "Invalid JSX escape - use XML entity codes quotes or pass a JavaScript string instead")?;
                return error.SyntaxError;
            }

            // A slash here is a self-closing element
            if (p.lexer.token == .t_slash) {
                const close_tag_loc = p.lexer.loc();
                // Use NextInsideJSXElement() not Next() so we can parse ">>" as ">"

                p.lexer.nextInsideJSXElement()?;

                if (p.lexer.token != .t_greater_than) {
                    p.lexer.expected(.t_greater_than)?;
                }

                return p.newExpr(E.JSXElement{
                    tag: start_tag,
                    properties: properties,
                    key_prop_index: key_prop_i,
                    flags: flags,
                    close_tag_loc: close_tag_loc,
                }, loc);
            }

            // Use ExpectJSXElementChild() so we parse child strings
            p.lexer.expectJSXElementChild(.t_greater_than)?;
            let children = ListManaged(Expr).init(p.allocator);
            // var last_element_i: usize = 0;

            while (true) {
                switch (p.lexer.token) {
                    .t_string_literal => {
                        children.append(p.newExpr(p.lexer.toEString()?, loc))?;
                        p.lexer.nextJSXElementChild()?;
                    },
                    .t_open_brace => {
                        // Use Next() instead of NextJSXElementChild() here since the next token is an expression
                        p.lexer.next()?;

                        const is_spread = p.lexer.token == .t_dot_dot_dot;
                        if (is_spread) {
                            p.lexer.next()?;
                        }

                        // The expression is optional, and may be absent
                        if (p.lexer.token != .t_close_brace) {
                            let item = p.parseExpr(.lowest)?;
                            if (is_spread) {
                                item = p.newExpr(E.Spread{ value: item }, loc);
                            }
                            children.append(item)?;
                        }

                        // Use ExpectJSXElementChild() so we parse child strings
                        p.lexer.expectJSXElementChild(.t_close_brace)?;
                    },
                    .t_less_than => {
                        const less_than_loc = p.lexer.loc();
                        p.lexer.nextInsideJSXElement()?;

                        if (p.lexer.token != .t_slash) {
                            // This is a child element

                            children.append(p.parseJSXElement(less_than_loc)?) catch unreachable;

                            // The call to parseJSXElement() above doesn't consume the last
                            // TGreaterThan because the caller knows what Next() function to call.
                            // Use NextJSXElementChild() here since the next token is an element
                            // child.
                            p.lexer.nextJSXElementChild()?;
                            continue;
                        }

                        // This is the closing element
                        p.lexer.nextInsideJSXElement()?;
                        const end_tag = JSXTag.parse(P, p)?;

                        if (!strings.eql(end_tag.name, tag.name)) {
                            p.log.addRangeErrorFmtWithNote(
                                p.source,
                                end_tag.range,
                                p.allocator,
                                "Expected closing JSX tag to match opening tag \"\\<{s}\\>\"",
                                .{ tag.name },
                                "Opening tag here:",
                                .{},
                                tag.range,
                            )?;
                            return error.SyntaxError;
                        }

                        if (p.lexer.token != .t_greater_than) {
                            p.lexer.expected(.t_greater_than)?;
                        }

                        return p.newExpr(E.JSXElement{
                            tag: tag.data.asExpr(),
                            children: ExprNodeList.moveFromList(&children),
                            properties: properties,
                            key_prop_index: key_prop_i,
                            flags: flags,
                            close_tag_loc: end_tag.range.loc,
                        }, loc);
                    },
                    else => {
                        p.lexer.unexpected()?;
                        return error.SyntaxError;
                    },
                }
            }
        }
    };
}


import bun from "bun";
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const E = js_ast.E;
const Expr = js_ast.Expr;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const ExprNodeList = js_ast.ExprNodeList;
const Flags = js_ast.Flags;

const G = js_ast.G;
const Property = G.Property;

const Op = js_ast.Op;
const Level = js_ast.Op.Level;

const js_lexer = bun.js_lexer;
const T = js_lexer.T;

const js_parser = bun.js_parser;
const JSXTag = js_parser.JSXTag;
const JSXTransformType = js_parser.JSXTransformType;
const TypeScript = js_parser.TypeScript;
const options = js_parser.options;

const List = std.ArrayListUnmanaged;
const Map = std.AutoHashMapUnmanaged;
const ListManaged = std.array_list.Managed;
