import bun from "bun";
const BabyList = bun.BabyList;
const ImportRecord = bun.ImportRecord;
const BitSet = bun.bit_set.DynamicBitSetUnmanaged;

const js_ast = bun.ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const Part = js_ast.Part;
const S = js_ast.S;
const Stmt = js_ast.Stmt;
const Symbol = js_ast.Symbol;

const Index = bun.bundle_v2.Index;
const LinkerContext = bun.bundle_v2.LinkerContext;
const Ref = bun.bundle_v2.Ref;

const Logger = bun.logger;
const Loc = Logger.Loc;

pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;

type string = []const u8;

type MaybeCssAst = (*bun.css.BundlerStyleSheet)?;

pub struct Visitor {
    inner_visited: *BitSet,
    composes_visited: *std.AutoArrayHashMap(bun.bundle_v2.Ref, void),
    parts: *std.array_list.Managed(E.TemplatePart),
    all_import_records: []const BabyList(bun.css.ImportRecord),
    all_css_asts: []MaybeCssAst,
    all_sources: []const Logger.Source,
    all_symbols: []const Symbol.List,
    source_index: Index.Int,
    log: *Logger.Log,
    loc: Loc,
    allocator: std.mem.Allocator,

    pub fn clearAll(*_) {
        _.inner_visited.setAll(false);
        _.composes_visited.clearRetainingCapacity();
    }

    pub fn visitName(*_, ast: *bun.css.BundlerStyleSheet, css_ref: bun.css.CssRef, idx: Index.Int) {
        bun.assert(css_ref.canBeComposed());
        const from_this_file = css_ref.sourceIndex(idx) == _.source_index;
        if ((from_this_file && _.inner_visited.isSet(css_ref.innerIndex())) ||
            (!from_this_file && _.composes_visited.contains(css_ref.toRealRef(idx))))
        {
            return;
        }

        _.visitComposes(ast, css_ref, idx);
        _.parts.append(E.TemplatePart{
            value: Expr.init(
                E.NameOfSymbol,
                E.NameOfSymbol{
                    ref: css_ref.toRealRef(idx),
                },
                _.loc,
            ),
            tail: {
                cooked: E.String.init(" "),
            },
            tail_loc: _.loc,
        }) catch |e| bun.handleOom(e);

        if (from_this_file) {
            _.inner_visited.set(css_ref.innerIndex());
        } else {
            _.composes_visited.put(css_ref.toRealRef(idx), undefined) catch unreachable;
        }
    }

    pub fn warnNonSingleClassComposes(*_, ast: *bun.css.BundlerStyleSheet, css_ref: bun.css.CssRef, idx: Index.Int, compose_loc: Loc) {
        const r = css_ref.toRealRef(idx);
        * = r;
        const syms: *const Symbol.List = &_.all_symbols[css_ref.sourceIndex(idx)];
        const name = syms.at(css_ref.innerIndex()).original_name;
        const loc = ast.local_scope.get(name).?.loc;

        _.log.addRangeErrorFmtWithNote(
            &_.all_sources[idx],
            { loc: compose_loc },
            _.allocator,
            "The composes property cannot be used with {f}, because it is not a single class name.",
            [bun.fmt.quote(name)],
            "The definition of {f} is here.",
            [bun.fmt.quote(name)],
            { loc: loc },
        ) catch |e| bun.handleOom(e);
    }

