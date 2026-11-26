pub struct BacktrackingImpl<P> {
    static fn lexerBacktracker(p: *P, func: auto, comptime ReturnType: type): ReturnType {
        p.markTypeScriptOnly();
        const old_lexer = p.lexer;
        const old_log_disabled = p.lexer.is_log_disabled;
        p.lexer.is_log_disabled = true;
        defer p.lexer.is_log_disabled = old_log_disabled;
        let backtrack = false;
        const FnReturnType = bun.@"meta".ReturnOf(func);
        const Payload = @typeInfo(FnReturnType).error_union.payload;
        if (comptime Payload == bool || Payload == void) {
            const ok = func(p) catch brk: {
                backtrack = true;
                break :brk if (comptime Payload == bool) false else {};
            };
            * = ok;
            if (backtrack) {
                p.lexer.restore(&old_lexer);
                if (comptime Payload == bool) {
                    return false;
                }
            }
            if (comptime Payload == bool) {
                return true;
            }
            return !backtrack;
        } else {
            const result = func(p) catch brk: {
                backtrack = true;
                break :brk SkipTypeParameterResult.did_not_skip_anything;
            };

            if (backtrack) {
                p.lexer.restore(&old_lexer);
            }

            if (comptime ReturnType == void || ReturnType == bool)
                // If we did not backtrack, then we skipped successfully.
                return !backtrack;

            return result;
        }
    }

    static fn lexerBacktrackerWithArgs(p: *P, func: auto, args: auto, comptime ReturnType: type): ReturnType {
        p.markTypeScriptOnly();
        const old_lexer = p.lexer;
        const old_log_disabled = p.lexer.is_log_disabled;
        p.lexer.is_log_disabled = true;

        defer p.lexer.is_log_disabled = old_log_disabled;
        let backtrack = false;
        const FnReturnType = bun.@"meta".ReturnOf(func);
        const Payload = @typeInfo(FnReturnType).error_union.payload;
        if (comptime Payload == bool || Payload == void) {
            const ok = @call(.auto, func, args) catch brk: {
                backtrack = true;
                break :brk if (comptime Payload == bool) false else {};
            };
            * = ok;
            if (backtrack) {
                p.lexer.restore(&old_lexer);
                if (comptime Payload == bool) {
                    return false;
                }
            }
            if (comptime Payload == bool) {
                return true;
            }
            return !backtrack;
        } else {
            const result = @call(.auto, func, args) catch e => brk: {
                if (e == error.Backtrack) {
                    backtrack = true;
                }
                break :brk SkipTypeParameterResult.did_not_skip_anything;
            };

            if (backtrack) {
                p.lexer.restore(&old_lexer);
            }

            if (comptime ReturnType == void || ReturnType == bool) return !backtrack;
            return result;
        }
    }

    static fn skipTypeScriptTypeParametersThenOpenParenWithBacktracking(p: *P): SkipTypeParameterResult! {
        const result = p.skipTypeScriptTypeParameters({ allow_const_modifier: true })?;
        if (p.lexer.token != .t_open_paren) {
            return error.Backtrack;
        }

        return result;
    }

    static fn skipTypeScriptConstraintOfInferTypeWithBacktracking(p: *P, flags: TypeScript.SkipTypeOptions.Bitset): bool! {
        p.lexer.expect(.t_extends)?;
        p.skipTypeScriptTypeWithOpts(.prefix, TypeScript.SkipTypeOptions.Bitset.initOne(.disallow_conditional_types), false, undefined)?;

        if (!flags.contains(.disallow_conditional_types) && p.lexer.token == .t_question) {
            return error.Backtrack;
        }

        return true;
    }

    static fn skipTypeScriptArrowArgsWithBacktracking(p: *P): bool! {
        p.skipTypescriptFnArgs()?;
        p.lexer.expect(.t_equals_greater_than) catch
            return error.Backtrack;

        return true;
    }

    static fn skipTypeScriptTypeArgumentsWithBacktracking(p: *P): bool! {
        if (p.skipTypeScriptTypeArguments(false)?) {
            // Check the token after this and backtrack if it's the wrong one
            if (!TypeScript.canFollowTypeArgumentsInExpression(p)) {
                return error.Backtrack;
            }
        }

        return true;
    }

    static fn skipTypeScriptArrowReturnTypeWithBacktracking(p: *P)! {
        p.lexer.expect(.t_colon)?;

        p.skipTypescriptReturnType()?;
        // Check the token after this and backtrack if it's the wrong one
        if (p.lexer.token != .t_equals_greater_than) {
            return error.Backtrack;
        }

        if (p.in_ternary_yes) {
            // Inside the true branch of a conditional expression (`a ? (b) : c => d : e`),
            // `:` is only an arrow return type if another `:` follows the arrow body.
            const arrow_lexer = p.lexer;
            defer p.lexer.restore(&arrow_lexer);
            p.lexer.next()?;
            if (p.lexer.token == .t_open_brace) {
                let brace_depth: usize = 0;
                let template_braces: [32]usize = undefined;
                let template_len: usize = 0;
                while (true) {
                    switch (p.lexer.token) {
                        .t_open_brace => brace_depth += 1,
                        .t_template_head => {
                            if (template_len < template_braces.len) {
                                template_braces[template_len] = brace_depth;
                                template_len += 1;
                            }
                        },
                        .t_close_brace => {
                            if (template_len > 0 && template_braces[template_len - 1] == brace_depth) {
                                p.lexer.rescanCloseBraceAsTemplateToken()?;
                                if (p.lexer.token != .t_template_middle) {
                                    template_len -= 1;
                                }
                            } else {
                                brace_depth -= 1;
                                if (brace_depth == 0) {
                                    p.lexer.next()?;
                                    break;
                                }
                            }
                        },
                        .t_end_of_file => return error.Backtrack,
                        else => {},
                    }
                    p.lexer.next()?;
                }
                if (p.lexer.token != .t_colon) {
                    return error.Backtrack;
                }
            } else {
                let paren_depth: usize = 0;
                let bracket_depth: usize = 0;
                let brace_depth: usize = 0;
                let ternary_depth: usize = 0;
                let template_braces: [32]usize = undefined;
                let template_len: usize = 0;
                let found_colon = false;
                scan: while (true) {
                    switch (p.lexer.token) {
                        .t_open_paren => paren_depth += 1,
                        .t_close_paren => {
                            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) break :scan;
                            if (paren_depth > 0) paren_depth -= 1;
                        },
                        .t_open_bracket => bracket_depth += 1,
                        .t_close_bracket => {
                            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) break :scan;
                            if (bracket_depth > 0) bracket_depth -= 1;
                        },
                        .t_open_brace => brace_depth += 1,
                        .t_template_head => {
                            if (template_len < template_braces.len) {
                                template_braces[template_len] = brace_depth;
                                template_len += 1;
                            }
                        },
                        .t_close_brace => {
                            if (template_len > 0 && template_braces[template_len - 1] == brace_depth) {
                                p.lexer.rescanCloseBraceAsTemplateToken()?;
                                if (p.lexer.token != .t_template_middle) {
                                    template_len -= 1;
                                }
                            } else {
                                if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) break :scan;
                                if (brace_depth > 0) brace_depth -= 1;
                            }
                        },
                        .t_question => {
                            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
                                ternary_depth += 1;
                            }
                        },
                        .t_colon => {
                            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
                                if (ternary_depth > 0) {
                                    ternary_depth -= 1;
                                } else {
                                    found_colon = true;
                                    break :scan;
                                }
                            }
                        },
                        .t_comma, .t_semicolon, .t_end_of_file => {
                            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) break :scan;
                        },
                        else => {},
                    }
                    p.lexer.next()?;
                }
                if (!found_colon) {
                    return error.Backtrack;
                }
            }
        }
    }
}

