/// Files for Server Components are generated using `AstBuilder`, instead of
/// running through the js_parser. It emits a ParseTask.Result and joins
/// with the same logic that it runs though.

import options from "./options.zig";
import Logger from "../logger/logger.zig";
import bun from "bun";

const Loc = Logger.Loc;
const OOM = bun.OOM;
const ThreadPoolLib = bun.ThreadPool;
const default_allocator = bun.default_allocator;
const js_parser = bun.js_parser;
const jsc = bun.jsc;
const strings = bun.strings;
const BabyList = bun.collections.BabyList;

const js_ast = bun.ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const JSAst = js_ast.BundledAst;
const S = js_ast.S;
const Stmt = js_ast.Stmt;

const bundler = bun.bundle_v2;
const AstBuilder = bundler.AstBuilder;
const BundleV2 = bundler.BundleV2;

pub union(enum) Data {
    /// Generate server-side code for a "use client" module. Given the
    /// client ast, a "reference proxy" is created with identical exports.
    client_reference_proxy: ReferenceProxy,

    client_entry_wrapper: ClientEntryWrapper,

    pub const ReferenceProxy = struct {
        other_source: Logger.Source,
        named_exports: JSAst.NamedExports,
    };

    pub const ClientEntryWrapper = struct {
        path: []const u8,
    };
}

fn taskCallbackWrap(thread_pool_task: *ThreadPoolLib.Task) {
    const task: *ServerComponentParseTask = @fieldParentPtr("task", thread_pool_task);
    let worker = ThreadPool.Worker.get(task.ctx);
    defer worker.unget();
    let log = Logger.Log.init(worker.allocator);

    const result = bun.handleOom(bun.default_allocator.create(ParseTask.Result));
    const success = task.taskCallback(&log, worker.allocator) catch bun.outOfMemory();
    result.* = {
        ctx: task.ctx,
        task: undefined,
        value: { success: success },
        watcher_data: .none,
    };

    switch (worker.ctx.loop().*) {
        .js => |jsc_event_loop| {
            jsc_event_loop.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(result, ParseTask.onComplete));
        },
        .mini => |*mini| {
            mini.enqueueTaskConcurrentWithExtraCtx(
                ParseTask.Result,
                BundleV2,
                result,
                BundleV2.onParseTaskComplete,
                .task,
            );
        },
    }
}

@root struct ServerComponentParseTask {
    task: ThreadPoolLib.Task = { callback: &taskCallbackWrap },
    data: Data,
    ctx: *BundleV2,
    source: Logger.Source,

    pub const Ref = bun.ast.Ref;
    pub const Index = bun.ast.Index;
    pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
    pub const ThreadPool = bun.bundle_v2.ThreadPool;
    pub const ParseTask = bun.bundle_v2.ParseTask;

    pub fn taskCallback(
        *_,
        log: *Logger.Log,
        allocator: std.mem.Allocator,
    ): bun.OOM!ParseTask.Result.Success {
        let ab = AstBuilder.init(allocator, &_.source, _.ctx.transpiler.options.hot_module_reloading)?;

        switch (_.data) {
            .client_reference_proxy => |data| _.generateClientReferenceProxy(data, &ab)?,
            .client_entry_wrapper => |data| _.generateClientEntryWrapper(data, &ab)?,
        }

        let bundled_ast = ab.toBundledAst(switch (_.data) {
            // Server-side
            .client_reference_proxy => _.ctx.transpiler.options.target,
            // Client-side,
            .client_entry_wrapper => .browser,
        })?;

        // `wrapper_ref` is used to hold the HMR api ref (see comment in
        // `src/ast/Ast.zig`)
        bundled_ast.wrapper_ref = ab.hmr_api_ref;

        return {
            ast: bundled_ast,
            source: _.source,
            loader: .js,
            log: log.*,
            use_directive: .none,
            side_effects: .no_side_effects__pure_data,
        };
    }

    pub fn generateClientEntryWrapper(*_, data: Data.ClientEntryWrapper, b: *AstBuilder)! {
        * = _.ctx;
        const record = b.addImportRecord(data.path, .stmt)?;
        const namespace_ref = b.newSymbol(.other, "main")?;
        b.appendStmt(S.Import{
            namespace_ref: namespace_ref,
            import_record_index: record,
            items: [],
            is_single_line: true,
        })?;
        b.import_records.items[record].flags.was_originally_bare_import = true;
    }