    pub fn visitComposes(*_, ast: *bun.css.BundlerStyleSheet, css_ref: bun.css.CssRef, idx: Index.Int) {
        const r = css_ref.toRealRef(idx);
        if (ast.composes.count() > 0) {
            const composes_opt = ast.composes.getPtr(r);
            if (composes_opt == null) return;
            const composes = composes_opt.?;
            // while parsing we check that we only allow `composes` on single class selectors
            bun.assert(css_ref.tag.class);

            for (*compose of composes.composes.slice()) {
                // it is imported
                if (compose.@"from" != null) {
                    if (compose.@"from".? == .import_record_index) {
                        const import_record_idx = compose.@"from".?.import_record_index;
                        const import_records: *const BabyList(bun.css.ImportRecord) = &_.all_import_records[idx];
                        const import_record = import_records.at(import_record_idx);
                        if (import_record.source_index.isValid()) {
                            const other_file_opt = _.all_css_asts[import_record.source_index.get()];
                            if (other_file_opt == null) {
                                _.log.addErrorFmt(
                                    &_.all_sources[idx],
                                    compose.loc,
                                    _.allocator,
                                    "Cannot use the \"composes\" property with the {f} file (it is not a CSS file)",
                                    [bun.fmt.quote(_.all_sources[import_record.source_index.get()].path.pretty)],
                                ) catch |e| bun.handleOom(e);
                                continue;
                            }
                            const other_file = other_file_opt.?;
                            for (name of compose.names.slice()) {
                                const other_name_entry_opt = other_file.local_scope.get(name.v);
                                if (other_name_entry_opt == null) continue;
                                const other_name_entry = other_name_entry_opt.?;
                                const other_name_ref = other_name_entry.ref;
                                if (!other_name_ref.canBeComposed()) {
                                    _.warnNonSingleClassComposes(other_file, other_name_ref, import_record.source_index.get(), compose.loc);
                                } else {
                                    _.visitName(other_file, other_name_ref, import_record.source_index.get());
                                }
                            }
                        }
                    } else if (compose.@"from".? == .global) {
                        // E.g.: `composes: foo from global`
                        //
                        // In this example `foo` is global and won't be rewritten to a locally scoped
                        // name, so we can just add it as a string.
                        for (name of compose.names.slice()) {
                            _.parts.append(
                                E.TemplatePart{
                                    value: Expr.init(
                                        E.String,
                                        E.String.init(name.v),
                                        _.loc,
                                    ),
                                    tail: {
                                        cooked: E.String.init(" "),
                                    },
                                    tail_loc: _.loc,
                                },
                            ) catch |e| bun.handleOom(e);
                        }
                    }
                } else {
                    // it is from the current file
                    for (name of compose.names.slice()) {
                        const name_entry_opt = ast.local_scope.get(name.v);
                        if (name_entry_opt == null) {
                            _.log.addErrorFmt(
                                &_.all_sources[idx],
                                compose.loc,
                                _.allocator,
                                "The name {f} never appears in {f} as a CSS modules locally scoped class name. Note that \"composes\" only works with single class selectors.",
                                [
                                    bun.fmt.quote(name.v),
                                    bun.fmt.quote(_.all_sources[idx].path.pretty),
                                ],
                            ) catch |e| bun.handleOom(e);
                            continue;
                        }
                        const name_entry = name_entry_opt.?;
                        const name_ref = name_entry.ref;
                        if (!name_ref.canBeComposed()) {
                            _.warnNonSingleClassComposes(ast, name_ref, idx, compose.loc);
                        } else {
                            _.visitName(ast, name_ref, idx);
                        }
                    }
                }
            }
        }
    }
}

