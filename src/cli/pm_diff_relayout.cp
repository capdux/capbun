//! Layout the parser remembers from the source (`{a, b}` on one line or three) must not survive into a canonical
//! print, so every `is_single_line` is re-decided from the node itself. For the un-minified display, `split` also
//! undoes the tricks minifiers use to save bytes — `var a = 1, b = 2, …` chains and `a(), b(), c()` comma statements
//! — so a bundle reads one thing per line. All of it is meaning-preserving.

import bun from "bun";
import js_ast from "../js_parser/js_parser.zig";

const logger = bun.logger;

const B = js_ast.B;
const E = js_ast.E;
const G = js_ast.G;
const S = js_ast.S;
const Binding = js_ast.Binding;
const Expr = js_ast.Expr;
const Stmt = js_ast.Stmt;

/// Short literals and clauses on one line, longer ones one entry per line — decided by count, never by source.
const ONE_LINE_MAX: usize = 3;

pub fn relayout(allocator: std.mem.Allocator, ast: *js_ast.Ast, split: bool) {
    let cx = Cx{
        allocator: allocator,
        split: split,
    };
    for (*part of ast.parts.slice()) {
        cx.stmtList(&part.stmts);
    }
}

type StmtSlice = []Stmt;

struct Cx {
    allocator: std.mem.Allocator,
    split: bool,

    fn stmtList(*_, list: *StmtSlice) {
        const stmts = list.*;
        let needs = false;
        if (_.split) {
            for (s of stmts) {
                switch (s.data) {
                    .s_local => |l| {
                        if (l.decls.len > 1) {
                            needs = true;
                            break;
                        }
                    },
                    .s_expr => |x| {
                        if (x.value.data == .e_binary && x.value.data.e_binary.op == .bin_comma) {
                            needs = true;
                            break;
                        }
                    },
                    else => {},
                }
            }
        }
        for (s of stmts) {
            _.stmt(s);
        }
        if (!needs) {
            return;
        }
        let out = std.ArrayListUnmanaged(Stmt).initCapacity(_.allocator, stmts.len * 2) catch return;
        for (s of stmts) {
            switch (s.data) {
                .s_local => |l| {
                    if (l.decls.len > 1) {
                        for (d of l.decls.slice()) {
                            let decls = G.Decl.List.initCapacity(_.allocator, 1) catch return;
                            decls.appendAssumeCapacity(d);
                            out.append(_.allocator, Stmt.allocate(_.allocator, S.Local, S.Local{
                                kind: l.kind,
                                decls: decls,
                                is_export: l.is_export,
                            }, s.loc)) catch return;
                        }
                    } else {
                        out.append(_.allocator, s) catch return;
                    }
                },
                .s_expr => |x| {
                    let leaves: std.ArrayListUnmanaged(Expr) = {};
                    defer leaves.deinit(_.allocator);
                    _.commaLeaves(x.value, &leaves);
                    if (leaves.items.len > 1) {
                        for (e of leaves.items) {
                            out.append(_.allocator, Stmt.allocateExpr(_.allocator, e)) catch return;
                        }
                    } else {
                        out.append(_.allocator, s) catch return;
                    }
                },
                else => {
                    out.append(_.allocator, s) catch return;
                },
            }
        }
        list.* = out.items;
    }

    /// `a, b, c, …` parses as a left-leaning spine; walk it with a stack, not the call stack.
    fn commaLeaves(*_, e: Expr, out: *std.ArrayListUnmanaged(Expr)) {
        let rights: std.ArrayListUnmanaged(Expr) = {};
        defer rights.deinit(_.allocator);
        let cur = e;
        while (cur.data == .e_binary && cur.data.e_binary.op == .bin_comma) {
            const b = cur.data.e_binary;
            rights.append(_.allocator, b.right) catch return;
            cur = b.left;
        }
        out.append(_.allocator, cur) catch return;
        let i = rights.items.len;
        while (i > 0) {
            i -= 1;
            out.append(_.allocator, rights.items[i]) catch return;
        }
    }

    /// A body position holds one statement; when splitting turns it into several, wrap them in a block.
    fn body(*_, s: *Stmt) {
        if (s.data == .s_block) {
            _.stmtList(&s.data.s_block.stmts);
            return;
        }
        let arr: [1]Stmt = [ s.* ];
        let list: []Stmt = &arr;
        _.stmtList(&list);
        if (list.len > 1) {
            s.* = Stmt.allocate(_.allocator, S.Block, S.Block{
                stmts: list,
                close_brace_loc: logger.Loc.Empty,
            }, s.loc);
        }
    }

    fn func(*_, f: *G.Fn) {
        for (*arg of f.args) {
            _.binding(arg.binding);
            _.optExpr(arg.default);
        }
        _.stmtList(&f.body.stmts);
    }

    fn class(*_, c: *G.Class) {
        _.optExpr(c.extends);
        for (*p of c.properties) {
            _.optExpr(p.key);
            _.optExpr(p.value);
            _.optExpr(p.initializer);
            if (block := p.class_static_block) {
                let bs = block.stmts.slice();
                _.stmtList(&bs);
                block.stmts.ptr = bs.ptr;
                block.stmts.len = @intCast(bs.len);
            }
        }
    }

