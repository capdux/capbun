/// For CommonJS, all statements are copied `inside_wrapper_suffix` and this returns.
/// The conversion logic is completely different for format .internal_bake_dev
///
/// For ESM, this function populates all three lists:
/// 1. outside_wrapper_prefix: all import statements, unmodified.
/// 2. inside_wrapper_prefix: a var decl line and a call to `module.retrieve`
/// 3. inside_wrapper_suffix: all non-import statements
///
/// The imports are rewritten at print time to fit the packed array format
/// that the HMR runtime can decode. This encoding is low on JS objects and
/// indentation.
///
/// 1 ┃ "module/esm": [ [
///   ┃   'module_1', 1, "add",
///   ┃   'module_2', 2, "mul", "div",
///   ┃   'module_3', 0, // bare or import star
///     ], [ "default" ], [], (hmr) => {
/// 2 ┃   var [module_1, module_2, module_3] = hmr.imports;
///   ┃   hmr.onUpdate = [
///   ┃     (module) => (module_1 = module),
///   ┃     (module) => (module_2 = module),
///   ┃     (module) => (module_3 = module),
///   ┃   ];
///
/// 3 ┃   console.log("my module", module_1.add(1, module_2.mul(2, 3));
///   ┃   module.exports = {
///   ┃     default: module_3.something(module_2.div),
///   ┃   };
///     }, false ],
///        ----- "is the module async?"
pub fn convertStmtsForChunkForDevServer(
    c: *LinkerContext,
    stmts: *StmtList,
    part_stmts: []const js_ast.Stmt,
    allocator: std.mem.Allocator,
    ast: *JSAst,
)! {
    const hmr_api_ref = ast.wrapper_ref;
    const hmr_api_id = Expr.initIdentifier(hmr_api_ref, Logger.Loc.Empty);
    let esm_decls: std.ArrayListUnmanaged(B.Array.Item) = .empty;
    let esm_callbacks: std.ArrayListUnmanaged(Expr) = .empty;

    for (*record of ast.import_records.slice()) {
        if (record.path.is_disabled) continue;
        if (record.source_index.isValid() && c.parse_graph.input_files.items(.loader)[record.source_index.get()] == .css) {
            record.path.is_disabled = true;
            continue;
        }
        // Make sure the printer gets the resolved path
        if (record.source_index.isValid()) {
            record.path = c.parse_graph.input_files.items(.source)[record.source_index.get()].path;
        }
    }

    // Modules which do not have side effects
    for (stmt of part_stmts) {
        switch (stmt.data) {
            .s_import st => {
                const record = ast.import_records.mut(st.import_record_index);
                if (record.path.is_disabled) continue;

                if (record.flags.is_unused) {
                    // Barrel optimization: this import was deferred (unused submodule).
                    // Don't add to dep array, but declare the namespace ref as an
                    // empty object so body code referencing it doesn't throw.
                    if (st.star_name_loc != null || st.items.len > 0 || st.default_name != null) {
                        stmts.inside_wrapper_prefix.appendNonDependency(Stmt.alloc(S.Local, .{
                            kind: .k_var,
                            decls: G.Decl.List.fromSlice(allocator, &[ G.Decl{
                                binding: Binding.alloc(allocator, B.Identifier{ ref: st.namespace_ref }, stmt.loc),
                                value: Expr.init(E.Object, .{}, stmt.loc),
                            } ])?,
                        }, stmt.loc))?;
                    }
                    continue;
                }

                const is_builtin = record.tag == .builtin || record.tag == .bun || record.tag == .runtime;
                const is_bare_import = st.star_name_loc == null && st.items.len == 0 && st.default_name == null;

                if (is_builtin) {
                    if (!is_bare_import) {
                        // hmr.importBuiltin('...') or hmr.require('bun:wrap')
                        const call = Expr.init(E.Call, .{
                            target: Expr.init(E.Dot, .{
                                target: hmr_api_id,
                                name: if (record.tag == .runtime) "require" else "builtin",
                                name_loc: stmt.loc,
                            }, stmt.loc),
                            args: .fromOwnedSlice(allocator.dupe(Expr, &[ Expr.init(E.String, .{
                                data: if (record.tag == .runtime) "bun:wrap" else record.path.pretty,
                            }, record.range.loc) ])?),
                        }, stmt.loc);

                        // var namespace = ...;
                        stmts.inside_wrapper_prefix.appendNonDependency(Stmt.alloc(S.Local, .{
                            kind: .k_var, // remove a tdz
                            decls: G.Decl.List.fromSlice(allocator, &[ G.Decl{
                                binding: Binding.alloc(
                                    allocator,
                                    B.Identifier{ ref: st.namespace_ref },
                                    st.star_name_loc ?? stmt.loc,
                                ),
                                value: call,
                            } ])?,
                        }, stmt.loc))?;
                    }
                } else {
                    const loc = st.star_name_loc ?? stmt.loc;
                    if (is_bare_import) {
                        esm_decls.append(allocator, .{ binding: .{ data: .b_missing, loc: .Empty } })?;
                        esm_callbacks.append(allocator, Expr.init(E.Arrow, .noop_return_undefined, .Empty))?;
                    } else {
                        const binding = Binding.alloc(allocator, B.Identifier{ ref: st.namespace_ref }, loc);
                        esm_decls.append(allocator, .{ binding: binding })?;
                        esm_callbacks.append(allocator, Expr.init(E.Arrow, .{
                            args: allocator.dupe(G.Arg, &[ G.Arg{
                                binding: Binding.alloc(allocator, B.Identifier{
                                    ref: ast.module_ref,
                                }, .Empty),
                            } ])?,
                            prefer_expr: true,
                            body: G.FnBody.initReturnExpr(allocator, Expr.init(E.Binary, .{
                                op: .bin_assign,
                                left: Expr.initIdentifier(st.namespace_ref, .Empty),
                                right: Expr.initIdentifier(ast.module_ref, .Empty),
                            }, .Empty))?,
                        }, .Empty))?;
                    }

                    stmts.append(.outside_wrapper_prefix, stmt)?;
                }
            },
            else => stmts.append(.inside_wrapper_suffix, stmt)?,
        }
    }

    if (esm_decls.items.len > 0) {
        // var ...;
        stmts.inside_wrapper_prefix.appendNonDependency(Stmt.alloc(S.Local, .{
            kind: .k_var, // remove a tdz
            decls: G.Decl.List.fromSlice(allocator, &[ G.Decl{
                binding: Binding.alloc(allocator, B.Array{
                    items: esm_decls.items,
                    is_single_line: true,
                }, .Empty),
                value: Expr.init(E.Dot, .{
                    target: hmr_api_id,
                    name: "imports",
                    name_loc: .Empty,
                }, .Empty),
            } ])?,
        }, .Empty))?;
        // hmr.onUpdate = [ ... ];
        stmts.inside_wrapper_prefix.appendNonDependency(Stmt.alloc(S.SExpr, .{
            value: Expr.init(E.Binary, .{
                op: .bin_assign,
                left: Expr.init(E.Dot, .{
                    target: hmr_api_id,
                    name: "updateImport",
                    name_loc: .Empty,
                }, .Empty),
                right: Expr.init(E.Array, .{
                    items: .moveFromList(&esm_callbacks),
                    is_single_line: esm_callbacks.items.len <= 2,
                }, .Empty),
            }, .Empty),
        }, .Empty))?;
    }
}

pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;

import bun from "bun";

const js_ast = bun.ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const JSAst = js_ast.BundledAst;
const S = js_ast.S;
const Stmt = js_ast.Stmt;

const LinkerContext = bun.bundle_v2.LinkerContext;
const StmtList = LinkerContext.StmtList;

const Logger = bun.logger;
const Loc = Logger.Loc;
