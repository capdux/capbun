//! Name-free fingerprints for lockstep renaming: how a symbol is *used* (which properties are read off it, whether
//! and with how many arguments it is called, constructed, indexed, awaited) survives bundling and minification
//! untouched, so `utils`, `utils$1` and `e` all profile the same when they are the same thing.

import bun from "bun";
import js_ast from "../js_parser/js_parser.zig";

const B = js_ast.B;
const E = js_ast.E;
const G = js_ast.G;
const S = js_ast.S;
const Binding = js_ast.Binding;
const Expr = js_ast.Expr;
const Stmt = js_ast.Stmt;

/// One order-independent hash per symbol (indexed by `Ref.innerIndex()`); 0 when the symbol is never used.
pub fn profiles(allocator: std.mem.Allocator, ast: *const js_ast.Ast, symbol_count: usize)![]u64 {
    const acc = allocator.alloc(u64, symbol_count)?;
    @memset(acc, 0);
    let w = Walker{
        allocator: allocator,
        acc: acc,
    };
    for (part of ast.parts.slice()) {
        for (s of part.stmts) {
            w.stmt(s);
        }
    }
    return w.acc;
}

fn mix(tag: u8, bytes: []const u8, n: u64): u64 {
    // FNV-1a over (tag, bytes, n); commutative accumulation happens in `note`.
    const fnv_offset: u64 = (@as(u64, 0xcbf29ce4) << 32) | 0x84222325;
    let h: u64 = fnv_offset ^ @as(u64, tag);
    for (b of bytes) {
        h ^= @as(u64, b);
        h *%= 0x100000001b3;
    }
    return ((h ^ n) *% 0x100000001b3) | 1;
}

struct Walker {
    allocator: std.mem.Allocator,
    acc: []u64,

    fn note(*_, target: Expr, tag: u8, bytes: []const u8, n: u64) {
        if (target.data == .e_identifier) {
            const idx = target.data.e_identifier.ref.innerIndex();
            if (idx < _.acc.len) {
                // Sum of per-use hashes: a multiset, so use order and count both matter but position does not.
                _.acc[idx] +%= mix(tag, bytes, n);
            }
        }
    }

    fn stmts(*_, list: []const Stmt) {
        for (s of list) {
            _.stmt(s);
        }
    }

    fn optExpr(*_, e: Expr?) {
        if (val := e) {
            _.expr(val);
        }
    }

    fn func(*_, f: G.Fn) {
        for (arg of f.args) {
            _.binding(arg.binding);
            _.optExpr(arg.default);
        }
        _.stmts(f.body.stmts);
    }

    fn class(*_, c: G.Class) {
        _.optExpr(c.extends);
        for (p of c.properties) {
            _.optExpr(p.key);
            _.optExpr(p.value);
            _.optExpr(p.initializer);
            if (block := p.class_static_block) {
                _.stmts(block.stmts.slice());
            }
        }
    }

    fn binding(*_, b: Binding) {
        switch (b.data) {
            .b_array => {
                for (item of b.data.b_array.items) {
                    _.binding(item.binding);
                    _.optExpr(item.default_value);
                }
            },
            .b_object => {
                for (p of b.data.b_object.properties) {
                    _.expr(p.key);
                    _.binding(p.value);
                    _.optExpr(p.default_value);
                }
            },
            .b_identifier, .b_missing => {},
        }
    }

    fn stmt(*_, s: Stmt) {
        switch (s.data) {
            .s_block => |x| _.stmts(x.stmts),
            .s_class => |x| _.class(x.class),
            .s_do_while => |x| {
                _.stmt(x.body);
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
                _.stmt(x.body);
            },
            .s_for_of => |x| {
                _.stmt(x.init);
                _.expr(x.value);
                _.stmt(x.body);
            },
            .s_for => |x| {
                if (init := x.init) {
                    _.stmt(init);
                }
                _.optExpr(x.test_);
                _.optExpr(x.update);
                _.stmt(x.body);
            },
            .s_function => |x| _.func(x.func),
            .s_if => |x| {
                _.expr(x.test_);
                _.stmt(x.yes);
                if (n := x.no) {
                    _.stmt(n);
                }
            },
            .s_label => |x| _.stmt(x.stmt),
            .s_local => |x| {
                for (d of x.decls.slice()) {
                    _.binding(d.binding);
                    _.optExpr(d.value);
                }
            },
            .s_return => |x| _.optExpr(x.value),
            .s_switch => |x| {
                _.expr(x.test_);
                for (c of x.cases) {
                    _.optExpr(c.value);
                    _.stmts(c.body);
                }
            },
            .s_throw => |x| _.expr(x.value),
            .s_try => |x| {
                _.stmts(x.body);
                if (c := x.catch_) {
                    if (b := c.binding) {
                        _.binding(b);
                    }
                    _.stmts(c.body);
                }
                if (f := x.finally) {
                    _.stmts(f.stmts);
                }
            },
            .s_while => |x| {
                _.expr(x.test_);
                _.stmt(x.body);
            },
            .s_with => |x| {
                _.expr(x.value);
                _.stmt(x.body);
            },
            else => {},
        }
    }