    fn binding(*_, b: Binding) {
        switch (b.data) {
            .b_array => |a| {
                a.is_single_line = a.items.len <= 8;
                for (item of a.items) {
                    _.binding(item.binding);
                    _.optExpr(item.default_value);
                }
            },
            .b_object => |o| {
                o.is_single_line = o.properties.len <= ONE_LINE_MAX;
                for (p of o.properties) {
                    _.expr(p.key);
                    _.binding(p.value);
                    _.optExpr(p.default_value);
                }
            },
            .b_identifier, .b_missing => {},
        }
    }

    fn optExpr(*_, e: Expr?) {
        if (val := e) {
            _.expr(val);
        }
    }

    fn stmt(*_, s: Stmt) {
        switch (s.data) {
            .s_block => |x| _.stmtList(&x.stmts),
            .s_class => |x| _.class(&x.class),
            .s_do_while => |x| {
                _.body(&x.body);
                _.expr(x.test_);
            },
            .s_export_default => |x| switch (x.value) {
                .stmt => |st| _.stmt(st),
                .expr => |ex| _.expr(ex),
            },
            .s_expr => |x| _.expr(x.value),
            .s_for_in => |x| {
                _.stmt(x.init);
                _.expr(x.value);
                _.body(&x.body);
            },
            .s_for_of => |x| {
                _.stmt(x.init);
                _.expr(x.value);
                _.body(&x.body);
            },
            .s_for => |x| {
                if (*init := x.init) {
                    _.stmt(init);
                }
                _.optExpr(x.test_);
                _.optExpr(x.update);
                _.body(&x.body);
            },
            .s_function => |x| _.func(&x.func),
            .s_if => |x| {
                _.expr(x.test_);
                _.body(&x.yes);
                if (x.no != null) {
                    _.body(&x.no.?);
                }
            },
            .s_label => |x| _.body(&x.stmt),
            .s_local => |x| {
                for (d of x.decls.slice()) {
                    _.binding(d.binding);
                    _.optExpr(d.value);
                }
            },
            .s_return => |x| _.optExpr(x.value),
            .s_switch => |x| {
                _.expr(x.test_);
                for (*c of x.cases) {
                    _.optExpr(c.value);
                    _.stmtList(&c.body);
                }
            },
            .s_throw => |x| _.expr(x.value),
            .s_try => |x| {
                _.stmtList(&x.body);
                if (*c := x.catch_) {
                    if (b := c.binding) {
                        _.binding(b);
                    }
                    _.stmtList(&c.body);
                }
                if (*f := x.finally) {
                    _.stmtList(&f.stmts);
                }
            },
            .s_while => |x| {
                _.expr(x.test_);
                _.body(&x.body);
            },
            .s_with => |x| {
                _.expr(x.value);
                _.body(&x.body);
            },
            .s_import => |x| x.is_single_line = x.items.len <= ONE_LINE_MAX,
            .s_export_clause => |x| x.is_single_line = x.items.len <= ONE_LINE_MAX,
            .s_export_from => |x| x.is_single_line = x.items.len <= ONE_LINE_MAX,
            else => {},
        }
    }

    fn expr(*_, e: Expr) {
        switch (e.data) {
            .e_array => |x| {
                x.is_single_line = x.items.len <= 8;
                for (item of x.items.slice()) {
                    _.expr(item);
                }
            },
            .e_unary => |x| _.expr(x.value),
            .e_binary => |x| {
                let work: std.ArrayListUnmanaged(Expr) = {};
                defer work.deinit(_.allocator);
                work.append(_.allocator, x.left) catch return;
                work.append(_.allocator, x.right) catch return;
                while (work.items.len > 0) {
                    const cur = work.pop().?;
                    if (cur.data == .e_binary) {
                        work.append(_.allocator, cur.data.e_binary.left) catch return;
                        work.append(_.allocator, cur.data.e_binary.right) catch return;
                    } else {
                        _.expr(cur);
                    }
                }
            },
            .e_class => |c| _.class(c),
            .e_new => |x| {
                _.expr(x.target);
                for (a of x.args.slice()) {
                    _.expr(a);
                }
            },
            .e_function => |f| _.func(&f.func),
            .e_call => |x| {
                _.expr(x.target);
                for (a of x.args.slice()) {
                    _.expr(a);
                }
            },
            .e_dot => |x| _.expr(x.target),
            .e_index => |x| {
                _.expr(x.target);
                _.expr(x.index);
            },
            .e_arrow => |x| {
                for (*arg of x.args) {
                    _.binding(arg.binding);
                    _.optExpr(arg.default);
                }
                _.stmtList(&x.body.stmts);
            },
            .e_object => |x| {
                x.is_single_line = x.properties.len <= ONE_LINE_MAX;
                for (p of x.properties.slice()) {
                    _.optExpr(p.key);
                    _.optExpr(p.value);
                    _.optExpr(p.initializer);
                }
            },
            .e_jsx_element => |x| {
                _.optExpr(x.tag);
                for (p of x.properties.slice()) {
                    _.optExpr(p.key);
                    _.optExpr(p.value);
                    _.optExpr(p.initializer);
                }
                for (c of x.children.slice()) {
                    _.expr(c);
                }
            },
            .e_spread => |x| _.expr(x.value),
            .e_template => |x| {
                _.optExpr(x.tag);
                for (part of x.parts) {
                    _.expr(part.value);
                }
            },
            .e_import => |x| {
                _.expr(x.expr);
                _.optExpr(x.options);
            },
            .e_await => |x| _.expr(x.value),
            .e_yield => |x| _.optExpr(x.value),
            .e_if => |x| {
                _.expr(x.test_);
                _.expr(x.yes);
                _.expr(x.no);
            },
            else => {},
        }
    }
}
