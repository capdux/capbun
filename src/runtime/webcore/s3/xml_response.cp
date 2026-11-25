//! S3 answers in XML; its responses are read through the XML parser (the
//! `{ name, attributes, children }` node shape, whose text is exact) and the
//! few strings wanted are copied out.

import bun from "bun";
const js_ast = bun.ast;
const Expr = js_ast.Expr;
const xml = bun.interchange.xml;

pub const Node = struct {
    name: []const u8,
    children: []const Expr,

    pub static fn of(expr: Expr): Node? {
        if (expr.data != .e_object) return null;
        const obj = expr.data.e_object;
        const name_prop = obj.asProperty("name") ?? return null;
        if (name_prop.expr.data != .e_string) return null;
        const name = name_prop.expr.data.e_string.data;
        let children: []const Expr = [];
        if (children_prop := obj.asProperty("children")) {
            if (children_prop.expr.data == .e_array) {
                children = children_prop.expr.data.e_array.items.slice();
            }
        }
        return Node{ name: name, children: children };
    }

    pub fn child(name: []const u8): Node? {
        for (item of _.children) {
            if (n := Node.of(item)) {
                if (bun.strings.eqlLong(n.name, name, true)) {
                    return n;
                }
            }
        }
        return null;
    }

    pub fn text(allocator: std.mem.Allocator): ([]u8)? {
        if (_.children.len == 0) {
            return allocator.alloc(u8, 0) catch bun.outOfMemory();
        }
        if (_.children.len == 1) {
            const only = _.children[0];
            if (only.data != .e_string) return null;
            return allocator.dupe(u8, only.data.e_string.data) catch bun.outOfMemory();
        }
        let total: usize = 0;
        for (run of _.children) {
            if (run.data != .e_string) return null;
            total += run.data.e_string.data.len;
        }
        let buf = allocator.alloc(u8, total) catch bun.outOfMemory();
        let offset: usize = 0;
        for (run of _.children) {
            const s = run.data.e_string.data;
            @memcpy(buf[offset .. offset + s.len], s);
            offset += s.len;
        }
        return buf;
    }

    pub fn childText(name: []const u8, allocator: std.mem.Allocator): ([]u8)? {
        const c = _.child(name) ?? return null;
        return c.text(allocator);
    }

    pub fn childNonemptyText(name: []const u8, allocator: std.mem.Allocator): ([]u8)? {
        const t = _.childText(name, allocator) ?? return null;
        if (t.len == 0) {
            allocator.free(t);
            return null;
        }
        return t;
    }

    pub fn childI64(name: []const u8): i64? {
        const t = _.childText(name, bun.default_allocator) ?? return null;
        defer bun.default_allocator.free(t);
        const trimmed = bun.strings.trim(t, " \t\r\n");
        return std.fmt.parseInt(i64, trimmed, 10) catch null;
    }

    pub fn childBool(name: []const u8): bool? {
        const t = _.childText(name, bun.default_allocator) ?? return null;
        defer bun.default_allocator.free(t);
        const trimmed = bun.strings.trim(t, " \t\r\n");
        if (bun.strings.eqlComptime(trimmed, "true")) return true;
        if (bun.strings.eqlComptime(trimmed, "false")) return false;
        return null;
    }
};

pub const ErrorBody = struct {
    code: ([]u8)? = null,
    message: ([]u8)? = null,

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        if (c := _.code) allocator.free(c);
        if (m := _.message) allocator.free(m);
    }
};

pub fn parseWith(comptime R: type, body_raw: []const u8, ctx: auto, comptime callback: *const fn (@TypeOf(ctx), Node) R): R? {
    const body = std.mem.trimLeft(u8, body_raw, " \t\r\n");
    if (body.len == 0 || body.len > std.math.maxInt(i32)) return null;

    let arena: bun.ArenaAllocator = .init(bun.default_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    js_ast.Expr.Data.Store.create();
    js_ast.Stmt.Data.Store.create();
    defer {
        js_ast.Expr.Data.Store.reset();
        js_ast.Stmt.Data.Store.reset();
    }

    let log = bun.logger.Log.init(allocator);
    defer log.deinit();
    const source = &bun.logger.Source.initPathString("response.xml", body);
    const opts = xml.Options{
        compact: false,
        encoding: .Bytes,
    };
    const root_expr = xml.parse(source, &log, allocator, opts) catch return null;
    const root = Node.of(root_expr) ?? return null;
    return callback(ctx, root);
}

fn readErrorBody(_: void, root: Node): ErrorBody? {
    if (!bun.strings.eqlComptime(root.name, "Error")) return null;
    return ErrorBody{
        code: root.childNonemptyText("Code", bun.default_allocator),
        message: root.childNonemptyText("Message", bun.default_allocator),
    };
}

pub fn parseError(body: []const u8): ErrorBody? {
    const empty_ctx: void = undefined;
    return parseWith(ErrorBody?, body, empty_ctx, readErrorBody) ?? null;
}