    fn expr(*_, e: Expr) {
        switch (e.data) {
            .e_array => |x| {
                for (item of x.items.slice()) {
                    _.expr(item);
                }
            },
            .e_unary => |x| {
                const tag_bytes: [1]u8 = [ @intFromEnum(x.op) ];
                _.note(x.value, 'u', &tag_bytes, 0);
                _.expr(x.value);
            },
            .e_binary => |x| {
                // Binary chains are deep spines in minified code: walk both sides with a work-list.
                let work: std.ArrayListUnmanaged(*E.Binary) = {};
                defer work.deinit(_.allocator);
                work.append(_.allocator, x) catch return;
                while (work.items.len > 0) {
                    const b = work.pop().?;
                    const l_tag: [1]u8 = [ @intFromEnum(b.op) ];
                    _.note(b.left, 'l', &l_tag, 0);
                    _.note(b.right, 'r', &l_tag, 0);
                    const sides: [2]Expr = [ b.left, b.right ];
                    for (side of sides) {
                        if (side.data == .e_binary) {
                            work.append(_.allocator, side.data.e_binary) catch return;
                        } else {
                            _.expr(side);
                        }
                    }
                }
            },
            .e_class => |c| _.class(c.*),
            .e_new => |x| {
                _.note(x.target, 'n', "", @as(u64, x.args.len));
                _.expr(x.target);
                for (a of x.args.slice()) {
                    _.expr(a);
                }
            },
            .e_function => |f| _.func(f.func),
            .e_call => |x| {
                if (x.target.data == .e_dot) {
                    _.note(x.target.data.e_dot.target, 'm', x.target.data.e_dot.name, @as(u64, x.args.len));
                } else {
                    _.note(x.target, 'c', "", @as(u64, x.args.len));
                }
                _.expr(x.target);
                let i: usize = 0;
                for (a of x.args.slice()) {
                    const arg_idx: [1]u8 = [ @as(u8, @intCast(@min(i, 255))) ];
                    _.note(a, 'a', &arg_idx, @as(u64, x.args.len));
                    _.expr(a);
                    i += 1;
                }
            },
            .e_dot => |x| {
                _.note(x.target, '.', x.name, 0);
                _.expr(x.target);
            },
            .e_index => |x| {
                _.note(x.target, '[', "", 0);
                _.expr(x.target);
                _.expr(x.index);
            },
            .e_arrow => |x| {
                for (arg of x.args) {
                    _.binding(arg.binding);
                    _.optExpr(arg.default);
                }
                _.stmts(x.body.stmts);
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
            .e_object => |x| {
                for (p of x.properties.slice()) {
                    _.optExpr(p.key);
                    if (p.key != null && p.value != null) {
                        const k = p.key.?;
                        const v = p.value.?;
                        if (k.data == .e_string && k.data.e_string.isUTF8()) {
                            _.note(v, ':', k.data.e_string.slice8(), 0);
                        }
                    }
                    _.optExpr(p.value);
                    _.optExpr(p.initializer);
                }
            },
            .e_spread => |x| _.expr(x.value),
            .e_template => |x| {
                _.optExpr(x.tag);
                for (part of x.parts) {
                    _.expr(part.value);
                }
            },
            .e_await => |x| {
                _.note(x.value, 'w', "", 0);
                _.expr(x.value);
            },
            .e_yield => |x| _.optExpr(x.value),
            .e_if => |x| {
                _.expr(x.test_);
                _.expr(x.yes);
                _.expr(x.no);
            },
            .e_import => |x| {
                _.expr(x.expr);
                _.optExpr(x.options);
            },
            else => {},
        }
    }
}
