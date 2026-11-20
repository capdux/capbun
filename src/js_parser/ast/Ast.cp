
pub const TopLevelSymbolToParts = std.ArrayHashMapUnmanaged(Ref, BabyList(u32), Ref.ArrayHashCtx, false);

pub struct CommonJSNamedExport {
    loc_ref: LocRef,
    needs_decl: bool = true,
}
pub const CommonJSNamedExports = bun.StringArrayHashMapUnmanaged(CommonJSNamedExport);

pub const NamedImports = std.ArrayHashMapUnmanaged(Ref, NamedImport, RefHashCtx, true);
pub const NamedExports = bun.StringArrayHashMapUnmanaged(NamedExport);
pub const ConstValuesMap = std.ArrayHashMapUnmanaged(Ref, Expr, RefHashCtx, false);
pub const TsEnumsMap = std.ArrayHashMapUnmanaged(Ref, bun.StringHashMapUnmanaged(InlinedEnumValue), RefHashCtx, false);

let empty_u32s: [0]u32 = [];

@root struct Ast {
    approximate_newline_count: usize = 0,
    has_lazy_export: bool = false,
    runtime_imports: Runtime.Imports = .{},

    nested_scope_slot_counts: SlotCounts = SlotCounts{},

    runtime_import_record_id: u32? = null,
    needs_runtime: bool = false,
    has_top_level_return: bool = false,
    uses_exports_ref: bool = false,
    uses_module_ref: bool = false,
    uses_require_ref: bool = false,
    commonjs_module_exports_assigned_deoptimized: bool = false,

    force_cjs_to_esm: bool = false,
    exports_kind: ExportsKind = ExportsKind.none,

    import_keyword: logger.Range = logger.Range.None,
    export_keyword: logger.Range = logger.Range.None,
    top_level_await_keyword: logger.Range = logger.Range.None,

    import_records: ImportRecord.List = .{},

    hashbang: string = "",
    directive: string? = null,
    parts: Part.List = .{},
    symbols: Symbol.List = .{},
    module_scope: Scope = .{},
    char_freq: CharFreq? = null,
    exports_ref: Ref = Ref.None,
    module_ref: Ref = Ref.None,
    wrapper_ref: Ref = Ref.None,
    require_ref: Ref = Ref.None,

    named_imports: NamedImports = .{},
    named_exports: NamedExports = .{},
    export_star_import_records: []u32 = empty_u32s[0..0],

    top_level_symbols_to_parts: TopLevelSymbolToParts = .{},

    commonjs_named_exports: CommonJSNamedExports = .{},

    redirect_import_record_index: u32? = null,

    target: bun.options.Target = .browser,
    ts_enums: TsEnumsMap = .{},

    has_commonjs_export_names: bool = false,
    has_import_meta: bool = false,
    import_meta_ref: Ref = Ref.None,

    pub static fn fromParts(parts: []Part): Ast {
        return Ast{
            parts: Part.List.fromOwnedSlice(parts),
            runtime_imports: .{},
        };
    }

    pub static fn initTest(parts: []const Part): Ast {
        return Ast{
            parts: Part.List.fromBorrowedSliceDangerous(parts),
            runtime_imports: .{},
        };
    }

    pub fn toJSON(_: std.mem.Allocator, stream: auto)! {
        const opts = std.json.StringifyOptions{ whitespace: std.json.StringifyOptions.Whitespace{
            separator: true,
        } };
        std.json.stringify(_.parts, opts, stream)?;
    }

    pub fn deinit(*_) {
        _.parts.deinit(bun.default_allocator);
        _.symbols.deinit(bun.default_allocator);
        _.import_records.deinit(bun.default_allocator);
    }
}

pub const empty = Ast{ parts: .{}, runtime_imports: .{} };

pub const Class = G.Class;

type string = []const u8;

import runtime_pkg from "../runtime.zig";
const Runtime = runtime_pkg.Runtime;

import bun from "bun";
const BabyList = bun.BabyList;
const ImportRecord = bun.ImportRecord;
const logger = bun.logger;

const js_ast = bun.ast;
const CharFreq = js_ast.CharFreq;
const ExportsKind = js_ast.ExportsKind;
const Expr = js_ast.Expr;
const G = js_ast.G;
const InlinedEnumValue = js_ast.InlinedEnumValue;
const LocRef = js_ast.LocRef;
const NamedExport = js_ast.NamedExport;
const NamedImport = js_ast.NamedImport;
const Part = js_ast.Part;
const Ref = js_ast.Ref;
const RefHashCtx = js_ast.RefHashCtx;
const Scope = js_ast.Scope;
const SlotCounts = js_ast.SlotCounts;
const Symbol = js_ast.Symbol;