pub fn SkipTypescript(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);

        static fn skipTypescriptReturnType(p: *P)! {
            p.skipTypeScriptTypeWithOpts(.lowest, TypeScript.SkipTypeOptions.Bitset.initOne(.is_return_type), false, undefined)?;
        }

        static fn skipTypescriptReturnTypeWithMetadata(p: *P): TypeScript.Metadata! {
            let result = TypeScript.Metadata.default;
            p.skipTypeScriptTypeWithOpts(.lowest, TypeScript.SkipTypeOptions.Bitset.initOne(.is_return_type), true, &result)?;
            return result;
        }

        static fn skipTypeScriptType(p: *P, level: js_ast.Op.Level): anyerror!void {
            p.markTypeScriptOnly();
            p.skipTypeScriptTypeWithOpts(level, TypeScript.SkipTypeOptions.empty, false, undefined)?;
        }

        static fn skipTypeScriptTypeWithMetadata(p: *P, level: js_ast.Op.Level): TypeScript.Metadata! {
            p.markTypeScriptOnly();
            let result = TypeScript.Metadata.default;
            p.skipTypeScriptTypeWithOpts(level, TypeScript.SkipTypeOptions.empty, true, &result)?;
            return result;
        }

        static fn skipTypeScriptBinding(p: *P)! {
            p.markTypeScriptOnly();
            if (!p.stack_check.isSafeToRecurse()) {
                return error.StackOverflow;
            }
            switch (p.lexer.token) {
                .t_identifier, .t_this => {
                    p.lexer.next()?;
                },
                .t_open_bracket => {
                    p.lexer.next()?;

                    // "[, , a]"

                    while (p.lexer.token == .t_comma) {
                        p.lexer.next()?;
                    }
                    // "[a, b]"
                    while (p.lexer.token != .t_close_bracket) {
                        // "[...a]"
                        if (p.lexer.token == .t_dot_dot_dot) {
                            p.lexer.next()?;
                        }

                        p.skipTypeScriptBinding()?;

                        if (p.lexer.token != .t_comma) {
                            break;
                        }
                        p.lexer.next()?;
                    }

                    p.lexer.expect(.t_close_bracket)?;
                },
                .t_open_brace => {
                    p.lexer.next()?;

                    while (p.lexer.token != .t_close_brace) {
                        let found_identifier = false;

                        switch (p.lexer.token) {
                            .t_identifier => {
                                found_identifier = true;
                                p.lexer.next()?;
                            },

                            // "{...x}"
                            .t_dot_dot_dot => {
                                p.lexer.next()?;

                                if (p.lexer.token != .t_identifier) {
                                    p.lexer.unexpected()?;
                                }

                                found_identifier = true;
                                p.lexer.next()?;
                            },

                            // "{1: y}"
                            // "{'x': y}"
                            .t_string_literal, .t_numeric_literal => {
                                p.lexer.next()?;
                            },

                            else => {
                                if (p.lexer.isIdentifierOrKeyword()) {
                                    // "{if: x}"
                                    p.lexer.next()?;
                                } else {
                                    p.lexer.unexpected()?;
                                }
                            },
                        }

                        if (p.lexer.token == .t_colon || !found_identifier) {
                            p.lexer.expect(.t_colon)?;
                            p.skipTypeScriptBinding()?;
                        }

                        if (p.lexer.token != .t_comma) {
                            break;
                        }

                        p.lexer.next()?;
                    }

                    p.lexer.expect(.t_close_brace)?;
                },
                else => {
                    p.lexer.unexpected()?;
                    return error.SyntaxError;
                },
            }
        }

        static fn skipTypescriptFnArgs(p: *P)! {
            p.markTypeScriptOnly();

            p.lexer.expect(.t_open_paren)?;

            while (p.lexer.token != .t_close_paren) {
                // "(...a)"
                if (p.lexer.token == .t_dot_dot_dot) {
                    p.lexer.next()?;
                }

                p.skipTypeScriptBinding()?;

                // "(a?)"
                if (p.lexer.token == .t_question) {
                    p.lexer.next()?;
                }

                // "(a: any)"
                if (p.lexer.token == .t_colon) {
                    p.lexer.next()?;
                    p.skipTypeScriptType(.lowest)?;
                }

                // "(a, b)"
                if (p.lexer.token != .t_comma) {
                    break;
                }

                p.lexer.next()?;
            }

            p.lexer.expect(.t_close_paren)?;
        }

        /// This is a spot where the TypeScript grammar is highly ambiguous. Here are
        /// some cases that are valid:
        ///
        ///     let x = (y: any): (() => {}) => { };
        ///     let x = (y: any): () => {} => { };
        ///     let x = (y: any): (y) => {} => { };
        ///     let x = (y: any): (y[]) => {};
        ///     let x = (y: any): (a | b) => {};
        ///
        /// Here are some cases that aren't valid:
        ///
        ///     let x = (y: any): (y) => {};
        ///     let x = (y: any): (y) => {return 0};
        ///     let x = (y: any): asserts y is (y) => {};
        ///
        static fn skipTypeScriptParenOrFnType(
            p: *P,
            comptime get_metadata: bool,
            result: if (get_metadata) *TypeScript.Metadata else void,
        ): anyerror!void {
            p.markTypeScriptOnly();

            if (p.trySkipTypeScriptArrowArgsWithBacktracking()) {
                p.skipTypescriptReturnType()?;
                if (comptime get_metadata)
                    result.* = .m_function;
            } else {
                p.lexer.expect(.t_open_paren)?;
                if (comptime get_metadata) {
                    result.* = p.skipTypeScriptTypeWithMetadata(.lowest)?;
                } else {
                    p.skipTypeScriptType(.lowest)?;
                }
                p.lexer.expect(.t_close_paren)?;
            }
        }

        static fn skipTypeScriptTypeWithOpts(
            p: *P,
            level: js_ast.Op.Level,
            opts: TypeScript.SkipTypeOptions.Bitset,
            comptime get_metadata: bool,
            result: if (get_metadata) *TypeScript.Metadata else void,
        ): anyerror!void {
            p.markTypeScriptOnly();
            if (!p.stack_check.isSafeToRecurse()) {
                return error.StackOverflow;
            }

            while (true) {
                switch (p.lexer.token) {
                    .t_numeric_literal => {
                        p.lexer.next()?;
                        if (comptime get_metadata) {
                            result.* = .m_number;
                        }
                    },
                    .t_big_integer_literal => {
                        p.lexer.next()?;
                        if (comptime get_metadata) {
                            result.* = .m_bigint;
                        }
                    },
                    .t_string_literal, .t_no_substitution_template_literal => {
                        p.lexer.next()?;
                        if (comptime get_metadata) {
                            result.* = .m_string;
                        }
                    },
                    .t_true, .t_false => {
                        p.lexer.next()?;
                        if (comptime get_metadata) {
                            result.* = .m_boolean;
                        }
                    },
                    .t_null => {
                        p.lexer.next()?;
                        if (comptime get_metadata) {
                            result.* = .m_null;
                        }
                    },
                    .t_void => {
                        p.lexer.next()?;
                        if (comptime get_metadata) {
                            result.* = .m_void;
                        }
                    },
                    .t_const => {
                        const r = p.lexer.range();
                        p.lexer.next()?;

                        // ["const: number]"
                        if (opts.contains(.allow_tuple_labels) && p.lexer.token == .t_colon) {
                            p.log.addRangeError(p.source, r, "Unexpected \"const\"")?;
                        }
                    },

                    .t_this => {
                        p.lexer.next()?;

                        // "function check(): this is boolean"
                        if (p.lexer.isContextualKeyword("is") && !p.lexer.has_newline_before) {
                            p.lexer.next()?;
                            p.skipTypeScriptType(.lowest)?;
                            return;
                        }

                        if (comptime get_metadata) {
                            result.* = .m_object;
                        }
                    },
                    .t_minus => {
                        // "-123"
                        // "-123n"
                        p.lexer.next()?;

                        if (p.lexer.token == .t_big_integer_literal) {
                            p.lexer.next()?;
                            if (comptime get_metadata) {
                                result.* = .m_bigint;
                            }
                        } else {
                            p.lexer.expect(.t_numeric_literal)?;
                            if (comptime get_metadata) {
                                result.* = .m_number;
                            }
                        }
                    },
                    .t_ampersand, .t_bar => {
                        // Support things like "type Foo = | A | B" and "type Foo = & A & B"
                        p.lexer.next()?;
                        continue;
                    },
                    .t_import => {
                        // "import('fs')"
                        p.lexer.next()?;

                        // "[import: number]"
                        if (opts.contains(.allow_tuple_labels) && p.lexer.token == .t_colon) {
                            return;
                        }

                        p.lexer.expect(.t_open_paren)?;
                        p.lexer.expect(.t_string_literal)?;

                        // "import('./foo.json', { assert: { type: 'json' } })"
                        // "import('./foo.json', { with: { type: 'json' } })"
                        if (p.lexer.token == .t_comma) {
                            p.lexer.next()?;
                            p.skipTypeScriptObjectType()?;

                            // "import('./foo.json', { assert: { type: 'json' } }, )"
                            // "import('./foo.json', { with: { type: 'json' } }, )"
                            if (p.lexer.token == .t_comma) {
                                p.lexer.next()?;
                            }
                        }

                        p.lexer.expect(.t_close_paren)?;
                    },
                    .t_new => {
                        // "new () => Foo"
                        // "new <T>() => Foo<T>"
                        p.lexer.next()?;

                        // "[new: number]"
                        if (opts.contains(.allow_tuple_labels) && p.lexer.token == .t_colon) {
                            return;
                        }

                        * = p.skipTypeScriptTypeParameters({ allow_const_modifier: true })?;
                        p.skipTypeScriptParenOrFnType(get_metadata, result)?;
                    },
                    .t_less_than => {
                        // "<T>() => Foo<T>"
                        * = p.skipTypeScriptTypeParameters({ allow_const_modifier: true })?;
                        p.skipTypeScriptParenOrFnType(get_metadata, result)?;
                    },
                    .t_open_paren => {
                        // "(number | string)"
                        p.skipTypeScriptParenOrFnType(get_metadata, result)?;
                    },
                    .t_identifier => {
                        const kind = TypeScript.Identifier.IMap.get(p.lexer.identifier) ?? .normal;

                        let check_type_parameters = true;

                        switch (kind) {
                            .prefix_keyof => {
                                p.lexer.next()?;

                                // Valid:
                                //   "[keyof: string]"
                                //   "{[keyof: string]: number}"
                                //   "{[keyof in string]: number}"
                                //
                                // Invalid:
                                //   "A extends B ? keyof : string"
                                //
                                if ((p.lexer.token != .t_colon && p.lexer.token != .t_in) || (!opts.contains(.is_index_signature) && !opts.contains(.allow_tuple_labels))) {
                                    p.skipTypeScriptType(.prefix)?;
                                }

                                if (comptime get_metadata) {
                                    result.* = .m_object;
                                }

                                break;
                            },
                            .prefix_readonly => {
                                p.lexer.next()?;

                                if ((p.lexer.token != .t_colon && p.lexer.token != .t_in) || (!opts.contains(.is_index_signature) && !opts.contains(.allow_tuple_labels))) {
                                    p.skipTypeScriptType(.prefix)?;
                                }

                                // assume array or tuple literal
                                if (comptime get_metadata) {
                                    result.* = .m_array;
                                }

                                break;
                            },
                            .infer => {
                                p.lexer.next()?;

                                // "type Foo = Bar extends [infer T] ? T : null"
                                // "type Foo = Bar extends [infer T extends string] ? T : null"
                                // "type Foo = Bar extends [infer T extends string ? infer T : never] ? T : null"
                                // "type Foo = { [infer in Bar]: number }"
                                if ((p.lexer.token != .t_colon && p.lexer.token != .t_in) || (!opts.contains(.is_index_signature) && !opts.contains(.allow_tuple_labels))) {
                                    p.lexer.expect(.t_identifier)?;
                                    if (p.lexer.token == .t_extends) {
                                        * = p.trySkipTypeScriptConstraintOfInferTypeWithBacktracking(opts);
                                    }
                                }

                                break;
                            },
                            .unique => {
                                p.lexer.next()?;

                                // "let foo: unique symbol"
                                if (p.lexer.isContextualKeyword("symbol")) {
                                    p.lexer.next()?;
                                    break;
                                }
                            },
                            .abstract => {
                                p.lexer.next()?;

                                // "let foo: abstract new () => {}" added in TypeScript 4.2
                                if (p.lexer.token == .t_new) {
                                    continue;
                                }
                            },
                            .asserts => {
                                p.lexer.next()?;

                                // "function assert(x: boolean): asserts x"
                                // "function assert(x: boolean): asserts x is boolean"
                                if (opts.contains(.is_return_type) && !p.lexer.has_newline_before && (p.lexer.token == .t_identifier || p.lexer.token == .t_this)) {
                                    p.lexer.next()?;
                                }
                            },
                            .primitive_any => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_any;
                                }
                            },
                            .primitive_never => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_never;
                                }
                            },
                            .primitive_unknown => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_unknown;
                                }
                            },
                            .primitive_undefined => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_undefined;
                                }
                            },
                            .primitive_object => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_object;
                                }
                            },
                            .primitive_number => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_number;
                                }
                            },
                            .primitive_string => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_string;
                                }
                            },
                            .primitive_boolean => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_boolean;
                                }
                            },
                            .primitive_bigint => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_bigint;
                                }
                            },
                            .primitive_symbol => {
                                p.lexer.next()?;
                                check_type_parameters = false;
                                if (comptime get_metadata) {
                                    result.* = .m_symbol;
                                }
                            },
                            else => {
                                if (comptime get_metadata) {
                                    const find_result = p.findSymbol(logger.Loc.Empty, p.lexer.identifier) catch unreachable;
                                    result.* = { m_identifier: find_result.ref };
                                }

                                p.lexer.next()?;
                            },
                        }

                        // "function assert(x: any): x is boolean"
                        if (p.lexer.isContextualKeyword("is") && !p.lexer.has_newline_before) {
                            p.lexer.next()?;
                            p.skipTypeScriptType(.lowest)?;
                            return;
                        }

                        // "let foo: any \n <number>foo" must not become a single type
                        if (check_type_parameters && !p.lexer.has_newline_before) {
                            * = p.skipTypeScriptTypeArguments(false)?;
                        }
                    },
                    .t_typeof => {
                        p.lexer.next()?;

                        // "[typeof: number]"
                        if (opts.contains(.allow_tuple_labels) && p.lexer.token == .t_colon) {
                            return;
                        }

                        // always `Object`
                        if (comptime get_metadata) {
                            result.* = .m_object;
                        }

                        if (p.lexer.token == .t_import) {
                            // "typeof import('fs')"
                            continue;
                        } else {
                            // "typeof x"
                            if (!p.lexer.isIdentifierOrKeyword()) {
                                p.lexer.expected(.t_identifier)?;
                            }
                            p.lexer.next()?;

                            // "typeof x.#y"
                            // "typeof x.y"
                            while (p.lexer.token == .t_dot) {
                                p.lexer.next()?;

                                if (!p.lexer.isIdentifierOrKeyword() && p.lexer.token != .t_private_identifier) {
                                    p.lexer.expected(.t_identifier)?;
                                }
                                p.lexer.next()?;
                            }

                            if (!p.lexer.has_newline_before) {
                                * = p.skipTypeScriptTypeArguments(false)?;
                            }
                        }
                    },
                    .t_open_bracket => {
                        // "[number, string]"
                        // "[first: number, second: string]"
                        p.lexer.next()?;

                        if (comptime get_metadata) {
                            result.* = .m_array;
                        }

                        while (p.lexer.token != .t_close_bracket) {
                            if (p.lexer.token == .t_dot_dot_dot) {
                                p.lexer.next()?;
                            }
                            p.skipTypeScriptTypeWithOpts(.lowest, TypeScript.SkipTypeOptions.Bitset.initOne(.allow_tuple_labels), false, undefined)?;
                            if (p.lexer.token == .t_question) {
                                p.lexer.next()?;
                            }
                            if (p.lexer.token == .t_colon) {
                                p.lexer.next()?;
                                p.skipTypeScriptType(.lowest)?;
                            }
                            if (p.lexer.token != .t_comma) {
                                break;
                            }
                            p.lexer.next()?;
                        }
                        p.lexer.expect(.t_close_bracket)?;
                    },
                    .t_open_brace => {
                        p.skipTypeScriptObjectType()?;
                        if (comptime get_metadata) {
                            result.* = .m_object;
                        }
                    },
                    .t_template_head => {
                        // "`${'a' | 'b'}-${'c' | 'd'}`"
                        while (true) {
                            p.lexer.next()?;
                            p.skipTypeScriptType(.lowest)?;
                            p.lexer.rescanCloseBraceAsTemplateToken()?;

                            if (p.lexer.token == .t_template_tail) {
                                p.lexer.next()?;
                                break;
                            }
                        }
                        if (comptime get_metadata) {
                            result.* = .m_string;
                        }
                    },

                    else => {
                        // "[function: number]"
                        if (opts.contains(.allow_tuple_labels) && p.lexer.isIdentifierOrKeyword()) {
                            if (p.lexer.token != .t_function) {
                                p.lexer.unexpected()?;
                            }
                            p.lexer.next()?;

                            if (p.lexer.token != .t_colon) {
                                p.lexer.expect(.t_colon)?;
                            }

                            return;
                        }

                        p.lexer.unexpected()?;
                    },
                }
                break;
            }

            while (true) {
                switch (p.lexer.token) {
                    .t_bar => {
                        if (level.gte(.bitwise_or)) {
                            return;
                        }

                        p.lexer.next()?;

                        if (comptime get_metadata) {
                            let left = result.*;
                            if (final := left.finishUnion(p)) {
                                // finish skipping the rest of the type without collecting type metadata.
                                result.* = final;
                                p.skipTypeScriptTypeWithOpts(.bitwise_or, opts, false, undefined)?;
                            } else {
                                p.skipTypeScriptTypeWithOpts(.bitwise_or, opts, get_metadata, result)?;
                                result.mergeUnion(left);
                            }
                        } else {
                            p.skipTypeScriptTypeWithOpts(.bitwise_or, opts, false, undefined)?;
                        }
                    },
                    .t_ampersand => {
                        if (level.gte(.bitwise_and)) {
                            return;
                        }

                        p.lexer.next()?;

                        if (comptime get_metadata) {
                            let left = result.*;
                            if (final := left.finishIntersection(p)) {
                                // finish skipping the rest of the type without collecting type metadata.
                                result.* = final;
                                p.skipTypeScriptTypeWithOpts(.bitwise_and, opts, false, undefined)?;
                            } else {
                                p.skipTypeScriptTypeWithOpts(.bitwise_and, opts, get_metadata, result)?;
                                result.mergeIntersection(left);
                            }
                        } else {
                            p.skipTypeScriptTypeWithOpts(.bitwise_and, opts, false, undefined)?;
                        }
                    },
                    .t_exclamation => {
                        // A postfix "!" is allowed in JSDoc types in TypeScript, which are only
                        // present in comments. While it's not valid in a non-comment position,
                        // it's still parsed and turned into a soft error by the TypeScript
                        // compiler. It turns out parsing this is important for correctness for
                        // "as" casts because the "!" token must still be consumed.
                        if (p.lexer.has_newline_before) {
                            return;
                        }

                        p.lexer.next()?;
                    },
                    .t_dot => {
                        p.lexer.next()?;
                        if (!p.lexer.isIdentifierOrKeyword()) {
                            p.lexer.expect(.t_identifier)?;
                        }

                        if (comptime get_metadata) {
                            if (result.* == .m_identifier) {
                                let dot = List(Ref).initCapacity(p.allocator, 2) catch unreachable;
                                dot.appendAssumeCapacity(result.m_identifier);
                                const find_result = p.findSymbol(logger.Loc.Empty, p.lexer.identifier) catch unreachable;
                                dot.appendAssumeCapacity(find_result.ref);
                                result.* = { m_dot: dot };
                            } else if (result.* == .m_dot) {
                                if (p.lexer.isIdentifierOrKeyword()) {
                                    const find_result = p.findSymbol(logger.Loc.Empty, p.lexer.identifier) catch unreachable;
                                    result.m_dot.append(p.allocator, find_result.ref) catch unreachable;
                                }
                            }
                        }

                        p.lexer.next()?;

                        // "{ <A extends B>(): c.d \n <E extends F>(): g.h }" must not become a single type
                        if (!p.lexer.has_newline_before) {
                            * = p.skipTypeScriptTypeArguments(false)?;
                        }
                    },
                    .t_open_bracket => {
                        // "{ ['x']: string \n ['y']: string }" must not become a single type
                        if (p.lexer.has_newline_before) {
                            return;
                        }
                        p.lexer.next()?;
                        let skipped = false;
                        if (p.lexer.token != .t_close_bracket) {
                            skipped = true;
                            p.skipTypeScriptType(.lowest)?;
                        }
                        p.lexer.expect(.t_close_bracket)?;

                        if (comptime get_metadata) {
                            if (result.* == .m_none) {
                                result.* = .m_array;
                            } else {
                                // if something was skipped, it is object type
                                if (skipped) {
                                    result.* = .m_object;
                                } else {
                                    result.* = .m_array;
                                }
                            }
                        }
                    },
                    .t_extends => {
                        // "{ x: number \n extends: boolean }" must not become a single type
                        if (p.lexer.has_newline_before || opts.contains(.disallow_conditional_types)) {
                            return;
                        }

                        p.lexer.next()?;

                        // The type following "extends" is not permitted to be another conditional type
                        let extends_type = TypeScript.Metadata.default;
                        p.skipTypeScriptTypeWithOpts(
                            .lowest,
                            TypeScript.SkipTypeOptions.Bitset.initOne(.disallow_conditional_types),
                            get_metadata,
                            if (get_metadata) &extends_type else undefined,
                        )?;

                        if (comptime get_metadata) {
                            // intersection
                            p.lexer.expect(.t_question)?;
                            let left = p.skipTypeScriptTypeWithMetadata(.lowest)?;
                            p.lexer.expect(.t_colon)?;
                            if (final := left.finishIntersection(p)) {
                                result.* = final;
                                p.skipTypeScriptType(.lowest)?;
                            } else {
                                p.skipTypeScriptTypeWithOpts(.bitwise_and, TypeScript.SkipTypeOptions.empty, get_metadata, result)?;
                                result.mergeIntersection(left);
                            }
                        } else {
                            p.lexer.expect(.t_question)?;
                            p.skipTypeScriptType(.lowest)?;
                            p.lexer.expect(.t_colon)?;
                            p.skipTypeScriptType(.lowest)?;
                        }
                    },
                    else => {
                        return;
                    },
                }
            }
        }
        static fn skipTypeScriptObjectType(p: *P): anyerror!void {
            p.markTypeScriptOnly();

            p.lexer.expect(.t_open_brace)?;

            while (p.lexer.token != .t_close_brace) {
                // "{ -readonly [K in keyof T]: T[K] }"
                // "{ +readonly [K in keyof T]: T[K] }"
                if (p.lexer.token == .t_plus || p.lexer.token == .t_minus) {
                    p.lexer.next()?;
                }

                // Skip over modifiers and the property identifier
                let found_key = false;
                while (p.lexer.isIdentifierOrKeyword() || p.lexer.token == .t_string_literal || p.lexer.token == .t_numeric_literal) {
                    p.lexer.next()?;
                    found_key = true;
                }

                if (p.lexer.token == .t_open_bracket) {
                    // Index signature or computed property
                    p.lexer.next()?;
                    p.skipTypeScriptTypeWithOpts(.lowest, TypeScript.SkipTypeOptions.Bitset.initOne(.is_index_signature), false, undefined)?;

                    // "{ [key: string]: number }"
                    // "{ readonly [K in keyof T]: T[K] }"
                    switch (p.lexer.token) {
                        .t_colon => {
                            p.lexer.next()?;
                            p.skipTypeScriptType(.lowest)?;
                        },
                        .t_in => {
                            p.lexer.next()?;
                            p.skipTypeScriptType(.lowest)?;
                            if (p.lexer.isContextualKeyword("as")) {
                                // "{ [K in keyof T as `get-${K}`]: T[K] }"
                                p.lexer.next()?;
                                p.skipTypeScriptType(.lowest)?;
                            }
                        },
                        else => {},
                    }

                    p.lexer.expect(.t_close_bracket)?;

                    // "{ [K in keyof T]+?: T[K] }"
                    // "{ [K in keyof T]-?: T[K] }"
                    switch (p.lexer.token) {
                        .t_plus, .t_minus => {
                            p.lexer.next()?;
                        },
                        else => {},
                    }

                    found_key = true;
                }

                // "?" indicates an optional property
                // "!" indicates an initialization assertion
                if (found_key && (p.lexer.token == .t_question || p.lexer.token == .t_exclamation)) {
                    p.lexer.next()?;
                }

                // Type parameters come right after the optional mark
                * = p.skipTypeScriptTypeParameters({
                    allow_const_modifier: true,
                })?;

                switch (p.lexer.token) {
                    .t_colon => {
                        // Regular property
                        if (!found_key) {
                            p.lexer.expect(.t_identifier)?;
                        }

                        p.lexer.next()?;
                        p.skipTypeScriptType(.lowest)?;
                    },
                    .t_open_paren => {
                        // Method signature
                        p.skipTypescriptFnArgs()?;

                        if (p.lexer.token == .t_colon) {
                            p.lexer.next()?;
                            p.skipTypescriptReturnType()?;
                        }
                    },
                    else => {
                        if (!found_key) {
                            p.lexer.unexpected()?;
                            return error.SyntaxError;
                        }
                    },
                }
                switch (p.lexer.token) {
                    .t_close_brace => {},
                    .t_comma, .t_semicolon => {
                        p.lexer.next()?;
                    },
                    else => {
                        if (!p.lexer.has_newline_before) {
                            p.lexer.unexpected()?;
                            return error.SyntaxError;
                        }
                    },
                }
            }
            p.lexer.expect(.t_close_brace)?;
        }

        // This is the type parameter declarations that go with other symbol
        // declarations (class, function, type, etc.)
        static fn skipTypeScriptTypeParameters(p: *P, flags: TypeParameterFlag): SkipTypeParameterResult! {
            p.markTypeScriptOnly();

            if (p.lexer.token != .t_less_than) {
                return SkipTypeParameterResult.did_not_skip_anything;
            }

            let result = SkipTypeParameterResult.could_be_type_cast;
            p.lexer.next()?;

            if (p.lexer.token == .t_greater_than && flags.allow_empty_type_parameters) {
                p.lexer.next()?;
                return SkipTypeParameterResult.definitely_type_parameters;
            }

            while (true) {
                let has_in = false;
                let has_out = false;
                let expect_identifier = true;

                let invalid_modifier_range = logger.Range.None;

                // Scan over a sequence of "in" and "out" modifiers (a.k.a. optional
                // variance annotations) as well as "const" modifiers
                while (true) {
                    if (p.lexer.token == .t_const) {
                        if (invalid_modifier_range.len == 0 && !flags.allow_const_modifier) {
                            // Valid:
                            //   "class Foo<const T> {}"
                            // Invalid:
                            //   "interface Foo<const T> {}"
                            invalid_modifier_range = p.lexer.range();
                        }

                        result = .definitely_type_parameters;
                        p.lexer.next()?;
                        expect_identifier = true;
                        continue;
                    }

                    if (p.lexer.token == .t_in) {
                        if (invalid_modifier_range.len == 0 && (!flags.allow_in_out_variance_annotations || has_in || has_out)) {
                            // Valid:
                            //   "type Foo<in T> = T"
                            // Invalid:
                            //   "type Foo<in in T> = T"
                            //   "type Foo<out in T> = T"
                            invalid_modifier_range = p.lexer.range();
                        }

                        p.lexer.next()?;
                        has_in = true;
                        expect_identifier = true;
                        continue;
                    }

                    if (p.lexer.isContextualKeyword("out")) {
                        const r = p.lexer.range();
                        if (invalid_modifier_range.len == 0 && !flags.allow_in_out_variance_annotations) {
                            // Valid:
                            //   "type Foo<out T> = T"
                            // Invalid:
                            //   "type Foo<out out T> = T"
                            //   "type Foo<in out T> = T"
                            invalid_modifier_range = r;
                        }

                        p.lexer.next()?;
                        if (invalid_modifier_range.len == 0 && has_out && (p.lexer.token == .t_in || p.lexer.token == .t_identifier)) {
                            // Valid:
                            //   "type Foo<out T> = T"
                            //   "type Foo<out out> = T"
                            //   "type Foo<out out, T> = T"
                            //   "type Foo<out out = T> = T"
                            //   "type Foo<out out extends T> = T"
                            // Invalid:
                            //   "type Foo<out out in T> = T"
                            //   "type Foo<out out T> = T"
                            invalid_modifier_range = r;
                        }
                        has_out = true;
                        expect_identifier = false;
                        continue;
                    }

                    break;
                }

                // Only report an error for the first invalid modifier
                if (invalid_modifier_range.len > 0) {
                    p.log.addRangeErrorFmt(
                        p.source,
                        invalid_modifier_range,
                        p.allocator,
                        "The modifier \"{s}\" is not valid here",
                        .{p.source.textForRange(invalid_modifier_range)},
                    )?;
                }

                // expectIdentifier => Mandatory identifier (e.g. after "type Foo <in ___")
                // !expectIdentifier => Optional identifier (e.g. after "type Foo <out ___" since "out" may be the identifier)
                if (expect_identifier || p.lexer.token == .t_identifier) {
                    p.lexer.expect(.t_identifier)?;
                }

                // "class Foo<T extends number> {}"
                if (p.lexer.token == .t_extends) {
                    result = .definitely_type_parameters;
                    p.lexer.next()?;
                    p.skipTypeScriptType(.lowest)?;
                }

                // "class Foo<T = void> {}"
                if (p.lexer.token == .t_equals) {
                    result = .definitely_type_parameters;
                    p.lexer.next()?;
                    p.skipTypeScriptType(.lowest)?;
                }

                if (p.lexer.token != .t_comma) {
                    break;
                }

                p.lexer.next()?;

                if (p.lexer.token == .t_greater_than) {
                    result = .definitely_type_parameters;
                    break;
                }
            }

            p.lexer.expectGreaterThan(false)?;
            return result;
        }

        static fn skipTypeScriptTypeStmt(p: *P, opts: *ParseStatementOptions)! {
            if (opts.is_export) {
                switch (p.lexer.token) {
                    .t_open_brace => {
                        // "export type {foo}"
                        // "export type {foo} from 'bar'"
                        * = p.parseExportClause()?;
                        if (p.lexer.isContextualKeyword("from")) {
                            p.lexer.next()?;
                            * = p.parsePath()?;
                        }
                        p.lexer.expectOrInsertSemicolon()?;
                        return;
                    },
                    .t_asterisk => {
                        // https://github.com/microsoft/TypeScript/pull/52217
                        // - export type * as Foo from 'bar';
                        // - export type Foo from 'bar';
                        p.lexer.next()?;
                        if (p.lexer.isContextualKeyword("as")) {
                            // "export type * as ns from 'path'"
                            p.lexer.next()?;
                            * = p.parseClauseAlias("export")?;
                            p.lexer.next()?;
                        }
                        p.lexer.expectContextualKeyword("from")?;
                        * = p.parsePath()?;
                        p.lexer.expectOrInsertSemicolon()?;
                        return;
                    },
                    else => {},
                }
            }

            const name = p.lexer.identifier;
            p.lexer.expect(.t_identifier)?;

            if (opts.is_module_scope) {
                p.local_type_names.put(p.allocator, name, true) catch unreachable;
            }

            * = p.skipTypeScriptTypeParameters({
                allow_in_out_variance_annotations: true,
                allow_empty_type_parameters: true,
            })?;

            p.lexer.expect(.t_equals)?;
            p.skipTypeScriptType(.lowest)?;
            p.lexer.expectOrInsertSemicolon()?;
        }

        static fn skipTypeScriptInterfaceStmt(p: *P, opts: *ParseStatementOptions)! {
            const name = p.lexer.identifier;
            p.lexer.expect(.t_identifier)?;

            if (opts.is_module_scope) {
                p.local_type_names.put(p.allocator, name, true) catch unreachable;
            }

            * = p.skipTypeScriptTypeParameters({
                allow_in_out_variance_annotations: true,
                allow_empty_type_parameters: true,
            })?;

            if (p.lexer.token == .t_extends) {
                p.lexer.next()?;

                while (true) {
                    p.skipTypeScriptType(.lowest)?;
                    if (p.lexer.token != .t_comma) {
                        break;
                    }
                    p.lexer.next()?;
                }
            }

            if (p.lexer.isContextualKeyword("implements")) {
                p.lexer.next()?;
                while (true) {
                    p.skipTypeScriptType(.lowest)?;
                    if (p.lexer.token != .t_comma) {
                        break;
                    }
                    p.lexer.next()?;
                }
            }

            p.skipTypeScriptObjectType()?;
        }

        static fn skipTypeScriptTypeArguments(p: *P, comptime isInsideJSXElement: bool): bool! {
            p.markTypeScriptOnly();
            switch (p.lexer.token) {
                .t_less_than, .t_less_than_equals, .t_less_than_less_than, .t_less_than_less_than_equals => {},
                else => {
                    return false;
                },
            }

            p.lexer.expectLessThan(false)?;

            while (true) {
                p.skipTypeScriptType(.lowest)?;
                if (p.lexer.token != .t_comma) {
                    break;
                }
                p.lexer.next()?;
            }

            // This type argument list must end with a ">"
            p.lexer.expectGreaterThan(isInsideJSXElement)?;
            return true;
        }

        pub const Backtracking = BacktrackingImpl(P);

        static fn trySkipTypeScriptTypeParametersThenOpenParenWithBacktracking(p: *P): SkipTypeParameterResult {
            return Backtracking.lexerBacktracker(p, Backtracking.skipTypeScriptTypeParametersThenOpenParenWithBacktracking, SkipTypeParameterResult);
        }

        static fn trySkipTypeScriptTypeArgumentsWithBacktracking(p: *P): bool {
            return Backtracking.lexerBacktracker(p, Backtracking.skipTypeScriptTypeArgumentsWithBacktracking, bool);
        }

        static fn trySkipTypeScriptArrowReturnTypeWithBacktracking(p: *P): bool {
            return Backtracking.lexerBacktracker(p, Backtracking.skipTypeScriptArrowReturnTypeWithBacktracking, bool);
        }

        static fn trySkipTypeScriptArrowArgsWithBacktracking(p: *P): bool {
            return Backtracking.lexerBacktracker(p, Backtracking.skipTypeScriptArrowArgsWithBacktracking, bool);
        }

        static fn trySkipTypeScriptConstraintOfInferTypeWithBacktracking(p: *P, flags: TypeScript.SkipTypeOptions.Bitset): bool {
            return Backtracking.lexerBacktrackerWithArgs(p, Backtracking.skipTypeScriptConstraintOfInferTypeWithBacktracking, .{ p, flags }, bool);
        }
    };
}

type string = []const u8;

import bun from "bun";
const assert = bun.assert;
const logger = bun.logger;

const js_ast = bun.ast;
const B = js_ast.B;
const E = js_ast.E;

const Op = js_ast.Op;
const Level = js_ast.Op.Level;

const js_lexer = bun.js_lexer;
const T = js_lexer.T;

const js_parser = bun.js_parser;
const JSXTransformType = js_parser.JSXTransformType;
const ParseStatementOptions = js_parser.ParseStatementOptions;
const Ref = js_parser.Ref;
const SkipTypeParameterResult = js_parser.SkipTypeParameterResult;
const TypeParameterFlag = js_parser.TypeParameterFlag;
const TypeScript = js_parser.TypeScript;
const fs = js_parser.fs;

const List = std.ArrayListUnmanaged;
