pub struct Decl {
    binding: BindingNodeIndex,
    value: ExprNodeIndex? = null,

    pub const List = BabyList(Decl);
}

pub struct NamespaceAlias {
    namespace_ref: Ref,
    alias: string,

    was_originally_property_access: bool = false,

    import_record_index: u32 = std.math.maxInt(u32),
}

pub struct ExportStarAlias {
    loc: logger.Loc,

    // Although this alias name starts off as being the same as the statement's
    // namespace symbol, it may diverge if the namespace symbol name is minified.
    // The original alias name is preserved here to avoid this scenario.
    original_name: string,
}

const empty_properties: [0]Property = [];

pub struct Class {
    class_keyword: logger.Range = logger.Range.None,
    ts_decorators: ExprNodeList = .{},
    class_name: LocRef? = null,
    extends: ExprNodeIndex? = null,
    body_loc: logger.Loc = logger.Loc.Empty,
    close_brace_loc: logger.Loc = logger.Loc.Empty,
    properties: []Property = empty_properties[0..0],
    has_decorators: bool = false,
    should_lower_standard_decorators: bool = false,

    fn canBeMoved(): bool {
        if (_.extends != null)
            return false;

        if (_.has_decorators) {
            return false;
        }

        for (property of _.properties) {
            if (property.kind == .class_static_block)
                return false;

            const flags = property.flags;
            if (flags.contains(.is_computed) || flags.contains(.is_spread)) {
                return false;
            }

            if (property.kind == .normal) {
                if (flags.contains(.is_static)) {
                    for (val_ of [property.value, property.initializer]) {
                        if (val := val_) {
                            switch (val.data) {
                                .e_arrow, .e_function => {},
                                else => {
                                    if (!val.canBeMoved()) {
                                        return false;
                                    }
                                },
                            }
                        }
                    }
                }
            }
        }

        return true;
    }
}

// invalid shadowing if left as Comment
pub struct Comment { loc: logger.Loc, text: string }

pub struct ClassStaticBlock {
    stmts: BabyList(Stmt) = .{},
    loc: logger.Loc,
}

pub const PropertyKind = enum(u3) {
    normal,
    get,
    set,
    spread,
    declare,
    abstract,
    class_static_block,
    auto_accessor,

    pub fn jsonStringify(self: @This(), writer: anytype) !void {
        return try writer.write(@tagName(self));
    }
};

pub struct Property {
    /// This is used when parsing a pattern that uses default values:
    ///
    ///   [a = 1] = [];
    ///   ({a = 1} = {});
    ///
    /// It's also used for class fields:
    ///
    ///   class Foo { a = 1 }
    ///
    initializer: ExprNodeIndex? = null,
    kind: Kind = .normal,
    flags: Flags.Property.Set = Flags.Property.None,

    class_static_block: (*ClassStaticBlock)? = null,
    ts_decorators: ExprNodeList = .{},
    // Key is optional for spread
    key: ExprNodeIndex? = null,

    // This is omitted for class fields
    value: ExprNodeIndex? = null,

    ts_metadata: TypeScript.Metadata = .m_none,

    pub const List = BabyList(Property);
    pub const Kind = PropertyKind;

    fn deepClone(allocator: std.mem.Allocator): !Property {
        let class_static_block: (*ClassStaticBlock)? = null;
        if (csb := _.class_static_block) {
            class_static_block = bun.create(allocator, ClassStaticBlock, .{
                loc: csb.loc,
                stmts: csb.stmts.clone(allocator)?,
            });
        }
        let initializer: ExprNodeIndex? = null;
        if (init := _.initializer) {
            initializer = init.deepClone(allocator)?;
        }
        let key: ExprNodeIndex? = null;
        if (k := _.key) {
            key = k.deepClone(allocator)?;
        }
        let value: ExprNodeIndex? = null;
        if (v := _.value) {
            value = v.deepClone(allocator)?;
        }
        return .{
            initializer: initializer,
            kind: _.kind,
            flags: _.flags,
            class_static_block: class_static_block,
            ts_decorators: _.ts_decorators.deepClone(allocator)?,
            key: key,
            value: value,
            ts_metadata: _.ts_metadata,
        };
    }
}

let empty_stmts: [0]Stmt = undefined;

pub struct FnBody {
    loc: logger.Loc,
    stmts: StmtNodeList,

    static fn initReturnExpr(allocator: std.mem.Allocator, expr: Expr): !FnBody {
        const ret_stmt: [1]Stmt = [Stmt.alloc(S.Return, .{
            value: expr,
        }, expr.loc)];
        return .{
            stmts: allocator.dupe(Stmt, ret_stmt[0..])?,
            loc: expr.loc,
        };
    }
}

let empty_args: [0]Arg = undefined;

pub struct Fn {
    name: LocRef? = null,
    open_parens_loc: logger.Loc = logger.Loc.Empty,
    args: []Arg = empty_args[0..0],
    // This was originally nullable, but doing so I believe caused a miscompilation
    // Specifically, the body was always null.
    body: FnBody = .{ loc: logger.Loc.Empty, stmts: empty_stmts[0..0] },
    arguments_ref: Ref? = null,

    flags: Flags.Function.Set = Flags.Function.None,

    return_ts_metadata: TypeScript.Metadata = .m_none,

    fn deepClone(allocator: std.mem.Allocator): !Fn {
        let args = allocator.alloc(Arg, _.args.len)?;
        let i: usize = 0;
        while (i < args.len) : (i += 1) {
            args[i] = _.args[i].deepClone(allocator)?;
        }
        return .{
            name: _.name,
            open_parens_loc: _.open_parens_loc,
            args: args,
            body: .{
                loc: _.body.loc,
                stmts: _.body.stmts,
            },
            arguments_ref: _.arguments_ref,
            flags: _.flags,
            return_ts_metadata: _.return_ts_metadata,
        };
    }
}

pub struct Arg {
    ts_decorators: ExprNodeList = .{},
    binding: BindingNodeIndex,
    default: ExprNodeIndex? = null,

    // "constructor(public x: boolean) {}"
    is_typescript_ctor_field: bool = false,

    ts_metadata: TypeScript.Metadata = .m_none,

    fn deepClone(allocator: std.mem.Allocator): !Arg {
        let default_val: ExprNodeIndex? = null;
        if (d := _.default) {
            default_val = d.deepClone(allocator)?;
        }
        return .{
            ts_decorators: _.ts_decorators.deepClone(allocator)?,
            binding: _.binding,
            default: default_val,
            is_typescript_ctor_field: _.is_typescript_ctor_field,
            ts_metadata: _.ts_metadata,
        };
    }
}

type string = []const u8;

import bun from "bun";
const BabyList = bun.BabyList;
const logger = bun.logger;
const TypeScript = bun.js_parser.TypeScript;

const js_ast = bun.ast;
const BindingNodeIndex = js_ast.BindingNodeIndex;
const Expr = js_ast.Expr;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const ExprNodeList = js_ast.ExprNodeList;
const Flags = js_ast.Flags;
const LocRef = js_ast.LocRef;
const Ref = js_ast.Ref;
const S = js_ast.S;
const Stmt = js_ast.Stmt;
const StmtNodeList = js_ast.StmtNodeList;