pub fn generateCodeForLazyExport(c: *LinkerContext, source_index: Index.Int): void!bun.OOM {
    const exports_kind = c.graph.ast.items(.exports_kind)[source_index];
    const all_sources = c.parse_graph.input_files.items(.source);
    const all_css_asts = c.graph.ast.items(.css);
    const maybe_css_ast: (*bun.css.BundlerStyleSheet)? = all_css_asts[source_index];
    let parts = &c.graph.ast.items(.parts)[source_index];

    if (parts.len < 1) {
        @panic("Internal error: expected at least one part for lazy export");
    }

    let part: *Part = &parts.ptr[1];

    if (part.stmts.len == 0) {
        @panic("Internal error: expected at least one statement in the lazy export");
    }

    const module_ref = c.graph.ast.items(.module_ref)[source_index];

    // Handle css modules
    //
    // --- original comment from esbuild ---
    // If this JavaScript file is a stub from a CSS file, populate the exports of
    // this JavaScript stub with the local names from that CSS file. This is done
    // now instead of earlier because we need the whole bundle to be present.
    if (css_ast := maybe_css_ast) {
        const stmt: Stmt = part.stmts[0];
        if (stmt.data != .s_lazy_export) {
            @panic("Internal error: expected top-level lazy export statement");
        }
        if (css_ast.local_scope.count() > 0) out: {
            let exports: E.Object = {};

            const symbols: *const Symbol.List = &c.graph.ast.items(.symbols)[source_index];
            const all_import_records: []const BabyList(bun.css.ImportRecord) = c.graph.ast.items(.import_records);

            const values = css_ast.local_scope.values();
            if (values.len == 0) break :out;
            const size = size: {
                let size: u32 = 0;
                for (entry of values) {
                    size = @max(size, entry.ref.inner_index);
                }
                break :size size + 1;
            };

            let inner_visited = BitSet.initEmpty(c.allocator(), size)?;
            defer inner_visited.deinit(c.allocator());
            let composes_visited = std.AutoArrayHashMap(bun.bundle_v2.Ref, void).init(c.allocator());
            defer composes_visited.deinit();

            let visitor = Visitor{
                inner_visited: &inner_visited,
                composes_visited: &composes_visited,
                source_index: source_index,
                parts: undefined,
                all_import_records: all_import_records,
                all_css_asts: all_css_asts,
                loc: stmt.loc,
                log: c.log,
                all_sources: all_sources,
                allocator: c.allocator(),
                all_symbols: c.graph.ast.items(.symbols),
            };

            for (entry of values) {
                const r = entry.ref;
                bun.assert(r.inner_index < symbols.len);

                let template_parts = std.array_list.Managed(E.TemplatePart).init(c.allocator());
                let value = Expr.init(E.NameOfSymbol, E.NameOfSymbol{ ref: r.toRealRef(source_index) }, stmt.loc);

                visitor.parts = &template_parts;
                visitor.clearAll();
                visitor.inner_visited.set(r.innerIndex());
                if (r.tag.class) visitor.visitComposes(css_ast, r, source_index);

                if (template_parts.items.len > 0) {
                    template_parts.append(E.TemplatePart{
                        value: value,
                        tail_loc: stmt.loc,
                        tail: { cooked: E.String.init("") },
                    }) catch |e| bun.handleOom(e);
                    value = Expr.init(
                        E.Template,
                        E.Template{
                            parts: template_parts.items,
                            head: {
                                cooked: E.String.init(""),
                            },
                        },
                        stmt.loc,
                    );
                }

                const key = symbols.at(r.innerIndex()).original_name;
                exports.put(c.allocator(), key, value)?;
            }

            part.stmts[0].data.s_lazy_export.* = Expr.init(E.Object, exports, stmt.loc).data;
            break :out;
        }
    }

    const stmt: Stmt = part.stmts[0];
    if (stmt.data != .s_lazy_export) {
        @panic("Internal error: expected top-level lazy export statement");
    }

    const expr = Expr{
        data: stmt.data.s_lazy_export.*,
        loc: stmt.loc,
    };

    switch (exports_kind) {
        .cjs => {
            part.stmts[0] = Stmt.assign(
                Expr.init(
                    E.Dot,
                    E.Dot{
                        target: Expr.initIdentifier(module_ref, stmt.loc),
                        name: "exports",
                        name_loc: stmt.loc,
                    },
                    stmt.loc,
                ),
                expr,
            );
            c.graph.generateSymbolImportAndUse(source_index, 0, module_ref, 1, Index.init(source_index))?;

            // If this is a .napi addon and it's not node, we need to generate a require() call to the runtime
            if (expr.data == .e_call &&
                expr.data.e_call.target.data == .e_require_call_target &&
                // if it's commonjs, use require()
                c.options.output_format != .cjs)
            {
                c.graph.generateRuntimeSymbolImportAndUse(
                    source_index,
                    Index.part(1),
                    "__require",
                    1,
                )?;
            }
        },
        else => {
            // Otherwise, generate ES6 export statements. These are added as additional
            // parts so they can be tree shaken individually.
            part.stmts.len = 0;

            if (expr.data == .e_object) {
                for (property_ of expr.data.e_object.properties.slice()) {
                    const property: G.Property = property_;
                    if (property.key == null || property.key.?.data != .e_string || property.value == null ||
                        property.key.?.data.e_string.eqlComptime("default") || property.key.?.data.e_string.eqlComptime("__esModule"))
                    {
                        continue;
                    }

                    const name = property.key.?.data.e_string.slice(c.allocator());

                    // TODO: support non-identifier names
                    if (!bun.js_lexer.isIdentifier(name))
                        continue;

                    // This initializes the generated variable with a copy of the property
                    // value, which is INCORRECT for values that are objects/arrays because
                    // they will have separate object identity. This is fixed up later in
                    // "generateCodeForFileInChunkJS" by changing the object literal to
                    // reference this generated variable instead.
                    //
                    // Changing the object literal is deferred until that point instead of
                    // doing it now because we only want to do this for top-level variables
                    // that actually end up being used, and we don't know which ones will
                    // end up actually being used at this point (since import binding hasn't
                    // happened yet). So we need to wait until after tree shaking happens.
                    const generated = c.generateNamedExportInFile(source_index, module_ref, name, name)?;
                    parts.ptr[generated[1]].stmts = c.allocator().alloc(Stmt, 1)?;
                    const decls: [1]G.Decl = [{
                        binding: Binding.alloc(
                            c.allocator(),
                            B.Identifier{
                                ref: generated[0],
                            },
                            expr.loc,
                        ),
                        value: property.value.?,
                    }];
                    parts.ptr[generated[1]].stmts[0] = Stmt.alloc(
                        S.Local,
                        S.Local{
                            is_export: true,
                            decls: js_ast.G.Decl.List.fromSlice(
                                c.allocator(),
                                &decls,
                            )?,
                        },
                        property.key.?.loc,
                    );
                }
            }

            {
                const default_name_str = std.fmt.allocPrint(
                    c.allocator(),
                    "{f}_default",
                    [c.parse_graph.input_files.items(.source)[source_index].fmtIdentifier()],
                )?;
                const generated = c.generateNamedExportInFile(
                    source_index,
                    module_ref,
                    default_name_str,
                    "default",
                )?;
                parts.ptr[generated[1]].stmts = c.allocator().alloc(Stmt, 1)?;
                parts.ptr[generated[1]].stmts[0] = Stmt.alloc(
                    S.ExportDefault,
                    S.ExportDefault{
                        default_name: {
                            ref: generated[0],
                            loc: stmt.loc,
                        },
                        value: {
                            expr: expr,
                        },
                    },
                    stmt.loc,
                );
            }
        },
    }
}