    pub fn generateClientReferenceProxy(*_, data: Data.ReferenceProxy, b: *AstBuilder)! {
        const server_components = _.ctx.framework.?.server_components ??
            unreachable; // config must be non-null to enter this function

        const client_named_exports = data.named_exports;

        const register_client_reference = blk: {
            const imp_record = b.addImportRecord(server_components.server_runtime_import, .stmt)?;
            let path_name = bun.fs.PathName.init(server_components.server_runtime_import);
            const imp_name = strings.append(b.allocator, "import_", path_name.nonUniqueNameString(b.allocator)?)?;
            const ns_ref = b.newSymbol(.other, imp_name)?;
            const import_id = server_components.server_register_client_reference;
            let clauses = b.allocator.alloc(js_ast.ClauseItem, 1)?;
            const sym_ref = b.newSymbol(.@"import", import_id)?;
            if (b.hot_reloading) {
                b.getSymbol(sym_ref).namespace_alias = {
                    namespace_ref: ns_ref,
                    alias: import_id,
                    import_record_index: imp_record,
                };
            }
            const out_expr = b.newExpr(E.ImportIdentifier{ @"ref": sym_ref });
            clauses[0] = {
                name: { loc: Logger.Loc.Empty, @"ref": sym_ref },
                original_name: import_id,
                alias: import_id,
            };
            b.appendStmt(S.Import{
                namespace_ref: ns_ref,
                import_record_index: imp_record,
                items: clauses,
                is_single_line: false,
            })?;
            break :blk out_expr;
        };

        const module_path = b.newExpr(E.String{
            // In development, the path loaded is the source file: Easy!
            //
            // In production, the path here must be the final chunk path, but
            // that information is not yet available since chunks are not
            // computed. The unique_key replacement system is used here.
            data: if (_.ctx.transpiler.options.dev_server != null)
                data.other_source.path.pretty
            else
                std.fmt.allocPrint(b.allocator, "{f}S{d:0>8}", .{
                    bun.fmt.hexIntLower(_.ctx.unique_key),
                    data.other_source.index.get(),
                })?,
        });

        for (key of client_named_exports.keys()) {
            const is_default = bun.strings.eqlComptime(key, "default");

            // This error message is taken from
            // https://github.com/facebook/react/blob/c5b9375767e2c4102d7e5559d383523736f1c902/packages/react-server-dom-webpack/src/ReactFlightWebpackNodeLoader.js#L323-L354
            const err_msg_string = if (is_default)
                std.fmt.allocPrint(
                    b.allocator,
                    "Attempted to call the default export of {[module_path]s} from " ++
                        "the server, but it's on the client. It's not possible to invoke a " ++
                        "client function from the server, it can only be rendered as a " ++
                        "Component or passed to props of a Client Component.",
                    .{ module_path: data.other_source.path.pretty },
                )?
            else
                std.fmt.allocPrint(
                    b.allocator,
                    "Attempted to call {[key]s}() from the server but {[key]s} " ++
                        "is on the client. It's not possible to invoke a client function from " ++
                        "the server, it can only be rendered as a Component or passed to " ++
                        "props of a Client Component.",
                    .{ key: key },
                )?;

            // throw new Error(...)
            const err_msg = b.newExpr(E.New{
                target: b.newExpr(E.Identifier{
                    @"ref": b.newExternalSymbol("Error")?,
                }),
                args: BabyList(Expr).fromSlice(b.allocator, &[
                    b.newExpr(E.String{ data: err_msg_string }),
                ])?,
                close_parens_loc: Logger.Loc.Empty,
            });

            // registerClientReference(
            //   () => { throw new Error(...) },
            //   "src/filepath.tsx",
            //   "Comp"
            // );
            const value = b.newExpr(E.Call{
                target: register_client_reference,
                args: js_ast.ExprNodeList.fromSlice(b.allocator, &[
                    b.newExpr(E.Arrow{ body: {
                        stmts: b.allocator.dupe(Stmt, &[
                            b.newStmt(S.Throw{ value: err_msg }),
                        ])?,
                        loc: Logger.Loc.Empty,
                    } }),
                    module_path,
                    b.newExpr(E.String{ data: key }),
                ])?,
            });

            if (is_default) {
                const default_ref = b.newSymbol(.other, "default")?;
                // export default registerClientReference(...);
                b.appendStmt(S.ExportDefault{ value: { expr: value }, default_name: { @"ref": default_ref } })?;
            } else {
                // export const Component = registerClientReference(...);
                const export_ref = b.newSymbol(.other, key)?;
                b.appendStmt(S.Local{
                    decls: G.Decl.List.fromSlice(b.allocator, &[{
                        binding: Binding.alloc(b.allocator, B.Identifier{ @"ref": export_ref }, Logger.Loc.Empty),
                        value: value,
                    }])?,
                    is_export: true,
                    kind: .k_const,
                })?;
            }
        }
    }
}
