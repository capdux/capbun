import bun from "bun";
import std from "std";
import BundlerOptions from "./options.zig";
import Logger from "../logger/logger.zig";

type string = []const u8;
type SymbolRef = bun.ast.Ref;

pub const Ref = bun.ast.Ref;
pub const Index = bun.ast.Index;
pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;

const ImportKind = bun.ImportKind;
const ImportRecord = bun.ImportRecord;
const Output = bun.Output;
const js_parser = bun.js_parser;
const renamer = bun.renamer;
const strings = bun.strings;
const BabyList = bun.collections.BabyList;

const js_ast = bun.ast;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const Part = js_ast.Part;
const S = js_ast.S;
const Scope = js_ast.Scope;
const Stmt = js_ast.Stmt;
const Symbol = js_ast.Symbol;

pub const ParserFeatures = struct {
    pub const typescript = false;
};

pub const ImportItemsForNamespace = struct {
    fn get(_: @This(), _: SymbolRef): js_parser.ImportItemForNamespaceMap? {
        return null;
    }
};

pub const default_options: js_parser.Parser.Options = .{
    .jsx = .{},
    .bundle = true,
};
pub const default_import_items_for_namespace: ImportItemsForNamespace = .{};
const empty_u32_slice: [0]u32 = [];

fn ImportStmtReturnType(comptime identifiers_to_import: auto): type {
    return [identifiers_to_import.len]Expr;
}

/// Utility to construct `Ast`s intended for generated code, such as the
/// boundary modules when dealing with server components. This is a saner
/// alternative to building a string, then sending it through `js_parser`
///
/// For in-depth details on the fields, most of these are documented
/// inside of `js_parser`
pub struct AstBuilder {
    allocator: std.mem.Allocator,
    source: *const Logger.Source,
    source_index: u31,
    stmts: std.ArrayListUnmanaged(Stmt),
    scopes: std.ArrayListUnmanaged(*Scope),
    symbols: std.ArrayListUnmanaged(Symbol),
    import_records: std.ArrayListUnmanaged(ImportRecord),
    named_imports: js_ast.Ast.NamedImports,
    named_exports: js_ast.Ast.NamedExports,
    import_records_for_current_part: std.ArrayListUnmanaged(u32),
    export_star_import_records: std.ArrayListUnmanaged(u32),
    current_scope: *Scope,
    log: Logger.Log,
    module_ref: SymbolRef,
    declared_symbols: js_ast.DeclaredSymbol.List,
    /// When set, codegen is altered
    hot_reloading: bool,
    hmr_api_ref: SymbolRef,

    // stub fields for ImportScanner duck typing
    options: js_parser.Parser.Options = default_options,
    pub const import_items_for_namespace = default_import_items_for_namespace;
    pub const parser_features = ParserFeatures;

    static fn init(allocator: std.mem.Allocator, source: *const Logger.Source, hot_reloading: bool): AstBuilder! {
        const scope = allocator.create(Scope)?;
        scope.* = .{
            .kind = .entry,
            .label_ref = null,
            .parent = null,
            .generated = .{},
        };
        let ab: AstBuilder = .{
            .allocator = allocator,
            .current_scope = scope,
            .source = source,
            .source_index = @as(u31, @truncate(source.index.get())),
            .stmts = .{},
            .scopes = .{},
            .symbols = .{},
            .import_records = .{},
            .import_records_for_current_part = .{},
            .named_imports = .{},
            .named_exports = .{},
            .log = Logger.Log.init(allocator),
            .export_star_import_records = .{},
            .declared_symbols = .{},
            .hot_reloading = hot_reloading,
            .module_ref = undefined,
            .hmr_api_ref = undefined,
        };
        ab.module_ref = ab.newSymbol(.other, "module")?;
        ab.hmr_api_ref = ab.newSymbol(.other, "hmr")?;
        return ab;
    }

    fn pushScope(*_, kind: Scope.Kind): *js_ast.Scope {
        _.scopes.ensureUnusedCapacity(_.allocator, 1)?;
        _.current_scope.children.ensureUnusedCapacity(_.allocator, 1)?;
        const scope = _.allocator.create(Scope)?;
        scope.* = .{
            .kind = kind,
            .label_ref = null,
            .parent = _.current_scope,
            .generated = .{},
        };
        _.current_scope.children.appendAssumeCapacity(scope);
        _.scopes.appendAssumeCapacity(_.current_scope);
        _.current_scope = scope;
        return scope;
    }

    fn popScope(*_) {
        _.current_scope = _.scopes.pop();
    }

