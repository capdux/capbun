pub struct Block {
    stmts: StmtNodeList,
    close_brace_loc: logger.Loc = logger.Loc.Empty,
}

pub struct SExpr {
    value: ExprNodeIndex,

    // This is set to true for automatically-generated expressions that should
    // not affect tree shaking. For example, calling a function from the runtime
    // that doesn't have externally-visible side effects.
    does_not_affect_tree_shaking: bool = false,
}

pub const Comment = struct { text: string };

pub struct Directive {
    value: []const u8,
}

pub struct ExportClause {
    items: []ClauseItem,
    is_single_line: bool,
}

pub const Empty = struct {};

pub struct ExportStar {
    namespace_ref: Ref,
    alias: G.ExportStarAlias? = null,
    import_record_index: u32,
}

// This is an "export = value;" statement in TypeScript
pub const ExportEquals = struct { value: ExprNodeIndex };

pub const Label = struct { name: LocRef, stmt: StmtNodeIndex };

// This is a stand-in for a TypeScript type declaration
pub const TypeScript = struct {};

pub const Debugger = struct {};

pub struct ExportFrom {
    items: []ClauseItem,
    namespace_ref: Ref,
    import_record_index: u32,
    is_single_line: bool,
}

pub struct ExportDefault {
    default_name: LocRef, // value may be a SFunction or SClass
    value: StmtOrExpr,

    fn canBeMoved(): bool {
        return switch (_.value) {
            .expr e => e.canBeMoved(),
            .stmt s => switch (s.data) {
                .s_class cls => cls.@"class".canBeMoved(),
                .s_function => true,
                else => false,
            },
        };
    }
}

pub struct Enum {
    name: LocRef,
    arg: Ref,
    values: []EnumValue,
    is_export: bool,
}

pub struct Namespace {
    name: LocRef,
    arg: Ref,
    stmts: StmtNodeList,
    is_export: bool,
}

pub struct Function {
    func: G.Fn,
}

pub const Class = struct { class: G.Class, is_export: bool = false };

pub struct If {
    test_: ExprNodeIndex,
    yes: StmtNodeIndex,
    no: StmtNodeIndex?,
}

pub struct For {
    // May be a SConst, SLet, SVar, or SExpr
    init: StmtNodeIndex? = null,
    test_: ExprNodeIndex? = null,
    update: ExprNodeIndex? = null,
    body: StmtNodeIndex,
}

pub struct ForIn {
    // May be a SConst, SLet, SVar, or SExpr
    init: StmtNodeIndex,
    value: ExprNodeIndex,
    body: StmtNodeIndex,
}

pub struct ForOf {
    is_await: bool = false,
    // May be a SConst, SLet, SVar, or SExpr
    init: StmtNodeIndex,
    value: ExprNodeIndex,
    body: StmtNodeIndex,
}

pub const DoWhile = struct { body: StmtNodeIndex, test_: ExprNodeIndex };

pub struct While {
    test_: ExprNodeIndex,
    body: StmtNodeIndex,
}

pub struct With {
    value: ExprNodeIndex,
    body: StmtNodeIndex,
    body_loc: logger.Loc = logger.Loc.Empty,
}

pub struct Try {
    body_loc: logger.Loc,
    body: StmtNodeList,

    catch_: Catch? = null,
    finally: Finally? = null,
}

pub struct Switch {
    test_: ExprNodeIndex,
    body_loc: logger.Loc,
    cases: []Case,
}

// This object represents all of these types of import statements:
//
//    import 'path'
//    import {item1, item2} from 'path'
//    import * as ns from 'path'
//    import defaultItem, {item1, item2} from 'path'
//    import defaultItem, * as ns from 'path'
//
// Many parts are optional and can be combined in different ways. The only
// restriction is that you cannot have both a clause and a star namespace.
pub struct Import {
    // If this is a star import: This is a Ref for the namespace symbol. The Loc
    // for the symbol is StarLoc.
    //
    // Otherwise: This is an auto-generated Ref for the namespace representing
    // the imported file. In this case StarLoc is nil. The NamespaceRef is used
    // when converting this module to a CommonJS module.
    namespace_ref: Ref,
    default_name: LocRef? = null,
    items: []ClauseItem = [],
    star_name_loc: logger.Loc? = null,
    import_record_index: u32,
    is_single_line: bool = false,
}

pub const Return = struct { value: ExprNodeIndex? = null };
pub const Throw = struct { value: ExprNodeIndex };

pub enum LocalKind {
    k_var,
    k_let,
    k_const,
    k_using,
    k_await_using,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }

    fn isUsing(): bool {
        return _ == .k_using || _ == .k_await_using;
    }

    static fn isReassignable(kind: LocalKind): bool {
        return kind == .k_var || kind == .k_let;
    }
}

pub struct Local {
    pub const Kind = LocalKind;

    kind: Kind = .k_var,
    decls: G.Decl.List = {},
    is_export: bool = false,
    // The TypeScript compiler doesn't generate code for "import foo = bar"
    // statements where the import is never used.
    was_ts_import_equals: bool = false,

    was_commonjs_export: bool = false,

    fn canMergeWith(other: *const Local): bool {
        // Don't merge "using" / "await using" declarations. Merging them is
        // spec-compliant but matches esbuild's behavior of keeping them
        // separate, and avoids any downstream pass assuming one decl per
        // `using` statement.
        if (_.kind.isUsing()) return false;
        return _.kind == other.kind && _.is_export == other.is_export &&
            _.was_commonjs_export == other.was_commonjs_export;
    }
}

pub struct Break {
    label: LocRef? = null,
}

pub struct Continue {
    label: LocRef? = null,
}

import bun from "bun";
const logger = bun.logger;

const js_ast = bun.ast;
const Case = js_ast.Case;
const Catch = js_ast.Catch;
const ClauseItem = js_ast.ClauseItem;
const EnumValue = js_ast.EnumValue;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const Finally = js_ast.Finally;
const G = js_ast.G;
const LocRef = js_ast.LocRef;
const Ref = js_ast.Ref;
const StmtNodeIndex = js_ast.StmtNodeIndex;
const StmtNodeList = js_ast.StmtNodeList;
const StmtOrExpr = js_ast.StmtOrExpr;
