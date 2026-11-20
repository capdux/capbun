/// Lowering for TC39 standard ES decorators.
/// Extracted from P.zig to reduce duplication via shared helpers.
struct PrivateLoweredInfo {
    storage_ref: Ref,
    method_fn_ref: Ref? = null,
    getter_fn_ref: Ref? = null,
    setter_fn_ref: Ref? = null,
    accessor_desc_ref: Ref? = null,
}

const PrivateLoweredMap = std.AutoHashMapUnmanaged(u32, PrivateLoweredInfo);

struct StdDecModeExpr {
    cls: *G.Class,
    loc: logger.Loc,
    name_from_context: ([]const u8)? = null,
}

union(enum) StdDecMode {
    stmt: void,
    expr: StdDecModeExpr,
}

struct FieldInitEntry {
    prop: Property,
    is_private: bool,
    is_accessor: bool,
}

enum StaticElementKind {
    block,
    field_or_accessor,
}

struct StaticElement {
    kind: StaticElementKind,
    index: usize,
}

struct RewriteReplaceRef {
    old: Ref,
    new_ref: Ref,
}

struct RewriteReplaceThis {
    sym_ref: Ref,
    loc: logger.Loc,
}

union(enum) RewriteKind {
    replace_ref: RewriteReplaceRef,
    replace_this: RewriteReplaceThis,
}