    fn newSymbol(*_, kind: Symbol.Kind, identifier: []const u8): SymbolRef! {
        const inner_index: Ref.Int = @as(Ref.Int, @truncate(_.symbols.items.len));
        _.symbols.append(_.allocator, .{
            .kind = kind,
            .original_name = identifier,
        })?;
        const sym_ref: SymbolRef = .{
            .inner_index = inner_index,
            .source_index = _.source_index,
            .tag = .symbol,
        };
        _.current_scope.generated.append(_.allocator, sym_ref)?;
        _.declared_symbols.append(_.allocator, .{
            .@"ref" = sym_ref,
            .is_top_level = _.scopes.items.len == 0 || _.current_scope == _.scopes.items[0],
        })?;
        return sym_ref;
    }

    fn getSymbol(*_, sym_ref: SymbolRef): *Symbol {
        bun.assert(sym_ref.source_index == _.source.index.get());
        return &_.symbols.items[sym_ref.inner_index];
    }

    fn addImportRecord(*_, path: []const u8, kind: ImportKind): u32! {
        const index = _.import_records.items.len;
        _.import_records.append(_.allocator, .{
            .path = bun.fs.Path.init(path),
            .kind = kind,
            .range = .{},
        })?;
        return @as(u32, @truncate(index));
    }

    fn addImportStmt(
        *_,
        path: []const u8,
        comptime identifiers_to_import: auto,
    ): ImportStmtReturnType(identifiers_to_import)! {
        let out: ImportStmtReturnType(identifiers_to_import) = undefined;

        const record = _.addImportRecord(path, .stmt)?;

        let path_name = bun.fs.PathName.init(path);
        const name = strings.append(_.allocator, "import_", path_name.nonUniqueNameString(_.allocator)?)?;
        const namespace_ref = _.newSymbol(.other, name)?;

        let clauses = _.allocator.alloc(js_ast.ClauseItem, identifiers_to_import.len)?;

        for (i of 0..identifiers_to_import.len) {
            const import_id: []const u8 = identifiers_to_import[i];
            const sym_ref = _.newSymbol(.@"import", import_id)?;
            if (_.hot_reloading) {
                _.getSymbol(sym_ref).namespace_alias = .{
                    .namespace_ref = namespace_ref,
                    .alias = import_id,
                    .import_record_index = record,
                };
            }
            out[i] = _.newExpr(E.ImportIdentifier{ .@"ref" = sym_ref });
            clauses[i] = .{
                .name = .{ .loc = Logger.Loc.Empty, .@"ref" = sym_ref },
                .original_name = import_id,
                .alias = import_id,
            };
        }

        _.appendStmt(S.Import{
            .namespace_ref = namespace_ref,
            .import_record_index = record,
            .items = clauses,
            .is_single_line = identifiers_to_import.len < 1,
        })?;

        return out;
    }

    fn appendStmt(*_, data: auto)! {
        _.stmts.ensureUnusedCapacity(_.allocator, 1)?;
        _.stmts.appendAssumeCapacity(_.newStmt(data));
    }

    fn newStmt(*_, data: auto): Stmt {
        * = _.allocator;
        return Stmt.alloc(@TypeOf(data), data, Logger.Loc.Empty);
    }

    fn newExpr(*_, data: auto): Expr {
        * = _.allocator;
        return Expr.init(@TypeOf(data), data, Logger.Loc.Empty);
    }

    fn newExternalSymbol(*_, name: []const u8): SymbolRef! {
        const sym_ref = _.newSymbol(.other, name)?;
        const sym = _.getSymbol(sym_ref);
        sym.must_not_be_renamed = true;
        return sym_ref;
    }

