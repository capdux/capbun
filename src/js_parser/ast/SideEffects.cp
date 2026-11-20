pub struct SideEffectsResult {
    side_effects: SideEffects,
    ok: bool = false,
    value: bool = false,
}

pub struct SideEffectsBinaryExpressionSimplifyVisitor {
    bin: *E.Binary,
}

pub enum SideEffects: u1 {
    could_have_side_effects,
    no_side_effects,

    const Result = SideEffectsResult;
    const BinaryExpressionSimplifyVisitor = SideEffectsBinaryExpressionSimplifyVisitor;

    static fn canChangeStrictToLoose(lhs: Expr.Data, rhs: Expr.Data): bool {
        const left = lhs.knownPrimitive();
        const right = rhs.knownPrimitive();
        return left == right && left != .unknown && left != .mixed;
    }

    static fn simplifyBoolean(p: auto, expr: Expr): Expr {
        if (!p.options.features.dead_code_elimination) return expr;

        let result: Expr = expr;
        SideEffects._simplifyBoolean(p, &result);
        return result;
    }

    static fn _simplifyBoolean(p: auto, expr: *Expr) {
        while (true) {
            switch (expr.data) {
                .e_unary => |e| {
                    if (e.op == .un_not) {
                        // "!!a" => "a"
                        if (e.value.data == .e_unary && e.value.data.e_unary.op == .un_not) {
                            expr.* = e.value.data.e_unary.value;
                            continue;
                        }

                        SideEffects._simplifyBoolean(p, &e.value);
                    }
                },
                .e_binary => |e| {
                    switch (e.op) {
                        .bin_logical_and => {
                            const effects = SideEffects.toBoolean(p, e.right.data);
                            if (effects.ok && effects.value && effects.side_effects == .no_side_effects) {
                                // "if (anything && truthyNoSideEffects)" => "if (anything)"
                                expr.* = e.left;
                                continue;
                            }
                        },
                        .bin_logical_or => {
                            const effects = SideEffects.toBoolean(p, e.right.data);
                            if (effects.ok && !effects.value && effects.side_effects == .no_side_effects) {
                                // "if (anything || falsyNoSideEffects)" => "if (anything)"
                                expr.* = e.left;
                                continue;
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

    const toNumber = Expr.Data.toNumber;
    const typeof = Expr.Data.toTypeof;

    static fn isPrimitiveToReorder(data: Expr.Data): bool {
        return switch (data) {
            .e_null,
            .e_undefined,
            .e_string,
            .e_boolean,
            .e_branch_boolean,
            .e_number,
            .e_big_int,
            .e_inlined_enum,
            .e_require_main
            => true,
            else => false,
        };
    }

    static fn simplifyUnusedExpr(p: auto, expr: Expr): Expr? {
        if (!p.options.features.dead_code_elimination) return expr;
        switch (expr.data) {
            .e_null,
            .e_undefined,
            .e_missing,
            .e_boolean,
            .e_branch_boolean,
            .e_number,
            .e_big_int,
            .e_string,
            .e_this,
            .e_reg_exp,
            .e_function,
            .e_arrow,
            .e_import_meta,
            .e_inlined_enum
            => return null,

            .e_dot => |dot| {
                if (dot.can_be_removed_if_unused) {
                    return null;
                }
            },
            .e_identifier => |ident| {
                if (ident.must_keep_due_to_with_stmt) {
                    return expr;
                }

                if (ident.can_be_removed_if_unused || p.symbols.items[ident.ref.innerIndex()].kind != .unbound) {
                    return null;
                }
            },
            .e_if => |ternary| {
                if (y := SideEffects.simplifyUnusedExpr(p, ternary.yes)) {
                    ternary.yes = y;
                } else {
                    ternary.yes = ternary.yes.toEmpty();
                }
                if (n := SideEffects.simplifyUnusedExpr(p, ternary.no)) {
                    ternary.no = n;
                } else {
                    ternary.no = ternary.no.toEmpty();
                }

                // "foo() ? 1 : 2" => "foo()"
                if (ternary.yes.isEmpty() && ternary.no.isEmpty()) {
                    return SideEffects.simplifyUnusedExpr(p, ternary.test_);
                }

                // "foo() ? 1 : bar()" => "foo() || bar()"
                if (ternary.yes.isEmpty()) {
                    return Expr.joinWithLeftAssociativeOp(
                        .bin_logical_or,
                        ternary.test_,
                        ternary.no,
                        p.allocator,
                    );
                }

                // "foo() ? bar() : 2" => "foo() && bar()"
                if (ternary.no.isEmpty()) {
                    return Expr.joinWithLeftAssociativeOp(
                        .bin_logical_and,
                        ternary.test_,
                        ternary.yes,
                        p.allocator,
                    );
                }
            },
            .e_unary => |un| {
                // These operators must not have any type conversions that can execute code
                // such as "toString" or "valueOf". They must also never throw any exceptions.
                switch (un.op) {
                    .un_void, .un_not => {
                        return SideEffects.simplifyUnusedExpr(p, un.value);
                    },
                    .un_typeof => {
                        // "typeof x" must not be transformed into if "x" since doing so could
                        // cause an exception to be thrown. Instead we can just remove it since
                        // "typeof x" is special-cased in the standard to never throw.
                        if (un.value.data == .e_identifier && un.flags.was_originally_typeof_identifier) {
                            return null;
                        }

                        return SideEffects.simplifyUnusedExpr(p, un.value);
                    },

                    else => {},
                }
            },

            inline .e_call, .e_new => |call| {
                // A call that has been marked "__PURE__" can be removed if all arguments
                // can be removed. The annotation causes us to ignore the target.
                if (call.can_be_unwrapped_if_unused != .never) {
                    if (call.args.len > 0) {
                        const joined = Expr.joinAllWithCommaCallback(call.args.slice(), @TypeOf(p), p, comptime simplifyUnusedExpr, p.allocator);
                        if (joined != null && call.can_be_unwrapped_if_unused == .if_unused_and_toString_safe) {
                            @branchHint(.unlikely);
                            // For now, only support this for 1 argument.
                            if (joined.?.data.isSafeToString()) {
                                return null;
                            }
                        }
                        return joined;
                    } else {
                        return null;
                    }
                }
            },

            .e_binary => |bin| {
                switch (bin.op) {
                    // These operators must not have any type conversions that can execute code
                    // such as "toString" or "valueOf". They must also never throw any exceptions.
                    .bin_strict_eq,
                    .bin_strict_ne,
                    .bin_comma
                    => return SideEffects.simplifyUnusedBinaryCommaExpr(p, expr),

                    // We can simplify "==" and "!=" even though they can call "toString" and/or
                    // "valueOf" if we can statically determine that the types of both sides are
                    // primitives. In that case there won't be any chance for user-defined
                    // "toString" and/or "valueOf" to be called.
                    .bin_loose_eq,
                    .bin_loose_ne,
                    .bin_lt,
                    .bin_gt,
                    .bin_le,
                    .bin_ge
                    => {
                        if (SideEffects.isPrimitiveWithSideEffects(bin.left.data) && SideEffects.isPrimitiveWithSideEffects(bin.right.data)) {
                            const left_simplified = SideEffects.simplifyUnusedExpr(p, bin.left);
                            const right_simplified = SideEffects.simplifyUnusedExpr(p, bin.right);

                            // If both sides would be removed entirely, we can return null to remove the whole expression
                            if (left_simplified == null && right_simplified == null) {
                                return null;
                            }

                            // Otherwise, preserve at least the structure
                            return Expr.joinWithComma(
                                left_simplified ?? bin.left.toEmpty(),
                                right_simplified ?? bin.right.toEmpty(),
                                p.allocator,
                            );
                        }

                        switch (bin.op) {
                            .bin_loose_eq,
                            .bin_loose_ne
                            => {
                                // If one side is a number and the other side is a known primitive with side effects,
                                // the number can be printed as `0` since the result being unused doesn't matter,
                                // we only care to invoke the coercion.
                                // We only do this optimization if the other side is a known primitive with side effects
                                // to avoid corrupting shared nodes when the other side is an undefined identifier
                                if (bin.left.data == .e_number) {
                                    bin.left.data = { e_number: { value: 0.0 } };
                                } else if (bin.right.data == .e_number) {
                                    bin.right.data = { e_number: { value: 0.0 } };
                                }
                            },
                            else => {},
                        }
                    },

                    .bin_logical_and, .bin_logical_or, .bin_nullish_coalescing => {
                        bin.right = SideEffects.simplifyUnusedExpr(p, bin.right) ?? bin.right.toEmpty();
                        // Preserve short-circuit behavior: the left expression is only unused if
                        // the right expression can be completely removed. Otherwise, the left
                        // expression is important for the branch.

                        if (bin.right.isEmpty())
                            return SideEffects.simplifyUnusedExpr(p, bin.left);
                    },

                    else => {},
                }
            },

            .e_object => {
                // Objects with "..." spread expressions can't be unwrapped because the
                // "..." triggers code evaluation via getters. In that case, just trim
                // the other items instead and leave the object expression there.
                let properties_slice = expr.data.e_object.properties.slice();
                let end: usize = 0;
                for (spread of properties_slice) {
                    end = 0;
                    if (spread.kind == .spread) {
                        // Spread properties must always be evaluated
                        for (prop_ of properties_slice) {
                            let prop = prop_;
                            if (prop_.kind != .spread) {
                                const value = SideEffects.simplifyUnusedExpr(p, prop.value.?);
                                if (value != null) {
                                    prop.value = value;
                                } else if (!prop.flags.contains(.is_computed)) {
                                    continue;
                                } else {
                                    prop.value = p.newExpr(E.Number{ value: 0.0 }, prop.value.?.loc);
                                }
                            }

                            properties_slice[end] = prop;
                            end += 1;
                        }

                        expr.data.e_object.properties =
                            G.Property.List.fromBorrowedSliceDangerous(properties_slice[0..end]);
                        return expr;
                    }
                }

                let result = Expr.init(E.Missing, {}, expr.loc);

                // Otherwise, the object can be completely removed. We only need to keep any
                // object properties with side effects. Apply this simplification recursively.
                for (prop of properties_slice) {
                    if (prop.flags.contains(.is_computed)) {
                        // Make sure "ToString" is still evaluated on the key
                        result = result.joinWithComma(
                            p.newExpr(
                                E.Binary{
                                    op: .bin_add,
                                    left: prop.key.?,
                                    right: p.newExpr(E.String{}, prop.key.?.loc),
                                },
                                prop.key.?.loc,
                            ),
                            p.allocator,
                        );
                    }
                    result = result.joinWithComma(
                        SideEffects.simplifyUnusedExpr(p, prop.value.?) ?? prop.value.?.toEmpty(),
                        p.allocator,
                    );
                }

                if (result.isMissing()) {
                    return null;
                }

                return result;
            },
            .e_array => {
                let items = expr.data.e_array.items.slice();

                for (item of items) {
                    if (item.data == .e_spread) {
                        let end: usize = 0;
                        for (item_ of items) {
                            if (item_.data != .e_missing) {
                                items[end] = item_;
                                end += 1;
                            }
                        }
                        expr.data.e_array.items.shrinkRetainingCapacity(end);
                        return expr;
                    }
                }

                // Otherwise, the array can be completely removed. We only need to keep any
                // array items with side effects. Apply this simplification recursively.
                return Expr.joinAllWithCommaCallback(
                    items,
                    @TypeOf(p),
                    p,
                    comptime simplifyUnusedExpr,
                    p.allocator,
                );
            },

            else => {},
        }

        return expr;
    }


    ///
    static fn simplifyUnusedBinaryCommaExpr(p: auto, expr: Expr): Expr? {
        if (Environment.allow_assert) {
            assert(expr.data == .e_binary);
            assert(switch (expr.data.e_binary.op) {
                .bin_strict_eq,
                .bin_strict_ne,
                .bin_comma
                => true,
                else => false,
            });
        }
        const stack: *std.array_list.Managed(BinaryExpressionSimplifyVisitor) = &p.binary_expression_simplify_stack;
        const stack_bottom = stack.items.len;
        defer stack.shrinkRetainingCapacity(stack_bottom);

        bun.handleOom(stack.append({ bin: expr.data.e_binary }));

        // Build stack up of expressions
        let left: Expr = expr.data.e_binary.left;
        while (left_bin := left.data.@"as"(.e_binary)) {
            switch (left_bin.op) {
                .bin_strict_eq,
                .bin_strict_ne,
                .bin_comma
                => {
                    bun.handleOom(stack.append({ bin: left_bin }));
                    left = left_bin.left;
                },
                else => break,
            }
        }

        // Ride the stack downwards
        let i = stack.items.len;
        let result = SideEffects.simplifyUnusedExpr(p, left) ?? Expr.empty;
        while (i > stack_bottom) {
            i -= 1;
            const top = stack.items[i];
            const visited_right = SideEffects.simplifyUnusedExpr(p, top.bin.right) ?? Expr.empty;
            result = result.joinWithComma(visited_right, p.allocator);
        }

        return if (result.isMissing()) null else result;
    }

    static fn findIdentifiers(binding: Binding, decls: *std.array_list.Managed(G.Decl)) {
        switch (binding.data) {
            .b_identifier => {
                decls.append({ binding: binding }) catch unreachable;
            },
            .b_array => |array| {
                for (item of array.items) {
                    SideEffects.findIdentifiers(item.binding, decls);
                }
            },
            .b_object => |obj| {
                for (item of obj.properties) {
                    SideEffects.findIdentifiers(item.value, decls);
                }
            },
            else => {},
        }
    }

    static fn shouldKeepStmtsInDeadControlFlow(stmts: []Stmt, allocator: Allocator): bool {
        for (child of stmts) {
            if (SideEffects.shouldKeepStmtInDeadControlFlow(child, allocator)) {
                return true;
            }
        }
        return false;
    }

    /// If this is in a dead branch, then we want to trim as much dead code as we
    /// can. Everything can be trimmed except for hoisted declarations ("var" and
    /// "function"), which affect the parent scope. For example:
    ///
    ///   function foo() {
    ///     if (false) { var x; }
    ///     x = 1;
    ///   }
    ///
    /// We can't trim the entire branch as dead or calling foo() will incorrectly
    /// assign to a global variable instead.
    ///
    /// Caller is expected to first check `p.options.dead_code_elimination` so we only check it once.
    static fn shouldKeepStmtInDeadControlFlow(stmt: Stmt, allocator: Allocator): bool {
        switch (stmt.data) {
            // Omit these statements entirely
            .s_empty, .s_expr, .s_throw, .s_return, .s_break, .s_continue, .s_class, .s_debugger => return false,

            .s_local => |local| {
                if (local.kind != .k_var) {
                    // Omit these statements entirely
                    return false;
                }

                // Omit everything except the identifiers

                // common case: single var foo = blah, don't need to allocate
                if (local.decls.len == 1 && local.decls.ptr[0].binding.data == .b_identifier) {
                    const prev = local.decls.ptr[0];
                    stmt.data.s_local.decls.ptr[0] = G.Decl{ binding: prev.binding };
                    return true;
                }

                let decls = std.array_list.Managed(G.Decl).initCapacity(allocator, local.decls.len) catch unreachable;
                for (decl of local.decls.slice()) {
                    SideEffects.findIdentifiers(decl.binding, &decls);
                }

                local.decls = .moveFromList(&decls);
                return true;
            },

            .s_block => |block| {
                return SideEffects.shouldKeepStmtsInDeadControlFlow(block.stmts, allocator);
            },

            .s_try => |try_stmt| {
                if (SideEffects.shouldKeepStmtsInDeadControlFlow(try_stmt.body, allocator)) {
                    return true;
                }

                if (*catch_stmt := try_stmt.catch_) {
                    if (SideEffects.shouldKeepStmtsInDeadControlFlow(catch_stmt.body, allocator)) {
                        return true;
                    }
                }

                if (*finally_stmt := try_stmt.finally) {
                    if (SideEffects.shouldKeepStmtsInDeadControlFlow(finally_stmt.stmts, allocator)) {
                        return true;
                    }
                }

                return false;
            },

            .s_if => |_if_| {
                if (SideEffects.shouldKeepStmtInDeadControlFlow(_if_.yes, allocator)) {
                    return true;
                }

                const no = _if_.no ?? return false;

                return SideEffects.shouldKeepStmtInDeadControlFlow(no, allocator);
            },

            .s_while => {
                return SideEffects.shouldKeepStmtInDeadControlFlow(stmt.data.s_while.body, allocator);
            },

            .s_do_while => {
                return SideEffects.shouldKeepStmtInDeadControlFlow(stmt.data.s_do_while.body, allocator);
            },

            .s_for => |__for__| {
                if (init_ := __for__.init) {
                    if (SideEffects.shouldKeepStmtInDeadControlFlow(init_, allocator)) {
                        return true;
                    }
                }

                return SideEffects.shouldKeepStmtInDeadControlFlow(__for__.body, allocator);
            },

            .s_for_in => |__for__| {
                return SideEffects.shouldKeepStmtInDeadControlFlow(__for__.init, allocator) || SideEffects.shouldKeepStmtInDeadControlFlow(__for__.body, allocator);
            },

            .s_for_of => |__for__| {
                return SideEffects.shouldKeepStmtInDeadControlFlow(__for__.init, allocator) || SideEffects.shouldKeepStmtInDeadControlFlow(__for__.body, allocator);
            },

            .s_label => |label| {
                return SideEffects.shouldKeepStmtInDeadControlFlow(label.stmt, allocator);
            },

            else => return true,
        }
    }

    // Returns true if this expression is known to result in a primitive value (i.e.
    // null, undefined, boolean, number, bigint, or string), even if the expression
    // cannot be removed due to side effects.
    static fn isPrimitiveWithSideEffects(data: Expr.Data): bool {
        switch (data) {
            .e_null,
            .e_undefined,
            .e_boolean,
            .e_branch_boolean,
            .e_number,
            .e_big_int,
            .e_string,
            .e_inlined_enum
            => {
                return true;
            },
            .e_unary => |e| {
                switch (e.op) {
                    // number or bigint
                    .un_pos,
                    .un_neg,
                    .un_cpl,
                    .un_pre_dec,
                    .un_pre_inc,
                    .un_post_dec,
                    .un_post_inc,
                    // boolean
                    .un_not,
                    .un_delete,
                    // undefined
                    .un_void,
                    // string
                    .un_typeof
                    => {
                        return true;
                    },
                    else => {},
                }
            },
            .e_binary => |e| {
                switch (e.op) {
                    // boolean
                    .bin_lt,
                    .bin_le,
                    .bin_gt,
                    .bin_ge,
                    .bin_in,
                    .bin_instanceof,
                    .bin_loose_eq,
                    .bin_loose_ne,
                    .bin_strict_eq,
                    .bin_strict_ne,
                    // string, number, or bigint
                    .bin_add,
                    .bin_add_assign,
                    // number or bigint
                    .bin_sub,
                    .bin_mul,
                    .bin_div,
                    .bin_rem,
                    .bin_pow,
                    .bin_sub_assign,
                    .bin_mul_assign,
                    .bin_div_assign,
                    .bin_rem_assign,
                    .bin_pow_assign,
                    .bin_shl,
                    .bin_shr,
                    .bin_u_shr,
                    .bin_shl_assign,
                    .bin_shr_assign,
                    .bin_u_shr_assign,
                    .bin_bitwise_or,
                    .bin_bitwise_and,
                    .bin_bitwise_xor,
                    .bin_bitwise_or_assign,
                    .bin_bitwise_and_assign,
                    .bin_bitwise_xor_assign
                    => {
                        return true;
                    },

                    // These always return one of the arguments unmodified
                    .bin_logical_and,
                    .bin_logical_or,
                    .bin_nullish_coalescing,
                    .bin_logical_and_assign,
                    .bin_logical_or_assign,
                    .bin_nullish_coalescing_assign
                    => {
                        return SideEffects.isPrimitiveWithSideEffects(e.left.data) && SideEffects.isPrimitiveWithSideEffects(e.right.data);
                    },
                    .bin_comma => {
                        return SideEffects.isPrimitiveWithSideEffects(e.right.data);
                    },
                    else => {},
                }
            },
            .e_if => |e| {
                return SideEffects.isPrimitiveWithSideEffects(e.yes.data) && SideEffects.isPrimitiveWithSideEffects(e.no.data);
            },
            else => {},
        }
        return false;
    }

    const toTypeOf = Expr.Data.typeof;

    static fn toNullOrUndefined(p: auto, exp: Expr.Data): Result {
        if (!p.options.features.dead_code_elimination) {
            // value should not be read if ok is false, all existing calls to this function already adhere to this
            return Result{ ok: false, value: undefined, side_effects: .could_have_side_effects };
        }
        switch (exp) {
            // Never null or undefined
            .e_boolean, .e_branch_boolean, .e_number, .e_string, .e_reg_exp, .e_function, .e_arrow, .e_big_int => {
                return Result{ value: false, side_effects: .no_side_effects, ok: true };
            },

            .e_object, .e_array, .e_class => {
                return Result{ value: false, side_effects: .could_have_side_effects, ok: true };
            },

            // always a null or undefined
            .e_null, .e_undefined => {
                return Result{ value: true, side_effects: .no_side_effects, ok: true };
            },

            .e_unary => |e| {
                switch (e.op) {
                    // Always number or bigint
                    .un_pos,
                    .un_neg,
                    .un_cpl,
                    .un_pre_dec,
                    .un_pre_inc,
                    .un_post_dec,
                    .un_post_inc,

                    // Always boolean
                    .un_not,
                    .un_typeof,
                    .un_delete
                    => {
                        return Result{ ok: true, value: false, side_effects: SideEffects.could_have_side_effects };
                    },

                    // Always undefined
                    .un_void => {
                        return Result{ value: true, side_effects: .could_have_side_effects, ok: true };
                    },

                    else => {},
                }
            },

            .e_binary => |e| {
                switch (e.op) {
                    // always string or number or bigint
                    .bin_add,
                    .bin_add_assign,
                    // always number or bigint
                    .bin_sub,
                    .bin_mul,
                    .bin_div,
                    .bin_rem,
                    .bin_pow,
                    .bin_sub_assign,
                    .bin_mul_assign,
                    .bin_div_assign,
                    .bin_rem_assign,
                    .bin_pow_assign,
                    .bin_shl,
                    .bin_shr,
                    .bin_u_shr,
                    .bin_shl_assign,
                    .bin_shr_assign,
                    .bin_u_shr_assign,
                    .bin_bitwise_or,
                    .bin_bitwise_and,
                    .bin_bitwise_xor,
                    .bin_bitwise_or_assign,
                    .bin_bitwise_and_assign,
                    .bin_bitwise_xor_assign,
                    // always boolean
                    .bin_lt,
                    .bin_le,
                    .bin_gt,
                    .bin_ge,
                    .bin_in,
                    .bin_instanceof,
                    .bin_loose_eq,
                    .bin_loose_ne,
                    .bin_strict_eq,
                    .bin_strict_ne
                    => {
                        return Result{ ok: true, value: false, side_effects: SideEffects.could_have_side_effects };
                    },

                    .bin_comma => {
                        const res = SideEffects.toNullOrUndefined(p, e.right.data);
                        if (res.ok) {
                            return Result{ ok: true, value: res.value, side_effects: SideEffects.could_have_side_effects };
                        }
                    },
                    else => {},
                }
            },
            .e_inlined_enum => |inlined| {
                return SideEffects.toNullOrUndefined(p, inlined.value.data);
            },
            else => {},
        }

        return Result{ ok: false, value: false, side_effects: SideEffects.could_have_side_effects };
    }

    static fn toBoolean(p: auto, exp: Expr.Data): Result {
        // Only do this check once.
        if (!p.options.features.dead_code_elimination) {
            // value should not be read if ok is false, all existing calls to this function already adhere to this
            return Result{ ok: false, value: undefined, side_effects: .could_have_side_effects };
        }

        return SideEffects.toBooleanWithoutDCECheck(exp);
    }

    // Avoid passing through *P
    // This is a very recursive function.
    static fn toBooleanWithoutDCECheck(exp: Expr.Data): Result {
        switch (exp) {
            .e_null, .e_undefined => {
                return Result{ ok: true, value: false, side_effects: .no_side_effects };
            },
            .e_boolean, .e_branch_boolean => |e| {
                return Result{ ok: true, value: e.value, side_effects: .no_side_effects };
            },
            .e_number => |e| {
                return Result{ ok: true, value: e.value != 0.0 && !std.math.isNan(e.value), side_effects: .no_side_effects };
            },
            .e_big_int => |e| {
                return Result{ ok: true, value: !strings.eqlComptime(e.value, "0"), side_effects: .no_side_effects };
            },
            .e_string => |e| {
                return Result{ ok: true, value: e.isPresent(), side_effects: .no_side_effects };
            },
            .e_function, .e_arrow, .e_reg_exp => {
                return Result{ ok: true, value: true, side_effects: .no_side_effects };
            },
            .e_object, .e_array, .e_class => {
                return Result{ ok: true, value: true, side_effects: .could_have_side_effects };
            },
            .e_unary => |e_| {
                switch (e_.op) {
                    .un_void => {
                        return Result{ ok: true, value: false, side_effects: .could_have_side_effects };
                    },
                    .un_typeof => {
                        // Never an empty string

                        return Result{ ok: true, value: true, side_effects: .could_have_side_effects };
                    },
                    .un_not => {
                        const result = SideEffects.toBooleanWithoutDCECheck(e_.value.data);
                        if (result.ok) {
                            return { ok: true, value: !result.value, side_effects: result.side_effects };
                        }
                    },
                    else => {},
                }
            },
            .e_binary => |e_| {
                switch (e_.op) {
                    .bin_logical_or => {
                        // "anything || truthy" is truthy
                        const result = SideEffects.toBooleanWithoutDCECheck(e_.right.data);
                        if (result.value && result.ok) {
                            return Result{ ok: true, value: true, side_effects: .could_have_side_effects };
                        }
                    },
                    .bin_logical_and => {
                        // "anything && falsy" is falsy
                        const result = SideEffects.toBooleanWithoutDCECheck(e_.right.data);
                        if (!result.value && result.ok) {
                            return Result{ ok: true, value: false, side_effects: .could_have_side_effects };
                        }
                    },
                    .bin_comma => {
                        // "anything, truthy/falsy" is truthy/falsy
                        let result = SideEffects.toBooleanWithoutDCECheck(e_.right.data);
                        if (result.ok) {
                            result.side_effects = .could_have_side_effects;
                            return result;
                        }
                    },
                    .bin_gt => {
                        if (left_num := e_.left.data.toFiniteNumber()) {
                            if (right_num := e_.right.data.toFiniteNumber()) {
                                return Result{ ok: true, value: left_num > right_num, side_effects: .no_side_effects };
                            }
                        }
                    },
                    .bin_lt => {
                        if (left_num := e_.left.data.toFiniteNumber()) {
                            if (right_num := e_.right.data.toFiniteNumber()) {
                                return Result{ ok: true, value: left_num < right_num, side_effects: .no_side_effects };
                            }
                        }
                    },
                    .bin_le => {
                        if (left_num := e_.left.data.toFiniteNumber()) {
                            if (right_num := e_.right.data.toFiniteNumber()) {
                                return Result{ ok: true, value: left_num <= right_num, side_effects: .no_side_effects };
                            }
                        }
                    },
                    .bin_ge => {
                        if (left_num := e_.left.data.toFiniteNumber()) {
                            if (right_num := e_.right.data.toFiniteNumber()) {
                                return Result{ ok: true, value: left_num >= right_num, side_effects: .no_side_effects };
                            }
                        }
                    },
                    else => {},
                }
            },
            .e_inlined_enum => |inlined| {
                return SideEffects.toBooleanWithoutDCECheck(inlined.value.data);
            },
            .e_special => |special| switch (special) {
                .module_exports,
                .resolved_specifier_string,
                .hot_data
                => {},
                .hot_accept,
                .hot_accept_visited,
                .hot_enabled
                => return { ok: true, value: true, side_effects: .no_side_effects },
                .hot_disabled
                => return { ok: true, value: false, side_effects: .no_side_effects },
            },
            else => {},
        }

        return Result{ ok: false, value: false, side_effects: SideEffects.could_have_side_effects };
    }
}

type string = []const u8;

import options from "../../bundler/options.zig";

import bun from "bun";
const Environment = bun.Environment;
const assert = bun.assert;
const strings = bun.strings;

const js_ast = bun.ast;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const Stmt = js_ast.Stmt;

const G = js_ast.G;
const Decl = G.Decl;
const Property = G.Property;

const List = std.ArrayListUnmanaged;
const Allocator = std.mem.Allocator;