pub fn LowerDecorators(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    return struct {
        const P = js_parser.NewParser_(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);

        // ── Expression builder helpers ───────────────────────

        /// recordUsage + E.Identifier in one call.
        static fn useRef(p: *P, sym_ref: Ref, l: logger.Loc): Expr {
            p.recordUsage(sym_ref);
            return p.newExpr(E.Identifier{ ref: sym_ref }, l);
        }

        /// Allocate args + callRuntime in one call.
        static fn callRt(p: *P, l: logger.Loc, comptime name: []const u8, args: []const Expr): Expr {
            const a = bun.handleOom(p.allocator.alloc(Expr, args.len));
            @memcpy(a, args);
            return p.callRuntime(l, name, a);
        }

        /// newSymbol + scope.generated.append in one call.
        static fn newSym(p: *P, kind: Symbol.Kind, name: []const u8): Ref {
            const sym_ref = p.newSymbol(kind, name) catch unreachable;
            bun.handleOom(p.current_scope.generated.append(p.allocator, sym_ref));
            return sym_ref;
        }

        /// Single var declaration statement.
        static fn varDecl(p: *P, sym_ref: Ref, value: Expr?, l: logger.Loc): Stmt {
            let decls = bun.handleOom(p.allocator.alloc(G.Decl, 1));
            decls[0] = { binding: p.b(B.Identifier{ ref: sym_ref }, l), value: value };
            return p.s(S.Local{ decls: Decl.List.fromOwnedSlice(decls) }, l);
        }

        /// Two-variable declaration statement.
        static fn varDecl2(p: *P, r1: Ref, v1: Expr?, r2: Ref, v2: Expr?, l: logger.Loc): Stmt {
            let decls = bun.handleOom(p.allocator.alloc(G.Decl, 2));
            decls[0] = { binding: p.b(B.Identifier{ ref: r1 }, l), value: v1 };
            decls[1] = { binding: p.b(B.Identifier{ ref: r2 }, l), value: v2 };
            return p.s(S.Local{ decls: Decl.List.fromOwnedSlice(decls) }, l);
        }

        /// recordUsage + Expr.assign.
        static fn assignTo(p: *P, sym_ref: Ref, value: Expr, l: logger.Loc): Expr {
            p.recordUsage(sym_ref);
            return Expr.assign(p.newExpr(E.Identifier{ ref: sym_ref }, l), value);
        }

        /// new WeakMap() expression.
        static fn newWeakMapExpr(p: *P, l: logger.Loc): Expr {
            return p.newExpr(E.New{
                target: p.newExpr(E.Identifier{ ref: (p.findSymbol(l, "WeakMap") catch unreachable).ref }, l),
                args: ExprNodeList.empty,
                close_parens_loc: l,
            }, l);
        }

        /// new WeakSet() expression.
        static fn newWeakSetExpr(p: *P, l: logger.Loc): Expr {
            return p.newExpr(E.New{
                target: p.newExpr(E.Identifier{ ref: (p.findSymbol(l, "WeakSet") catch unreachable).ref }, l),
                args: ExprNodeList.empty,
                close_parens_loc: l,
            }, l);
        }

        /// Create a static block property from a single expression.
        static fn makeStaticBlock(p: *P, expr: Expr, l: logger.Loc): Property {
            const stmts = bun.handleOom(p.allocator.alloc(Stmt, 1));
            stmts[0] = p.s(S.SExpr{ value: expr }, l);
            const sb = bun.handleOom(p.allocator.create(G.ClassStaticBlock));
            sb.* = { loc: l, stmts: bun.BabyList(Stmt).fromOwnedSlice(stmts) };
            return { kind: .class_static_block, class_static_block: sb };
        }

        /// Build property access: target.name or target[key].
        static fn memberTarget(p: *P, target_expr: Expr, prop: Property): Expr {
            const key_expr = prop.key.?;
            if (prop.flags.contains(.is_computed) || key_expr.data == .e_number) {
                return p.newExpr(E.Index{ target: target_expr, index: key_expr }, key_expr.loc);
            } else if (key_expr.data == .e_string) {
                return p.newExpr(E.Dot{
                    target: target_expr,
                    name: key_expr.data.e_string.data,
                    name_loc: key_expr.loc,
                }, key_expr.loc);
            } else {
                return p.newExpr(E.Index{ target: target_expr, index: key_expr }, key_expr.loc);
            }
        }

        static fn initFlag(idx: usize): f64 {
            return @floatFromInt((4 + 2 * idx) << 1);
        }

        static fn extraInitFlag(idx: usize): f64 {
            return @floatFromInt(((5 + 2 * idx) << 1) | 1);
        }

        /// Emit __privateAdd for a given storage ref. Appends to constructor or static blocks.
        static fn emitPrivateAdd(
            p: *P,
            is_static: bool,
            storage_ref: Ref,
            value: Expr?,
            loc: logger.Loc,
            constructor_inject: *ListManaged(Stmt),
            static_blocks: *ListManaged(Property),
        ) {
            const target = p.newExpr(E.This{}, loc);
            if (v := value) {
                const call = callRt(p, loc, "__privateAdd", &[ target, useRef(p, storage_ref, loc), v ]);
                if (is_static) {
                    static_blocks.append(makeStaticBlock(p, call, loc)) catch unreachable;
                } else {
                    constructor_inject.append(p.s(S.SExpr{ value: call }, loc)) catch unreachable;
                }
            } else {
                const call = callRt(p, loc, "__privateAdd", &[ target, useRef(p, storage_ref, loc) ]);
                if (is_static) {
                    static_blocks.append(makeStaticBlock(p, call, loc)) catch unreachable;
                } else {
                    constructor_inject.append(p.s(S.SExpr{ value: call }, loc)) catch unreachable;
                }
            }
        }

        /// Get the method kind code (1=method, 2=getter, 3=setter).
        static fn methodKind(prop: *const Property): u8 {
            return switch (prop.kind) {
                .get => 2,
                .set => 3,
                else => 1,
            };
        }

        /// Get fn variable suffix for a given kind code.
        static fn fnSuffix(k: u8): []const u8 {
            return if (k == 2) "_get" else if (k == 3) "_set" else "_fn";
        }

        // ── Generic tree rewriter ────────────────────────────

        static fn rewriteExpr(p: *P, expr: *Expr, kind: RewriteKind) {
            switch (kind) {
                .replace_ref => |r| {
                    if (expr.data == .e_identifier && expr.data.e_identifier.ref.eql(r.old)) {
                        p.recordUsage(r.new_ref);
                        expr.data = { e_identifier: { ref: r.new_ref } };
                        return;
                    }
                },
                .replace_this => |r| {
                    if (expr.data == .e_this) {
                        expr.* = useRef(p, r.sym_ref, r.loc);
                        return;
                    }
                },
            }
            switch (expr.data) {
                .e_binary => |e| {
                    rewriteExpr(p, &e.left, kind);
                    rewriteExpr(p, &e.right, kind);
                },
                .e_call => |e| {
                    rewriteExpr(p, &e.target, kind);
                    const call_args = e.args.slice();
                    for (idx of 0..call_args.len) rewriteExpr(p, &call_args[@as(usize, @intCast(idx))], kind);
                },
                .e_new => |e| {
                    rewriteExpr(p, &e.target, kind);
                    const new_args = e.args.slice();
                    for (idx of 0..new_args.len) rewriteExpr(p, &new_args[@as(usize, @intCast(idx))], kind);
                },
                .e_index => |e| {
                    rewriteExpr(p, &e.target, kind);
                    rewriteExpr(p, &e.index, kind);
                },
                .e_dot => |e| rewriteExpr(p, &e.target, kind),
                .e_spread => |e| rewriteExpr(p, &e.value, kind),
                .e_unary => |e| rewriteExpr(p, &e.value, kind),
                .e_if => |e| {
                    rewriteExpr(p, &e.test_, kind);
                    rewriteExpr(p, &e.yes, kind);
                    rewriteExpr(p, &e.no, kind);
                },
                .e_array => |e| {
                    const items = e.items.slice();
                    for (idx of 0..items.len) rewriteExpr(p, &items[idx], kind);
                },
                .e_object => |e| {
                    for (*prop of e.properties.slice()) {
                        if (prop.value != null) rewriteExpr(p, &prop.value.?, kind);
                        if (prop.initializer != null) rewriteExpr(p, &prop.initializer.?, kind);
                    }
                },
                .e_template => |e| {
                    if (e.tag != null) rewriteExpr(p, &e.tag.?, kind);
                    for (*part of e.parts) rewriteExpr(p, &part.value, kind);
                },
                .e_arrow => |e| rewriteStmts(p, e.body.stmts, kind),
                .e_function => |e| {
                    switch (kind) {
                        .replace_this => {},
                        .replace_ref => {
                            if (e.func.body.stmts.len > 0)
                                rewriteStmts(p, e.func.body.stmts, kind);
                        },
                    }
                },
                .e_class => {},
                else => {},
            }
        }

        static fn rewriteStmts(p: *P, stmts: []Stmt, kind: RewriteKind) {
            for (*cur_stmt of stmts) {
                switch (cur_stmt.data) {
                    .s_expr => |sexpr| {
                        let val = sexpr.value;
                        rewriteExpr(p, &val, kind);
                        cur_stmt = p.s(S.SExpr{
                            value: val,
                            does_not_affect_tree_shaking: sexpr.does_not_affect_tree_shaking,
                        }, cur_stmt.loc);
                    },
                    .s_local => |local| {
                        for (*decl of local.decls.slice()) {
                            if (decl.value != null) rewriteExpr(p, &decl.value.?, kind);
                        }
                    },
                    .s_return => |ret| {
                        if (ret.value != null) rewriteExpr(p, &ret.value.?, kind);
                    },
                    .s_throw => |data| rewriteExpr(p, &data.value, kind),
                    .s_if => |data| {
                        rewriteExpr(p, &data.test_, kind);
                        rewriteStmts(p, (&data.yes)[0..1], kind);
                        if (data.no != null) rewriteStmts(p, (&data.no.?)[0..1], kind);
                    },
                    .s_block => |data| rewriteStmts(p, data.stmts, kind),
                    .s_for => |data| {
                        if (data.init != null) rewriteStmts(p, (&data.init.?)[0..1], kind);
                        if (*t := data.test_) rewriteExpr(p, t, kind);
                        if (*u := data.update) rewriteExpr(p, u, kind);
                        rewriteStmts(p, (&data.body)[0..1], kind);
                    },
                    .s_for_in => |data| {
                        rewriteExpr(p, &data.value, kind);
                        rewriteStmts(p, (&data.body)[0..1], kind);
                    },
                    .s_for_of => |data| {
                        rewriteExpr(p, &data.value, kind);
                        rewriteStmts(p, (&data.body)[0..1], kind);
                    },
                    .s_while => |data| {
                        rewriteExpr(p, &data.test_, kind);
                        rewriteStmts(p, (&data.body)[0..1], kind);
                    },
                    .s_do_while => |data| {
                        rewriteExpr(p, &data.test_, kind);
                        rewriteStmts(p, (&data.body)[0..1], kind);
                    },
                    .s_switch => |data| {
                        rewriteExpr(p, &data.test_, kind);
                        for (*case of data.cases) {
                            if (*v := case.value) rewriteExpr(p, v, kind);
                            rewriteStmts(p, case.body, kind);
                        }
                    },
                    .s_try => |data| {
                        rewriteStmts(p, data.body, kind);
                        if (c := data.catch_) rewriteStmts(p, c.body, kind);
                        if (f := data.finally) rewriteStmts(p, f.stmts, kind);
                    },
                    .s_label => |data| rewriteStmts(p, (&data.stmt)[0..1], kind),
                    .s_with => |data| {
                        rewriteExpr(p, &data.value, kind);
                        rewriteStmts(p, (&data.body)[0..1], kind);
                    },
                    else => {},
                }
            }
        }

        // ── Private access rewriting ─────────────────────────

        static fn privateGetExpr(p: *P, obj: Expr, info: PrivateLoweredInfo, l: logger.Loc): Expr {
            if (desc_ref := info.accessor_desc_ref) {
                return callRt(p, l, "__privateGet", &[
                    obj,
                    useRef(p, info.storage_ref, l),
                    p.newExpr(E.Dot{ target: useRef(p, desc_ref, l), name: "get", name_loc: l }, l),
                ]);
            } else if (fn_ref := info.getter_fn_ref) {
                return callRt(p, l, "__privateGet", &[ obj, useRef(p, info.storage_ref, l), useRef(p, fn_ref, l) ]);
            } else if (fn_ref := info.method_fn_ref) {
                return callRt(p, l, "__privateMethod", &[ obj, useRef(p, info.storage_ref, l), useRef(p, fn_ref, l) ]);
            } else {
                return callRt(p, l, "__privateGet", &[ obj, useRef(p, info.storage_ref, l) ]);
            }
        }

        static fn privateSetExpr(p: *P, obj: Expr, info: PrivateLoweredInfo, val: Expr, l: logger.Loc): Expr {
            if (desc_ref := info.accessor_desc_ref) {
                return callRt(p, l, "__privateSet", &[
                    obj,
                    useRef(p, info.storage_ref, l),
                    val,
                    p.newExpr(E.Dot{ target: useRef(p, desc_ref, l), name: "set", name_loc: l }, l),
                ]);
            } else if (fn_ref := info.setter_fn_ref) {
                return callRt(p, l, "__privateSet", &[ obj, useRef(p, info.storage_ref, l), val, useRef(p, fn_ref, l) ]);
            } else {
                return callRt(p, l, "__privateSet", &[ obj, useRef(p, info.storage_ref, l), val ]);
            }
        }

        static fn rewritePrivateAccessesInExpr(p: *P, expr: *Expr, map: *const PrivateLoweredMap) {
            switch (expr.data) {
                .e_index => |e| {
                    rewritePrivateAccessesInExpr(p, &e.target, map);
                    if (e.index.data == .e_private_identifier) {
                        if (info := map.get(e.index.data.e_private_identifier.ref.innerIndex())) {
                            expr.* = privateGetExpr(p, e.target, info, expr.loc);
                            return;
                        }
                    }
                    rewritePrivateAccessesInExpr(p, &e.index, map);
                },
                .e_binary => |e| {
                    if (e.op == .bin_assign && e.left.data == .e_index) {
                        if (e.left.data.e_index.index.data == .e_private_identifier) {
                            if (info := map.get(e.left.data.e_index.index.data.e_private_identifier.ref.innerIndex())) {
                                rewritePrivateAccessesInExpr(p, &e.left.data.e_index.target, map);
                                rewritePrivateAccessesInExpr(p, &e.right, map);
                                expr.* = privateSetExpr(p, e.left.data.e_index.target, info, e.right, expr.loc);
                                return;
                            }
                        }
                    }
                    if (e.op == .bin_in && e.left.data == .e_private_identifier) {
                        if (info := map.get(e.left.data.e_private_identifier.ref.innerIndex())) {
                            rewritePrivateAccessesInExpr(p, &e.right, map);
                            expr.* = callRt(p, expr.loc, "__privateIn", &[
                                useRef(p, info.storage_ref, expr.loc),
                                e.right,
                            ]);
                            return;
                        }
                    }
                    rewritePrivateAccessesInExpr(p, &e.left, map);
                    rewritePrivateAccessesInExpr(p, &e.right, map);
                },
                .e_call => |e| {
                    if (e.target.data == .e_index) {
                        if (e.target.data.e_index.index.data == .e_private_identifier) {
                            if (info := map.get(e.target.data.e_index.index.data.e_private_identifier.ref.innerIndex())) {
                                rewritePrivateAccessesInExpr(p, &e.target.data.e_index.target, map);
                                const obj_expr = e.target.data.e_index.target;
                                const private_access = privateGetExpr(p, obj_expr, info, expr.loc);
                                const call_target = p.newExpr(E.Dot{
                                    target: private_access,
                                    name: "call",
                                    name_loc: expr.loc,
                                }, expr.loc);
                                const orig_args = e.args.slice();
                                const new_args = bun.handleOom(p.allocator.alloc(Expr, 1 + orig_args.len));
                                new_args[0] = obj_expr;
                                let ai: usize = 0;
                                while (ai < orig_args.len) : (ai += 1) {
                                    const arg = &orig_args[ai];
                                    rewritePrivateAccessesInExpr(p, arg, map);
                                    new_args[1 + ai] = arg.*;
                                }
                                e.target = call_target;
                                e.args = ExprNodeList.fromOwnedSlice(new_args);
                                return;
                            }
                        }
                    }
                    rewritePrivateAccessesInExpr(p, &e.target, map);
                    for (idx of 0..e.args.len) rewritePrivateAccessesInExpr(p, &e.args.slice()[idx], map);
                },
                .e_unary => |e| rewritePrivateAccessesInExpr(p, &e.value, map),
                .e_dot => |e| rewritePrivateAccessesInExpr(p, &e.target, map),
                .e_spread => |e| rewritePrivateAccessesInExpr(p, &e.value, map),
                .e_if => |e| {
                    rewritePrivateAccessesInExpr(p, &e.test_, map);
                    rewritePrivateAccessesInExpr(p, &e.yes, map);
                    rewritePrivateAccessesInExpr(p, &e.no, map);
                },
                .e_await => |e| rewritePrivateAccessesInExpr(p, &e.value, map),
                .e_yield => |e| {
                    if (e.value != null) rewritePrivateAccessesInExpr(p, &e.value.?, map);
                },
                .e_new => |e| {
                    rewritePrivateAccessesInExpr(p, &e.target, map);
                    for (idx of 0..e.args.len) rewritePrivateAccessesInExpr(p, &e.args.slice()[idx], map);
                },
                .e_array => |e| {
                    for (idx of 0..e.items.len) rewritePrivateAccessesInExpr(p, &e.items.slice()[idx], map);
                },
                .e_object => |e| {
                    for (*prop of e.properties.slice()) {
                        if (prop.value != null) rewritePrivateAccessesInExpr(p, &prop.value.?, map);
                        if (prop.initializer != null) rewritePrivateAccessesInExpr(p, &prop.initializer.?, map);
                    }
                },
                .e_template => |e| {
                    if (e.tag != null) rewritePrivateAccessesInExpr(p, &e.tag.?, map);
                    for (*part of e.parts) rewritePrivateAccessesInExpr(p, &part.value, map);
                },
                .e_function => |e| rewritePrivateAccessesInStmts(p, e.func.body.stmts, map),
                .e_arrow => |e| rewritePrivateAccessesInStmts(p, e.body.stmts, map),
                else => {},
            }
        }

        static fn rewritePrivateAccessesInStmts(p: *P, stmts: []Stmt, map: *const PrivateLoweredMap) {
            for (*stmt_item of stmts) {
                switch (stmt_item.data) {
                    .s_expr => |data| rewritePrivateAccessesInExpr(p, &data.value, map),
                    .s_return => |data| {
                        if (data.value != null) rewritePrivateAccessesInExpr(p, &data.value.?, map);
                    },
                    .s_throw => |data| rewritePrivateAccessesInExpr(p, &data.value, map),
                    .s_local => |data| {
                        for (*decl of data.decls.slice()) {
                            if (decl.value != null) rewritePrivateAccessesInExpr(p, &decl.value.?, map);
                        }
                    },
                    .s_if => |data| {
                        rewritePrivateAccessesInExpr(p, &data.test_, map);
                        rewritePrivateAccessesInStmts(p, (&data.yes)[0..1], map);
                        if (data.no != null) rewritePrivateAccessesInStmts(p, (&data.no.?)[0..1], map);
                    },
                    .s_block => |data| rewritePrivateAccessesInStmts(p, data.stmts, map),
                    .s_for => |data| {
                        if (data.init != null) rewritePrivateAccessesInStmts(p, (&data.init.?)[0..1], map);
                        if (*t := data.test_) rewritePrivateAccessesInExpr(p, t, map);
                        if (*u := data.update) rewritePrivateAccessesInExpr(p, u, map);
                        rewritePrivateAccessesInStmts(p, (&data.body)[0..1], map);
                    },
                    .s_for_in => |data| {
                        rewritePrivateAccessesInExpr(p, &data.value, map);
                        rewritePrivateAccessesInStmts(p, (&data.body)[0..1], map);
                    },
                    .s_for_of => |data| {
                        rewritePrivateAccessesInExpr(p, &data.value, map);
                        rewritePrivateAccessesInStmts(p, (&data.body)[0..1], map);
                    },
                    .s_while => |data| {
                        rewritePrivateAccessesInExpr(p, &data.test_, map);
                        rewritePrivateAccessesInStmts(p, (&data.body)[0..1], map);
                    },
                    .s_do_while => |data| {
                        rewritePrivateAccessesInExpr(p, &data.test_, map);
                        rewritePrivateAccessesInStmts(p, (&data.body)[0..1], map);
                    },
                    .s_switch => |data| {
                        rewritePrivateAccessesInExpr(p, &data.test_, map);
                        for (*case of data.cases) {
                            if (*v := case.value) rewritePrivateAccessesInExpr(p, v, map);
                            rewritePrivateAccessesInStmts(p, case.body, map);
                        }
                    },
                    .s_try => |data| {
                        rewritePrivateAccessesInStmts(p, data.body, map);
                        if (c := data.catch_) rewritePrivateAccessesInStmts(p, c.body, map);
                        if (f := data.finally) rewritePrivateAccessesInStmts(p, f.stmts, map);
                    },
                    .s_label => |data| rewritePrivateAccessesInStmts(p, (&data.stmt)[0..1], map),
                    .s_with => |data| {
                        rewritePrivateAccessesInExpr(p, &data.value, map);
                        rewritePrivateAccessesInStmts(p, (&data.body)[0..1], map);
                    },
                    else => {},
                }
            }
        }

        // ── Public API ───────────────────────────────────────

        static fn lowerStandardDecoratorsStmt(p: *P, stmt: Stmt): []Stmt {
            const m: StdDecMode = .stmt;
            return lowerImpl(p, stmt, m);
        }

        static fn lowerStandardDecoratorsExpr(p: *P, cls: *G.Class, l: logger.Loc, name_from_context: ([]const u8)?): Expr {
            const result = lowerImpl(p, Stmt.empty(), { expr: {
                cls: cls,
                loc: l,
                name_from_context: name_from_context,
            } });
            if (result.len == 0) return p.newExpr(E.Missing{}, l);
            return result[0].data.s_expr.value;
        }

        // ── Core lowering ────────────────────────────────────

        static fn lowerImpl(p: *P, stmt: Stmt, mode: StdDecMode): []Stmt {
            const is_expr = mode == .expr;
            let cls = switch (mode) {
                .stmt => &stmt.data.s_class.class,
                .expr e => e.cls,
            };
            const loc = switch (mode) {
                .stmt => stmt.loc,
                .expr e => e.loc,
            };
            const name_from_context: ([]const u8)? = switch (mode) {
                .expr e => e.name_from_context,
                .stmt => null,
            };

            // ── Phase 1: Setup ───────────────────────────────
            let class_name_ref: Ref = undefined;
            let class_name_loc: logger.Loc = undefined;
            let expr_class_ref: Ref? = null;
            let expr_class_is_anonymous = false;
            let expr_var_decls = ListManaged(G.Decl).init(p.allocator);

            if (is_expr) {
                expr_class_ref = newSym(p, .other, "_class");
                expr_var_decls.append({ binding: p.b(B.Identifier{ ref: expr_class_ref.? }, loc) }) catch unreachable;
                if (cn := cls.class_name) {
                    class_name_ref = cn.ref.?;
                    class_name_loc = cn.loc;
                } else {
                    class_name_ref = expr_class_ref.?;
                    class_name_loc = loc;
                    expr_class_is_anonymous = true;
                    if (name := name_from_context) {
                        cls.class_name = { ref: newSym(p, .other, name), loc: loc };
                    }
                }
            } else {
                class_name_ref = cls.class_name.?.ref.?;
                class_name_loc = cls.class_name.?.loc;
            }

            let inner_class_ref: Ref = class_name_ref;
            if (!is_expr) {
                const cns = p.symbols.items[class_name_ref.innerIndex()].original_name;
                inner_class_ref = newSym(p, .other, std.fmt.allocPrint(p.allocator, "_{s}", .{cns}) catch unreachable);
            }

            const class_decorators = cls.ts_decorators;
            cls.ts_decorators = {};

            const init_ref = newSym(p, .other, "_init");
            if (is_expr) {
                expr_var_decls.append({ binding: p.b(B.Identifier{ ref: init_ref }, loc) }) catch unreachable;
            }

            let base_ref: Ref? = null;
            if (cls.@"extends" != null) {
                base_ref = newSym(p, .other, "_base");
                if (is_expr) {
                    expr_var_decls.append({ binding: p.b(B.Identifier{ ref: base_ref.? }, loc) }) catch unreachable;
                }
            }

            // ── Phase 2: Pre-evaluate decorators/keys ────────
            let dec_counter: usize = 0;
            let class_dec_ref: Ref? = null;
            let class_dec_stmt: Stmt = Stmt.empty();
            let class_dec_assign_expr: Expr? = null;
            if (class_decorators.len > 0) {
                dec_counter += 1;
                class_dec_ref = newSym(p, .other, "_dec");
                const arr = p.newExpr(E.Array{ items: class_decorators }, loc);
                if (is_expr) {
                    expr_var_decls.append({ binding: p.b(B.Identifier{ ref: class_dec_ref.? }, loc) }) catch unreachable;
                    class_dec_assign_expr = assignTo(p, class_dec_ref.?, arr, loc);
                } else {
                    class_dec_stmt = varDecl(p, class_dec_ref.?, arr, loc);
                }
            }

            let prop_dec_refs: std.AutoHashMapUnmanaged(usize, Ref) = .{};
            let computed_key_refs: std.AutoHashMapUnmanaged(usize, Ref) = {};
            let pre_eval_stmts = ListManaged(Stmt).init(p.allocator);
            let computed_key_counter: usize = 0;

            let prop_idx: usize = 0;
            while (prop_idx < cls.properties.len) : (prop_idx += 1) {
                const prop = &cls.properties[prop_idx];
                if (prop.kind == .class_static_block) continue;
                if (prop.ts_decorators.len > 0) {
                    dec_counter += 1;
                    const dec_name = if (dec_counter == 1)
                        "_dec"
                    else
                        std.fmt.allocPrint(p.allocator, "_dec{d}", .{dec_counter}) catch unreachable;
                    const dec_ref = newSym(p, .other, dec_name);
                    prop_dec_refs.put(p.allocator, prop_idx, dec_ref) catch unreachable;
                    if (is_expr) {
                        expr_var_decls.append({ binding: p.b(B.Identifier{ ref: dec_ref }, loc) }) catch unreachable;
                    }
                    pre_eval_stmts.append(varDecl(p, dec_ref, p.newExpr(E.Array{ items: prop.ts_decorators }, loc), loc)) catch unreachable;
                }
                if (prop.flags.contains(.is_computed) && prop.key != null && prop.ts_decorators.len > 0) {
                    computed_key_counter += 1;
                    const key_name = if (computed_key_counter == 1)
                        "_computedKey"
                    else
                        std.fmt.allocPrint(p.allocator, "_computedKey{d}", .{computed_key_counter}) catch unreachable;
                    const key_ref = newSym(p, .other, key_name);
                    computed_key_refs.put(p.allocator, prop_idx, key_ref) catch unreachable;
                    if (is_expr) {
                        expr_var_decls.append({ binding: p.b(B.Identifier{ ref: key_ref }, loc) }) catch unreachable;
                    }
                    pre_eval_stmts.append(varDecl(p, key_ref, prop.key.?, loc)) catch unreachable;
                    prop.key = useRef(p, key_ref, prop.key.?.loc);
                }
            }

            // Replace class name refs in pre-eval expressions for inner binding
            {
                const replacement_ref = if (is_expr) (expr_class_ref ?? class_name_ref) else inner_class_ref;
                if (!replacement_ref.eql(class_name_ref)) {
                    const rk: RewriteKind = { replace_ref: { old: class_name_ref, new_ref: replacement_ref } };
                    for (*pre_stmt of pre_eval_stmts.items) {
                        if (pre_stmt.data == .s_local) {
                            for (*decl of pre_stmt.data.s_local.decls.slice()) {
                                if (decl.value != null) rewriteExpr(p, &decl.value.?, rk);
                            }
                        }
                    }
                }
            }

            // For named class expressions: swap to expr_class_ref for suffix ops
            let original_class_name_for_decorator: ([]const u8)? = null;
            if (is_expr && !expr_class_is_anonymous && expr_class_ref != null) {
                original_class_name_for_decorator = p.symbols.items[class_name_ref.innerIndex()].original_name;
                class_name_ref = expr_class_ref.?;
                class_name_loc = loc;
            }

            // ── Phase 3: __decoratorStart + base decls ───────
            const init_start_expr: Expr = brk: {
                let base_expr = p.newExpr(E.Undefined{}, loc);
                if (br := base_ref) {
                    base_expr = p.newExpr(E.Identifier{ ref: br }, loc);
                }
                break :brk callRt(p, loc, "__decoratorStart", &[base_expr]);
            };

            let base_decl_stmt: Stmt = Stmt.empty();
            if (!is_expr) {
                if (br := base_ref) base_decl_stmt = varDecl(p, br, cls.@"extends", loc);
            }

            const base_assign_expr: Expr? = if (is_expr && base_ref != null)
                assignTo(p, base_ref.?, cls.@"extends".?, loc)
            else
                null;

            if (br := base_ref) cls.@"extends" = useRef(p, br, loc);

            const init_decl_stmt: Stmt = if (!is_expr)
                varDecl(p, init_ref, init_start_expr, loc)
            else
                Stmt.empty();

            // ── Phase 4: Property loop ───────────────────────
            let suffix_exprs = ListManaged(Expr).init(p.allocator);
            let constructor_inject_stmts = ListManaged(Stmt).init(p.allocator);
            let new_properties = ListManaged(Property).init(p.allocator);
            let static_non_field_elements = ListManaged(Expr).init(p.allocator);
            let instance_non_field_elements = ListManaged(Expr).init(p.allocator);
            let has_static_private_methods = false;
            let has_instance_private_methods = false;
            let static_field_decorate = ListManaged(Expr).init(p.allocator);
            let instance_field_decorate = ListManaged(Expr).init(p.allocator);
            let static_accessor_count: usize = 0;
            let instance_accessor_count: usize = 0;
            let static_init_entries = ListManaged(FieldInitEntry).init(p.allocator);
            let instance_init_entries = ListManaged(FieldInitEntry).init(p.allocator);
            let static_element_order = ListManaged(StaticElement).init(p.allocator);
            let extracted_static_blocks = ListManaged(*G.ClassStaticBlock).init(p.allocator);
            let prefix_stmts = ListManaged(Stmt).init(p.allocator);
            let private_lowered_map = PrivateLoweredMap{};
            let accessor_storage_counter: usize = 0;
            let emitted_private_adds: std.AutoHashMapUnmanaged(u32, void) = .{};
            let static_private_add_blocks = ListManaged(Property).init(p.allocator);

            // Pre-scan: determine if all private members need lowering
            let lower_all_private = false;
            {
                let has_any_private = false;
                let has_any_decorated = false;
                for (cprop of cls.properties) {
                    if (cprop.kind == .class_static_block) continue;
                    if (cprop.ts_decorators.len > 0) {
                        has_any_decorated = true;
                        if (cprop.key != null && cprop.key.?.data == .e_private_identifier) {
                            lower_all_private = true;
                            break;
                        }
                    }
                    if (cprop.key != null && cprop.key.?.data == .e_private_identifier) {
                        has_any_private = true;
                    }
                }
                if (!lower_all_private && has_any_private && has_any_decorated)
                    lower_all_private = true;
            }

            prop_idx = 0;
            while (prop_idx < cls.properties.len) : (prop_idx += 1) {
                const prop = &cls.properties[prop_idx];
                if (prop.ts_decorators.len == 0) {
                    // ── Non-decorated property ──
                    if (lower_all_private && prop.key != null &&
                        prop.key.?.data == .e_private_identifier && prop.kind != .class_static_block &&
                        prop.kind != .auto_accessor)
                    {
                        const nk_expr = prop.key.?;
                        const npriv_orig = p.symbols.items[nk_expr.data.e_private_identifier.ref.innerIndex()].original_name;
                        const npriv_inner = nk_expr.data.e_private_identifier.ref.innerIndex();

                        if (prop.flags.contains(.is_method)) {
                            // Non-decorated private method/getter/setter → WeakSet + fn extraction
                            const nk = methodKind(prop);
                            const existing = private_lowered_map.get(npriv_inner);
                            let ws_ref: Ref = undefined;
                            if (ex := existing) {
                                ws_ref = ex.storage_ref;
                            } else {
                                ws_ref = newSym(p, .other, std.fmt.allocPrint(p.allocator, "_{s}", .{npriv_orig[1..]}) catch unreachable);
                            }
                            const fn_nm = std.fmt.allocPrint(p.allocator, "_{s}{s}", .{ npriv_orig[1..], fnSuffix(nk) }) catch unreachable;
                            const fn_ref = newSym(p, .other, fn_nm);

                            let new_info = (existing ?? PrivateLoweredInfo{ storage_ref: ws_ref });
                            if (nk == 1) new_info.method_fn_ref = fn_ref else if (nk == 2) new_info.getter_fn_ref = fn_ref else new_info.setter_fn_ref = fn_ref;
                            private_lowered_map.put(p.allocator, npriv_inner, new_info) catch unreachable;

                            if (existing == null) {
                                prefix_stmts.append(varDecl2(p, ws_ref, newWeakSetExpr(p, loc), fn_ref, null, loc)) catch unreachable;
                            } else {
                                prefix_stmts.append(varDecl(p, fn_ref, null, loc)) catch unreachable;
                            }

                            // Assign function: _fn = function() { ... }
                            prefix_stmts.append(p.s(S.SExpr{
                                value: assignTo(p, fn_ref, (prop.value ?? p.newExpr(E.Undefined{}, loc)), loc),
                            }, loc)) catch unreachable;

                            // __privateAdd (once per name)
                            if (!emitted_private_adds.contains(npriv_inner)) {
                                const void_val: void = undefined;
                                emitted_private_adds.put(p.allocator, npriv_inner, void_val) catch unreachable;
                                emitPrivateAdd(p, prop.flags.contains(.is_static), ws_ref, null, loc, &constructor_inject_stmts, &static_private_add_blocks);
                            }
                            continue;
                        } else {
                            // Non-decorated private field → WeakMap
                            const wm_nm = std.fmt.allocPrint(p.allocator, "_{s}", .{npriv_orig[1..]}) catch unreachable;
                            const wm_ref = newSym(p, .other, wm_nm);
                            private_lowered_map.put(p.allocator, npriv_inner, { storage_ref: wm_ref }) catch unreachable;
                            prefix_stmts.append(varDecl(p, wm_ref, newWeakMapExpr(p, loc), loc)) catch unreachable;

                            const init_val = (prop.initializer ?? p.newExpr(E.Undefined{}, loc));
                            if (!prop.flags.contains(.is_static)) {
                                constructor_inject_stmts.append(p.s(S.SExpr{
                                    value: callRt(p, loc, "__privateAdd", &[
                                        p.newExpr(E.This{}, loc),
                                        useRef(p, wm_ref, loc),
                                        init_val,
                                    ]),
                                }, loc)) catch unreachable;
                            } else {
                                static_private_add_blocks.append(makeStaticBlock(
                                    p,
                                    callRt(p, loc, "__privateAdd", &[
                                        p.newExpr(E.This{}, loc),
                                        useRef(p, wm_ref, loc),
                                        init_val,
                                    ]),
                                    loc,
                                )) catch unreachable;
                            }
                            continue;
                        }
                    }
                    // Undecorated auto-accessor → WeakMap + getter/setter
                    if (prop.kind == .auto_accessor) {
                        const accessor_name = brk: {
                            if (prop.key.?.data == .e_string)
                                break :brk std.fmt.allocPrint(p.allocator, "_{s}", .{prop.key.?.data.e_string.data}) catch unreachable;
                            const name = std.fmt.allocPrint(p.allocator, "_accessor_storage{d}", .{accessor_storage_counter}) catch unreachable;
                            accessor_storage_counter += 1;
                            break :brk name;
                        };
                        const wm_ref = newSym(p, .other, accessor_name);
                        prefix_stmts.append(varDecl(p, wm_ref, newWeakMapExpr(p, loc), loc)) catch unreachable;

                        // Getter: get foo() { return __privateGet(this, _foo); }
                        let get_body = bun.handleOom(p.allocator.alloc(Stmt, 1));
                        get_body[0] = p.s(S.Return{ value: callRt(p, loc, "__privateGet", &[
                            p.newExpr(E.This{}, loc),
                            useRef(p, wm_ref, loc),
                        ]) }, loc);
                        const get_fn = bun.handleOom(p.allocator.create(G.Fn));
                        get_fn.* = { body: { stmts: get_body, loc: loc } };

                        // Setter: set foo(v) { __privateSet(this, _foo, v); }
                        const setter_param_ref = newSym(p, .other, "v");
                        let set_body = bun.handleOom(p.allocator.alloc(Stmt, 1));
                        set_body[0] = p.s(S.SExpr{ value: callRt(p, loc, "__privateSet", &[
                            p.newExpr(E.This{}, loc),
                            useRef(p, wm_ref, loc),
                            useRef(p, setter_param_ref, loc),
                        ]) }, loc);
                        let setter_fn_args = bun.handleOom(p.allocator.alloc(G.Arg, 1));
                        setter_fn_args[0] = { binding: p.b(B.Identifier{ ref: setter_param_ref }, loc) };
                        const set_fn = bun.handleOom(p.allocator.create(G.Fn));
                        set_fn.* = { args: setter_fn_args, body: { stmts: set_body, loc: loc } };

                        let getter_flags = prop.flags;
                        getter_flags.insert(.is_method);
                        new_properties.append({
                            key: prop.key,
                            value: p.newExpr(E.Function{ func: get_fn.* }, loc),
                            kind: .get,
                            flags: getter_flags,
                        }) catch unreachable;
                        new_properties.append({
                            key: prop.key,
                            value: p.newExpr(E.Function{ func: set_fn.* }, loc),
                            kind: .set,
                            flags: getter_flags,
                        }) catch unreachable;

                        const init_val = (prop.initializer ?? p.newExpr(E.Undefined{}, loc));
                        if (!prop.flags.contains(.is_static)) {
                            constructor_inject_stmts.append(p.s(S.SExpr{
                                value: callRt(p, loc, "__privateAdd", &[
                                    p.newExpr(E.This{}, loc),
                                    useRef(p, wm_ref, loc),
                                    init_val,
                                ]),
                            }, loc)) catch unreachable;
                        } else {
                            suffix_exprs.append(callRt(p, loc, "__privateAdd", &[
                                useRef(p, class_name_ref, class_name_loc),
                                useRef(p, wm_ref, loc),
                                init_val,
                            ])) catch unreachable;
                        }
                        continue;
                    }
                    // Static blocks → extract to suffix
                    if (prop.kind == .class_static_block) {
                        if (sb := prop.class_static_block) {
                            static_element_order.append({ kind: .block, index: extracted_static_blocks.items.len }) catch unreachable;
                            extracted_static_blocks.append(sb) catch unreachable;
                        }
                        continue;
                    }
                    new_properties.append(prop.*) catch unreachable;
                    continue;
                }

                // ── Decorated property ──
                let flags: f64 = 0;
                if (prop.flags.contains(.is_method)) {
                    flags = switch (prop.kind) {
                        .get => 2,
                        .set => 3,
                        else => 1,
                    };
                } else {
                    flags = switch (prop.kind) {
                        .auto_accessor => 4,
                        else => 5,
                    };
                }
                if (prop.flags.contains(.is_static)) flags += 8;
                const is_private = prop.key.?.data == .e_private_identifier;
                if (is_private) flags += 16;

                let decorator_array = p.newExpr(E.Array{ items: prop.ts_decorators }, loc);
                if (dec_ref := prop_dec_refs.get(prop_idx)) {
                    decorator_array = useRef(p, dec_ref, loc);
                }

                const key_expr = prop.key.?;
                const k = @as(u8, @intFromFloat(flags)) & 7;

                let dec_arg_count: usize = 5;
                let private_storage_ref: Ref? = null;
                let private_extra_ref: Ref? = null;
                let private_method_fn_ref: Ref? = null;

                if (is_private) {
                    const private_orig = p.symbols.items[key_expr.data.e_private_identifier.ref.innerIndex()].original_name;
                    const priv_inner = key_expr.data.e_private_identifier.ref.innerIndex();

                    if (k >= 1 && k <= 3) {
                        // Decorated private method/getter/setter → WeakSet
                        const existing = private_lowered_map.get(priv_inner);
                        let ws_ref: Ref = undefined;
                        if (ex := existing) {
                            ws_ref = ex.storage_ref;
                        } else {
                            ws_ref = newSym(p, .other, std.fmt.allocPrint(p.allocator, "_{s}", .{private_orig[1..]}) catch unreachable);
                        }
                        private_storage_ref = ws_ref;
                        const fn_ref = newSym(p, .other, std.fmt.allocPrint(p.allocator, "_{s}{s}", .{ private_orig[1..], fnSuffix(k) }) catch unreachable);
                        private_method_fn_ref = fn_ref;

                        let new_info = (existing ?? PrivateLoweredInfo{ storage_ref: ws_ref });
                        if (k == 1) new_info.method_fn_ref = fn_ref else if (k == 2) new_info.getter_fn_ref = fn_ref else new_info.setter_fn_ref = fn_ref;
                        private_lowered_map.put(p.allocator, priv_inner, new_info) catch unreachable;

                        if (existing == null) {
                            prefix_stmts.append(varDecl2(p, ws_ref, newWeakSetExpr(p, loc), fn_ref, null, loc)) catch unreachable;
                        } else {
                            prefix_stmts.append(varDecl(p, fn_ref, null, loc)) catch unreachable;
                        }
                        dec_arg_count = 6;
                    } else if (k == 5) {
                        // Decorated private field → WeakMap
                        const wm_ref = newSym(p, .other, std.fmt.allocPrint(p.allocator, "_{s}", .{private_orig[1..]}) catch unreachable);
                        private_storage_ref = wm_ref;
                        private_lowered_map.put(p.allocator, priv_inner, { storage_ref: wm_ref }) catch unreachable;
                        prefix_stmts.append(varDecl(p, wm_ref, newWeakMapExpr(p, loc), loc)) catch unreachable;
                        dec_arg_count = 5;
                    } else if (k == 4) {
                        // Decorated private auto-accessor → WeakMap + descriptor
                        const wm_ref = newSym(p, .other, std.fmt.allocPrint(p.allocator, "_{s}", .{private_orig[1..]}) catch unreachable);
                        private_storage_ref = wm_ref;
                        const acc_ref = newSym(p, .other, std.fmt.allocPrint(p.allocator, "_{s}_acc", .{private_orig[1..]}) catch unreachable);
                        private_method_fn_ref = acc_ref;
                        private_lowered_map.put(p.allocator, priv_inner, {
                            storage_ref: wm_ref,
                            accessor_desc_ref: acc_ref,
                        }) catch unreachable;
                        prefix_stmts.append(varDecl2(p, wm_ref, newWeakMapExpr(p, loc), acc_ref, null, loc)) catch unreachable;
                        dec_arg_count = 6;
                    }
                } else if (k == 4) {
                    // Decorated public auto-accessor → WeakMap
                    const accessor_name = brk: {
                        if (key_expr.data == .e_string)
                            break :brk std.fmt.allocPrint(p.allocator, "_{s}", .{key_expr.data.e_string.data}) catch unreachable;
                        const name = std.fmt.allocPrint(p.allocator, "_accessor_storage{d}", .{accessor_storage_counter}) catch unreachable;
                        accessor_storage_counter += 1;
                        break :brk name;
                    };
                    const wm_ref = newSym(p, .other, accessor_name);
                    private_extra_ref = wm_ref;
                    prefix_stmts.append(varDecl(p, wm_ref, newWeakMapExpr(p, loc), loc)) catch unreachable;
                    dec_arg_count = 6;
                }

                // Build __decorateElement args
                const target_ref = if (is_expr && expr_class_ref != null) expr_class_ref.? else class_name_ref;
                let dec_args = bun.handleOom(p.allocator.alloc(Expr, dec_arg_count));
                dec_args[0] = p.newExpr(E.Identifier{ ref: init_ref }, loc);
                dec_args[1] = p.newExpr(E.Number{ value: flags }, loc);
                dec_args[2] = if (is_private)
                    p.newExpr(E.String{ data: p.symbols.items[key_expr.data.e_private_identifier.ref.innerIndex()].original_name }, loc)
                else
                    key_expr;
                dec_args[3] = decorator_array;

                if (is_private && private_storage_ref != null) {
                    dec_args[4] = useRef(p, private_storage_ref.?, loc);
                    if (dec_arg_count == 6) {
                        if (k >= 1 && k <= 3) {
                            dec_args[5] = (prop.value ?? p.newExpr(E.Undefined{}, loc));
                        } else if (k == 4) {
                            dec_args[5] = useRef(p, private_storage_ref.?, loc);
                        } else {
                            dec_args[5] = p.newExpr(E.Undefined{}, loc);
                        }
                    }
                } else {
                    p.recordUsage(target_ref);
                    dec_args[4] = p.newExpr(E.Identifier{ ref: target_ref }, class_name_loc);
                    if (dec_arg_count == 6) {
                        if (extra_ref := private_extra_ref) {
                            dec_args[5] = useRef(p, extra_ref, loc);
                        } else {
                            dec_args[5] = p.newExpr(E.Undefined{}, loc);
                        }
                    }
                }

                const raw_element = p.callRuntime(loc, "__decorateElement", dec_args);
                let element = raw_element;
                if (fn_ref := private_method_fn_ref) {
                    element = assignTo(p, fn_ref, raw_element, loc);
                }

                // Categorize the element
                if (k >= 4) {
                    // Field (k=5) or accessor (k=4) — remove from class body
                    let prop_copy = prop.*;
                    prop_copy.ts_decorators = {};
                    if (is_private) {
                        if (ps_ref := private_storage_ref)
                            prop_copy.key = p.newExpr(E.Identifier{ ref: ps_ref }, loc);
                    }
                    if (pe_ref := private_extra_ref)
                        prop_copy.value = p.newExpr(E.Identifier{ ref: pe_ref }, loc);

                    const is_accessor = (k == 4);
                    const init_entry = FieldInitEntry{ prop: prop_copy, is_private: is_private, is_accessor: is_accessor };

                    if (prop.flags.contains(.is_static)) {
                        if (is_accessor) {
                            static_non_field_elements.append(element) catch unreachable;
                            static_accessor_count += 1;
                        } else {
                            static_field_decorate.append(element) catch unreachable;
                        }
                        static_element_order.append({ kind: .field_or_accessor, index: static_init_entries.items.len }) catch unreachable;
                        static_init_entries.append(init_entry) catch unreachable;
                    } else {
                        if (is_accessor) {
                            instance_non_field_elements.append(element) catch unreachable;
                            instance_accessor_count += 1;
                        } else {
                            instance_field_decorate.append(element) catch unreachable;
                        }
                        instance_init_entries.append(init_entry) catch unreachable;
                    }
                } else if (is_private && private_storage_ref != null) {
                    // Private method/getter/setter — remove from class body
                    const priv_inner2 = key_expr.data.e_private_identifier.ref.innerIndex();
                    if (!emitted_private_adds.contains(priv_inner2)) {
                        const void_val2: void = undefined;
                        emitted_private_adds.put(p.allocator, priv_inner2, void_val2) catch unreachable;
                        emitPrivateAdd(p, prop.flags.contains(.is_static), private_storage_ref.?, null, loc, &constructor_inject_stmts, &static_private_add_blocks);
                    }
                    if (prop.flags.contains(.is_static)) {
                        static_non_field_elements.append(element) catch unreachable;
                        has_static_private_methods = true;
                    } else {
                        instance_non_field_elements.append(element) catch unreachable;
                        has_instance_private_methods = true;
                    }
                } else {
                    // Public method/getter/setter — keep in class body
                    let new_prop = prop.*;
                    new_prop.ts_decorators = {};
                    new_properties.append(new_prop) catch unreachable;
                    if (prop.flags.contains(.is_static)) {
                        static_non_field_elements.append(element) catch unreachable;
                    } else {
                        instance_non_field_elements.append(element) catch unreachable;
                    }
                }
            }

            // ── Phase 5: Rewrite private accesses ────────────
            if (private_lowered_map.count() > 0) {
                for (*nprop of new_properties.items) {
                    if (nprop.value != null) rewritePrivateAccessesInExpr(p, &nprop.value.?, &private_lowered_map);
                    if (sb := nprop.class_static_block)
                        rewritePrivateAccessesInStmts(p, sb.stmts.slice(), &private_lowered_map);
                }
                for (*entry of instance_init_entries.items) {
                    if (entry.prop.initializer != null) rewritePrivateAccessesInExpr(p, &entry.prop.initializer.?, &private_lowered_map);
                }
                for (*entry of static_init_entries.items) {
                    if (entry.prop.initializer != null) rewritePrivateAccessesInExpr(p, &entry.prop.initializer.?, &private_lowered_map);
                }
                for (sb of extracted_static_blocks.items)
                    rewritePrivateAccessesInStmts(p, sb.stmts.slice(), &private_lowered_map);
                for (idx of 0..static_non_field_elements.items.len) rewritePrivateAccessesInExpr(p, &static_non_field_elements.items[idx], &private_lowered_map);
                for (idx of 0..instance_non_field_elements.items.len) rewritePrivateAccessesInExpr(p, &instance_non_field_elements.items[idx], &private_lowered_map);
                for (idx of 0..static_field_decorate.items.len) rewritePrivateAccessesInExpr(p, &static_field_decorate.items[idx], &private_lowered_map);
                for (idx of 0..instance_field_decorate.items.len) rewritePrivateAccessesInExpr(p, &instance_field_decorate.items[idx], &private_lowered_map);
                rewritePrivateAccessesInStmts(p, pre_eval_stmts.items, &private_lowered_map);
                rewritePrivateAccessesInStmts(p, prefix_stmts.items, &private_lowered_map);
            }

            // ── Phase 6: Emit suffix ─────────────────────────
            const static_field_count = static_field_decorate.items.len;
            const total_accessor_count = static_accessor_count + instance_accessor_count;
            const static_field_base_idx = total_accessor_count;
            const instance_accessor_base_idx = static_accessor_count;
            const instance_field_base_idx = total_accessor_count + static_field_count;

            // 1-4: __decorateElement calls in spec order
            suffix_exprs.appendSlice(static_non_field_elements.items) catch unreachable;
            suffix_exprs.appendSlice(instance_non_field_elements.items) catch unreachable;
            suffix_exprs.appendSlice(static_field_decorate.items) catch unreachable;
            suffix_exprs.appendSlice(instance_field_decorate.items) catch unreachable;

            // 5: Class decorator
            if (class_decorators.len > 0) {
                p.recordUsage(class_name_ref);
                let class_name_str: []const u8 = p.symbols.items[class_name_ref.innerIndex()].original_name;
                if (name := original_class_name_for_decorator) {
                    class_name_str = name;
                } else if (is_expr && expr_class_is_anonymous) {
                    if (nfc := name_from_context) {
                        class_name_str = nfc;
                    } else {
                        class_name_str = "";
                    }
                }

                let cls_dec_args = bun.handleOom(p.allocator.alloc(Expr, 5));
                cls_dec_args[0] = p.newExpr(E.Identifier{ ref: init_ref }, loc);
                cls_dec_args[1] = p.newExpr(E.Number{ value: 0 }, loc);
                cls_dec_args[2] = p.newExpr(E.String{ data: class_name_str }, loc);
                if (cdr := class_dec_ref) {
                    cls_dec_args[3] = useRef(p, cdr, loc);
                } else {
                    cls_dec_args[3] = p.newExpr(E.Array{ items: class_decorators }, loc);
                }
                cls_dec_args[4] = if (is_expr)
                    useRef(p, expr_class_ref.?, loc)
                else
                    p.newExpr(E.Identifier{ ref: class_name_ref }, class_name_loc);

                suffix_exprs.append(assignTo(p, class_name_ref, p.callRuntime(loc, "__decorateElement", cls_dec_args), class_name_loc)) catch unreachable;
            }

            // 6: Static method extra initializers
            if (static_non_field_elements.items.len > 0 || has_static_private_methods) {
                suffix_exprs.append(callRt(p, loc, "__runInitializers", &[
                    useRef(p, init_ref, loc),
                    p.newExpr(E.Number{ value: 3 }, loc),
                    useRef(p, class_name_ref, class_name_loc),
                ])) catch unreachable;
            }

            // 7: Static elements in source order
            {
                let s_accessor_idx: usize = 0;
                let s_field_idx: usize = 0;
                for (elem of static_element_order.items) {
                    switch (elem.kind) {
                        .block => {
                            const sb = extracted_static_blocks.items[elem.index];
                            const stmts_slice = sb.stmts.slice();
                            rewriteStmts(p, stmts_slice, { replace_this: { sym_ref: class_name_ref, loc: class_name_loc } });

                            // Check if all statements are simple expressions
                            const all_exprs = blk: {
                                for (sb_stmt of stmts_slice) {
                                    if (sb_stmt.data != .s_expr) break :blk false;
                                }
                                break :blk true;
                            };

                            if (all_exprs) {
                                for (sb_stmt of stmts_slice) {
                                    suffix_exprs.append(sb_stmt.data.s_expr.value) catch unreachable;
                                }
                            } else {
                                // Wrap in IIFE to preserve non-expression statements
                                const iife_body = p.newExpr(E.Arrow{
                                    body: { loc: loc, stmts: stmts_slice },
                                    is_async: false,
                                }, loc);
                                suffix_exprs.append(p.newExpr(E.Call{
                                    target: iife_body,
                                    args: ExprNodeList.empty,
                                }, loc)) catch unreachable;
                            }
                        },
                        .field_or_accessor => {
                            const entry = static_init_entries.items[elem.index];
                            const field_idx: usize = if (entry.is_accessor) brk: {
                                const idx = s_accessor_idx;
                                s_accessor_idx += 1;
                                break :brk idx;
                            } else brk: {
                                const idx = static_field_base_idx + s_field_idx;
                                s_field_idx += 1;
                                break :brk idx;
                            };

                            const run_args_count: usize = if (entry.prop.initializer != null) 4 else 3;
                            const run_args = bun.handleOom(p.allocator.alloc(Expr, run_args_count));
                            run_args[0] = useRef(p, init_ref, loc);
                            run_args[1] = p.newExpr(E.Number{ value: initFlag(field_idx) }, loc);
                            run_args[2] = useRef(p, class_name_ref, class_name_loc);
                            if (init_val := entry.prop.initializer) run_args[3] = init_val;
                            const run_init_call = p.callRuntime(loc, "__runInitializers", run_args);

                            if (entry.is_accessor || entry.is_private) {
                                const wm_ref_expr = if (entry.is_accessor && !entry.is_private)
                                    entry.prop.value.?
                                else
                                    entry.prop.key.?;
                                suffix_exprs.append(callRt(p, loc, "__privateAdd", &[
                                    useRef(p, class_name_ref, class_name_loc),
                                    wm_ref_expr,
                                    run_init_call,
                                ])) catch unreachable;
                            } else {
                                const assign_target = memberTarget(p, useRef(p, class_name_ref, class_name_loc), entry.prop);
                                suffix_exprs.append(Expr.assign(assign_target, run_init_call)) catch unreachable;
                            }

                            // Extra initializer
                            suffix_exprs.append(callRt(p, loc, "__runInitializers", &[
                                useRef(p, init_ref, loc),
                                p.newExpr(E.Number{ value: extraInitFlag(field_idx) }, loc),
                                useRef(p, class_name_ref, class_name_loc),
                            ])) catch unreachable;
                        },
                    }
                }
            }

            // 8: Class extra initializers
            if (class_decorators.len > 0) {
                suffix_exprs.append(callRt(p, loc, "__runInitializers", &[
                    useRef(p, init_ref, loc),
                    p.newExpr(E.Number{ value: 1 }, loc),
                    useRef(p, class_name_ref, class_name_loc),
                ])) catch unreachable;
            }

            // 9: __decoratorMetadata
            suffix_exprs.append(callRt(p, loc, "__decoratorMetadata", &[
                useRef(p, init_ref, loc),
                useRef(p, class_name_ref, class_name_loc),
            ])) catch unreachable;

            // ── Phase 7: Constructor injection ───────────────
            if (instance_non_field_elements.items.len > 0 || has_instance_private_methods) {
                constructor_inject_stmts.append(p.s(S.SExpr{
                    value: callRt(p, loc, "__runInitializers", &[
                        useRef(p, init_ref, loc),
                        p.newExpr(E.Number{ value: 5 }, loc),
                        p.newExpr(E.This{}, loc),
                    ]),
                }, loc)) catch unreachable;
            }

            // Instance field/accessor init + extra-init
            {
                let i_accessor_idx: usize = 0;
                let i_field_idx: usize = 0;
                for (entry of instance_init_entries.items) {
                    const field_idx: usize = if (entry.is_accessor) brk: {
                        const idx = instance_accessor_base_idx + i_accessor_idx;
                        i_accessor_idx += 1;
                        break :brk idx;
                    } else brk: {
                        const idx = instance_field_base_idx + i_field_idx;
                        i_field_idx += 1;
                        break :brk idx;
                    };

                    const run_args_count: usize = if (entry.prop.initializer != null) 4 else 3;
                    const run_args = bun.handleOom(p.allocator.alloc(Expr, run_args_count));
                    run_args[0] = useRef(p, init_ref, loc);
                    run_args[1] = p.newExpr(E.Number{ value: initFlag(field_idx) }, loc);
                    run_args[2] = p.newExpr(E.This{}, loc);
                    if (init_val := entry.prop.initializer) run_args[3] = init_val;
                    const run_init_call = p.callRuntime(loc, "__runInitializers", run_args);

                    if (entry.is_accessor || entry.is_private) {
                        const wm_ref_expr = if (entry.is_accessor && !entry.is_private)
                            entry.prop.value.?
                        else
                            entry.prop.key.?;
                        constructor_inject_stmts.append(p.s(S.SExpr{
                            value: callRt(p, loc, "__privateAdd", &[
                                p.newExpr(E.This{}, loc),
                                wm_ref_expr,
                                run_init_call,
                            ]),
                        }, loc)) catch unreachable;
                    } else {
                        constructor_inject_stmts.append(
                            Stmt.assign(memberTarget(p, p.newExpr(E.This{}, loc), entry.prop), run_init_call),
                        ) catch unreachable;
                    }

                    // Extra initializer
                    constructor_inject_stmts.append(p.s(S.SExpr{
                        value: callRt(p, loc, "__runInitializers", &[
                            useRef(p, init_ref, loc),
                            p.newExpr(E.Number{ value: extraInitFlag(field_idx) }, loc),
                            p.newExpr(E.This{}, loc),
                        ]),
                    }, loc)) catch unreachable;
                }
            }

            // Inject into constructor
            if (constructor_inject_stmts.items.len > 0) {
                let found_constructor = false;
                for (*nprop of new_properties.items) {
                    if (nprop.flags.contains(.is_method) && nprop.key != null &&
                        nprop.key.?.data == .e_string && nprop.key.?.data.e_string.eqlComptime("constructor"))
                    {
                        const func = nprop.value.?.data.e_function;
                        let body_stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, func.func.body.stmts);
                        let super_index: usize? = null;
                        let index: usize = 0;
                        while (index < body_stmts.items.len) : (index += 1) {
                            const item = body_stmts.items[index];
                            if (item.data != .s_expr) continue;
                            if (item.data.s_expr.value.data != .e_call) continue;
                            if (item.data.s_expr.value.data.e_call.target.data != .e_super) continue;
                            super_index = index;
                            break;
                        }
                        let insert_at: usize = 0;
                        if (j := super_index) {
                            insert_at = j + 1;
                        }
                        body_stmts.insertSlice(insert_at, constructor_inject_stmts.items) catch unreachable;
                        func.func.body.stmts = body_stmts.items;
                        found_constructor = true;
                        break;
                    }
                }

                if (!found_constructor) {
                    let ctor_stmts = ListManaged(Stmt).init(p.allocator);
                    if (cls.@"extends" != null) {
                        const target = p.newExpr(E.Super{}, loc);
                        const args_ref = newSym(p, .unbound, arguments_str);
                        const spread = p.newExpr(E.Spread{ value: p.newExpr(E.Identifier{ ref: args_ref }, loc) }, loc);
                        const call_args = bun.handleOom(ExprNodeList.initOne(p.allocator, spread));
                        ctor_stmts.append(
                            p.s(S.SExpr{ value: p.newExpr(E.Call{ target: target, args: call_args }, loc) }, loc),
                        ) catch unreachable;
                    }
                    ctor_stmts.appendSlice(constructor_inject_stmts.items) catch unreachable;
                    new_properties.insert(0, G.Property{
                        flags: Flags.Property.init({ is_method: true }),
                        key: p.newExpr(E.String{ data: "constructor" }, loc),
                        value: p.newExpr(E.Function{ func: G.Fn{
                            name: null,
                            open_parens_loc: logger.Loc.Empty,
                            args: [],
                            body: { loc: loc, stmts: ctor_stmts.items },
                            flags: Flags.Function.init({}),
                        } }, loc),
                    }) catch unreachable;
                }
            }

            // Static private __privateAdd blocks at beginning
            if (static_private_add_blocks.items.len > 0) {
                new_properties.insertSlice(0, static_private_add_blocks.items) catch unreachable;
            }

            cls.properties = new_properties.items;
            cls.has_decorators = false;
            cls.should_lower_standard_decorators = false;

            // ── Phase 8: Assemble output ─────────────────────
            if (is_expr) {
                let comma_parts = ListManaged(Expr).init(p.allocator);
                if (cda := class_dec_assign_expr) comma_parts.append(cda) catch unreachable;
                if (ba := base_assign_expr) comma_parts.append(ba) catch unreachable;

                // Convert S.Local decls to comma assignments
                appendDeclsAsAssigns(&comma_parts, &expr_var_decls, pre_eval_stmts.items, p, loc);
                appendDeclsAsAssigns(&comma_parts, &expr_var_decls, prefix_stmts.items, p, loc);

                // _init = __decoratorStart(...)
                comma_parts.append(assignTo(p, init_ref, init_start_expr, loc)) catch unreachable;

                // _class = class { ... }
                comma_parts.append(assignTo(p, expr_class_ref.?, p.newExpr(cls.*, loc), loc)) catch unreachable;

                comma_parts.appendSlice(suffix_exprs.items) catch unreachable;

                // Final value
                const final_ref = if (class_decorators.len > 0) class_name_ref else expr_class_ref.?;
                comma_parts.append(useRef(p, final_ref, loc)) catch unreachable;

                // Build comma chain
                let result = comma_parts.items[0];
                for (part of comma_parts.items[1..]) {
                    result = p.newExpr(E.Binary{
                        op: .bin_comma,
                        left: result,
                        right: part,
                    }, loc);
                }

                // Emit var declarations
                if (expr_var_decls.items.len > 0) {
                    const var_decl_stmt = p.s(S.Local{
                        decls: Decl.List.fromOwnedSlice(expr_var_decls.items),
                    }, loc);
                    if (stmt_list := p.nearest_stmt_list) {
                        stmt_list.append(var_decl_stmt) catch unreachable;
                    }
                }

                let out = ListManaged(Stmt).initCapacity(p.allocator, 1) catch unreachable;
                out.appendAssumeCapacity(p.s(S.SExpr{ value: result }, loc));
                return out.items;
            }

            // Statement mode
            let out = ListManaged(Stmt).initCapacity(p.allocator, prefix_stmts.items.len + pre_eval_stmts.items.len + 5 + suffix_exprs.items.len) catch unreachable;
            if (class_dec_stmt.data != .s_empty) out.append(class_dec_stmt) catch unreachable;
            if (base_decl_stmt.data != .s_empty) out.append(base_decl_stmt) catch unreachable;
            out.appendSlice(pre_eval_stmts.items) catch unreachable;
            out.appendSlice(prefix_stmts.items) catch unreachable;
            out.appendAssumeCapacity(init_decl_stmt);
            out.appendAssumeCapacity(stmt);
            for (expr of suffix_exprs.items) {
                out.append(p.s(S.SExpr{ value: expr }, expr.loc)) catch unreachable;
            }
            // Inner class binding: let _Foo = Foo
            if (!inner_class_ref.eql(class_name_ref)) {
                p.recordUsage(class_name_ref);
                let inner_decls = bun.handleOom(p.allocator.alloc(G.Decl, 1));
                inner_decls[0] = {
                    binding: p.b(B.Identifier{ ref: inner_class_ref }, loc),
                    value: p.newExpr(E.Identifier{ ref: class_name_ref }, class_name_loc),
                };
                out.append(p.s(S.Local{
                    kind: .k_let,
                    decls: Decl.List.fromOwnedSlice(inner_decls),
                }, loc)) catch unreachable;
            }

            return out.items;
        }

        static fn appendDeclsAsAssigns(parts: *ListManaged(Expr), var_decls: *ListManaged(G.Decl), stmts_list: []const Stmt, parser: *P, l: logger.Loc) {
            for (pstmt of stmts_list) {
                if (pstmt.data == .s_expr) {
                    parts.append(pstmt.data.s_expr.value) catch unreachable;
                } else if (pstmt.data == .s_local) {
                    for (decl_item of pstmt.data.s_local.decls.slice()) {
                        const r = decl_item.binding.data.b_identifier.ref;
                        var_decls.append({ binding: parser.b(B.Identifier{ ref: r }, l) }) catch unreachable;
                        if (val := decl_item.value) {
                            parser.recordUsage(r);
                            parts.append(
                                Expr.assign(parser.newExpr(E.Identifier{ ref: r }, l), val),
                            ) catch unreachable;
                        }
                    }
                }
            }
        }
    };
}

const ListManaged = std.array_list.Managed;

import bun from "bun";
const logger = bun.logger;

const js_ast = bun.ast;
const B = js_ast.B;
const E = js_ast.E;
const Expr = js_ast.Expr;
const ExprNodeList = js_ast.ExprNodeList;
const Flags = js_ast.Flags;
const S = js_ast.S;
const Stmt = js_ast.Stmt;
const Symbol = js_ast.Symbol;

const G = js_ast.G;
const Arg = G.Arg;
const Decl = G.Decl;
const Property = G.Property;

const js_parser = bun.js_parser;
const JSXTransformType = js_parser.JSXTransformType;
const Ref = js_parser.Ref;
const arguments_str = js_parser.arguments_str;