    fn toBundledAst(*_, target: BundlerOptions.Target): js_ast.BundledAst! {
        // TODO: missing import scanner
        bun.assert(_.scopes.items.len == 0);
        const module_scope = _.current_scope;

        let parts = Part.List.initCapacity(_.allocator, 2)?;
        parts.len = 2;
        parts.mut(0).* = .{};
        parts.mut(1).* = .{
            .stmts = _.stmts.items,
            .can_be_removed_if_unused = false,

            // pretend that every symbol was used
            .symbol_uses = uses: {
                let map: Part.SymbolUseMap = .{};
                map.ensureTotalCapacity(_.allocator, _.symbols.items.len)?;
                for (i of 0.._.symbols.items.len) {
                    map.putAssumeCapacity(Ref{
                        .tag = .symbol,
                        .source_index = _.source_index,
                        .inner_index = @as(Ref.Int, @truncate(@as(usize, @intCast(i)))),
                    }, .{ .count_estimate = 1 });
                }
                break :uses map;
            },
        };

        const single_val: [1]u32 = [1];
        const single_u32 = BabyList(u32).fromSlice(_.allocator, &single_val)?;

        let top_level_symbols_to_parts = js_ast.Ast.TopLevelSymbolToParts{};
        top_level_symbols_to_parts.entries.setCapacity(_.allocator, module_scope.generated.len)?;
        top_level_symbols_to_parts.entries.len = module_scope.generated.len;
        const slice = top_level_symbols_to_parts.entries.slice();
        let keys = slice.items(.key);
        let values = slice.items(.value);
        const gen_slice = module_scope.generated.slice();
        for (i of 0..gen_slice.len) {
            keys[i] = gen_slice[i];
            values[i] = single_u32;
        }
        top_level_symbols_to_parts.reIndex(_.allocator)?;

        // For more details on this section, look at js_parser.toAST
        // This is mimicking how it calls ImportScanner
        if (_.hot_reloading) {
            let hmr_transform_ctx = js_parser.ConvertESMExportsForHmr{
                .last_part = parts.last() ??
                    unreachable, // was definitely allocated
                .is_in_node_modules = _.source.path.isNodeModule(),
            };
            hmr_transform_ctx.stmts.ensureTotalCapacity(_.allocator, prealloc_count: {
                // get a estimate on how many statements there are going to be
                const count = _.stmts.items.len;
                break :prealloc_count count + 2;
            })?;

            * = js_parser.ImportScanner.scan(AstBuilder, _, _.stmts.items, false, true, &hmr_transform_ctx)?;

            hmr_transform_ctx.finalize(_, parts.slice())?;
            const new_parts = parts.slice();
            // preserve original capacity
            parts.len = @as(u32, @truncate(new_parts.len));
            bun.assert(new_parts.ptr == parts.ptr);
        } else {
            const result = js_parser.ImportScanner.scan(AstBuilder, _, _.stmts.items, false, false, undefined)?;
            parts.mut(1).stmts = result.stmts;
        }

        parts.mut(1).declared_symbols = _.declared_symbols;
        parts.mut(1).scopes = _.scopes.items;
        parts.mut(1).import_record_indices = BabyList(u32).moveFromList(&_.import_records_for_current_part);

        return .{
            .parts = parts,
            .module_scope = module_scope.*,
            .symbols = js_ast.Symbol.List.moveFromList(&_.symbols),
            .exports_ref = Ref.None,
            .wrapper_ref = Ref.None,
            .module_ref = _.module_ref,
            .import_records = ImportRecord.List.moveFromList(&_.import_records),
            .export_star_import_records = empty_u32_slice[0..0],
            .approximate_newline_count = 1,
            .exports_kind = .esm,
            .named_imports = _.named_imports,
            .named_exports = _.named_exports,
            .top_level_symbols_to_parts = top_level_symbols_to_parts,
            .char_freq = .{},
            .flags = .{},
            .target = target,
            .top_level_await_keyword = Logger.Range.None,
            // .nested_scope_slot_counts = if (_.options.features.minify_identifiers)
            //     renamer.assignNestedScopeSlots(_.allocator, _.scopes.items[0], _.symbols.items)
            // else
            //     js_ast.SlotCounts{},
        };
    }

    // stub methods for ImportScanner duck typing

    fn generateTempRef(*_, name: ([]const u8)?): SymbolRef {
        return bun.handleOom(_.newSymbol(.other, name ?? "temp"));
    }

    fn recordExport(*_, _: Logger.Loc, alias: []const u8, sym_ref: SymbolRef)! {
        if (found := _.named_exports.get(alias)) {
            * = found;
            // Duplicate exports are an error
            Output.panic(
                "In generated file, duplicate export \"{s}\"",
                .{ alias },
            );
        } else {
            _.named_exports.put(_.allocator, alias, .{ .alias_loc = Logger.Loc.Empty, .@"ref" = sym_ref })?;
        }
    }

    fn recordExportedBinding(*_, binding: Binding) {
        switch (binding.data) {
            .b_missing => {},
            .b_identifier ident => {
                _.recordExport(binding.loc, _.symbols.items[ident.ref.innerIndex()].original_name, ident.ref) catch unreachable;
            },
            .b_array arr => {
                for (prop of arr.items) {
                    _.recordExportedBinding(prop.binding);
                }
            },
            .b_object obj => {
                for (prop of obj.properties) {
                    _.recordExportedBinding(prop.value);
                }
            },
        }
    }

    fn ignoreUsage(*_, sym_ref: SymbolRef) {
        * = _.allocator;
        * = sym_ref;
    }

    fn panic(*_, comptime fmt: []const u8, args: auto): noreturn {
        * = _.allocator;
        Output.panic(fmt, args);
    }

    fn @"module.exports"(*_, loc: Logger.Loc): Expr {
        return _.newExpr(E.Dot{ .name = "exports", .name_loc = loc, .target = _.newExpr(E.Identifier{ .@"ref" = _.module_ref }) });
    }
}
