import skipTypescript_mod from "./skipTypescript.zig";
import parse_mod from "./parse.zig";
import visit_mod from "./visit.zig";
import visitBinaryExpression_mod from "./visitBinaryExpression.zig";
import maybe_mod from "./maybe.zig";
import symbols_mod from "./symbols.zig";
import lowerDecorators_mod from "./lowerDecorators.zig";
import defines_mod from "../../bundler/defines.zig";
pub struct DeinitGuard<T> {
    ptr: *T!?,
    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub struct FreeGuard {
    allocator: std.mem.Allocator,
    slice: ([]const u8)?,
    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!?,
    fba: *std.heap.FixedBufferAllocator!?,
    allocator: std.mem.Allocator,
    ~init() {
        if (p := _.ptr) {
            if (f := _.fba) {
                if (f.end_index >= 1023) {
                    p.deinit(_.allocator);
                }
            } else {
                p.deinit(_.allocator);
            }
        }
    }
}

const RecentlyVisitedTSNamespace = struct {
    expr: Expr.Data = Expr.empty.data,
    map: (*js_ast.TSNamespaceMemberMap)? = null,

    const ExpressionData = union(enum) {
        ref: Ref,
        ptr: *E.Dot,
    };
};

const ReactRefreshImportClause = struct {
    name: []const u8,
    enabled: bool,
    ref: Ref,
};

const ReactRefreshExportKind = enum { named, default };

const import_meta_hot_accept_err = "Dependencies to `import.meta.hot.accept` must be statically analyzable module specifiers matching direct imports.";

struct ParserFeatures {
    typescript: bool = false,
    jsx: JSXTransformType = .none,
    scan_only: bool = false,
}

// workaround for https://github.com/ziglang/zig/issues/10903
pub fn NewParser(
    comptime parser_features: ParserFeatures,
): type {
    return NewParser_(
        parser_features.typescript,
        parser_features.jsx,
        parser_features.scan_only,
    );
}
pub fn NewParser_(
    comptime parser_feature__typescript: bool,
    comptime parser_feature__jsx: JSXTransformType,
    comptime parser_feature__scan_only: bool,
): type {
    const js_parser_features: ParserFeatures = {
        typescript: parser_feature__typescript,
        jsx: parser_feature__jsx,
        scan_only: parser_feature__scan_only,
    };

    // P is for Parser!
    return struct {
        const js_parser_jsx = js_parser_features.jsx;
        pub const is_typescript_enabled = js_parser_features.typescript;
        pub const is_jsx_enabled = js_parser_jsx != .none;
        pub const only_scan_imports_and_do_not_visit = js_parser_features.scan_only;
        type ImportRecordPtr = *std.array_list.Managed(ImportRecord);
        const ImportRecordList = if (only_scan_imports_and_do_not_visit) ImportRecordPtr else std.array_list.Managed(ImportRecord);
        const NamedImportsType = if (only_scan_imports_and_do_not_visit) *js_ast.Ast.NamedImports else js_ast.Ast.NamedImports;
        const NeedsJSXType = if (only_scan_imports_and_do_not_visit) bool else void;
        pub const track_symbol_usage_during_parse_pass = only_scan_imports_and_do_not_visit && is_typescript_enabled;
        const ParsePassSymbolUsageType = if (track_symbol_usage_during_parse_pass) *ScanPassResult.ParsePassSymbolUsageMap else void;

        pub const parser_features: ParserFeatures = js_parser_features;
        const P = @This();
        pub const jsx_transform_type: JSXTransformType = js_parser_jsx;
        pub const allow_macros = FeatureFlags.is_macro_enabled;
        const MacroCallCountType = if (allow_macros) u32 else u0;

        const skipTypescript_zig = skipTypescript_mod.SkipTypescript(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        pub const skipTypescriptReturnType = skipTypescript_zig.skipTypescriptReturnType;
        pub const skipTypescriptReturnTypeWithMetadata = skipTypescript_zig.skipTypescriptReturnTypeWithMetadata;
        pub const skipTypeScriptType = skipTypescript_zig.skipTypeScriptType;
        pub const skipTypeScriptTypeWithMetadata = skipTypescript_zig.skipTypeScriptTypeWithMetadata;
        pub const skipTypeScriptBinding = skipTypescript_zig.skipTypeScriptBinding;
        pub const skipTypescriptFnArgs = skipTypescript_zig.skipTypescriptFnArgs;
        pub const skipTypeScriptParenOrFnType = skipTypescript_zig.skipTypeScriptParenOrFnType;
        pub const skipTypeScriptTypeWithOpts = skipTypescript_zig.skipTypeScriptTypeWithOpts;
        pub const skipTypeScriptObjectType = skipTypescript_zig.skipTypeScriptObjectType;
        pub const skipTypeScriptTypeParameters = skipTypescript_zig.skipTypeScriptTypeParameters;
        pub const skipTypeScriptTypeStmt = skipTypescript_zig.skipTypeScriptTypeStmt;
        pub const skipTypeScriptInterfaceStmt = skipTypescript_zig.skipTypeScriptInterfaceStmt;
        pub const skipTypeScriptTypeArguments = skipTypescript_zig.skipTypeScriptTypeArguments;
        pub const trySkipTypeScriptTypeParametersThenOpenParenWithBacktracking = skipTypescript_zig.trySkipTypeScriptTypeParametersThenOpenParenWithBacktracking;
        pub const trySkipTypeScriptTypeArgumentsWithBacktracking = skipTypescript_zig.trySkipTypeScriptTypeArgumentsWithBacktracking;
        pub const trySkipTypeScriptArrowReturnTypeWithBacktracking = skipTypescript_zig.trySkipTypeScriptArrowReturnTypeWithBacktracking;
        pub const trySkipTypeScriptArrowArgsWithBacktracking = skipTypescript_zig.trySkipTypeScriptArrowArgsWithBacktracking;
        pub const trySkipTypeScriptConstraintOfInferTypeWithBacktracking = skipTypescript_zig.trySkipTypeScriptConstraintOfInferTypeWithBacktracking;

        const parse_zig = parse_mod.Parse(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        pub const parsePrefix = parse_zig.parsePrefix;
        pub const parseSuffix = parse_zig.parseSuffix;
        pub const parseStmt = parse_zig.parseStmt;
        pub const parseProperty = parse_zig.parseProperty;
        pub const parseFn = parse_zig.parseFn;
        pub const parseFnStmt = parse_zig.parseFnStmt;
        pub const parseFnExpr = parse_zig.parseFnExpr;
        pub const parseFnBody = parse_zig.parseFnBody;
        pub const parseArrowBody = parse_zig.parseArrowBody;
        pub const parseJSXElement = parse_zig.parseJSXElement;
        pub const parseImportExpr = parse_zig.parseImportExpr;
        pub const parseImportClause = parse_zig.parseImportClause;
        pub const parseExportClause = parse_zig.parseExportClause;
        pub const parseExprOrBindings = parse_zig.parseExprOrBindings;
        pub const parseExpr = parse_zig.parseExpr;
        pub const parseExprWithFlags = parse_zig.parseExprWithFlags;
        pub const parseExprCommon = parse_zig.parseExprCommon;
        pub const parseYieldExpr = parse_zig.parseYieldExpr;
        pub const parseClass = parse_zig.parseClass;
        pub const parseTemplateParts = parse_zig.parseTemplateParts;
        pub const parseStringLiteral = parse_zig.parseStringLiteral;
        pub const parseCallArgs = parse_zig.parseCallArgs;
        pub const parseJSXPropValueIdentifier = parse_zig.parseJSXPropValueIdentifier;
        pub const parseParenExpr = parse_zig.parseParenExpr;
        pub const parseLabelName = parse_zig.parseLabelName;
        pub const parseClassStmt = parse_zig.parseClassStmt;
        pub const parseClauseAlias = parse_zig.parseClauseAlias;
        pub const parseExprOrLetStmt = parse_zig.parseExprOrLetStmt;
        pub const parseBinding = parse_zig.parseBinding;
        pub const parsePropertyBinding = parse_zig.parsePropertyBinding;
        pub const parseAndDeclareDecls = parse_zig.parseAndDeclareDecls;
        pub const parsePath = parse_zig.parsePath;
        pub const parseStmtsUpTo = parse_zig.parseStmtsUpTo;
        pub const parseAsyncPrefixExpr = parse_zig.parseAsyncPrefixExpr;
        pub const parseTypeScriptDecorators = parse_zig.parseTypeScriptDecorators;
        pub const parseStandardDecorator = parse_zig.parseStandardDecorator;
        pub const parseTypeScriptNamespaceStmt = parse_zig.parseTypeScriptNamespaceStmt;
        pub const parseTypeScriptImportEqualsStmt = parse_zig.parseTypeScriptImportEqualsStmt;
        pub const parseTypescriptEnumStmt = parse_zig.parseTypescriptEnumStmt;

        const astVisit = visit_mod.Visit(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        pub const visitStmtsAndPrependTempRefs = astVisit.visitStmtsAndPrependTempRefs;
        pub const recordDeclaredSymbol = astVisit.recordDeclaredSymbol;
        pub const visitExpr = astVisit.visitExpr;
        pub const visitExprInOut = astVisit.visitExprInOut;
        pub const visitFunc = astVisit.visitFunc;
        pub const visitArgs = astVisit.visitArgs;
        pub const visitTSDecorators = astVisit.visitTSDecorators;
        pub const visitDecls = astVisit.visitDecls;
        pub const visitBindingAndExprForMacro = astVisit.visitBindingAndExprForMacro;
        pub const visitDecl = astVisit.visitDecl;
        pub const visitForLoopInit = astVisit.visitForLoopInit;
        pub const visitBinding = astVisit.visitBinding;
        pub const visitLoopBody = astVisit.visitLoopBody;
        pub const visitSingleStmtBlock = astVisit.visitSingleStmtBlock;
        pub const visitSingleStmt = astVisit.visitSingleStmt;
        pub const visitClass = astVisit.visitClass;
        pub const visitStmts = astVisit.visitStmts;
        pub const visitAndAppendStmt = astVisit.visitAndAppendStmt;

        pub const BinaryExpressionVisitor = visitBinaryExpression_mod.CreateBinaryExpressionVisitor(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only).BinaryExpressionVisitor;

        const maybe = maybe_mod.AstMaybe(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        pub const maybeRelocateVarsToTopLevel = maybe.maybeRelocateVarsToTopLevel;
        pub const maybeRewritePropertyAccess = maybe.maybeRewritePropertyAccess;
        pub const maybeCommaSpreadError = maybe.maybeCommaSpreadError;
        pub const maybeDefinedHelper = maybe.maybeDefinedHelper;
        pub const checkIfDefinedHelper = maybe.checkIfDefinedHelper;

        const symbols_zig = symbols_mod.Symbols(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        pub const findSymbol = symbols_zig.findSymbol;
        pub const findSymbolWithRecordUsage = symbols_zig.findSymbolWithRecordUsage;

        const lowerDecorators_zig = lowerDecorators_mod.LowerDecorators(parser_feature__typescript, parser_feature__jsx, parser_feature__scan_only);
        pub const lowerStandardDecoratorsStmt = lowerDecorators_zig.lowerStandardDecoratorsStmt;
        pub const lowerStandardDecoratorsExpr = lowerDecorators_zig.lowerStandardDecoratorsExpr;

        @"macro": MacroState = undefined,
        allocator: Allocator,
        options: Parser.Options,
        log: *logger.Log,
        define: *Define,
        source: *const logger.Source,
        lexer: js_lexer.Lexer,
        allow_in: bool = false,
        allow_private_identifiers: bool = false,

        has_top_level_return: bool = false,
        latest_return_had_semicolon: bool = false,
        has_import_meta: bool = false,
        has_es_module_syntax: bool = false,
        top_level_await_keyword: logger.Range = logger.Range.None,
        fn_or_arrow_data_parse: FnOrArrowDataParse = FnOrArrowDataParse{},
        fn_or_arrow_data_visit: FnOrArrowDataVisit = FnOrArrowDataVisit{},
        fn_only_data_visit: FnOnlyDataVisit = FnOnlyDataVisit{},
        allocated_names: List(string) = {},
        // allocated_names: ListManaged(string) = ListManaged(string).init(bun.default_allocator),
        // allocated_names_pool: ?*AllocatedNamesPool.Node = null,
        latest_arrow_arg_loc: logger.Loc = logger.Loc.Empty,
        forbid_suffix_after_as_loc: logger.Loc = logger.Loc.Empty,
        current_scope: *js_ast.Scope = undefined,
        scopes_for_current_part: List(*js_ast.Scope) = {},
        symbols: ListManaged(js_ast.Symbol) = undefined,
        ts_use_counts: List(u32) = {},
        exports_ref: Ref = Ref.None,
        require_ref: Ref = Ref.None,
        module_ref: Ref = Ref.None,
        filename_ref: Ref = Ref.None,
        dirname_ref: Ref = Ref.None,
        import_meta_ref: Ref = Ref.None,
        hmr_api_ref: Ref = Ref.None,

        /// If bake is enabled and this is a server-side file, we want to use
        /// special `Response` class inside the `bun:app` built-in module to
        /// support syntax like `return Response(<jsx />, {...})` or `return Response.render("/my-page")`
        /// or `return Response.redirect("/other")`.
        ///
        /// So we'll need to add a `import { Response } from 'bun:app'` to the
        /// top of the file
        ///
        /// We need to declare this `response_ref` upfront
        response_ref: Ref = Ref.None,
        /// We also need to declare the namespace ref for `bun:app` and attach
        /// it to the symbol so the code generated `e_import_identifier`'s
        bun_app_namespace_ref: Ref = Ref.None,

        /// Used to track the `feature` function from `import { feature } from "bun:bundle"`.
        /// When visiting e_call, if the target ref matches this, we replace the call with
        /// a boolean based on whether the feature flag is enabled.
        bundler_feature_flag_ref: Ref = Ref.None,
        /// Set to true when visiting an if/ternary condition. feature() calls are only valid in this context.
        in_branch_condition: bool = false,

        scopes_in_order_visitor_index: usize = 0,
        has_classic_runtime_warned: bool = false,
        macro_call_count: MacroCallCountType = 0,

        hoisted_ref_for_sloppy_mode_block_fn: RefRefMap = {},

        /// Used for transforming export default -> module.exports
        has_export_default: bool = false,
        has_export_keyword: bool = false,

        // Used for forcing CommonJS
        has_with_scope: bool = false,

        is_file_considered_to_have_esm_exports: bool = false,

        has_called_runtime: bool = false,

        legacy_cjs_import_stmts: std.array_list.Managed(Stmt),

        injected_define_symbols: List(Ref) = {},
        symbol_uses: SymbolUseMap = {},
        declared_symbols: DeclaredSymbol.List = {},
        declared_symbols_for_reuse: DeclaredSymbol.List = {},
        runtime_imports: RuntimeImports = RuntimeImports{},

        /// Used with unwrap_commonjs_packages
        imports_to_convert_from_require: List(DeferredImportNamespace) = {},
        unwrap_all_requires: bool = false,

        commonjs_named_exports: js_ast.Ast.CommonJSNamedExports = {},
        commonjs_named_exports_deoptimized: bool = false,
        commonjs_module_exports_assigned_deoptimized: bool = false,
        commonjs_named_exports_needs_conversion: u32 = std.math.maxInt(u32),
        had_commonjs_named_exports_this_visit: bool = false,
        commonjs_replacement_stmts: StmtNodeList = [],

        parse_pass_symbol_uses: ParsePassSymbolUsageType = undefined,

        /// Used by commonjs_at_runtime
        has_commonjs_export_names: bool = false,

        stack_check: bun.StackCheck,

        /// When this flag is enabled, we attempt to fold all expressions that
        /// TypeScript would consider to be "constant expressions". This flag is
        /// enabled inside each enum body block since TypeScript requires numeric
        /// constant folding in enum definitions.
        ///
        /// We also enable this flag in certain cases in JavaScript files such as when
        /// parsing "const" declarations at the top of a non-ESM file, but we still
        /// reuse TypeScript's notion of "constant expressions" for our own convenience.
        ///
        /// As of TypeScript 5.0, a "constant expression" is defined as follows:
        ///
        ///   An expression is considered a constant expression if it is
        ///
        ///   * a number or string literal,
        ///   * a unary +, -, or ~ applied to a numeric constant expression,
        ///   * a binary +, -, *, /, %, **, <<, >>, >>>, |, &, ^ applied to two numeric constant expressions,
        ///   * a binary + applied to two constant expressions whereof at least one is a string,
        ///   * a template expression where each substitution expression is a constant expression,
        ///   * a parenthesized constant expression,
        ///   * a dotted name (e.g. x.y.z) that references a const variable with a constant expression initializer and no type annotation,
        ///   * a dotted name that references an enum member with an enum literal type, or
        ///   * a dotted name indexed by a string literal (e.g. x.y["z"]) that references an enum member with an enum literal type.
        ///
        /// More detail: https://github.com/microsoft/TypeScript/pull/50528. Note that
        /// we don't implement certain items in this list. For example, we don't do all
        /// number-to-string conversions since ours might differ from how JavaScript
        /// would do it, which would be a correctness issue.
        ///
        /// This flag is also set globally when minify_syntax is enabled, in which this means
        /// we always fold constant expressions.
        should_fold_typescript_constant_expressions: bool = false,

        emitted_namespace_vars: RefMap = RefMap{},
        is_exported_inside_namespace: RefRefMap = {},
        local_type_names: StringBoolMap = StringBoolMap{},

        // This is the reference to the generated function argument for the namespace,
        // which is different than the reference to the namespace itself:
        //
        //   namespace ns {
        //   }
        //
        // The code above is transformed into something like this:
        //
        //   var ns1;
        //   (function(ns2) {
        //   })(ns1 or (ns1 = {}));
        //
        // This variable is "ns2" not "ns1". It is only used during the second
        // "visit" pass.
        enclosing_namespace_arg_ref: Ref? = null,

        jsx_imports: JSXImport.Symbols = {},

        /// only applicable when `.options.features.react_fast_refresh` is set.
        /// populated before visit pass starts.
        react_refresh: ReactRefresh = {},

        /// only applicable when `.options.features.server_components` is
        /// configured to wrap exports. populated before visit pass starts.
        server_components_wrap_ref: Ref = Ref.None,

        jest: Jest = {},

        // Imports (both ES6 and CommonJS) are tracked at the top level
        import_records: ImportRecordList,
        import_records_for_current_part: List(u32) = {},
        export_star_import_records: List(u32) = {},
        import_symbol_property_uses: SymbolPropertyUseMap = {},

        // These are for handling ES6 imports and exports
        esm_import_keyword: logger.Range = logger.Range.None,
        esm_export_keyword: logger.Range = logger.Range.None,
        enclosing_class_keyword: logger.Range = logger.Range.None,
        import_items_for_namespace: std.AutoHashMapUnmanaged(Ref, ImportItemForNamespaceMap) = {},
        is_import_item: RefMap = {},
        named_imports: NamedImportsType,
        named_exports: js_ast.Ast.NamedExports,
        import_namespace_cc_map: Map(ImportNamespaceCallOrConstruct, bool) = {},

        // When we're only scanning the imports
        // If they're using the automatic JSX runtime
        // We won't know that we need to import JSX robustly because we don't track
        // symbol counts. Instead, we ask:
        // "Did we parse anything that looked like JSX"?
        // If yes, then automatically add the JSX import.
        needs_jsx_import: NeedsJSXType,

        // The parser does two passes and we need to pass the scope tree information
        // from the first pass to the second pass. That's done by tracking the calls
        // to pushScopeForParsePass() and popScope() during the first pass in
        // scopesInOrder.
        //
        // Then, when the second pass calls pushScopeForVisitPass() and popScope(),
        // we consume entries from scopesInOrder and make sure they are in the same
        // order. This way the second pass can efficiently use the same scope tree
        // as the first pass without having to attach the scope tree to the AST.
        //
        // We need to split this into two passes because the pass that declares the
        // symbols must be separate from the pass that binds identifiers to declared
        // symbols to handle declaring a hoisted "var" symbol in a nested scope and
        // binding a name to it in a parent or sibling scope.
        scopes_in_order: ScopeOrderList = {},
        scope_order_to_visit: []ScopeOrder = [],

        // These properties are for the visit pass, which runs after the parse pass.
        // The visit pass binds identifiers to declared symbols, does constant
        // folding, substitutes compile-time variable definitions, and lowers certain
        // syntactic constructs as appropriate.
        stmt_expr_value: Expr.Data,
        call_target: Expr.Data,
        delete_target: Expr.Data,
        loop_body: Stmt.Data,
        module_scope: *js_ast.Scope = undefined,
        module_scope_directive_loc: logger.Loc = {},
        is_control_flow_dead: bool = false,

        /// We must be careful to avoid revisiting nodes that have scopes.
        is_revisit_for_substitution: bool = false,

        method_call_must_be_replaced_with_undefined: bool = false,

        // Inside a TypeScript namespace, an "export declare" statement can be used
        // to cause a namespace to be emitted even though it has no other observable
        // effect. This flag is used to implement this feature.
        //
        // Specifically, namespaces should be generated for all of the following
        // namespaces below except for "f", which should not be generated:
        //
        //   namespace a { export declare const a }
        //   namespace b { export declare let [[b]] }
        //   namespace c { export declare function c() }
        //   namespace d { export declare class d {} }
        //   namespace e { export declare enum e {} }
        //   namespace f { export declare namespace f {} }
        //
        // The TypeScript compiler compiles this into the following code (notice "f"
        // is missing):
        //
        //   var a; (function (a_1) {})(a or (a = {}));
        //   var b; (function (b_1) {})(b or (b = {}));
        //   var c; (function (c_1) {})(c or (c = {}));
        //   var d; (function (d_1) {})(d or (d = {}));
        //   var e; (function (e_1) {})(e or (e = {}));
        //
        // Note that this should not be implemented by declaring symbols for "export
        // declare" statements because the TypeScript compiler doesn't generate any
        // code for these statements, so these statements are actually references to
        // global variables. There is one exception, which is that local variables
        // *should* be declared as symbols because they are replaced with. This seems
        // like very arbitrary behavior but it's what the TypeScript compiler does,
        // so we try to match it.
        //
        // Specifically, in the following code below "a" and "b" should be declared
        // and should be substituted with "ns.a" and "ns.b" but the other symbols
        // shouldn't. References to the other symbols actually refer to global
        // variables instead of to symbols that are exported from the namespace.
        // This is the case as of TypeScript 4.3. I assume this is a TypeScript bug:
        //
        //   namespace ns {
        //     export declare const a
        //     export declare let [[b]]
        //     export declare function c()
        //     export declare class d { }
        //     export declare enum e { }
        //     console.log(a, b, c, d, e)
        //   }
        //
        // The TypeScript compiler compiles this into the following code:
        //
        //   var ns;
        //   (function (ns) {
        //       console.log(ns.a, ns.b, c, d, e);
        //   })(ns or (ns = {}));
        //
        // Relevant issue: https://github.com/evanw/esbuild/issues/1158
        has_non_local_export_declare_inside_namespace: bool = false,

        // This helps recognize the "await import()" pattern. When this is present,
        // warnings about non-string import paths will be omitted inside try blocks.
        await_target: js_ast.Expr.Data? = null,

        to_expr_wrapper_namespace: Binding2ExprWrapper.Namespace,
        to_expr_wrapper_hoisted: Binding2ExprWrapper.Hoisted,

        // This helps recognize the "import().catch()" pattern. We also try to avoid
        // warning about this just like the "try { await import() }" pattern.
        then_catch_chain: ThenCatchChain,

        // Temporary variables used for lowering
        temp_refs_to_declare: List(TempRef) = {},
        temp_ref_count: i32 = 0,

        // When bundling, hoisted top-level local variables declared with "var" in
        // nested scopes are moved up to be declared in the top-level scope instead.
        // The old "var" statements are turned into regular assignments instead. This
        // makes it easier to quickly scan the top-level statements for "var" locals
        // with the guarantee that all will be found.
        relocated_top_level_vars: List(js_ast.LocRef) = {},

        // ArrowFunction is a special case in the grammar. Although it appears to be
        // a PrimaryExpression, it's actually an AssignmentExpression. This means if
        // a AssignmentExpression ends up producing an ArrowFunction then nothing can
        // come after it other than the comma operator, since the comma operator is
        // the only thing above AssignmentExpression under the Expression rule:
        //
        //   AssignmentExpression:
        //     ArrowFunction
        //     ConditionalExpression
        //     LeftHandSideExpression = AssignmentExpression
        //     LeftHandSideExpression AssignmentOperator AssignmentExpression
        //
        //   Expression:
        //     AssignmentExpression
        //     Expression , AssignmentExpression
        //
        after_arrow_body_loc: logger.Loc = logger.Loc.Empty,
        import_transposer: ImportTransposer,
        require_transposer: RequireTransposer,
        require_resolve_transposer: RequireResolveTransposer,

        const_values: js_ast.Ast.ConstValuesMap = {},

        // These are backed by stack fallback allocators in _parse, and are uninitialized until then.
        binary_expression_stack: ListManaged(BinaryExpressionVisitor) = undefined,
        binary_expression_simplify_stack: ListManaged(SideEffects.BinaryExpressionSimplifyVisitor) = undefined,

        /// We build up enough information about the TypeScript namespace hierarchy to
        /// be able to resolve scope lookups and property accesses for TypeScript enum
        /// and namespace features. Each JavaScript scope object inside a namespace
        /// has a reference to a map of exported namespace members from sibling scopes.
        ///
        /// In addition, there is a map from each relevant symbol reference to the data
        /// associated with that namespace or namespace member: "ref_to_ts_namespace_member".
        /// This gives enough info to be able to resolve queries into the namespace.
        ref_to_ts_namespace_member: std.AutoHashMapUnmanaged(Ref, js_ast.TSNamespaceMember.Data) = {},
        /// When visiting expressions, namespace metadata is associated with the most
        /// recently visited node. If namespace metadata is present, "tsNamespaceTarget"
        /// will be set to the most recently visited node (as a way to mark that this
        /// node has metadata) and "tsNamespaceMemberData" will be set to the metadata.
        ts_namespace: RecentlyVisitedTSNamespace = {},
        top_level_enums: std.ArrayListUnmanaged(Ref) = {},

        scopes_in_order_for_enum: std.AutoArrayHashMapUnmanaged(logger.Loc, []ScopeOrder) = {},

        // If this is true, then all top-level statements are wrapped in a try/catch
        will_wrap_module_in_try_catch_for_using: bool = false,

        /// Used for react refresh, it must be able to insert `const _s = $RefreshSig$();`
        nearest_stmt_list: (*ListManaged(Stmt))? = null,

        /// Name from assignment context for anonymous decorated class expressions.
        /// Set before visitExpr, consumed by lowerStandardDecoratorsImpl.
        decorator_class_name: ([]const u8)? = null,


        /// use this instead of checking p.source.index
        /// because when not bundling, p.source.index is `0`
        const ImportTransposer = ExpressionTransposer(P, *const TransposeState, P.transposeImport);
        const RequireTransposer = ExpressionTransposer(P, *const TransposeState, P.transposeRequire);
        const RequireResolveTransposer = ExpressionTransposer(P, Expr, P.transposeRequireResolve);

        const Binding2ExprWrapper = struct {
            pub const Namespace = Binding.ToExpr(P, P.wrapIdentifierNamespace);
            pub const Hoisted = Binding.ToExpr(P, P.wrapIdentifierHoisting);
        };

        pub const LowerUsingDeclarationsContext = struct {
            first_using_loc: logger.Loc,
            stack_ref: Ref,
            has_await_using: bool,

            pub fn init(p: *P): LowerUsingDeclarationsContext! {
                return LowerUsingDeclarationsContext{
                    first_using_loc: logger.Loc.Empty,
                    stack_ref: p.generateTempRef("__stack"),
                    has_await_using: false,
                };
            }

            pub fn scanStmts(ctx: *LowerUsingDeclarationsContext, p: *P, stmts: []Stmt) {
                for (stmt of stmts) {
                    switch (stmt.data) {
                        .s_local => |local| {
                            if (!local.kind.isUsing()) continue;

                            if (ctx.first_using_loc.isEmpty()) {
                                ctx.first_using_loc = stmt.loc;
                            }
                            if (local.kind == .k_await_using) {
                                ctx.has_await_using = true;
                            }
                            for (*decl of local.decls.slice()) {
                                if (decl.value != null) {
                                    const decl_value = &decl.value.?;
                                    const value_loc = decl_value.loc;
                                    p.recordUsage(ctx.stack_ref);
                                    const args = bun.handleOom(p.allocator.alloc(Expr, 3));
                                    args[0] = Expr{
                                        data: { e_identifier: { ref: ctx.stack_ref } },
                                        loc: stmt.loc,
                                    };
                                    args[1] = decl_value.*;
                                    // 1. always pass this param for hopefully better jit performance
                                    // 2. pass 1 or 0 to be shorter than `true` or `false`
                                    args[2] = Expr{
                                        data: { e_number: { value: if (local.kind == .k_await_using) 1 else 0 } },
                                        loc: stmt.loc,
                                    };
                                    decl.value = p.callRuntime(value_loc, "__using", args);
                                }
                            }
                            if (p.will_wrap_module_in_try_catch_for_using && p.current_scope.kind == .entry) {
                                local.kind = .k_var;
                            } else {
                                local.kind = .k_const;
                            }
                        },
                        else => {},
                    }
                }
            }

            pub fn finalize(ctx: *LowerUsingDeclarationsContext, p: *P, stmts: []Stmt, should_hoist_fns: bool): ListManaged(Stmt) {
                let result = ListManaged(Stmt).init(p.allocator);
                let exports = ListManaged(js_ast.ClauseItem).init(p.allocator);
                let end: u32 = 0;
                for (stmt of stmts) {
                    switch (stmt.data) {
                        .s_directive, .s_import, .s_export_from, .s_export_star => {
                            // These can't go in a try/catch block
                            bun.handleOom(result.append(stmt));
                            continue;
                        },

                        .s_class => {
                            if (stmt.data.s_class.is_export) {
                                // can't go in try/catch; hoist out
                                bun.handleOom(result.append(stmt));
                                continue;
                            }
                        },

                        .s_export_default => {
                            continue; // this prevents re-exporting default since we already have it as an .s_export_clause
                        },

                        .s_export_clause => |data| {
                            // Merge export clauses together
                            bun.handleOom(exports.appendSlice(data.items));
                            continue;
                        },

                        .s_function => {
                            if (should_hoist_fns) {
                                // Hoist function declarations for cross-file ESM references
                                bun.handleOom(result.append(stmt));
                                continue;
                            }
                        },

                        .s_local => |local| {
                            // If any of these are exported, turn it into a "var" and add export clauses
                            if (local.is_export) {
                                local.is_export = false;
                                for (decl of local.decls.slice()) {
                                    if (decl.binding.data == .b_identifier) {
                                        const identifier = decl.binding.data.b_identifier;
                                        exports.append(js_ast.ClauseItem{
                                            name: {
                                                loc: decl.binding.loc,
                                                ref: identifier.ref,
                                            },
                                            alias: p.symbols.items[identifier.ref.inner_index].original_name,
                                            alias_loc: decl.binding.loc,
                                        }) catch |e| bun.handleOom(e);
                                        local.kind = .k_var;
                                    }
                                }
                            }
                        },

                        else => {},
                    }

                    stmts[end] = stmt;
                    end += 1;
                }

                const non_exported_statements = stmts[0..end];

                const caught_ref = p.generateTempRef("_catch");
                const err_ref = p.generateTempRef("_err");
                const has_err_ref = p.generateTempRef("_hasErr");

                let scope = p.current_scope;
                while (!scope.kindStopsHoisting()) {
                    scope = scope.parent.?;
                }

                const is_top_level = scope == p.module_scope;
                scope.generated.appendSlice(p.allocator, &.{
                    ctx.stack_ref,
                    caught_ref,
                    err_ref,
                    has_err_ref,
                }) catch |e| bun.handleOom(e);
                p.declared_symbols.ensureUnusedCapacity(
                    p.allocator,
                    // 5 to include the _promise decl later on:
                    if (ctx.has_await_using) 5 else 4,
                ) catch |e| bun.handleOom(e);
                p.declared_symbols.appendAssumeCapacity({ is_top_level: is_top_level, ref: ctx.stack_ref });
                p.declared_symbols.appendAssumeCapacity({ is_top_level: is_top_level, ref: caught_ref });
                p.declared_symbols.appendAssumeCapacity({ is_top_level: is_top_level, ref: err_ref });
                p.declared_symbols.appendAssumeCapacity({ is_top_level: is_top_level, ref: has_err_ref });

                const loc = ctx.first_using_loc;
                const call_dispose = call_dispose: {
                    p.recordUsage(ctx.stack_ref);
                    p.recordUsage(err_ref);
                    p.recordUsage(has_err_ref);
                    const args = bun.handleOom(p.allocator.alloc(Expr, 3));
                    args[0] = Expr{
                        data: { e_identifier: { ref: ctx.stack_ref } },
                        loc: loc,
                    };
                    args[1] = Expr{
                        data: { e_identifier: { ref: err_ref } },
                        loc: loc,
                    };
                    args[2] = Expr{
                        data: { e_identifier: { ref: has_err_ref } },
                        loc: loc,
                    };
                    break :call_dispose p.callRuntime(loc, "__callDispose", args);
                };

                const finally_stmts = finally: {
                    if (ctx.has_await_using) {
                        const promise_ref = p.generateTempRef("_promise");
                        bun.handleOom(scope.generated.append(p.allocator, promise_ref));
                        p.declared_symbols.appendAssumeCapacity({ is_top_level: is_top_level, ref: promise_ref });

                        const promise_ref_expr = p.newExpr(E.Identifier{ ref: promise_ref }, loc);

                        const await_expr = p.newExpr(E.Await{
                            value: promise_ref_expr,
                        }, loc);
                        p.recordUsage(promise_ref);

                        const statements = bun.handleOom(p.allocator.alloc(Stmt, 2));
                        statements[0] = p.s(S.Local{
                            decls: decls: {
                                const decls = bun.handleOom(p.allocator.alloc(Decl, 1));
                                decls[0] = {
                                    binding: p.b(B.Identifier{ ref: promise_ref }, loc),
                                    value: call_dispose,
                                };
                                break :decls G.Decl.List.fromOwnedSlice(decls);
                            },
                        }, loc);

                        // The "await" must not happen if an error was thrown before the
                        // "await using", so we conditionally await here:
                        //
                        //   var promise = __callDispose(stack, error, hasError);
                        //   promise && await promise;
                        //
                        statements[1] = p.s(S.SExpr{
                            value: p.newExpr(E.Binary{
                                op: .bin_logical_and,
                                left: promise_ref_expr,
                                right: await_expr,
                            }, loc),
                        }, loc);

                        break :finally statements;
                    } else {
                        const single = bun.handleOom(p.allocator.alloc(Stmt, 1));
                        single[0] = p.s(S.SExpr{ value: call_dispose }, call_dispose.loc);
                        break :finally single;
                    }
                };

                // Wrap everything in a try/catch/finally block
                p.recordUsage(caught_ref);
                bun.handleOom(result.ensureUnusedCapacity(2 + @as(usize, @intFromBool(exports.items.len > 0))));
                result.appendAssumeCapacity(p.s(S.Local{
                    decls: decls: {
                        const decls = bun.handleOom(p.allocator.alloc(Decl, 1));
                        decls[0] = {
                            binding: p.b(B.Identifier{ ref: ctx.stack_ref }, loc),
                            value: p.newExpr(E.Array{ is_single_line: false }, loc),
                        };
                        break :decls G.Decl.List.fromOwnedSlice(decls);
                    },
                    kind: .k_let,
                }, loc));
                result.appendAssumeCapacity(p.s(S.Try{
                    body: non_exported_statements,
                    body_loc: loc,
                    catch_: {
                        binding: p.b(B.Identifier{ ref: caught_ref }, loc),
                        body: catch_body: {
                            const statements = bun.handleOom(p.allocator.alloc(Stmt, 1));
                            statements[0] = p.s(S.Local{
                                decls: decls: {
                                    const decls = bun.handleOom(p.allocator.alloc(Decl, 2));
                                    decls[0] = {
                                        binding: p.b(B.Identifier{ ref: err_ref }, loc),
                                        value: p.newExpr(E.Identifier{ ref: caught_ref }, loc),
                                    };
                                    decls[1] = {
                                        binding: p.b(B.Identifier{ ref: has_err_ref }, loc),
                                        value: p.newExpr(E.Number{ value: 1 }, loc),
                                    };
                                    break :decls G.Decl.List.fromOwnedSlice(decls);
                                },
                            }, loc);
                            break :catch_body statements;
                        },
                        body_loc: loc,
                        loc: loc,
                    },
                    finally: {
                        loc: loc,
                        stmts: finally_stmts,
                    },
                }, loc));

                if (exports.items.len > 0) {
                    result.appendAssumeCapacity(p.s(S.ExportClause{
                        items: exports.items,
                        is_single_line: false,
                    }, loc));
                }

                return result;
            }
        };

        static inline fn isSourceRuntime(p: *const P): bool {
            return p.options.bundle && p.source.index.isRuntime();
        }

        /// Extracts a matchable "shape" from a dynamic import argument.
        /// Template literals: static parts joined by \x00 placeholders.
        /// Everything else: empty string.
        fn extractDynamicSpecifierShape(p: *P, arg: Expr, buf: *std.array_list.Managed(u8)): []const u8! {
            if (tmpl := arg.data.@"as"(.e_template)) {
                if (tmpl.tag != null) return ""; // tagged template — opaque
                switch (tmpl.head) {
                    .cooked => |*head| {
                        buf.appendSlice(head.slice(p.allocator))?;
                    },
                    .raw => return "", // shouldn't happen post-visit but be safe
                }
                for (*part of tmpl.parts) {
                    buf.append(0)?; // \x00 placeholder per interpolation
                    switch (part.tail) {
                        .cooked => |*tail| {
                            buf.appendSlice(tail.slice(p.allocator))?;
                        },
                        .raw => return "", // raw tail — treat as opaque
                    }
                }
                return buf.items;
            }
            return "";
        }

        static fn checkDynamicSpecifier(p: *P, arg: Expr, loc: logger.Loc, comptime kind: []const u8)! {
            if (!p.options.bundle || p.options.allow_unresolved.* == .all) return;

            let shape_buf = std.array_list.Managed(u8).init(p.allocator);
            const _shape_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &shape_buf };
            const shape = p.extractDynamicSpecifierShape(arg, &shape_buf)?;
            if (!p.options.allow_unresolved.allows(shape)) {
                const r = js_lexer.rangeOfIdentifier(p.source, loc);
                if (shape.len > 0) {
                    // Print a human-readable shape: replace \x00 with *
                    const display = p.allocator.dupe(u8, shape)?;
                    const _display_guard = FreeGuard{ allocator: p.allocator, slice: display };
                    for (*c of display) if (c == 0) {
                        c = '*';
                    };
                    try p.log.addRangeErrorFmtWithNote(
                        p.source,
                        r,
                        p.allocator,
                        "This " ++ kind ++ " expression will not be bundled because the argument is not a string literal",
                        {},
                        "The specifier shape \"{s}\" does not match any --allow-unresolved pattern. " ++
                            "To allow it, add a matching pattern: Bun.build({{ allowUnresolved: [\"{s}\"] }}) or --allow-unresolved '{s}'",
                        .{ display, display, display },
                        r,
                    );
                } else {
                    try p.log.addRangeErrorFmtWithNote(
                        p.source,
                        r,
                        p.allocator,
                        "This " ++ kind ++ " expression will not be bundled because the argument is not a string literal",
                        {},
                        "To allow opaque dynamic specifiers, use Bun.build({{ allowUnresolved: [\"\"] }}) or pass --allow-unresolved with an empty-string pattern",
                        {},
                        r,
                    );
                }
            }
        }

        static fn transposeImport(p: *P, arg: Expr, state: *const TransposeState): Expr {
            // The argument must be a string
            if (str := arg.data.@"as"(.e_string)) {
                // Ignore calls to import() if the control flow is provably dead here.
                // We don't want to spend time scanning the required files if they will
                // never be used.
                if (p.is_control_flow_dead) {
                    return p.newExpr(std.mem.zeroes(E.Null), arg.loc);
                }

                const import_record_index = p.addImportRecord(.dynamic, arg.loc, str.slice(p.allocator));

                if (tag := state.import_record_tag) {
                    p.import_records.items[import_record_index].tag = tag;
                }

                if (loader := state.import_loader) {
                    p.import_records.items[import_record_index].loader = loader;
                }

                p.import_records.items[import_record_index].flags.handles_import_errors = (state.is_await_target && p.fn_or_arrow_data_visit.try_body_count != 0) || state.is_then_catch_target;
                p.import_records_for_current_part.append(p.allocator, import_record_index) catch unreachable;

                return p.newExpr(E.Import{
                    expr: arg,
                    import_record_index: @as(u32, @intCast(import_record_index)),
                    options: state.import_options,
                }, state.loc);
            }

            if (p.options.warn_about_unbundled_modules) {
                // Use a debug log so people can see this if they want to
                const r = js_lexer.rangeOfIdentifier(p.source, state.loc);
                p.log.addRangeDebug(p.source, r, "This \"import\" expression cannot be bundled because the argument is not a string literal") catch unreachable;
            }

            bun.handleOom(p.checkDynamicSpecifier(arg, state.loc, "import()"));

            return p.newExpr(E.Import{
                expr: arg,
                options: state.import_options,
                import_record_index: std.math.maxInt(u32),
            }, state.loc);
        }

        static fn transposeRequireResolve(p: *P, arg: Expr, require_resolve_ref: Expr): Expr {
            // The argument must be a string
            if (arg.data == .e_string) {
                return p.transposeRequireResolveKnownString(arg);
            }

            if (p.options.warn_about_unbundled_modules) {
                // Use a debug log so people can see this if they want to
                const r = js_lexer.rangeOfIdentifier(p.source, arg.loc);
                p.log.addRangeDebug(p.source, r, "This \"require.resolve\" expression cannot be bundled because the argument is not a string literal") catch unreachable;
            }

            bun.handleOom(p.checkDynamicSpecifier(arg, arg.loc, "require.resolve()"));

            const args = p.allocator.alloc(Expr, 1) catch unreachable;
            args[0] = arg;

            return p.newExpr(E.Call{
                target: require_resolve_ref,
                args: ExprNodeList.fromOwnedSlice(args),
            }, arg.loc);
        }

        static inline fn transposeRequireResolveKnownString(p: *P, arg: Expr): Expr {
            bun.assert(arg.data == .e_string);

            // Ignore calls to import() if the control flow is provably dead here.
            // We don't want to spend time scanning the required files if they will
            // never be used.
            if (p.is_control_flow_dead) {
                return p.newExpr(std.mem.zeroes(E.Null), arg.loc);
            }

            const import_record_index = p.addImportRecord(.require_resolve, arg.loc, arg.data.e_string.string(p.allocator) catch unreachable);
            p.import_records.items[import_record_index].flags.handles_import_errors = p.fn_or_arrow_data_visit.try_body_count != 0;
            p.import_records_for_current_part.append(p.allocator, import_record_index) catch unreachable;

            return p.newExpr(
                E.RequireResolveString{
                    import_record_index: import_record_index,
                    // .leading_interior_comments = arg.getString().
                },
                arg.loc,
            );
        }

        static fn transposeRequire(p: *P, arg: Expr, state: *const TransposeState): Expr {
            if (!p.options.features.allow_runtime) {
                const args = bun.handleOom(p.allocator.alloc(Expr, 1));
                args[0] = arg;
                return p.newExpr(
                    E.Call{
                        target: p.valueForRequire(arg.loc),
                        args: ExprNodeList.fromOwnedSlice(args),
                    },
                    arg.loc,
                );
            }

            switch (arg.data) {
                .e_string => |str| {
                    // Ignore calls to require() if the control flow is provably dead here.
                    // We don't want to spend time scanning the required files if they will
                    // never be used.
                    if (p.is_control_flow_dead) {
                        return Expr{ data: nullExprData, loc: arg.loc };
                    }

                    str.resolveRopeIfNeeded(p.allocator);
                    const pathname = str.string(p.allocator) catch unreachable;
                    const path = fs.Path.init(pathname);

                    const handles_import_errors = p.fn_or_arrow_data_visit.try_body_count != 0;

                    // For unwrapping CommonJS into ESM to fully work
                    // we must also unwrap requires into imports.
                    let should_unwrap_pkg = false;
                    if (pkg := path.packageName()) {
                        should_unwrap_pkg = p.options.features.shouldUnwrapRequire(pkg);
                    }
                    const should_unwrap_require = p.options.features.unwrap_commonjs_to_esm &&
                        (p.unwrap_all_requires || should_unwrap_pkg) &&
                        !handles_import_errors;

                    if (should_unwrap_require) {
                        const import_record_index = p.addImportRecordByRangeAndPath(.stmt, p.source.rangeOfString(arg.loc), path);
                        p.import_records.items[import_record_index].flags.handles_import_errors = handles_import_errors;

                        // Note that this symbol may be completely removed later.
                        let path_name = fs.PathName.init(path.text);
                        const name = bun.handleOom(path_name.nonUniqueNameString(p.allocator));
                        const namespace_ref = bun.handleOom(p.newSymbol(.other, name));

                        p.imports_to_convert_from_require.append(p.allocator, {
                            namespace: {
                                ref: namespace_ref,
                                loc: arg.loc,
                            },
                            import_record_id: import_record_index,
                        }) catch |e| bun.handleOom(e);
                        bun.handleOom(p.import_items_for_namespace.put(p.allocator, namespace_ref, ImportItemForNamespaceMap.init(p.allocator)));
                        p.recordUsage(namespace_ref);

                        if (!state.is_require_immediately_assigned_to_decl) {
                            return p.newExpr(E.Identifier{
                                ref: namespace_ref,
                            }, arg.loc);
                        }

                        return p.newExpr(
                            E.RequireString{
                                import_record_index: import_record_index,
                                unwrapped_id: @as(u32, i32(p.imports_to_convert_from_require.items.len - 1)),
                            },
                            arg.loc,
                        );
                    }

                    const import_record_index = p.addImportRecordByRangeAndPath(.require, p.source.rangeOfString(arg.loc), path);
                    p.import_records.items[import_record_index].flags.handles_import_errors = handles_import_errors;
                    p.import_records_for_current_part.append(p.allocator, import_record_index) catch unreachable;

                    return p.newExpr(E.RequireString{ import_record_index: import_record_index }, arg.loc);
                },
                else => {
                    bun.handleOom(p.checkDynamicSpecifier(arg, arg.loc, "require()"));
                    p.recordUsageOfRuntimeRequire();
                    const args = p.allocator.alloc(Expr, 1) catch unreachable;
                    args[0] = arg;
                    return p.newExpr(
                        E.Call{
                            target: p.valueForRequire(arg.loc),
                            args: ExprNodeList.fromOwnedSlice(args),
                        },
                        arg.loc,
                    );
                },
            }
        }

        static inline fn shouldUnwrapCommonJSToESM(p: *const P): bool {
            return p.options.features.unwrap_commonjs_to_esm;
        }

        fn isBindingUsed(p: *P, binding: Binding, default_export_ref: Ref): bool {
            switch (binding.data) {
                .b_identifier => |ident| {
                    if (default_export_ref.eql(ident.ref)) return true;
                    if (p.named_imports.contains(ident.ref))
                        return true;

                    for (named_export of p.named_exports.values()) {
                        if (named_export.ref.eql(ident.ref))
                            return true;
                    }

                    const symbol: *const Symbol = &p.symbols.items[ident.ref.innerIndex()];
                    return symbol.use_count_estimate > 0;
                },
                .b_array => |array| {
                    for (item of array.items) {
                        if (isBindingUsed(p, item.binding, default_export_ref)) {
                            return true;
                        }
                    }

                    return false;
                },
                .b_object => |obj| {
                    for (prop of obj.properties) {
                        if (isBindingUsed(p, prop.value, default_export_ref)) {
                            return true;
                        }
                    }

                    return false;
                },
                .b_missing => return false,
            }
        }

        static fn treeShake(p: *P, parts: *[]js_ast.Part, merge: bool) {
            let parts_: []js_ast.Part = parts.*;
            defer {
                if (merge && parts_.len > 1) {
                    let first_none_part: usize = parts_.len;
                    let stmts_count: usize = 0;
                    let i: usize = 0;
                    while (i < parts_.len) : (i += 1) {
                        const part = parts_[i];
                        if (part.tag == .none) {
                            stmts_count += part.stmts.len;
                            first_none_part = @min(i, first_none_part);
                        }
                    }

                    if (first_none_part < parts_.len) {
                        const stmts_list = p.allocator.alloc(Stmt, stmts_count) catch unreachable;
                        let stmts_remain = stmts_list;

                        for (part of parts_) {
                            if (part.tag == .none) {
                                bun.copy(Stmt, stmts_remain, part.stmts);
                                stmts_remain = stmts_remain[part.stmts.len..];
                            }
                        }

                        parts_[first_none_part].stmts = stmts_list;

                        parts_ = parts_[0 .. first_none_part + 1];
                    }
                }

                parts.* = parts_;
            }
            let default_export_ref = Ref.None;
            if (default_ := p.named_exports.get("default")) {
                default_export_ref = default_.ref;
            }

            while (parts_.len > 1) {
                let parts_end: usize = 0;
                const last_end = parts_.len;

                for (part of parts_) {
                    let is_dead = part.can_be_removed_if_unused;
                    if (is_dead) {
                        for (stmt of part.stmts) {
                            switch (stmt.data) {
                                .s_local => |local| {
                                    if (local.is_export) {
                                        is_dead = false;
                                        break;
                                    }
                                    for (decl of local.decls.slice()) {
                                        if (isBindingUsed(p, decl.binding, default_export_ref)) {
                                            is_dead = false;
                                            break;
                                        }
                                    }
                                    if (!is_dead) break;
                                },
                                .s_if => |if_statement| {
                                    const result = SideEffects.toBoolean(p, if_statement.test_.data);
                                    if (!(result.ok && result.side_effects == .no_side_effects && !result.value)) {
                                        is_dead = false;
                                        break;
                                    }
                                },
                                .s_while => |while_statement| {
                                    const result = SideEffects.toBoolean(p, while_statement.test_.data);
                                    if (!(result.ok && result.side_effects == .no_side_effects && !result.value)) {
                                        is_dead = false;
                                        break;
                                    }
                                },
                                .s_for => |for_statement| {
                                    if (expr := for_statement.test_) {
                                        const result = SideEffects.toBoolean(p, expr.data);
                                        if (!(result.ok && result.side_effects == .no_side_effects && !result.value)) {
                                            is_dead = false;
                                            break;
                                        }
                                    }
                                },
                                .s_function => |func| {
                                    if (func.func.flags.contains(.is_export)) {
                                        is_dead = false;
                                        break;
                                    }
                                    if (name := func.func.name) {
                                        const symbol: *const Symbol = &p.symbols.items[name.ref.?.innerIndex()];

                                        if (name.ref.?.eql(default_export_ref) ||
                                            symbol.use_count_estimate > 0 ||
                                            p.named_exports.contains(symbol.original_name) ||
                                            p.named_imports.contains(name.ref.?) ||
                                            p.is_import_item.get(name.ref.?) != null)
                                        {
                                            is_dead = false;
                                            break;
                                        }
                                    }
                                },
                                .s_import,
                                .s_export_clause,
                                .s_export_from,
                                .s_export_default
                                => {
                                    is_dead = false;
                                    break;
                                },

                                .s_class => |cls| {
                                    if (cls.is_export) {
                                        is_dead = false;
                                        break;
                                    }
                                    if (name := cls.class.class_name) {
                                        const symbol: *const Symbol = &p.symbols.items[name.ref.?.innerIndex()];

                                        if (name.ref.?.eql(default_export_ref) ||
                                            symbol.use_count_estimate > 0 ||
                                            p.named_exports.contains(symbol.original_name) ||
                                            p.named_imports.contains(name.ref.?) ||
                                            p.is_import_item.get(name.ref.?) != null)
                                        {
                                            is_dead = false;
                                            break;
                                        }
                                    }
                                },

                                else => {
                                    is_dead = false;
                                    break;
                                },
                            }
                        }
                    }

                    if (is_dead) {
                        p.clearSymbolUsagesFromDeadPart(&part);

                        continue;
                    }

                    parts_[parts_end] = part;
                    parts_end += 1;
                }

                parts_.len = parts_end;
                if (last_end == parts_.len) {
                    break;
                }
            }
        }



        fn clearSymbolUsagesFromDeadPart(p: *P, part: *const js_ast.Part) {
            const symbol_use_refs = part.symbol_uses.keys();
            const symbol_use_values = part.symbol_uses.values();
            let symbols = p.symbols.items;

            let idx: usize = 0;
            while (idx < symbol_use_refs.len) : (idx += 1) {
                const sym_ref = symbol_use_refs[idx];
                const prev = symbol_use_values[idx];
                symbols[sym_ref.innerIndex()].use_count_estimate = symbols[sym_ref.innerIndex()].use_count_estimate -| prev.count_estimate;
            }
            const declared_refs = part.declared_symbols.refs();
            for (declared of declared_refs) {
                symbols[declared.innerIndex()].use_count_estimate = 0;
            }
        }

        static fn s(p_unused: *const P, t: auto, loc: logger.Loc): Stmt {
            const Type = @TypeOf(t);
            if (Type == type) {
                return Stmt.alloc(t, std.mem.zeroes(t), loc);
            }
            if (!is_typescript_enabled && (Type == S.TypeScript || Type == *S.TypeScript)) {
                @compileError("Attempted to use TypeScript syntax in a non-TypeScript environment");
            }

            // Output.print("\nStmt: {s} - {d}\n", .{ @typeName(@TypeOf(t)), loc.start });
            if (@typeInfo(Type) == .pointer) {
                // ExportFrom normally becomes import records during the visiting pass
                // However, we skip the visiting pass in this mode
                // So we must generate a minimum version of it here.
                if (comptime only_scan_imports_and_do_not_visit) {
                    // if (@TypeOf(t) == *S.ExportFrom) {
                    //     switch (call.target.data) {
                    //         .e_identifier => |ident| {
                    //             // is this a require("something")
                    //             if (strings.eqlComptime(p.loadNameFromRef(ident.ref), "require") and call.args.len == 1 and std.meta.activeTag(call.args[0].data) == .e_string) {
                    //                 _ = p.addImportRecord(.require, loc, call.args[0].data.e_string.string(p.allocator) catch unreachable);
                    //             }
                    //         },
                    //         else => {},
                    //     }
                    // }
                }

                return Stmt.init(std.@"meta".Child(Type), t, loc);
            } else {
                return Stmt.alloc(Type, t, loc);
            }
        }

        fn computeCharacterFrequency(p: *P): js_ast.CharFreq? {
            if (!p.options.features.minify_identifiers || p.isSourceRuntime()) {
                return null;
            }

            // Add everything in the file to the histogram
            let freq: js_ast.CharFreq = {
                freqs: @splat(0),
            };

            freq.scan(p.source.contents, 1);

            // Subtract out all comments
            for (comment_range of p.lexer.all_comments.items) {
                freq.scan(p.source.textForRange(comment_range), -1);
            }

            // Subtract out all import paths
            for (record of p.import_records.items) {
                freq.scan(record.path.text, -1);
            }

            const ScopeVisitor = struct {
                pub fn visit(symbols: []const js_ast.Symbol, char_freq: *js_ast.CharFreq, scope: *js_ast.Scope) {
                    let iter = scope.members.iterator();

                    while (entry := iter.next()) {
                        const symbol: *const Symbol = &symbols[entry.value_ptr.ref.innerIndex()];

                        if (symbol.slotNamespace() != .must_not_be_renamed) {
                            char_freq.scan(symbol.original_name, -@as(i32, i32(symbol.use_count_estimate)));
                        }
                    }

                    if (sym_ref := scope.label_ref) {
                        const symbol = &symbols[sym_ref.innerIndex()];

                        if (symbol.slotNamespace() != .must_not_be_renamed) {
                            char_freq.scan(symbol.original_name, -@as(i32, i32(symbol.use_count_estimate)) - 1);
                        }
                    }

                    for (child of scope.children.slice()) {
                        visit(symbols, char_freq, child);
                    }
                }
            };
            ScopeVisitor.visit(p.symbols.items, &freq, p.module_scope);

            // TODO: mangledProps

            return freq;
        }

        static fn newExpr(p: *P, t: auto, loc: logger.Loc): Expr {
            const Type = @TypeOf(t);
            if (comptime Type == type) {
                return Expr.init(t, std.mem.zeroes(t), loc);
            }

            comptime {
                if (jsx_transform_type == .none) {
                    if (Type == E.JSXElement || Type == *E.JSXElement) {
                        @compileError("JSXElement is not supported in this environment");
                    }
                }
            }

            // Output.print("\nExpr: {s} - {d}\n", .{ @typeName(@TypeOf(t)), loc.start });
            if (@typeInfo(Type) == .pointer) {
                if (comptime only_scan_imports_and_do_not_visit) {
                    if (Type == *E.Call) {
                        const call: *E.Call = t;
                        switch (call.target.data) {
                            .e_identifier => |ident| {
                                // is this a require("something")
                                if (strings.eqlComptime(p.loadNameFromRef(ident.ref), "require") && call.args.len == 1 && call.args.ptr[0].data == .e_string) {
                                    * = p.addImportRecord(.require, loc, call.args.at(0).data.e_string.string(p.allocator) catch unreachable);
                                }
                            },
                            else => {},
                        }
                    }
                }
                return Expr.init(std.@"meta".Child(Type), t.*, loc);
            } else {
                if (comptime only_scan_imports_and_do_not_visit) {
                    if (Type == E.Call) {
                        const call: E.Call = t;
                        switch (call.target.data) {
                            .e_identifier => |ident| {
                                // is this a require("something")
                                if (strings.eqlComptime(p.loadNameFromRef(ident.ref), "require") && call.args.len == 1 && call.args.ptr[0].data == .e_string) {
                                    * = p.addImportRecord(.require, loc, call.args.at(0).data.e_string.string(p.allocator) catch unreachable);
                                }
                            },
                            else => {},
                        }
                    }
                }
                return Expr.init(Type, t, loc);
            }
        }

        static fn b(p: *P, t: auto, loc: logger.Loc): Binding {
            if (@typeInfo(@TypeOf(t)) == .pointer) {
                return Binding.init(t, loc);
            } else {
                return Binding.alloc(p.allocator, t, loc);
            }
        }

        static fn recordExportedBinding(p: *P, binding: Binding) {
            switch (binding.data) {
                .b_missing => {},
                .b_identifier => |ident| {
                    p.recordExport(binding.loc, p.symbols.items[ident.ref.innerIndex()].original_name, ident.ref) catch unreachable;
                },
                .b_array => |array| {
                    for (prop of array.items) {
                        p.recordExportedBinding(prop.binding);
                    }
                },
                .b_object => |obj| {
                    for (prop of obj.properties) {
                        p.recordExportedBinding(prop.value);
                    }
                },
            }
        }

        static fn recordExport(p: *P, loc: logger.Loc, alias: string, sym_ref: Ref)! {
            if (name := p.named_exports.get(alias)) {
                // Duplicate exports are an error
                let notes = p.allocator.alloc(logger.Data, 1)?;
                notes[0] = logger.Data{
                    text: try std.fmt.allocPrint(p.allocator, "\"{s}\" was originally exported here", .{alias}),
                    location: logger.Location.initOrNull(p.source, js_lexer.rangeOfIdentifier(p.source, name.alias_loc)),
                };
                try p.log.addRangeErrorFmtWithNotes(
                    p.source,
                    js_lexer.rangeOfIdentifier(p.source, loc),
                    p.allocator,
                    notes,
                    "Multiple exports with the same name \"{s}\"",
                    .{std.mem.trim(u8, alias, "\"'")},
                );
            } else if (!p.isDeoptimizedCommonJS()) {
                p.named_exports.put(p.allocator, alias, js_ast.NamedExport{ alias_loc: loc, ref: sym_ref })?;
            }
        }

        static fn isDeoptimizedCommonJS(p: *P): bool {
            return p.commonjs_named_exports_deoptimized && p.commonjs_named_exports.count() > 0;
        }

        static fn recordUsage(p: *P, sym_ref: Ref) {
            if (p.is_revisit_for_substitution) return;
            // The use count stored in the symbol is used for generating symbol names
            // during minification. These counts shouldn't include references inside dead
            // code regions since those will be culled.
            if (!p.is_control_flow_dead) {
                if (comptime Environment.allow_assert) assert(p.symbols.items.len > sym_ref.innerIndex());
                p.symbols.items[sym_ref.innerIndex()].use_count_estimate += 1;
                let result = p.symbol_uses.getOrPut(p.allocator, sym_ref) catch unreachable;
                if (!result.found_existing) {
                    result.value_ptr.* = Symbol.Use{ count_estimate: 1 };
                } else {
                    result.value_ptr.count_estimate += 1;
                }
            }

            // The correctness of TypeScript-to-JavaScript conversion relies on accurate
            // symbol use counts for the whole file, including dead code regions. This is
            // tracked separately in a parser-only data structure.
            if (is_typescript_enabled) {
                p.ts_use_counts.items[sym_ref.innerIndex()] += 1;
            }
        }

        static fn logArrowArgErrors(p: *P, errors: *DeferredArrowArgErrors) {
            if (errors.invalid_expr_await.len > 0) {
                const r = errors.invalid_expr_await;
                p.log.addRangeError(p.source, r, "Cannot use an \"await\" expression here") catch unreachable;
            }

            if (errors.invalid_expr_yield.len > 0) {
                const r = errors.invalid_expr_yield;
                p.log.addRangeError(p.source, r, "Cannot use a \"yield\" expression here") catch unreachable;
            }
        }

        static fn keyNameForError(p: *P, key: *const js_ast.Expr): string {
            switch (key.data) {
                .e_string => {
                    return key.data.e_string.string(p.allocator) catch unreachable;
                },
                .e_private_identifier => |priv_ident| {
                    return p.loadNameFromRef(priv_ident.ref);
                },
                else => {
                    return "property";
                },
            }
        }

        /// This function is very very hot.
        static fn handleIdentifier(p: *P, loc: logger.Loc, ident: E.Identifier, original_name: string?, opts: IdentifierOpts): Expr {
            const sym_ref = ident.ref;

            if (p.options.features.inlining) {
                if (replacement := p.const_values.get(sym_ref)) {
                    p.ignoreUsage(sym_ref);
                    return replacement;
                }
            }

            // Create an error for assigning to an import namespace
            if ((opts.assign_target != .none || opts.is_delete_target) && p.symbols.items[sym_ref.innerIndex()].kind == .@"import") {
                const r = js_lexer.rangeOfIdentifier(p.source, loc);
                p.log.addRangeErrorFmt(p.source, r, p.allocator, "Cannot assign to import \"{s}\"", .{
                    p.symbols.items[sym_ref.innerIndex()].original_name,
                }) catch unreachable;
            }

            // Substitute an EImportIdentifier now if this has a namespace alias
            if (opts.assign_target == .none && !opts.is_delete_target) {
                const symbol = &p.symbols.items[sym_ref.inner_index];
                if (ns_alias := symbol.namespace_alias) {
                    if (ts_member_data := p.ref_to_ts_namespace_member.get(ns_alias.namespace_ref)) {
                        if (ts_member_data == .namespace) {
                            if (member := ts_member_data.namespace.get(ns_alias.alias)) {
                                switch (member.data) {
                                    .enum_number => |num| return p.wrapInlinedEnum(
                                        { loc: loc, data: { e_number: { value: num } } },
                                        p.symbols.items[sym_ref.inner_index].original_name,
                                    ),

                                    .enum_string => |str| return p.wrapInlinedEnum(
                                        { loc: loc, data: { e_string: str } },
                                        p.symbols.items[sym_ref.inner_index].original_name,
                                    ),

                                    .namespace => |map| {
                                        const expr = p.newExpr(E.Dot{
                                            target: p.newExpr(E.Identifier.init(ns_alias.namespace_ref), loc),
                                            name: ns_alias.alias,
                                            name_loc: loc,
                                        }, loc);
                                        p.ts_namespace = {
                                            expr: expr.data,
                                            map: map,
                                        };
                                        return expr;
                                    },

                                    else => {},
                                }
                            }
                        }
                    }

                    return p.newExpr(E.ImportIdentifier{
                        ref: ident.ref,
                        was_originally_identifier: true,
                    }, loc);
                }
            }

            // Substitute an EImportIdentifier now if this is an import item
            if (p.is_import_item.contains(sym_ref)) {
                return p.newExpr(
                    E.ImportIdentifier{ ref: sym_ref, was_originally_identifier: opts.was_originally_identifier },
                    loc,
                );
            }

            if (is_typescript_enabled) {
                if (member_data := p.ref_to_ts_namespace_member.get(sym_ref)) {
                    switch (member_data) {
                        .enum_number => |num| return p.wrapInlinedEnum(
                            { loc: loc, data: { e_number: { value: num } } },
                            p.symbols.items[sym_ref.inner_index].original_name,
                        ),

                        .enum_string => |str| return p.wrapInlinedEnum(
                            { loc: loc, data: { e_string: str } },
                            p.symbols.items[sym_ref.inner_index].original_name,
                        ),

                        .namespace => |map| {
                            const expr: Expr = {
                                data: { e_identifier: ident },
                                loc: loc,
                            };

                            p.ts_namespace = {
                                expr: expr.data,
                                map: map,
                            };

                            return expr;
                        },

                        else => {},
                    }
                }

                // Substitute a namespace export reference now if appropriate
                if (ns_ref := p.is_exported_inside_namespace.get(sym_ref)) {
                    const name = p.symbols.items[sym_ref.innerIndex()].original_name;

                    p.recordUsage(ns_ref);
                    const prop = p.newExpr(E.Dot{
                        target: p.newExpr(E.Identifier.init(ns_ref), loc),
                        name: name,
                        name_loc: loc,
                    }, loc);

                    if (p.ts_namespace.expr == .e_identifier &&
                        p.ts_namespace.expr.e_identifier.ref.eql(ident.ref))
                    {
                        p.ts_namespace.expr = prop.data;
                    }

                    return prop;
                }
            }

            if (name := original_name) {
                const result = p.findSymbol(loc, name) catch unreachable;
                let id_clone = ident;
                id_clone.ref = result.ref;
                return p.newExpr(id_clone, loc);
            }

            return {
                data: { e_identifier: ident },
                loc: loc,
            };
        }

        static fn generateImportStmtForBakeResponse(
            p: *P,
            parts: *ListManaged(js_ast.Part),
        )! {
            bun.assert(!p.response_ref.isNull());
            bun.assert(!p.bun_app_namespace_ref.isNull());
            const allocator = p.allocator;

            const import_path = "bun:app";

            const import_record_i = p.addImportRecordByRange(.stmt, logger.Range.None, import_path);

            let declared_symbols: DeclaredSymbol.List = {};
            declared_symbols.ensureTotalCapacity(allocator, 2)?;

            let stmts = allocator.alloc(Stmt, 1)?;

            declared_symbols.appendAssumeCapacity(
                DeclaredSymbol{ ref: p.bun_app_namespace_ref, is_top_level: true },
            );
            p.module_scope.generated.append(allocator, p.bun_app_namespace_ref)?;

            const clause_items = try allocator.dupe(js_ast.ClauseItem, &.{
                js_ast.ClauseItem{
                    alias: "Response",
                    original_name: "Response",
                    alias_loc: logger.Loc{},
                    name: LocRef{ ref: p.response_ref, loc: logger.Loc{} },
                },
            });

            declared_symbols.appendAssumeCapacity(DeclaredSymbol{
                ref: p.response_ref,
                is_top_level: true,
            });

            // ensure every e_import_identifier holds the namespace
            if (p.options.features.hot_module_reloading) {
                const symbol = &p.symbols.items[p.response_ref.inner_index];
                bun.assert(symbol.namespace_alias != null);
                symbol.namespace_alias.?.import_record_index = import_record_i;
            }

            p.is_import_item.put(allocator, p.response_ref, undefined)?;
            try p.named_imports.put(allocator, p.response_ref, js_ast.NamedImport{
                alias: "Response",
                alias_loc: logger.Loc{},
                namespace_ref: p.bun_app_namespace_ref,
                import_record_index: import_record_i,
            });

            stmts[0] = p.s(
                S.Import{
                    namespace_ref: p.bun_app_namespace_ref,
                    items: clause_items,
                    import_record_index: import_record_i,
                    is_single_line: true,
                },
                logger.Loc{},
            );

            let import_records = allocator.alloc(u32, 1)?;
            import_records[0] = import_record_i;

            // This import is placed in a part before the main code, however
            // the bundler ends up re-ordering this to be after... The order
            // does not matter as ESM imports are always hoisted.
            parts.append(js_ast.Part{
                stmts: stmts,
                declared_symbols: declared_symbols,
                import_record_indices: bun.BabyList(u32).fromOwnedSlice(import_records),
                tag: .runtime,
            }) catch unreachable;
        }

        static fn generateImportStmt(
            p: *P,
            import_path: string,
            imports: auto,
            parts: *ListManaged(js_ast.Part),
            symbols: auto,
            additional_stmt: Stmt?,
            comptime prefix: string,
            comptime is_internal: bool,
        ): anyerror!void {
            const allocator = p.allocator;
            const import_record_i = p.addImportRecordByRange(.stmt, logger.Range.None, import_path);
            let import_record: *ImportRecord = &p.import_records.items[import_record_i];
            if (comptime is_internal)
                import_record.path.namespace = "runtime";
            import_record.flags.is_internal = is_internal;
            const import_path_identifier = import_record.path.name.nonUniqueNameString(allocator)?;
            let namespace_identifier = allocator.alloc(u8, import_path_identifier.len + prefix.len)?;
            const clause_items = allocator.alloc(js_ast.ClauseItem, imports.len)?;
            let stmts = allocator.alloc(Stmt, 1 + if (additional_stmt != null) @as(usize, 1) else @as(usize, 0))?;
            let declared_symbols: DeclaredSymbol.List = {};
            declared_symbols.ensureTotalCapacity(allocator, imports.len + 1)?;
            bun.copy(u8, namespace_identifier, prefix);
            bun.copy(u8, namespace_identifier[prefix.len..], import_path_identifier);

            const namespace_ref = p.newSymbol(.other, namespace_identifier)?;
            declared_symbols.appendAssumeCapacity({
                ref: namespace_ref,
                is_top_level: true,
            });
            p.module_scope.generated.append(allocator, namespace_ref)?;
            let imp_i: usize = 0;
            while (imp_i < imports.len) : (imp_i += 1) {
                const alias = imports[imp_i];
                let clause_item = &clause_items[imp_i];
                const sym_ref = symbols.get(alias) ?? unreachable;
                const alias_name = if (@TypeOf(symbols) == RuntimeImports) RuntimeImports.all[alias] else alias;
                clause_item.* = js_ast.ClauseItem{
                    alias: alias_name,
                    original_name: alias_name,
                    alias_loc: logger.Loc{},
                    name: LocRef{ ref: sym_ref, loc: logger.Loc{} },
                };
                declared_symbols.appendAssumeCapacity({ ref: sym_ref, is_top_level: true });

                // ensure every e_import_identifier holds the namespace
                if (p.options.features.hot_module_reloading) {
                    const symbol = &p.symbols.items[sym_ref.inner_index];
                    if (symbol.namespace_alias == null) {
                        symbol.namespace_alias = {
                            namespace_ref: namespace_ref,
                            alias: alias_name,
                            import_record_index: import_record_i,
                        };
                    }
                }

                p.is_import_item.put(allocator, sym_ref, undefined)?;
                try p.named_imports.put(allocator, sym_ref, js_ast.NamedImport{
                    alias: alias_name,
                    alias_loc: logger.Loc{},
                    namespace_ref: namespace_ref,
                    import_record_index: import_record_i,
                });
            }

            stmts[0] = p.s(
                S.Import{
                    namespace_ref: namespace_ref,
                    items: clause_items,
                    import_record_index: import_record_i,
                    is_single_line: true,
                },
                logger.Loc{},
            );
            if (add := additional_stmt) {
                stmts[1] = add;
            }

            let import_records = allocator.alloc(@TypeOf(import_record_i), 1)?;
            import_records[0] = import_record_i;

            // This import is placed in a part before the main code, however
            // the bundler ends up re-ordering this to be after... The order
            // does not matter as ESM imports are always hoisted.
            parts.append(js_ast.Part{
                stmts: stmts,
                declared_symbols: declared_symbols,
                import_record_indices: bun.BabyList(u32).fromOwnedSlice(import_records),
                tag: .runtime,
            }) catch unreachable;
        }

        static fn generateReactRefreshImport(
            p: *P,
            parts: *ListManaged(js_ast.Part),
            import_path: []const u8,
            clauses: []const ReactRefreshImportClause,
        )! {
            switch (p.options.features.hot_module_reloading) {
                inline else => |hmr| try p.generateReactRefreshImportHmr(parts, import_path, clauses, hmr),
            }
        }


        fn generateReactRefreshImportHmr(
            p: *P,
            parts: *ListManaged(js_ast.Part),
            import_path: []const u8,
            clauses: []const ReactRefreshImportClause,
            comptime hot_module_reloading: bool,
        )! {
            // If `hot_module_reloading`, we are going to generate a require call:
            //
            //     const { $RefreshSig$, $RefreshReg$ } = require("react-refresh/runtime")`
            //
            // Otherwise we are going to settle on an import statement. Using
            // require is fine in HMR bundling because `react-refresh` itself is
            // already a CommonJS module, and it will actually be more efficient
            // at runtime this way.
            const allocator = p.allocator;
            const import_record_index = p.addImportRecordByRange(.stmt, logger.Range.None, import_path);

            const Item = if (hot_module_reloading) B.Object.Property else js_ast.ClauseItem;

            const len = 1 + @as(usize, @intFromBool(p.react_refresh.register_used)) +
                @as(usize, @intFromBool(p.react_refresh.signature_used));
            let items = List(Item).initCapacity(allocator, len)?;

            const stmts = allocator.alloc(Stmt, 1)?;
            let declared_symbols: DeclaredSymbol.List = {};
            declared_symbols.ensureTotalCapacity(allocator, len)?;

            const namespace_ref = p.newSymbol(.other, "RefreshRuntime")?;
            declared_symbols.appendAssumeCapacity({
                ref: namespace_ref,
                is_top_level: true,
            });
            p.module_scope.generated.append(allocator, namespace_ref)?;

            for (entry of clauses) {
                if (entry.enabled) {
                    if (hot_module_reloading) {
                        items.appendAssumeCapacity(B.Property{
                            key: p.newExpr(E.String{ data: entry.name }, logger.Loc.Empty),
                            value: p.b(B.Identifier{ ref: entry.ref }, logger.Loc.Empty),
                        });
                    } else {
                        items.appendAssumeCapacity(js_ast.ClauseItem{
                            alias: entry.name,
                            original_name: entry.name,
                            alias_loc: logger.Loc{},
                            name: LocRef{ ref: entry.ref, loc: logger.Loc{} },
                        });
                    }
                    declared_symbols.appendAssumeCapacity({ ref: entry.ref, is_top_level: true });
                    p.module_scope.generated.append(allocator, entry.ref)?;
                    p.is_import_item.put(allocator, entry.ref, undefined)?;
                    try p.named_imports.put(allocator, entry.ref, {
                        alias: entry.name,
                        alias_loc: logger.Loc.Empty,
                        namespace_ref: namespace_ref,
                        import_record_index: import_record_index,
                    });
                }
            }

            stmts[0] = p.s(if (hot_module_reloading)
                S.Local{
                    kind: .k_const,
                    decls: try Decl.List.fromSlice(p.allocator, &.{{
                        binding: p.b(B.Object{
                            properties: items.items,
                        }, logger.Loc.Empty),
                        value: p.newExpr(E.RequireString{
                            import_record_index: import_record_index,
                        }, logger.Loc.Empty),
                    }}),
                }
            else
                S.Import{
                    namespace_ref: namespace_ref,
                    items: items.items,
                    import_record_index: import_record_index,
                    is_single_line: false,
                }, logger.Loc.Empty);

            try parts.append({
                stmts: stmts,
                declared_symbols: declared_symbols,
                import_record_indices: try bun.BabyList(u32).fromSlice(allocator, &.{import_record_index}),
                tag: .runtime,
            });
        }

        static fn substituteSingleUseSymbolInStmt(p: *P, stmt: Stmt, sym_ref: Ref, replacement: Expr): bool {
            const expr: *Expr = brk: {
                switch (stmt.data) {
                    .s_expr => |exp| {
                        break :brk &exp.value;
                    },
                    .s_throw => |throw| {
                        break :brk &throw.value;
                    },
                    .s_return => |ret| {
                        if (ret.value != null) {
                            break :brk &ret.value.?;
                        }
                    },
                    .s_if => |if_stmt| {
                        break :brk &if_stmt.test_;
                    },
                    .s_switch => |switch_stmt| {
                        break :brk &switch_stmt.test_;
                    },
                    .s_local => |local| {
                        if (local.decls.len > 0) {
                            let first: *Decl = &local.decls.ptr[0];
                            if (first.value != null) {
                                if (first.binding.data == .b_identifier) {
                                    break :brk &first.value.?;
                                }
                            }
                        }
                    },
                    else => {},
                }

                return false;
            };

            // Only continue trying to insert this replacement into sub-expressions
            // after the first one if the replacement has no side effects:
            //
            //   // Substitution is ok
            //   let replacement = 123;
            //   return x + replacement;
            //
            //   // Substitution is not ok because "fn()" may change "x"
            //   let replacement = fn();
            //   return x + replacement;
            //
            //   // Substitution is not ok because "x == x" may change "x" due to "valueOf()" evaluation
            //   let replacement = [x];
            //   return (x == x) + replacement;
            //
            const replacement_can_be_removed = p.exprCanBeRemovedIfUnused(&replacement);
            switch (p.substituteSingleUseSymbolInExpr(expr.*, sym_ref, replacement, replacement_can_be_removed)) {
                .success => |result| {
                    if (result.data == .e_binary || result.data == .e_unary || result.data == .e_if) {
                        const prev_substituting = p.is_revisit_for_substitution;
                        p.is_revisit_for_substitution = true;
                        defer p.is_revisit_for_substitution = prev_substituting;
                        // O(n^2) and we will need to think more carefully about
                        // this once we implement syntax compression
                        expr.* = p.visitExpr(result);
                    } else {
                        expr.* = result;
                    }

                    return true;
                },
                else => {},
            }

            return false;
        }

        fn substituteSingleUseSymbolInExpr(
            p: *P,
            expr: Expr,
            sym_ref: Ref,
            replacement: Expr,
            replacement_can_be_removed: bool,
        ): Substitution {
            outer: {
                switch (expr.data) {
                    .e_identifier => |ident| {
                        if (ident.ref.eql(sym_ref) || p.symbols.items[ident.ref.innerIndex()].link.eql(sym_ref)) {
                            p.ignoreUsage(sym_ref);
                            return { success: replacement };
                        }
                    },
                    .e_new => |new_expr| {
                        switch (p.substituteSingleUseSymbolInExpr(new_expr.target, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                new_expr.target = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                new_expr.target = result;
                                return { failure: expr };
                            },
                        }

                        if (replacement_can_be_removed) {
                            for (*arg of new_expr.args.slice()) {
                                switch (p.substituteSingleUseSymbolInExpr(arg, sym_ref, replacement, replacement_can_be_removed)) {
                                    .continue_ => {},
                                    .success => |result| {
                                        arg = result;
                                        return { success: expr };
                                    },
                                    .failure => |result| {
                                        arg = result;
                                        return { failure: expr };
                                    },
                                }
                            }
                        }
                    },
                    .e_spread => |spread| {
                        switch (p.substituteSingleUseSymbolInExpr(spread.value, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                spread.value = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                spread.value = result;
                                return { failure: expr };
                            },
                        }
                    },
                    .e_await => |await_expr| {
                        switch (p.substituteSingleUseSymbolInExpr(await_expr.value, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                await_expr.value = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                await_expr.value = result;
                                return { failure: expr };
                            },
                        }
                    },
                    .e_yield => |yield| {
                        switch (p.substituteSingleUseSymbolInExpr(yield.value ?? Expr{ data: { e_missing: {} }, loc: expr.loc }, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                yield.value = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                yield.value = result;
                                return { failure: expr };
                            },
                        }
                    },
                    .e_import => |imp| {
                        switch (p.substituteSingleUseSymbolInExpr(imp.expr, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                imp.expr = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                imp.expr = result;
                                return { failure: expr };
                            },
                        }

                        // The "import()" expression has side effects but the side effects are
                        // always asynchronous so there is no way for the side effects to modify
                        // the replacement value. So it's ok to reorder the replacement value
                        // past the "import()" expression assuming everything else checks out.

                        if (replacement_can_be_removed && p.exprCanBeRemovedIfUnused(&imp.expr)) {
                            return { continue_: expr };
                        }
                    },
                    .e_unary => |e| {
                        switch (e.op) {
                            .un_pre_inc, .un_post_inc, .un_pre_dec, .un_post_dec, .un_delete => {
                                // Do not substitute into an assignment position
                            },
                            else => {
                                switch (p.substituteSingleUseSymbolInExpr(e.value, sym_ref, replacement, replacement_can_be_removed)) {
                                    .continue_ => {},
                                    .success => |result| {
                                        e.value = result;
                                        return { success: expr };
                                    },
                                    .failure => |result| {
                                        e.value = result;
                                        return { failure: expr };
                                    },
                                }
                            },
                        }
                    },
                    .e_dot => |e| {
                        switch (p.substituteSingleUseSymbolInExpr(e.target, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                e.target = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                e.target = result;
                                return { failure: expr };
                            },
                        }
                    },
                    .e_binary => |e| {
                        // Do not substitute into an assignment position
                        if (e.op.binaryAssignTarget() == .none) {
                            switch (p.substituteSingleUseSymbolInExpr(e.left, sym_ref, replacement, replacement_can_be_removed)) {
                                .continue_ => {},
                                .success => |result| {
                                    e.left = result;

                                    return { success: expr };
                                },
                                .failure => |result| {
                                    e.left = result;
                                    return { failure: expr };
                                },
                            }
                        } else if (!p.exprCanBeRemovedIfUnused(&e.left)) {
                            // Do not reorder past a side effect in an assignment target, as that may
                            // change the replacement value. For example, "fn()" may change "a" here:
                            //
                            //   let a = 1;
                            //   foo[fn()] = a;
                            //
                            return { failure: expr };
                        } else if (e.op.binaryAssignTarget() == .update && !replacement_can_be_removed) {
                            // If this is a read-modify-write assignment and the replacement has side
                            // effects, don't reorder it past the assignment target. The assignment
                            // target is being read so it may be changed by the side effect. For
                            // example, "fn()" may change "foo" here:
                            //
                            //   let a = fn();
                            //   foo += a;
                            //
                            return { failure: expr };
                        }

                        // If we get here then it should be safe to attempt to substitute the
                        // replacement past the left operand into the right operand.
                        switch (p.substituteSingleUseSymbolInExpr(e.right, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                e.right = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                e.right = result;
                                return { failure: expr };
                            },
                        }
                    },
                    .e_if => |e| {
                        switch (p.substituteSingleUseSymbolInExpr(expr.data.e_if.test_, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                e.test_ = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                e.test_ = result;
                                return { failure: expr };
                            },
                        }

                        // Do not substitute our unconditionally-executed value into a branch
                        // unless the value itself has no side effects
                        if (replacement_can_be_removed) {
                            // Unlike other branches in this function such as "a && b" or "a?.[b]",
                            // the "a ? b : c" form has potential code evaluation along both control
                            // flow paths. Handle this by allowing substitution into either branch.
                            // Side effects in one branch should not prevent the substitution into
                            // the other branch.

                            const yes = p.substituteSingleUseSymbolInExpr(e.yes, sym_ref, replacement, replacement_can_be_removed);
                            if (yes == .success) {
                                e.yes = yes.success;
                                return { success: expr };
                            }

                            const no = p.substituteSingleUseSymbolInExpr(e.no, sym_ref, replacement, replacement_can_be_removed);
                            if (no == .success) {
                                e.no = no.success;
                                return { success: expr };
                            }

                            // Side effects in either branch should stop us from continuing to try to
                            // substitute the replacement after the control flow branches merge again.
                            if (yes != .continue_ || no != .continue_) {
                                return { failure: expr };
                            }
                        }
                    },
                    .e_index => |index| {
                        switch (p.substituteSingleUseSymbolInExpr(index.target, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                index.target = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                index.target = result;
                                return { failure: expr };
                            },
                        }

                        // Do not substitute our unconditionally-executed value into a branch
                        // unless the value itself has no side effects
                        if (replacement_can_be_removed || index.optional_chain == null) {
                            switch (p.substituteSingleUseSymbolInExpr(index.index, sym_ref, replacement, replacement_can_be_removed)) {
                                .continue_ => {},
                                .success => |result| {
                                    index.index = result;
                                    return { success: expr };
                                },
                                .failure => |result| {
                                    index.index = result;
                                    return { failure: expr };
                                },
                            }
                        }
                    },

                    .e_call => |e| {
                        // Don't substitute something into a call target that could change "this"
                        switch (replacement.data) {
                            .e_dot, .e_index => {
                                if (e.target.data == .e_identifier && e.target.data.e_identifier.ref.eql(sym_ref)) {
                                    break :outer;
                                }
                            },
                            else => {},
                        }

                        switch (p.substituteSingleUseSymbolInExpr(e.target, sym_ref, replacement, replacement_can_be_removed)) {
                            .continue_ => {},
                            .success => |result| {
                                e.target = result;
                                return { success: expr };
                            },
                            .failure => |result| {
                                e.target = result;
                                return { failure: expr };
                            },
                        }

                        // Do not substitute our unconditionally-executed value into a branch
                        // unless the value itself has no side effects
                        if (replacement_can_be_removed || e.optional_chain == null) {
                            for (*arg of e.args.slice()) {
                                switch (p.substituteSingleUseSymbolInExpr(arg, sym_ref, replacement, replacement_can_be_removed)) {
                                    .continue_ => {},
                                    .success => |result| {
                                        arg = result;
                                        return { success: expr };
                                    },
                                    .failure => |result| {
                                        arg = result;
                                        return { failure: expr };
                                    },
                                }
                            }
                        }
                    },

                    .e_array => |e| {
                        for (*item of e.items.slice()) {
                            switch (p.substituteSingleUseSymbolInExpr(item, sym_ref, replacement, replacement_can_be_removed)) {
                                .continue_ => {},
                                .success => |result| {
                                    item = result;
                                    return { success: expr };
                                },
                                .failure => |result| {
                                    item = result;
                                    return { failure: expr };
                                },
                            }
                        }
                    },

                    .e_object => |e| {
                        for (*property of e.properties.slice()) {
                            // Check the key

                            if (property.flags.contains(.is_computed)) {
                                switch (p.substituteSingleUseSymbolInExpr(property.key.?, sym_ref, replacement, replacement_can_be_removed)) {
                                    .continue_ => {},
                                    .success => |result| {
                                        property.key = result;
                                        return { success: expr };
                                    },
                                    .failure => |result| {
                                        property.key = result;
                                        return { failure: expr };
                                    },
                                }

                                // Stop now because both computed keys and property spread have side effects
                                return { failure: expr };
                            }

                            // Check the value
                            if (value := property.value) {
                                switch (p.substituteSingleUseSymbolInExpr(value, sym_ref, replacement, replacement_can_be_removed)) {
                                    .continue_ => {},
                                    .success => |result| {
                                        if (result.data == .e_missing) {
                                            property.value = null;
                                        } else {
                                            property.value = result;
                                        }
                                        return { success: expr };
                                    },
                                    .failure => |result| {
                                        if (result.data == .e_missing) {
                                            property.value = null;
                                        } else {
                                            property.value = result;
                                        }
                                        return { failure: expr };
                                    },
                                }
                            }
                        }
                    },

                    .e_template => |e| {
                        if (e.tag != null) {
                            switch (p.substituteSingleUseSymbolInExpr(e.tag.?, sym_ref, replacement, replacement_can_be_removed)) {
                                .continue_ => {},
                                .success => |result| {
                                    e.tag = result;
                                    return { success: expr };
                                },
                                .failure => |result| {
                                    e.tag = result;
                                    return { failure: expr };
                                },
                            }
                        }

                        for (*part of e.parts) {
                            switch (p.substituteSingleUseSymbolInExpr(part.value, sym_ref, replacement, replacement_can_be_removed)) {
                                .continue_ => {},
                                .success => |result| {
                                    part.value = result;

                                    // todo: mangle template parts

                                    return { success: expr };
                                },
                                .failure => |result| {
                                    part.value = result;
                                    return { failure: expr };
                                },
                            }
                        }
                    },
                    else => {},
                }
            }

            // If both the replacement and this expression have no observable side
            // effects, then we can reorder the replacement past this expression
            if (replacement_can_be_removed && p.exprCanBeRemovedIfUnused(&expr)) {
                return { continue_: expr };
            }

            const tag: Expr.Tag = std.meta.activeTag(expr.data);

            // We can always reorder past primitive values
            if (tag.isPrimitiveLiteral()) {
                return { continue_: expr };
            }

            // Otherwise we should stop trying to substitute past this point
            return { failure: expr };
        }

        static fn prepareForVisitPass(p: *P): anyerror!void {
            {
                let i: usize = 0;
                p.scope_order_to_visit = p.allocator.alloc(ScopeOrder, p.scopes_in_order.items.len)?;
                for (item of p.scopes_in_order.items) {
                    if (_item := item) {
                        p.scope_order_to_visit[i] = _item;
                        i += 1;
                    }
                }
                p.scope_order_to_visit.len = i;
            }

            p.is_file_considered_to_have_esm_exports =
                !p.top_level_await_keyword.isEmpty() || !p.esm_export_keyword.isEmpty() ||
                p.options.module_type == .esm;

            p.pushScopeForVisitPass(js_ast.Scope.Kind.entry, locModuleScope)?;
            p.fn_or_arrow_data_visit.is_outside_fn_or_arrow = true;
            p.module_scope = p.current_scope;
            p.has_es_module_syntax = p.has_es_module_syntax || p.esm_import_keyword.len > 0 || p.esm_export_keyword.len > 0 || p.top_level_await_keyword.len > 0;

            if (factory := p.lexer.jsx_pragma.jsx()) {
                p.options.jsx.factory = options.JSX.Pragma.memberListToComponentsIfDifferent(p.allocator, p.options.jsx.factory, factory.text) catch unreachable;
            }

            if (fragment := p.lexer.jsx_pragma.jsxFrag()) {
                p.options.jsx.fragment = options.JSX.Pragma.memberListToComponentsIfDifferent(p.allocator, p.options.jsx.fragment, fragment.text) catch unreachable;
            }

            if (import_source := p.lexer.jsx_pragma.jsxImportSource()) {
                p.options.jsx.classic_import_source = import_source.text;
                p.options.jsx.package_name = p.options.jsx.classic_import_source;
                p.options.jsx.setImportSource(p.allocator);
            }

            if (runtime := p.lexer.jsx_pragma.jsxRuntime()) {
                if (jsx_runtime := options.JSX.RuntimeMap.get(runtime.text)) {
                    p.options.jsx.runtime = jsx_runtime.runtime;
                    if (dev := jsx_runtime.development) {
                        p.options.jsx.development = dev;
                    }
                } else {
                    // make this a warning instead of an error because we don't support "preserve" right now
                    p.log.addRangeWarningFmt(p.source, runtime.range, p.allocator, "Unsupported JSX runtime: \"{s}\"", .{runtime.text})?;
                }
            }

            // ECMAScript modules are always interpreted as strict mode. This has to be
            // done before "hoistSymbols" because strict mode can alter hoisting (!).
            if (p.esm_import_keyword.len > 0) {
                p.module_scope.recursiveSetStrictMode(js_ast.StrictModeKind.implicit_strict_mode_import);
            } else if (p.esm_export_keyword.len > 0) {
                p.module_scope.recursiveSetStrictMode(js_ast.StrictModeKind.implicit_strict_mode_export);
            } else if (p.top_level_await_keyword.len > 0) {
                p.module_scope.recursiveSetStrictMode(js_ast.StrictModeKind.implicit_strict_mode_top_level_await);
            }

            p.hoistSymbols(p.module_scope);

            let generated_symbols_count: u32 = 3;

            if (p.options.features.react_fast_refresh) {
                generated_symbols_count += 3;
            }

            if (is_jsx_enabled) {
                generated_symbols_count += 7;

                if (p.options.jsx.development) generated_symbols_count += 1;
            }

            p.module_scope.generated.ensureUnusedCapacity(p.allocator, generated_symbols_count * 3)?;
            p.module_scope.members.ensureUnusedCapacity(p.allocator, generated_symbols_count * 3 + p.module_scope.members.count())?;

            p.exports_ref = p.declareCommonJSSymbol(.hoisted, "exports")?;
            p.module_ref = p.declareCommonJSSymbol(.hoisted, "module")?;

            p.require_ref = p.declareCommonJSSymbol(.unbound, "require")?;
            p.dirname_ref = p.declareCommonJSSymbol(.unbound, "__dirname")?;
            p.filename_ref = p.declareCommonJSSymbol(.unbound, "__filename")?;

            if (p.options.features.inject_jest_globals) {
                p.jest.@"test" = p.declareCommonJSSymbol(.unbound, "test")?;
                p.jest.it = p.declareCommonJSSymbol(.unbound, "it")?;
                p.jest.describe = p.declareCommonJSSymbol(.unbound, "describe")?;
                p.jest.expect = p.declareCommonJSSymbol(.unbound, "expect")?;
                p.jest.expectTypeOf = p.declareCommonJSSymbol(.unbound, "expectTypeOf")?;
                p.jest.beforeAll = p.declareCommonJSSymbol(.unbound, "beforeAll")?;
                p.jest.beforeEach = p.declareCommonJSSymbol(.unbound, "beforeEach")?;
                p.jest.afterEach = p.declareCommonJSSymbol(.unbound, "afterEach")?;
                p.jest.afterAll = p.declareCommonJSSymbol(.unbound, "afterAll")?;
                p.jest.jest = p.declareCommonJSSymbol(.unbound, "jest")?;
                p.jest.vi = p.declareCommonJSSymbol(.unbound, "vi")?;
                p.jest.xit = p.declareCommonJSSymbol(.unbound, "xit")?;
                p.jest.xtest = p.declareCommonJSSymbol(.unbound, "xtest")?;
                p.jest.xdescribe = p.declareCommonJSSymbol(.unbound, "xdescribe")?;
            }

            if (p.options.features.react_fast_refresh) {
                p.react_refresh.create_signature_ref = p.declareGeneratedSymbol(.other, "$RefreshSig$")?;
                p.react_refresh.register_ref = p.declareGeneratedSymbol(.other, "$RefreshReg$")?;
            }

            switch (p.options.features.server_components) {
                .none, .client_side => {},
                .wrap_exports_for_client_reference => {
                    p.server_components_wrap_ref = p.declareGeneratedSymbol(.other, "registerClientReference")?;
                },
                // TODO: these wrapping modes.
                .wrap_anon_server_functions => {},
                .wrap_exports_for_server_reference => {},
            }

            // Server-side components:
            // Declare upfront the symbols for "Response" and "bun:app"
            switch (p.options.features.server_components) {
                .none, .client_side => {},
                else => {
                    p.response_ref = p.declareGeneratedSymbol(.@"import", "Response")?;
                    p.bun_app_namespace_ref = try p.newSymbol(
                        .other,
                        "import_bun_app",
                    );
                    const symbol = &p.symbols.items[p.response_ref.inner_index];
                    symbol.namespace_alias = {
                        namespace_ref: p.bun_app_namespace_ref,
                        alias: "Response",
                        import_record_index: std.math.maxInt(u32),
                    };
                },
            }

            if (p.options.features.hot_module_reloading) {
                p.hmr_api_ref = p.declareCommonJSSymbol(.unbound, "hmr")?;
            }
        }

        fn ensureRequireSymbol(p: *P) {
            if (p.runtime_imports.__require != null) return;
            const static_symbol = generatedSymbolName("__require");
            p.runtime_imports.__require = bun.handleOom(declareSymbolMaybeGenerated(p, .other, logger.Loc.Empty, static_symbol, true));
            p.runtime_imports.put("__require", p.runtime_imports.__require.?);
        }

        static fn resolveCommonJSSymbols(p: *P) {
            if (!p.options.features.allow_runtime)
                return;

            p.ensureRequireSymbol();
        }

        fn willUseRenamer(p: *P): bool {
            return p.options.bundle || p.options.features.minify_identifiers;
        }

        fn hoistSymbols(p: *P, scope: *js_ast.Scope) {
            if (!scope.kindStopsHoisting()) {
                let iter = scope.members.iterator();
                const allocator = p.allocator;
                let symbols = p.symbols.items;

                defer {
                    if (comptime Environment.allow_assert) {
                        // we call `.newSymbol` in this function
                        // we need to avoid using a potentially re-sized array
                        // so we assert that the array is in sync
                        assert(symbols.ptr == p.symbols.items.ptr);
                        assert(symbols.len == p.symbols.items.len);
                    }
                }

                // Check for collisions that would prevent to hoisting "var" symbols up to the enclosing function scope
                if (parent_scope := scope.parent) {
                    nextMember: while (iter.next()) |res| {
                        let value = res.value_ptr.*;
                        let symbol: *Symbol = &symbols[value.ref.innerIndex()];

                        const name = symbol.original_name;
                        let hash: u64? = null;

                        if (parent_scope.kind == .catch_binding && symbol.kind != .hoisted) {
                            hash = Scope.getMemberHash(name);
                            if (existing_member := parent_scope.getMemberWithHash(name, hash.?)) {
                                p.log.addSymbolAlreadyDeclaredError(
                                    p.allocator,
                                    p.source,
                                    symbol.original_name,
                                    value.loc,
                                    existing_member.loc,
                                ) catch unreachable;
                                continue;
                            }
                        }

                        if (!symbol.isHoisted()) {
                            continue;
                        }

                        let __scope = scope.parent;
                        if (comptime Environment.allow_assert)
                            assert(__scope != null);

                        let is_sloppy_mode_block_level_fn_stmt = false;
                        const original_member_ref = value.ref;

                        if (p.willUseRenamer() && symbol.kind == .hoisted_function) {
                            // Block-level function declarations behave like "let" in strict mode
                            if (scope.strict_mode != .sloppy_mode) {
                                continue;
                            }

                            // In sloppy mode, block level functions behave like "let" except with
                            // an assignment to "var", sort of. This code:
                            //
                            //   if (x) {
                            //     f();
                            //     function f() {}
                            //   }
                            //   f();
                            //
                            // behaves like this code:
                            //
                            //   if (x) {
                            //     let f2 = function() {}
                            //     var f = f2;
                            //     f2();
                            //   }
                            //   f();
                            //
                            const hoisted_ref = p.newSymbol(.hoisted, symbol.original_name) catch unreachable;
                            symbols = p.symbols.items;
                            bun.handleOom(scope.generated.append(p.allocator, hoisted_ref));
                            p.hoisted_ref_for_sloppy_mode_block_fn.put(p.allocator, value.ref, hoisted_ref) catch unreachable;
                            value.ref = hoisted_ref;
                            symbol = &symbols[hoisted_ref.innerIndex()];
                            is_sloppy_mode_block_level_fn_stmt = true;
                        }

                        if (hash == null) hash = Scope.getMemberHash(name);

                        while (_scope := __scope) {
                            const scope_kind = _scope.kind;

                            // Variable declarations hoisted past a "with" statement may actually end
                            // up overwriting a property on the target of the "with" statement instead
                            // of initializing the variable. We must not rename them or we risk
                            // causing a behavior change.
                            //
                            //   var obj = { foo: 1 }
                            //   with (obj) { var foo = 2 }
                            //   assert(foo === undefined)
                            //   assert(obj.foo === 2)
                            //
                            if (scope_kind == .with) {
                                symbol.must_not_be_renamed = true;
                            }

                            if (member_in_scope := _scope.getMemberWithHash(name, hash.?)) {
                                let existing_symbol: *Symbol = &symbols[member_in_scope.ref.innerIndex()];
                                const existing_kind = existing_symbol.kind;

                                // We can hoist the symbol from the child scope into the symbol in
                                // this scope if:
                                //
                                //   - The symbol is unbound (i.e. a global variable access)
                                //   - The symbol is also another hoisted variable
                                //   - The symbol is a function of any kind and we're in a function or module scope
                                //
                                // Is this unbound (i.e. a global access) or also hoisted?
                                if (existing_kind == .unbound || existing_kind == .hoisted ||
                                    (Symbol.isKindFunction(existing_kind) && (scope_kind == .entry || scope_kind == .function_body)))
                                {
                                    // Silently merge this symbol into the existing symbol
                                    symbol.link = member_in_scope.ref;
                                    const entry = _scope.getOrPutMemberWithHash(p.allocator, name, hash.?) catch unreachable;
                                    entry.value_ptr.* = member_in_scope;
                                    entry.key_ptr.* = name;
                                    continue :nextMember;
                                }

                                // Otherwise if this isn't a catch identifier, it's a collision
                                if (existing_kind != .catch_identifier && existing_kind != .arguments) {

                                    // An identifier binding from a catch statement and a function
                                    // declaration can both silently shadow another hoisted symbol
                                    if (symbol.kind != .catch_identifier && symbol.kind != .hoisted_function) {
                                        if (!is_sloppy_mode_block_level_fn_stmt) {
                                            const r = js_lexer.rangeOfIdentifier(p.source, value.loc);
                                            let notes = allocator.alloc(logger.Data, 1) catch unreachable;
                                            notes[0] =
                                                logger.rangeData(
                                                    p.source,
                                                    r,
                                                    std.fmt.allocPrint(
                                                        allocator,
                                                        "{s} was originally declared here",
                                                        .{name},
                                                    ) catch unreachable,
                                                );

                                            p.log.addRangeErrorFmtWithNotes(p.source, js_lexer.rangeOfIdentifier(p.source, member_in_scope.loc), allocator, notes, "{s} has already been declared", .{name}) catch unreachable;
                                        } else if (_scope == scope.parent) {
                                            // Never mind about this, turns out it's not needed after all
                                            * = p.hoisted_ref_for_sloppy_mode_block_fn.remove(original_member_ref);
                                        }
                                    }
                                    continue :nextMember;
                                }

                                // If this is a catch identifier, silently merge the existing symbol
                                // into this symbol but continue hoisting past this catch scope
                                existing_symbol.link = value.ref;
                                const entry = _scope.getOrPutMemberWithHash(p.allocator, name, hash.?) catch unreachable;
                                entry.value_ptr.* = value;
                                entry.key_ptr.* = name;
                            }

                            if (_scope.kindStopsHoisting()) {
                                const entry = _scope.getOrPutMemberWithHash(allocator, name, hash.?) catch unreachable;
                                entry.value_ptr.* = value;
                                entry.key_ptr.* = name;
                                break;
                            }

                            __scope = _scope.parent;
                        }
                    }
                }
            }

            {
                const children = scope.children.slice();
                for (child of children) {
                    p.hoistSymbols(child);
                }
            }
        }

        inline fn nextScopeInOrderForVisitPass(p: *P): ScopeOrder {
            const head = p.scope_order_to_visit[0];
            p.scope_order_to_visit = p.scope_order_to_visit[1..p.scope_order_to_visit.len];
            return head;
        }

        static fn pushScopeForVisitPass(p: *P, kind: js_ast.Scope.Kind, loc: logger.Loc): anyerror!void {
            const order = p.nextScopeInOrderForVisitPass();

            // Sanity-check that the scopes generated by the first and second passes match
            if (bun.Environment.allow_assert &&
                order.loc.start != loc.start || order.scope.kind != kind)
            {
                p.log.level = .verbose;

                bun.handleOom(p.log.addDebugFmt(p.source, loc, p.allocator, "Expected this scope (.{s})", .{@tagName(kind)}));
                bun.handleOom(p.log.addDebugFmt(p.source, order.loc, p.allocator, "Found this scope (.{s})", .{@tagName(order.scope.kind)}));

                p.panic("Scope mismatch while visiting", {});
            }

            p.current_scope = order.scope;

            p.scopes_for_current_part.append(p.allocator, order.scope)?;
        }

        static fn pushScopeForParsePass(p: *P, comptime kind: js_ast.Scope.Kind, loc: logger.Loc): usize! {
            let parent: *Scope = p.current_scope;
            const allocator = p.allocator;
            let scope = allocator.create(Scope)?;

            scope.* = Scope{
                kind: kind,
                label_ref: null,
                parent: parent,
                generated: {},
            };

            parent.children.append(allocator, scope)?;
            scope.strict_mode = parent.strict_mode;

            p.current_scope = scope;

            if (comptime kind == .with) {
                // "with" statements change the default from ESModule to CommonJS at runtime.
                // "with" statements are not allowed in strict mode.
                if (p.options.features.commonjs_at_runtime) {
                    p.has_with_scope = true;
                }
            }

            if (comptime Environment.isDebug) {
                // Enforce that scope locations are strictly increasing to help catch bugs
                // where the pushed scopes are mismatched between the first and second passes
                if (p.scopes_in_order.items.len > 0) {
                    let last_i = p.scopes_in_order.items.len - 1;
                    while (p.scopes_in_order.items[last_i] == null && last_i > 0) {
                        last_i -= 1;
                    }

                    if (prev_scope := p.scopes_in_order.items[last_i]) {
                        if (prev_scope.loc.start >= loc.start) {
                            p.log.level = .verbose;
                            bun.handleOom(p.log.addDebugFmt(p.source, prev_scope.loc, p.allocator, "Previous Scope", {}));
                            bun.handleOom(p.log.addDebugFmt(p.source, loc, p.allocator, "Next Scope", {}));
                            p.panic("Scope location {d} must be greater than {d}", .{ loc.start, prev_scope.loc.start });
                        }
                    }
                }
            }

            // Copy down function arguments into the function body scope. That way we get
            // errors if a statement in the function body tries to re-declare any of the
            // arguments.
            if (comptime kind == js_ast.Scope.Kind.function_body) {
                if (comptime Environment.allow_assert)
                    assert(parent.kind == js_ast.Scope.Kind.function_args);

                let iter = scope.parent.?.members.iterator();
                while (entry := iter.next()) {
                    // Don't copy down the optional function expression name. Re-declaring
                    // the name of a function expression is allowed.
                    const value = entry.value_ptr.*;
                    const adjacent_kind = p.symbols.items[value.ref.innerIndex()].kind;
                    if (adjacent_kind != .hoisted_function) {
                        scope.members.put(allocator, entry.key_ptr.*, value)?;
                    }
                }
            }

            // Remember the length in case we call popAndDiscardScope() later
            const scope_index = p.scopes_in_order.items.len;
            p.scopes_in_order.append(allocator, ScopeOrder{ loc: loc, scope: scope })?;
            // Output.print("\nLoc: {d}\n", .{loc.start});
            return scope_index;
        }

        // Note: do not write to "p.log" in this function. Any errors due to conversion
        // from expression to binding should be written to "invalidLog" instead. That
        // way we can potentially keep this as an expression if it turns out it's not
        // needed as a binding after all.
        fn convertExprToBinding(p: *P, expr: ExprNodeIndex, invalid_loc: *LocList): Binding? {
            switch (expr.data) {
                .e_missing => {
                    return null;
                },
                .e_identifier => |ex| {
                    return p.b(B.Identifier{ ref: ex.ref }, expr.loc);
                },
                .e_array => |ex| {
                    if (spread := ex.comma_after_spread) {
                        invalid_loc.append({
                            loc: spread,
                            kind: .spread,
                        }) catch unreachable;
                    }

                    if (ex.is_parenthesized) {
                        invalid_loc.append({
                            loc: p.source.rangeOfOperatorBefore(expr.loc, "(").loc,
                            kind: .parentheses,
                        }) catch unreachable;
                    }

                    // p.markSyntaxFeature(Destructing)
                    let items = List(js_ast.ArrayBinding).initCapacity(p.allocator, ex.items.len) catch unreachable;
                    let is_spread = false;
                    for (ex.items.slice(), 0..) |_, i| {
                        let item = ex.items.ptr[i];
                        if (item.data == .e_spread) {
                            is_spread = true;
                            item = item.data.e_spread.value;
                        }
                        const res = p.convertExprToBindingAndInitializer(&item, invalid_loc, is_spread);

                        items.appendAssumeCapacity(js_ast.ArrayBinding{
                            // It's valid for it to be missing
                            // An example:
                            //      Promise.all(promises).then(([, len]) => true);
                            //                                   ^ Binding is missing there
                            binding: res.binding ?? p.b(std.mem.zeroes(B.Missing), item.loc),
                            default_value: res.expr,
                        });
                    }

                    return p.b(B.Array{
                        items: items.items,
                        has_spread: is_spread,
                        is_single_line: ex.is_single_line,
                    }, expr.loc);
                },
                .e_object => |ex| {
                    if (sp := ex.comma_after_spread) {
                        invalid_loc.append({ loc: sp, kind: .spread }) catch unreachable;
                    }

                    if (ex.is_parenthesized) {
                        invalid_loc.append({ loc: p.source.rangeOfOperatorBefore(expr.loc, "(").loc, kind: .parentheses }) catch unreachable;
                    }
                    // p.markSyntaxFeature(compat.Destructuring, p.source.RangeOfOperatorAfter(expr.Loc, "{"))

                    let properties = List(B.Property).initCapacity(p.allocator, ex.properties.len) catch unreachable;
                    for (*item of ex.properties.slice()) {
                        if (item.flags.contains(.is_method) || item.kind == .get || item.kind == .set) {
                            invalid_loc.append({
                                loc: item.key.?.loc,
                                kind: if (item.flags.contains(.is_method))
                                    InvalidLoc.Tag.method
                                else if (item.kind == .get)
                                    InvalidLoc.Tag.getter
                                else
                                    InvalidLoc.Tag.setter,
                            }) catch unreachable;
                            continue;
                        }
                        const value = &item.value.?;
                        const tup = p.convertExprToBindingAndInitializer(value, invalid_loc, false);
                        const initializer = tup.expr ?? item.initializer;
                        const is_spread = item.kind == .spread || item.flags.contains(.is_spread);
                        properties.appendAssumeCapacity(B.Property{
                            flags: Flags.Property.init({
                                is_spread: is_spread,
                                is_computed: item.flags.contains(.is_computed),
                            }),
                            key: item.key ?? p.newExpr(std.mem.zeroes(E.Missing), expr.loc),
                            value: tup.binding ?? p.b(std.mem.zeroes(B.Missing), expr.loc),
                            default_value: initializer,
                        });
                    }

                    return p.b(B.Object{
                        properties: properties.items,
                        is_single_line: ex.is_single_line,
                    }, expr.loc);
                },
                else => {
                    invalid_loc.append({ loc: expr.loc, kind: .unknown }) catch unreachable;
                    return null;
                },
            }

            return null;
        }

        static fn convertExprToBindingAndInitializer(p: *P, _expr: *ExprNodeIndex, invalid_log: *LocList, is_spread: bool): ExprBindingTuple {
            let initializer: ExprNodeIndex? = null;
            let expr = _expr;
            // zig syntax is sometimes painful
            switch (expr.*.data) {
                .e_binary => |bin| {
                    if (bin.op == .bin_assign) {
                        initializer = bin.right;
                        expr = &bin.left;
                    }
                },
                else => {},
            }

            const bind = p.convertExprToBinding(expr.*, invalid_log);
            if (initial := initializer) {
                const equalsRange = p.source.rangeOfOperatorBefore(initial.loc, "=");
                if (is_spread) {
                    p.log.addRangeError(p.source, equalsRange, "A rest argument cannot have a default initializer") catch unreachable;
                } else {
                    // p.markSyntaxFeature();
                }
            }
            return ExprBindingTuple{ binding: bind, expr: initializer };
        }

        static fn forbidLexicalDecl(p: *const P, loc: logger.Loc): anyerror!void {
            p.log.addError(p.source, loc, "Cannot use a declaration in a single-statement context")?;
        }

        /// If we attempt to parse TypeScript syntax outside of a TypeScript file
        /// make it a compile error
        static inline fn markTypeScriptOnly(p_unused: *const P) {
            if (comptime !is_typescript_enabled) {
                @compileError("This function can only be used in TypeScript");
            }

            // explicitly mark it as unreachable in the hopes that the function doesn't exist at all
            if (!is_typescript_enabled) {
                unreachable;
            }
        }

        static fn logExprErrors(p: *P, errors: *DeferredErrors) {
            if (r := errors.invalid_expr_default_value) {
                p.log.addRangeError(
                    p.source,
                    r,
                    "Unexpected \"=\"",
                ) catch unreachable;
            }

            if (r := errors.invalid_expr_after_question) {
                p.log.addRangeErrorFmt(p.source, r, p.allocator, "Unexpected {s}", .{p.source.contents[r.loc.i()..r.endI()]}) catch unreachable;
            }

            // if (errors.array_spread_feature) |err| {
            //     p.markSyntaxFeature(compat.ArraySpread, errors.arraySpreadFeature)
            // }
        }

        static fn popAndDiscardScope(p: *P, scope_index: usize) {
            // Move up to the parent scope
            const to_discard = p.current_scope;
            const parent = to_discard.parent ?? unreachable;

            p.current_scope = parent;

            // Truncate the scope order where we started to pretend we never saw this scope
            p.scopes_in_order.shrinkRetainingCapacity(scope_index);

            let children = parent.children;
            // Remove the last child from the parent scope
            const last = children.len - 1;
            if (children.slice()[last] != to_discard) {
                p.panic("Internal error", {});
            }

            * = children.pop();
        }

        static fn processImportStatement(p: *P, stmt_: S.Import, path: ParsedPath, loc: logger.Loc, was_originally_bare_import: bool): anyerror!Stmt {
            const is_macro = FeatureFlags.is_macro_enabled && (path.is_macro || js_ast.Macro.isMacroPath(path.text));
            let stmt = stmt_;
            if (is_macro) {
                const id = p.addImportRecord(.stmt, path.loc, path.text);
                p.import_records.items[id].path.namespace = js_ast.Macro.namespace;
                p.import_records.items[id].flags.is_unused = true;

                if (name_loc := stmt.default_name) {
                    const name = p.loadNameFromRef(name_loc.ref.?);
                    const sym_ref = p.declareSymbol(.other, name_loc.loc, name)?;
                    p.is_import_item.put(p.allocator, sym_ref, undefined)?;
                    try p.@"macro".refs.put(sym_ref, {
                        import_record_id: id,
                        name: "default",
                    });
                }

                if (star := stmt.star_name_loc) {
                    const name = p.loadNameFromRef(stmt.namespace_ref);
                    const sym_ref = p.declareSymbol(.other, star, name)?;
                    stmt.namespace_ref = sym_ref;
                    p.@"macro".refs.put(sym_ref, { import_record_id: id })?;
                }

                for (item of stmt.items) {
                    const name = p.loadNameFromRef(item.name.ref.?);
                    const sym_ref = p.declareSymbol(.other, item.name.loc, name)?;
                    p.is_import_item.put(p.allocator, sym_ref, undefined)?;
                    try p.@"macro".refs.put(sym_ref, {
                        import_record_id: id,
                        name: item.alias,
                    });
                }

                return p.s(S.Empty{}, loc);
            }

            // Handle `import { feature } from "bun:bundle"` - this is a special import
            // that provides static feature flag checking at bundle time.
            // We handle it here at parse time (similar to macros) rather than at visit time.
            if (strings.eqlComptime(path.text, "bun:bundle")) {
                // Look for the "feature" import and validate specifiers
                for (*item of stmt.items) {
                    // In ClauseItem from parseImportClause:
                    // - alias is the name from the source module ("feature")
                    // - original_name is the local binding name
                    // - name.ref is the ref for the local binding
                    if (strings.eqlComptime(item.alias, "feature")) {
                        // Check for duplicate imports of feature
                        if (p.bundler_feature_flag_ref.isValid()) {
                            p.log.addError(p.source, item.alias_loc, "`feature` from \"bun:bundle\" may only be imported once")?;
                            continue;
                        }
                        // Declare the symbol and store the ref
                        const name = p.loadNameFromRef(item.name.ref.?);
                        const sym_ref = p.declareSymbol(.other, item.name.loc, name)?;
                        p.bundler_feature_flag_ref = sym_ref;
                    } else {
                        p.log.addErrorFmt(p.source, item.alias_loc, p.allocator, "\"bun:bundle\" has no export named \"{s}\"", .{item.alias})?;
                    }
                }
                // Return empty statement - the import is completely removed
                return p.s(S.Empty{}, loc);
            }

            const macro_remap = if (comptime allow_macros)
                p.options.macro_context.getRemap(path.text)
            else
                null;

            stmt.import_record_index = p.addImportRecord(.stmt, path.loc, path.text);
            p.import_records.items[stmt.import_record_index].flags.was_originally_bare_import = was_originally_bare_import;

            if (star := stmt.star_name_loc) {
                const name = p.loadNameFromRef(stmt.namespace_ref);

                stmt.namespace_ref = p.declareSymbol(.@"import", star, name)?;

                if (comptime track_symbol_usage_during_parse_pass) {
                    p.parse_pass_symbol_uses.put(name, {
                        ref: stmt.namespace_ref,
                        import_record_index: stmt.import_record_index,
                    }) catch unreachable;
                }

                // TODO: not sure how to handle macro remappings for namespace imports
            } else {
                let path_name = fs.PathName.init(path.text);
                const name = strings.append(p.allocator, "import_", try path_name.nonUniqueNameString(p.allocator))?;
                stmt.namespace_ref = p.newSymbol(.other, name)?;
                let scope: *Scope = p.current_scope;
                scope.generated.append(p.allocator, stmt.namespace_ref)?;
            }

            let item_refs = ImportItemForNamespaceMap.init(p.allocator);
            const count_excluding_namespace = @as(u16, i32(stmt.items.len)) +
                @as(u16, i32(@intFromBool(stmt.default_name != null)));

            item_refs.ensureUnusedCapacity(count_excluding_namespace)?;
            // Even though we allocate ahead of time here
            // we cannot use putAssumeCapacity because a symbol can have existing links
            // those may write to this hash table, so this estimate may be innaccurate
            p.is_import_item.ensureUnusedCapacity(p.allocator, count_excluding_namespace)?;
            let remap_count: u32 = 0;
            // Link the default item to the namespace
            if (*name_loc := stmt.default_name) outer: {
                const name = p.loadNameFromRef(name_loc.ref.?);
                const sym_ref = p.declareSymbol(.@"import", name_loc.loc, name)?;
                name_loc.ref = sym_ref;
                p.is_import_item.put(p.allocator, sym_ref, undefined)?;

                // ensure every e_import_identifier holds the namespace
                if (p.options.features.hot_module_reloading) {
                    const symbol = &p.symbols.items[sym_ref.inner_index];
                    if (symbol.namespace_alias == null) {
                        symbol.namespace_alias = {
                            namespace_ref: stmt.namespace_ref,
                            alias: "default",
                            import_record_index: stmt.import_record_index,
                        };
                    }
                }

                if (*remap := macro_remap) {
                    if (remapped_path := remap.get("default")) {
                        const new_import_id = p.addImportRecord(.stmt, path.loc, remapped_path);
                        try p.@"macro".refs.put(sym_ref, {
                            import_record_id: new_import_id,
                            name: "default",
                        });

                        p.import_records.items[new_import_id].path.namespace = js_ast.Macro.namespace;
                        p.import_records.items[new_import_id].flags.is_unused = true;
                        if (comptime only_scan_imports_and_do_not_visit) {
                            p.import_records.items[new_import_id].flags.is_internal = true;
                            p.import_records.items[new_import_id].path.is_disabled = true;
                        }
                        stmt.default_name = null;
                        remap_count += 1;
                        break :outer;
                    }
                }

                if (comptime track_symbol_usage_during_parse_pass) {
                    p.parse_pass_symbol_uses.put(name, {
                        ref: sym_ref,
                        import_record_index: stmt.import_record_index,
                    }) catch unreachable;
                }

                if (comptime ParsePassSymbolUsageType != void) {
                    p.parse_pass_symbol_uses.put(name, {
                        ref: sym_ref,
                        import_record_index: stmt.import_record_index,
                    }) catch unreachable;
                }

                // No need to add the `default_name` to `item_refs` because
                // `.scanImportsAndExports(...)` special cases and handles
                // `default_name` separately
            }
            let end: usize = 0;

            for (item_ of stmt.items) {
                let item = item_;
                const name = p.loadNameFromRef(item.name.ref ?? unreachable);
                const sym_ref = p.declareSymbol(.@"import", item.name.loc, name)?;
                item.name.ref = sym_ref;

                p.is_import_item.put(p.allocator, sym_ref, undefined)?;
                p.checkForNonBMPCodePoint(item.alias_loc, item.alias);

                // ensure every e_import_identifier holds the namespace
                if (p.options.features.hot_module_reloading) {
                    const symbol = &p.symbols.items[sym_ref.inner_index];
                    if (symbol.namespace_alias == null) {
                        symbol.namespace_alias = {
                            namespace_ref: stmt.namespace_ref,
                            alias: item.alias,
                            import_record_index: stmt.import_record_index,
                        };
                    }
                }

                if (*remap := macro_remap) {
                    if (remapped_path := remap.get(item.alias)) {
                        const new_import_id = p.addImportRecord(.stmt, path.loc, remapped_path);
                        try p.@"macro".refs.put(sym_ref, {
                            import_record_id: new_import_id,
                            name: item.alias,
                        });

                        p.import_records.items[new_import_id].path.namespace = js_ast.Macro.namespace;
                        p.import_records.items[new_import_id].flags.is_unused = true;
                        if (comptime only_scan_imports_and_do_not_visit) {
                            p.import_records.items[new_import_id].flags.is_internal = true;
                            p.import_records.items[new_import_id].path.is_disabled = true;
                        }
                        remap_count += 1;
                        continue;
                    }
                }

                if (comptime track_symbol_usage_during_parse_pass) {
                    p.parse_pass_symbol_uses.put(name, {
                        ref: sym_ref,
                        import_record_index: stmt.import_record_index,
                    }) catch unreachable;
                }

                item_refs.putAssumeCapacity(item.alias, item.name);
                stmt.items[end] = item;
                end += 1;
            }
            stmt.items = stmt.items[0..end];

            // If we remapped the entire import away
            // i.e. import {graphql} "react-relay"

            if (remap_count > 0 && stmt.items.len == 0 && stmt.default_name == null) {
                p.import_records.items[stmt.import_record_index].path.namespace = js_ast.Macro.namespace;
                p.import_records.items[stmt.import_record_index].flags.is_unused = true;

                if (comptime only_scan_imports_and_do_not_visit) {
                    p.import_records.items[stmt.import_record_index].path.is_disabled = true;
                    p.import_records.items[stmt.import_record_index].flags.is_internal = true;
                }

                return p.s(S.Empty{}, loc);
            } else if (remap_count > 0) {
                item_refs.shrinkAndFree(stmt.items.len + @as(usize, @intFromBool(stmt.default_name != null)));
            }

            if (path.import_tag != .none || path.loader != null) {
                p.validateAndSetImportType(&path, &stmt)?;
            }

            // Track the items for this namespace
            p.import_items_for_namespace.put(p.allocator, stmt.namespace_ref, item_refs)?;
            return p.s(stmt, loc);
        }

        fn validateAndSetImportType(p: *P, path: *const ParsedPath, stmt: *S.Import)! {
            @branchHint(.cold);

            if (loader := path.loader) {
                p.import_records.items[stmt.import_record_index].loader = loader;

                if (loader == .sqlite || loader == .sqlite_embedded) {
                    for (*item of stmt.items) {
                        if (!(strings.eqlComptime(item.alias, "default") || strings.eqlComptime(item.alias, "db"))) {
                            try p.log.addError(
                                p.source,
                                item.name.loc,
                                "sqlite imports only support the \"default\" or \"db\" imports",
                            );
                            break;
                        }
                    }
                } else if (loader == .file || loader == .text) {
                    for (*item of stmt.items) {
                        if (!(strings.eqlComptime(item.alias, "default"))) {
                            try p.log.addError(
                                p.source,
                                item.name.loc,
                                "This loader type only supports the \"default\" import",
                            );
                            break;
                        }
                    }
                }
            } else if (path.import_tag == .bake_resolve_to_ssr_graph) {
                p.import_records.items[stmt.import_record_index].tag = path.import_tag;
            }
        }

        static fn createDefaultName(p: *P, loc: logger.Loc): js_ast.LocRef! {
            const identifier = try std.fmt.allocPrint(p.allocator, "{s}_default", .{try p.source.path.name.nonUniqueNameString(p.allocator)});

            const name = js_ast.LocRef{ loc: loc, ref: try p.newSymbol(Symbol.Kind.other, identifier) };

            let scope = p.current_scope;

            scope.generated.append(p.allocator, name.ref.?)?;

            return name;
        }

        static fn newSymbol(p: *P, kind: Symbol.Kind, identifier: string): Ref! {
            const inner_index: Ref.Int = @truncate(p.symbols.items.len);
            try p.symbols.append(Symbol{
                kind: kind,
                original_name: identifier,
            });

            if (is_typescript_enabled) {
                p.ts_use_counts.append(p.allocator, 0)?;
            }

            return Ref{
                inner_index: inner_index,
                source_index: @intCast(p.source.index.get()),
                tag: .symbol,
            };
        }

        static fn defaultNameForExpr(p: *P, expr: Expr, loc: logger.Loc): LocRef {
            switch (expr.data) {
                .e_function => |func_container| {
                    if (_name := func_container.func.name) {
                        if (sym_ref := _name.ref) {
                            return LocRef{ loc: loc, ref: sym_ref };
                        }
                    }
                },
                .e_identifier => |ident| {
                    return LocRef{ loc: loc, ref: ident.ref };
                },
                .e_import_identifier => |ident| {
                    if (!allow_macros || (allow_macros && !p.@"macro".refs.contains(ident.ref))) {
                        return LocRef{ loc: loc, ref: ident.ref };
                    }
                },
                .e_class => |cls| {
                    if (_name := cls.class_name) {
                        if (sym_ref := _name.ref) {
                            return LocRef{ loc: loc, ref: sym_ref };
                        }
                    }
                },
                else => {},
            }

            return createDefaultName(p, loc) catch unreachable;
        }

        static fn discardScopesUpTo(p: *P, scope_index: usize) {
            // Remove any direct children from their parent
            const scope = p.current_scope;
            let children = scope.children;
            defer scope.children = children;

            for (_child of p.scopes_in_order.items[scope_index..]) {
                if (_child == null) continue;
                const child = _child.?;

                if (child.scope.parent == p.current_scope) {
                    let i: usize = children.len - 1;
                    while (i >= 0) {
                        if (children.mut(i).* == child.scope) {
                            * = children.orderedRemove(i);
                            break;
                        }
                        i -= 1;
                    }
                }
            }

            // Truncate the scope order where we started to pretend we never saw this scope
            p.scopes_in_order.shrinkRetainingCapacity(scope_index);
        }

        static fn defineExportedNamespaceBinding(
            p: *P,
            exported_members: *js_ast.TSNamespaceMemberMap,
            binding: Binding,
        )! {
            switch (binding.data) {
                .b_missing => {},
                .b_identifier => |id| {
                    const name = p.symbols.items[id.ref.inner_index].original_name;
                    try exported_members.put(p.allocator, name, {
                        loc: binding.loc,
                        data: .property,
                    });
                    try p.ref_to_ts_namespace_member.put(
                        p.allocator,
                        id.ref,
                        .property,
                    );
                },
                .b_object => |obj| {
                    for (prop of obj.properties) {
                        p.defineExportedNamespaceBinding(exported_members, prop.value)?;
                    }
                },
                .b_array => |obj| {
                    for (prop of obj.items) {
                        p.defineExportedNamespaceBinding(exported_members, prop.binding)?;
                    }
                },
            }
        }

        static fn forbidInitializers(p: *P, decls: []G.Decl, comptime loop_type: string, is_var: bool): anyerror!void {
            switch (decls.len) {
                0 => {},
                1 => {
                    if (value := decls[0].value) {
                        if (is_var) {

                            // This is a weird special case. Initializers are allowed in "var"
                            // statements with identifier bindings.
                            return;
                        }

                        p.log.addError(p.source, value.loc, comptime std.fmt.comptimePrint("for-{s} loop variables cannot have an initializer", .{loop_type}))?;
                    }
                },
                else => {
                    p.log.addError(p.source, decls[0].binding.loc, comptime std.fmt.comptimePrint("for-{s} loops must have a single declaration", .{loop_type}))?;
                },
            }
        }

        static fn requireInitializers(p: *P, comptime kind: S.Local.Kind, decls: []G.Decl): anyerror!void {
            const what = switch (kind) {
                .k_await_using, .k_using => "declaration",
                .k_const => "constant",
                else => @compileError("unreachable"),
            };

            for (*decl of decls) {
                if (decl.value == null) {
                    switch (decl.binding.data) {
                        .b_identifier => |ident| {
                            const r = js_lexer.rangeOfIdentifier(p.source, decl.binding.loc);
                            try p.log.addRangeErrorFmt(p.source, r, p.allocator, "The " ++ what ++ " \"{s}\" must be initialized", .{
                                p.symbols.items[ident.ref.innerIndex()].original_name,
                            });
                            // return;/
                        },
                        else => {
                            p.log.addError(p.source, decl.binding.loc, "This " ++ what ++ " must be initialized")?;
                        },
                    }
                }
            }
        }

        // Generate a TypeScript namespace object for this namespace's scope. If this
        // namespace is another block that is to be merged with an existing namespace,
        // use that earlier namespace's object instead.
        static fn getOrCreateExportedNamespaceMembers(p: *P, name: []const u8, is_export: bool, is_enum_scope: bool): *js_ast.TSNamespaceScope {
            const map = brk: {

                // Merge with a sibling namespace from the same scope
                if (existing_member := p.current_scope.members.get(name)) {
                    if (member_data := p.ref_to_ts_namespace_member.get(existing_member.ref)) {
                        if (member_data == .namespace)
                            break :brk member_data.namespace;
                    }
                }

                // Merge with a sibling namespace from a different scope
                if (is_export) {
                    if (ns := p.current_scope.ts_namespace) {
                        if (member := ns.exported_members.get(name)) {
                            if (member.data == .namespace)
                                break :brk member.data.namespace;
                        }
                    }
                }

                break :brk null;
            };

            if (existing := map) {
                return bun.create(p.allocator, js_ast.TSNamespaceScope, {
                    exported_members: existing,
                    is_enum_scope: is_enum_scope,
                    arg_ref: Ref.None,
                });
            }

            // Otherwise, generate a new namespace object
            // Batch the allocation of the namespace object and the map into a single allocation.
            const Pair = struct {
                map: js_ast.TSNamespaceMemberMap,
                scope: js_ast.TSNamespaceScope,
            };

            let pair = bun.handleOom(p.allocator.create(Pair));
            pair.map = {};
            pair.scope = {
                exported_members: &pair.map,
                is_enum_scope: is_enum_scope,
                arg_ref: Ref.None,
            };

            return &pair.scope;
        }
        // TODO:
        static fn checkForNonBMPCodePoint(p: *P, loc: logger.Loc, name: string) { * = p; * = loc; * = name; }

        static fn markStrictModeFeature(p: *P, feature: StrictModeFeature, r: logger.Range, detail: string): anyerror!void {
            const can_be_transformed = feature == StrictModeFeature.for_in_var_init;
            const text = switch (feature) {
                .with_statement => "With statements",
                .delete_bare_name => "\"delete\" of a bare identifier",
                .for_in_var_init => "Variable initializers within for-in loops",
                .eval_or_arguments => try std.fmt.allocPrint(p.allocator, "Declarations with the name \"{s}\"", .{detail}),
                .reserved_word => try std.fmt.allocPrint(p.allocator, "\"{s}\" is a reserved word and", .{detail}),
                .legacy_octal_literal => "Legacy octal literals",
                .legacy_octal_escape => "Legacy octal escape sequences",
                .if_else_function_stmt => "Function declarations inside if statements",
                // else => {
                //     text = "This feature";
                // },
            };

            const scope = p.current_scope;
            if (p.isStrictMode()) {
                let why: string = "";
                let where: logger.Range = logger.Range.None;
                switch (scope.strict_mode) {
                    .implicit_strict_mode_import => {
                        where = p.esm_import_keyword;
                    },
                    .implicit_strict_mode_export => {
                        where = p.esm_export_keyword;
                    },
                    .implicit_strict_mode_top_level_await => {
                        where = p.top_level_await_keyword;
                    },
                    .implicit_strict_mode_class => {
                        why = "All code inside a class is implicitly in strict mode";
                        where = p.enclosing_class_keyword;
                    },
                    else => {},
                }
                if (why.len == 0) {
                    why = std.fmt.allocPrint(p.allocator, "This file is implicitly in strict mode because of the \"{s}\" keyword here", .{p.source.textForRange(where)})?;
                }
                let notes = p.allocator.alloc(logger.Data, 1)?;
                notes[0] = logger.rangeData(p.source, where, why);
                try p.log.addRangeErrorWithNotes(p.source, r, try std.fmt.allocPrint(p.allocator, "{s} cannot be used in strict mode", .{text}), notes);
            } else if (!can_be_transformed && p.isStrictModeOutputFormat()) {
                try p.log.addRangeError(p.source, r, try std.fmt.allocPrint(p.allocator, "{s} cannot be used with the ESM output format due to strict mode", .{text}));
            }
        }

        static inline fn isStrictMode(p: *P): bool {
            return p.current_scope.strict_mode != .sloppy_mode;
        }

        static inline fn isStrictModeOutputFormat(p: *P): bool {
            return p.options.bundle && p.options.output_format.isESM();
        }

        static fn declareCommonJSSymbol(p: *P, comptime kind: Symbol.Kind, comptime name: string): Ref! {
            const name_hash = comptime Scope.getMemberHash(name);
            const member = p.module_scope.getMemberWithHash(name, name_hash);

            // If the code declared this symbol using "var name", then this is actually
            // not a collision. For example, node will let you do this:
            //
            //   var exports;
            //   module.exports.foo = 123;
            //   console.log(exports.foo);
            //
            // This works because node's implementation of CommonJS wraps the entire
            // source file like this:
            //
            //   (function(require, exports, module, __filename, __dirname) {
            //     var exports;
            //     module.exports.foo = 123;
            //     console.log(exports.foo);
            //   })
            //
            // Both the "exports" argument and "var exports" are hoisted variables, so
            // they don't collide.
            if (_member := member) {
                if (p.symbols.items[_member.ref.innerIndex()].kind == .hoisted && kind == .hoisted && !p.has_es_module_syntax) {
                    return _member.ref;
                }
            }

            // Create a new symbol if we didn't merge with an existing one above
            const sym_ref = p.newSymbol(kind, name)?;

            if (member == null) {
                p.module_scope.members.put(p.allocator, name, Scope.Member{ ref: sym_ref, loc: logger.Loc.Empty })?;
                return sym_ref;
            }

            // If the variable was declared, then it shadows this symbol. The code in
            // this module will be unable to reference this symbol. However, we must
            // still add the symbol to the scope so it gets minified (automatically-
            // generated code may still reference the symbol).
            p.module_scope.generated.append(p.allocator, sym_ref)?;
            return sym_ref;
        }

        static fn declareGeneratedSymbol(p: *P, kind: Symbol.Kind, comptime name: string): Ref! {
            // The bundler runs the renamer, so it is ok to not append a hash
            if (p.options.bundle) {
                return declareSymbolMaybeGenerated(p, kind, logger.Loc.Empty, name, true)?;
            }

            return declareSymbolMaybeGenerated(p, kind, logger.Loc.Empty, generatedSymbolName(name), true)?;
        }

        static fn declareSymbol(p: *P, kind: Symbol.Kind, loc: logger.Loc, name: string): Ref! {
            return @call(bun.callmod_inline, declareSymbolMaybeGenerated, .{ p, kind, loc, name, false })?;
        }

        static fn declareSymbolMaybeGenerated(p: *P, kind: Symbol.Kind, loc: logger.Loc, name: string, comptime is_generated: bool): Ref! {
            // p.checkForNonBMPCodePoint(loc, name)
            if (comptime !is_generated) {
                // Forbid declaring a symbol with a reserved word in strict mode
                if (p.isStrictMode() && name.ptr != arguments_str.ptr && js_lexer.StrictModeReservedWords.has(name)) {
                    p.markStrictModeFeature(.reserved_word, js_lexer.rangeOfIdentifier(p.source, loc), name)?;
                }
            }

            // Allocate a new symbol
            let sym_ref = p.newSymbol(kind, name)?;

            const scope = p.current_scope;
            const entry = scope.members.getOrPut(p.allocator, name)?;
            if (entry.found_existing) {
                const existing = entry.value_ptr.*;
                let symbol: *Symbol = &p.symbols.items[existing.ref.innerIndex()];

                if (comptime !is_generated) {
                    switch (scope.canMergeSymbols(symbol.kind, kind, is_typescript_enabled)) {
                        .forbidden => {
                            p.log.addSymbolAlreadyDeclaredError(p.allocator, p.source, symbol.original_name, loc, existing.loc)?;
                            return existing.ref;
                        },

                        .keep_existing => {
                            sym_ref = existing.ref;
                        },

                        .replace_with_new => {
                            symbol.link = sym_ref;

                            // If these are both functions, remove the overwritten declaration
                            if (kind.isFunction() && symbol.kind.isFunction()) {
                                symbol.remove_overwritten_function_declaration = true;
                            }
                        },

                        .become_private_get_set_pair => {
                            sym_ref = existing.ref;
                            symbol.kind = .private_get_set_pair;
                        },

                        .become_private_static_get_set_pair => {
                            sym_ref = existing.ref;
                            symbol.kind = .private_static_get_set_pair;
                        },

                        .overwrite_with_new => {},
                    }
                } else {
                    p.symbols.items[sym_ref.innerIndex()].link = existing.ref;
                }
            }
            entry.key_ptr.* = name;
            entry.value_ptr.* = js_ast.Scope.Member{ ref: sym_ref, loc: loc };
            if (comptime is_generated) {
                p.module_scope.generated.append(p.allocator, sym_ref)?;
            }
            return sym_ref;
        }

        static fn validateFunctionName(p: *P, func: G.Fn, kind: FunctionKind) {
            if (name := func.name) {
                const original_name = p.symbols.items[name.ref.?.innerIndex()].original_name;

                if (func.flags.contains(.is_async) && strings.eqlComptime(original_name, "await")) {
                    p.log.addRangeError(
                        p.source,
                        js_lexer.rangeOfIdentifier(p.source, name.loc),
                        "An async function cannot be named \"await\"",
                    ) catch unreachable;
                } else if (kind == .expr && func.flags.contains(.is_generator) && strings.eqlComptime(original_name, "yield")) {
                    p.log.addRangeError(
                        p.source,
                        js_lexer.rangeOfIdentifier(p.source, name.loc),
                        "An generator function expression cannot be named \"yield\"",
                    ) catch unreachable;
                }
            }
        }

        static fn declareBinding(p: *P, kind: Symbol.Kind, binding: *BindingNodeIndex, opts: *ParseStatementOptions): anyerror!void {
            switch (binding.data) {
                .b_missing => {},
                .b_identifier => |bind| {
                    if (!opts.is_typescript_declare || (opts.is_namespace_scope && opts.is_export)) {
                        bind.ref = p.declareSymbol(kind, binding.loc, p.loadNameFromRef(bind.ref))?;
                    }
                },
                .b_array => |bind| {
                    for (*item of bind.items) {
                        p.declareBinding(kind, &item.binding, opts) catch unreachable;
                    }
                },
                .b_object => |bind| {
                    for (*prop of bind.properties) {
                        p.declareBinding(kind, &prop.value, opts) catch unreachable;
                    }
                },
            }
        }

        static fn storeNameInRef(p: *P, name: string): Ref! {
            if (comptime track_symbol_usage_during_parse_pass) {
                if (res := p.parse_pass_symbol_uses.getPtr(name)) {
                    res.used = true;
                }
            }

            const in_source_contents = unsafe {
                @intFromPtr(p.source.contents.ptr) <= @intFromPtr(name.ptr) && (@intFromPtr(name.ptr) + name.len) <= (@intFromPtr(p.source.contents.ptr) + p.source.contents.len)
            };
            if (in_source_contents) {
                const source_index: u32 = unsafe { @intCast(@intFromPtr(name.ptr) - @intFromPtr(p.source.contents.ptr)) };
                return Ref.initSourceEnd({
                    source_index: source_index,
                    inner_index: @intCast(name.len),
                    tag: .source_contents_slice,
                });
            } else {
                const inner_index: u31 = @intCast(p.allocated_names.items.len);
                p.allocated_names.append(p.allocator, name)?;
                return Ref.init(
                    inner_index,
                    p.source.index.get(),
                    false,
                );
            }
        }

        static fn loadNameFromRef(p: *P, sym_ref: Ref): string {
            return switch (sym_ref.tag) {
                .symbol => p.symbols.items[sym_ref.innerIndex()].original_name,
                .source_contents_slice => p.source.contents[sym_ref.sourceIndex() .. sym_ref.sourceIndex() + sym_ref.innerIndex()],
                .allocated_name => p.allocated_names.items[sym_ref.innerIndex()],
                else => @panic("Internal error: JS parser tried to load an invalid name from a Ref"),
            };
        }

        static inline fn addImportRecord(p: *P, kind: ImportKind, loc: logger.Loc, name: string): u32 {
            return p.addImportRecordByRange(kind, p.source.rangeOfString(loc), name);
        }

        static fn addImportRecordByRange(p: *P, kind: ImportKind, range: logger.Range, name: string): u32 {
            return p.addImportRecordByRangeAndPath(kind, range, fs.Path.init(name));
        }

        static fn addImportRecordByRangeAndPath(p: *P, kind: ImportKind, range: logger.Range, path: fs.Path): u32 {
            const index = p.import_records.items.len;
            const record = ImportRecord{
                kind: kind,
                range: range,
                path: path,
            };
            p.import_records.append(record) catch unreachable;
            return @as(u32, i32(index));
        }

        static fn popScope(p: *P) {
            const current_scope = p.current_scope;
            // We cannot rename anything inside a scope containing a direct eval() call
            if (current_scope.contains_direct_eval) {
                let iter = current_scope.members.iterator();
                while (member := iter.next()) {

                    // Using direct eval when bundling is not a good idea in general because
                    // esbuild must assume that it can potentially reach anything in any of
                    // the containing scopes. We try to make it work but this isn't possible
                    // in some cases.
                    //
                    // For example, symbols imported using an ESM import are a live binding
                    // to the underlying symbol in another file. This is emulated during
                    // scope hoisting by erasing the ESM import and just referencing the
                    // underlying symbol in the flattened bundle directly. However, that
                    // symbol may have a different name which could break uses of direct
                    // eval:
                    //
                    //   // Before bundling
                    //   import { foo as bar } from './foo.js'
                    //   console.log(eval('bar'))
                    //
                    //   // After bundling
                    //   let foo = 123 // The contents of "foo.js"
                    //   console.log(eval('bar'))
                    //
                    // There really isn't any way to fix this. You can't just rename "foo" to
                    // "bar" in the example above because there may be a third bundled file
                    // that also contains direct eval and imports the same symbol with a
                    // different conflicting import alias. And there is no way to store a
                    // live binding to the underlying symbol in a variable with the import's
                    // name so that direct eval can access it:
                    //
                    //   // After bundling
                    //   let foo = 123 // The contents of "foo.js"
                    //   const bar = /* cannot express a live binding to "foo" here */
                    //   console.log(eval('bar'))
                    //
                    // Technically a "with" statement could potentially make this work (with
                    // a big hit to performance), but they are deprecated and are unavailable
                    // in strict mode. This is a non-starter since all ESM code is strict mode.
                    //
                    // So while we still try to obey the requirement that all symbol names are
                    // pinned when direct eval is present, we make an exception for top-level
                    // symbols in an ESM file when bundling is enabled. We make no guarantee
                    // that "eval" will be able to reach these symbols and we allow them to be
                    // renamed or removed by tree shaking.
                    // if (p.currentScope.parent == null and p.has_es_module_syntax) {
                    //     continue;
                    // }

                    p.symbols.items[member.value_ptr.ref.innerIndex()].must_not_be_renamed = true;
                }
            }

            const parent_scope = current_scope.parent ?? p.panic("Internal error: attempted to call popScope() on the topmost scope", {});
            p.current_scope = parent_scope;
        }

        static fn markExprAsParenthesized(p_unused: *P, expr: *Expr) {
            switch (expr.data) {
                .e_array => |ex| {
                    ex.is_parenthesized = true;
                },
                .e_object => |ex| {
                    ex.is_parenthesized = true;
                },
                else => {
                    return;
                },
            }
        }

        static fn panic(p: *P, comptime fmt: string, args: auto): noreturn {
            @branchHint(.cold);
            p.panicLoc(fmt, args, null);
        }

        static fn panicLoc(p: *P, comptime fmt: string, args: auto, loc: logger.Loc?): noreturn {
            const panic_buffer = p.allocator.alloc(u8, 32 * 1024) catch unreachable;
            let panic_stream = std.Io.Writer.fixed(panic_buffer);

            // panic during visit pass leaves the lexer at the end, which
            // would make this location absolutely useless.
            const location = loc ?? p.lexer.loc();
            if (location.start < p.lexer.source.contents.len && !location.isEmpty()) {
                p.log.addRangeErrorFmt(
                    p.source,
                    { loc: location },
                    p.allocator,
                    "panic here",
                    {},
                ) catch |e| bun.handleOom(e);
            }

            p.log.level = .verbose;
            p.log.print(&panic_stream) catch unreachable;

            Output.panic(fmt ++ "\n{s}", args ++ .{panic_stream.buffered()});
        }

        static fn jsxStringsToMemberExpression(p: *P, loc: logger.Loc, parts: []const []const u8): Expr! {
            const result = p.findSymbol(loc, parts[0])?;

            const value = p.handleIdentifier(
                loc,
                E.Identifier{
                    ref: result.ref,
                    must_keep_due_to_with_stmt: result.is_inside_with_scope,
                    can_be_removed_if_unused: true,
                },
                parts[0],
                {
                    was_originally_identifier: true,
                },
            );
            if (parts.len > 1) {
                return p.memberExpression(loc, value, parts[1..]);
            }

            return value;
        }

        fn memberExpression(p: *P, loc: logger.Loc, initial_value: Expr, parts: []const []const u8): Expr {
            let value = initial_value;

            for (part of parts) {
                if (p.maybeRewritePropertyAccess(
                    loc,
                    value,
                    part,
                    loc,
                    {
                        is_call_target: false,
                        assign_target: .none,
                        // .is_template_tag = false,
                        is_delete_target: false,
                    },
                )) |rewrote| {
                    value = rewrote;
                } else {
                    value = p.newExpr(
                        E.Dot{
                            target: value,
                            name: part,
                            name_loc: loc,

                            can_be_removed_if_unused: p.options.features.dead_code_elimination,
                        },
                        loc,
                    );
                }
            }

            return value;
        }

        static fn willNeedBindingPattern(p: *const P): bool {
            return switch (p.lexer.token) {
                // "[a] = b;"
                .t_equals => true,
                // "for ([a] in b) {}"
                .t_in => !p.allow_in,
                // "for ([a] of b) {}"
                .t_identifier => !p.allow_in && p.lexer.isContextualKeyword("of"),
                else => false,
            };
        }

        static fn appendPart(p: *P, parts: *ListManaged(js_ast.Part), stmts: []Stmt): anyerror!void {
            // Reuse the memory if possible
            // This is reusable if the last part turned out to be dead
            p.symbol_uses.clearRetainingCapacity();
            p.declared_symbols.clearRetainingCapacity();
            p.scopes_for_current_part.clearRetainingCapacity();
            p.import_records_for_current_part.clearRetainingCapacity();
            p.import_symbol_property_uses.clearRetainingCapacity();

            p.had_commonjs_named_exports_this_visit = false;

            const allocator = p.allocator;
            let opts = PrependTempRefsOpts{};
            let partStmts = ListManaged(Stmt).fromOwnedSlice(allocator, stmts);

            p.visitStmtsAndPrependTempRefs(&partStmts, &opts)?;

            // Insert any relocated variable statements now
            if (p.relocated_top_level_vars.items.len > 0) {
                let already_declared = RefMap{};
                let already_declared_allocator_stack = std.heap.stackFallback(1024, allocator);
                const already_declared_allocator = already_declared_allocator_stack.get();
                const _already_declared_guard = DeinitWithAllocGuard<RefMap>{
                    ptr: &already_declared,
                    fba: &already_declared_allocator_stack.fixed_buffer_allocator,
                    allocator: already_declared_allocator,
                };

                for (*local of p.relocated_top_level_vars.items) {
                    // Follow links because "var" declarations may be merged due to hoisting
                    while (local.ref != null) {
                        let symbol = &p.symbols.items[local.ref.?.innerIndex()];
                        if (!symbol.hasLink()) {
                            break;
                        }
                        local.ref = symbol.link;
                    }
                    if (local.ref == null) continue;
                    const sym_ref = local.ref.?;
                    const declaration_entry = already_declared.getOrPut(already_declared_allocator, sym_ref)?;
                    if (!declaration_entry.found_existing) {
                        const decls = allocator.alloc(G.Decl, 1)?;
                        decls[0] = Decl{
                            binding: p.b(B.Identifier{ ref: sym_ref }, local.loc),
                        };
                        try partStmts.append(p.s(
                            S.Local{ decls: G.Decl.List.fromOwnedSlice(decls) },
                            local.loc,
                        ));
                        p.declared_symbols.append(p.allocator, { ref: sym_ref, is_top_level: true })?;
                    }
                }
                p.relocated_top_level_vars.clearRetainingCapacity();
            }

            if (partStmts.items.len > 0) {
                const final_stmts = partStmts.items;

                try parts.append(js_ast.Part{
                    stmts: final_stmts,
                    symbol_uses: p.symbol_uses,
                    import_symbol_property_uses: p.import_symbol_property_uses,
                    declared_symbols: p.declared_symbols.toOwnedSlice(),
                    import_record_indices: bun.BabyList(u32).fromOwnedSlice(
                        p.import_records_for_current_part.toOwnedSlice(
                            p.allocator,
                        ) catch unreachable,
                    ),
                    scopes: try p.scopes_for_current_part.toOwnedSlice(p.allocator),
                    can_be_removed_if_unused: p.stmtsCanBeRemovedIfUnused(final_stmts),
                    tag: if (p.had_commonjs_named_exports_this_visit) js_ast.Part.Tag.commonjs_named_export else .none,
                });
                p.symbol_uses = {};
                p.import_symbol_property_uses = {};
                p.had_commonjs_named_exports_this_visit = false;
            } else if (p.declared_symbols.len() > 0 || p.symbol_uses.count() > 0) {
                // if the part is dead, invalidate all the usage counts
                p.clearSymbolUsagesFromDeadPart(&{ stmts: undefined, declared_symbols: p.declared_symbols, symbol_uses: p.symbol_uses });
                p.declared_symbols.clearRetainingCapacity();
                p.import_records_for_current_part.clearRetainingCapacity();
            }
        }

        fn bindingCanBeRemovedIfUnused(p: *P, binding: Binding): bool {
            if (!p.options.features.dead_code_elimination) return false;
            return bindingCanBeRemovedIfUnusedWithoutDCECheck(p, binding);
        }

        fn bindingCanBeRemovedIfUnusedWithoutDCECheck(p: *P, binding: Binding): bool {
            switch (binding.data) {
                .b_array => |bi| {
                    for (*item of bi.items) {
                        if (!p.bindingCanBeRemovedIfUnusedWithoutDCECheck(item.binding)) {
                            return false;
                        }

                        if (item.default_value != null) {
                            if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&item.default_value.?)) {
                                return false;
                            }
                        }
                    }
                },
                .b_object => |bi| {
                    for (*property of bi.properties) {
                        if (!property.flags.contains(.is_spread) && !p.exprCanBeRemovedIfUnusedWithoutDCECheck(&property.key)) {
                            return false;
                        }

                        if (!p.bindingCanBeRemovedIfUnusedWithoutDCECheck(property.value)) {
                            return false;
                        }

                        if (property.default_value != null) {
                            if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&property.default_value.?)) {
                                return false;
                            }
                        }
                    }
                },
                else => {},
            }

            return true;
        }

        fn stmtsCanBeRemovedIfUnused(p: *P, stmts: []Stmt): bool {
            if (!p.options.features.dead_code_elimination) return false;
            return stmtsCanBeRemovedifUnusedWithoutDCECheck(p, stmts);
        }

        fn stmtsCanBeRemovedifUnusedWithoutDCECheck(p: *P, stmts: []Stmt): bool {
            for (stmt of stmts) {
                switch (stmt.data) {
                    // These never have side effects
                    .s_function, .s_empty => {},

                    // Let these be removed if they are unused. Note that we also need to
                    // check if the imported file is marked as "sideEffects: false" before we
                    // can remove a SImport statement. Otherwise the import must be kept for
                    // its side effects.
                    .s_import => {},

                    .s_class => |st| {
                        if (!p.classCanBeRemovedIfUnused(&st.class)) {
                            return false;
                        }
                    },

                    .s_expr => |st| {
                        if (st.does_not_affect_tree_shaking) {
                            // Expressions marked with this are automatically generated and have
                            // no side effects by construction.
                            continue;
                        }

                        if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&st.value)) {
                            return false;
                        }
                    },

                    .s_local => |st| {
                        // "await" is a side effect because it affects code timing
                        if (st.kind == .k_await_using) return false;

                        for (*decl of st.decls.slice()) {
                            if (!p.bindingCanBeRemovedIfUnusedWithoutDCECheck(decl.binding)) {
                                return false;
                            }

                            if (decl.value != null) {
                                if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&decl.value.?)) {
                                    return false;
                                } else if (st.kind == .k_using) {
                                    // "using" declarations are only side-effect free if they are initialized to null or undefined
                                    if (decl.value.?.data != .e_null && decl.value.?.data != .e_undefined) {
                                        return false;
                                    }
                                }
                            }
                        }
                    },

                    .s_try => |try_| {
                        if (!p.stmtsCanBeRemovedifUnusedWithoutDCECheck(try_.body) || (try_.finally != null && !p.stmtsCanBeRemovedifUnusedWithoutDCECheck(try_.finally.?.stmts))) {
                            return false;
                        }
                    },

                    // Exports are tracked separately, so this isn't necessary
                    .s_export_clause, .s_export_from => {},

                    .s_export_default => |st| {
                        switch (st.value) {
                            .stmt => |s2| {
                                switch (s2.data) {
                                    .s_expr => |s_expr| {
                                        if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&s_expr.value)) {
                                            return false;
                                        }
                                    },

                                    // These never have side effects
                                    .s_function => {},

                                    .s_class => {
                                        if (!p.classCanBeRemovedIfUnused(&s2.data.s_class.class)) {
                                            return false;
                                        }
                                    },
                                    else => {
                                        // Standard decorator lowering can produce non-class
                                        // statements as the export default value; conservatively
                                        // assume they have side effects.
                                        return false;
                                    },
                                }
                            },
                            .expr => |*exp| {
                                if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(exp)) {
                                    return false;
                                }
                            },
                        }
                    },

                    else => {
                        // Assume that all statements not explicitly special-cased here have side
                        // effects, and cannot be removed even if unused
                        return false;
                    },
                }
            }

            return true;
        }

        static fn deoptimizeCommonJSNamedExports(p: *P) {
            // exists for debugging
            p.commonjs_named_exports_deoptimized = true;
        }

        static fn maybeKeepExprSymbolName(p: *P, expr: Expr, original_name: string, was_anonymous_named_expr: bool): Expr {
            return if (was_anonymous_named_expr) p.keepExprSymbolName(expr, original_name) else expr;
        }

        static fn valueForThis(p: *P, loc: logger.Loc): Expr? {
            // Substitute "this" if we're inside a static class property initializer
            if (p.fn_only_data_visit.should_replace_this_with_class_name_ref) {
                if (sym_ref := p.fn_only_data_visit.class_name_ref) {
                    p.recordUsage(sym_ref.*);
                    return p.newExpr(E.Identifier{ ref: sym_ref.* }, loc);
                }
            }

            // oroigianlly was !=- modepassthrough
            if (!p.fn_only_data_visit.is_this_nested) {
                if (p.has_es_module_syntax && p.commonjs_named_exports.count() == 0) {
                    // In an ES6 module, "this" is supposed to be undefined. Instead of
                    // doing this at runtime using "fn.call(undefined)", we do it at
                    // compile time using expression substitution here.
                    return Expr{ loc: loc, data: nullValueExpr };
                } else {
                    // In a CommonJS module, "this" is supposed to be the same as "exports".
                    // Instead of doing this at runtime using "fn.call(module.exports)", we
                    // do it at compile time using expression substitution here.
                    p.recordUsage(p.exports_ref);
                    p.deoptimizeCommonJSNamedExports();
                    return p.newExpr(E.Identifier{ ref: p.exports_ref }, loc);
                }
            }

            return null;
        }

        static fn isValidAssignmentTarget(p: *P, expr: Expr): bool {
            return switch (expr.data) {
                .e_identifier => |ident| !isEvalOrArguments(p.loadNameFromRef(ident.ref)),
                .e_dot => |e| e.optional_chain == null,
                .e_index => |e| e.optional_chain == null,
                .e_array => |e| !e.is_parenthesized,
                .e_object => |e| !e.is_parenthesized,
                else => false,
            };
        }

        /// This is only allowed to be called if allow_runtime is true
        /// If --target=bun, this does nothing.
        static fn recordUsageOfRuntimeRequire(p: *P) {
            // target bun does not have __require
            if (p.options.features.auto_polyfill_require) {
                bun.assert(p.options.features.allow_runtime);

                p.ensureRequireSymbol();
                p.recordUsage(p.runtimeIdentifierRef(logger.Loc.Empty, "__require"));
            }
        }

        static fn ignoreUsageOfRuntimeRequire(p: *P) {
            if (p.options.features.auto_polyfill_require) {
                bun.assert(p.runtime_imports.__require != null);
                p.ignoreUsage(p.runtimeIdentifierRef(logger.Loc.Empty, "__require"));
                p.symbols.items[p.require_ref.innerIndex()].use_count_estimate = p.symbols.items[p.require_ref.innerIndex()].use_count_estimate -| 1;
            }
        }

        static inline fn valueForRequire(p: *P, loc: logger.Loc): Expr {
            bun.assert(!p.isSourceRuntime());
            return Expr{
                data: {
                    e_require_call_target: undefined,
                },
                loc: loc,
            };
        }

        static inline fn valueForImportMetaMain(p: *P, inverted: bool, loc: logger.Loc): Expr {
            if (known := p.options.import_meta_main_value) {
                return { loc: loc, data: { e_boolean: { value: if (inverted) !known else known } } };
            } else {
                // Node.js does not have import.meta.main, so we end up lowering
                // this to `require.main === module`, but with the ESM format,
                // both `require` and `module` are not present, so the code
                // generation we need is:
                //
                //     import { createRequire } from "node:module";
                //     var __require = createRequire(import.meta.url);
                //     var import_meta_main = __require.main === __require.module;
                //
                // The printer can handle this for us, but we need to reference
                // a handle to the `__require` function.
                if (p.options.lower_import_meta_main_for_node_js) {
                    p.recordUsageOfRuntimeRequire();
                }

                return {
                    loc: loc,
                    data: { e_import_meta_main: { inverted: inverted } },
                };
            }
        }

        static fn keepExprSymbolName(p_unused: *P, _value: Expr, name_unused: string): Expr {
            return _value;
            // var start = p.expr_list.items.len;
            // p.expr_list.ensureUnusedCapacity(2) catch unreachable;
            // p.expr_list.appendAssumeCapacity(_value);
            // p.expr_list.appendAssumeCapacity(p.newExpr(E.String{
            //     .utf8 = name,
            // }, _value.loc));

            // var value = p.callRuntime(_value.loc, "ℹ", p.expr_list.items[start..p.expr_list.items.len]);
            // // Make sure tree shaking removes this if the function is never used
            // value.getCall().can_be_unwrapped_if_unused = true;
            // return value;
        }

        static fn isSimpleParameterList(args: []G.Arg, has_rest_arg: bool): bool {
            if (has_rest_arg) {
                return false;
            }

            for (arg of args) {
                if (std.meta.activeTag(arg.binding.data) != .b_identifier || arg.default != null) {
                    return false;
                }
            }

            return true;
        }

        // This one is never called in places that haven't already checked if DCE is enabled.
        static fn classCanBeRemovedIfUnused(p: *P, cls: *G.Class): bool {
            if (cls.extends != null) {
                if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&cls.extends.?)) {
                    return false;
                }
            }

            for (*property of cls.properties) {
                if (property.kind == .class_static_block) {
                    if (!p.stmtsCanBeRemovedifUnusedWithoutDCECheck(property.class_static_block.?.stmts.slice())) {
                        return false;
                    }
                    continue;
                }

                if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&(property.key ?? unreachable))) {
                    return false;
                }

                if (property.value != null) {
                    if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&property.value.?)) {
                        return false;
                    }
                }

                if (property.initializer != null) {
                    if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&property.initializer.?)) {
                        return false;
                    }
                }
            }

            return true;
        }

        // TODO:
        // When React Fast Refresh is enabled, anything that's a JSX component should not be removable
        // This is to improve the reliability of fast refresh between page loads.
        static fn exprCanBeRemovedIfUnused(p: *P, expr: *const Expr): bool {
            if (!p.options.features.dead_code_elimination) return false;

            return exprCanBeRemovedIfUnusedWithoutDCECheck(p, expr);
        }

        fn exprCanBeRemovedIfUnusedWithoutDCECheck(p: *P, expr: *const Expr): bool {
            switch (expr.data) {
                .e_null,
                .e_undefined,
                .e_missing,
                .e_boolean,
                .e_branch_boolean,
                .e_number,
                .e_big_int,
                .e_string,
                .e_this,
                .e_reg_exp,
                .e_function,
                .e_arrow,
                .e_import_meta
                => {
                    return true;
                },

                .e_inlined_enum => |e| return p.exprCanBeRemovedIfUnusedWithoutDCECheck(&e.value),

                .e_dot => |ex| {
                    return ex.can_be_removed_if_unused;
                },
                .e_class => |ex| {
                    return p.classCanBeRemovedIfUnused(ex);
                },
                .e_identifier => |ex| {
                    bun.assert(!ex.ref.isSourceContentsSlice()); // was not visited

                    if (ex.must_keep_due_to_with_stmt) {
                        return false;
                    }

                    // Unbound identifiers cannot be removed because they can have side effects.
                    // One possible side effect is throwing a ReferenceError if they don't exist.
                    // Another one is a getter with side effects on the global object:
                    //
                    //   Object.defineProperty(globalThis, 'x', {
                    //     get() {
                    //       sideEffect();
                    //     },
                    //   });
                    //
                    // Be very careful about this possibility. It's tempting to treat all
                    // identifier expressions as not having side effects but that's wrong. We
                    // must make sure they have been declared by the code we are currently
                    // compiling before we can tell that they have no side effects.
                    //
                    // Note that we currently ignore ReferenceErrors due to TDZ access. This is
                    // incorrect but proper TDZ analysis is very complicated and would have to
                    // be very conservative, which would inhibit a lot of optimizations of code
                    // inside closures. This may need to be revisited if it proves problematic.
                    if (ex.can_be_removed_if_unused || p.symbols.items[ex.ref.innerIndex()].kind != .unbound) {
                        return true;
                    }
                },
                .e_commonjs_export_identifier, .e_import_identifier => {

                    // References to an ES6 import item are always side-effect free in an
                    // ECMAScript environment.
                    //
                    // They could technically have side effects if the imported module is a
                    // CommonJS module and the import item was translated to a property access
                    // (which esbuild's bundler does) and the property has a getter with side
                    // effects.
                    //
                    // But this is very unlikely and respecting this edge case would mean
                    // disabling tree shaking of all code that references an export from a
                    // CommonJS module. It would also likely violate the expectations of some
                    // developers because the code *looks* like it should be able to be tree
                    // shaken.
                    //
                    // So we deliberately ignore this edge case and always treat import item
                    // references as being side-effect free.
                    return true;
                },
                .e_if => |ex| {
                    return p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.test_) &&
                        (p.isSideEffectFreeUnboundIdentifierRef(
                            ex.yes,
                            ex.test_,
                            true,
                        ) ||
                            p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.yes)) &&
                        (p.isSideEffectFreeUnboundIdentifierRef(
                            ex.no,
                            ex.test_,
                            false,
                        ) || p.exprCanBeRemovedIfUnusedWithoutDCECheck(
                            &ex.no,
                        ));
                },
                .e_array => |ex| {
                    for (item of ex.items.slice()) {
                        if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&item)) {
                            return false;
                        }
                    }

                    return true;
                },
                .e_object => |ex| {
                    for (*property of ex.properties.slice()) {

                        // The key must still be evaluated if it's computed or a spread
                        if (property.kind == .spread || (property.flags.contains(.is_computed) && !property.key.?.isPrimitiveLiteral()) || property.flags.contains(.is_spread)) {
                            return false;
                        }

                        if (property.value != null) {
                            if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&property.value.?)) {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                .e_call => |ex| {
                    // A call that has been marked "__PURE__" can be removed if all arguments
                    // can be removed. The annotation causes us to ignore the target.
                    if (ex.can_be_unwrapped_if_unused != .never) {
                        const args = ex.args.slice();
                        for (i of 0..args.len) {
                            const arg = &args[i];
                            if (!(p.exprCanBeRemovedIfUnusedWithoutDCECheck(arg) || (ex.can_be_unwrapped_if_unused == .if_unused_and_toString_safe && arg.data.isSafeToString()))) {
                                return false;
                            }
                        }
                        return true;
                    }
                },
                .e_new => |ex| {

                    // A call that has been marked "__PURE__" can be removed if all arguments
                    // can be removed. The annotation causes us to ignore the target.
                    if (ex.can_be_unwrapped_if_unused != .never) {
                        const args = ex.args.slice();
                        for (i of 0..args.len) {
                            const arg = &args[i];
                            if (!(p.exprCanBeRemovedIfUnusedWithoutDCECheck(arg) || (ex.can_be_unwrapped_if_unused == .if_unused_and_toString_safe && arg.data.isSafeToString()))) {
                                return false;
                            }
                        }

                        return true;
                    }
                },
                .e_unary => |ex| {
                    switch (ex.op) {
                        // These operators must not have any type conversions that can execute code
                        // such as "toString" or "valueOf". They must also never throw any exceptions.
                        .un_void, .un_not => {
                            return p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.value);
                        },

                        // The "typeof" operator doesn't do any type conversions so it can be removed
                        // if the result is unused and the operand has no side effects. However, it
                        // has a special case where if the operand is an identifier expression such
                        // as "typeof x" and "x" doesn't exist, no reference error is thrown so the
                        // operation has no side effects.
                        //
                        // Note that there *is* actually a case where "typeof x" can throw an error:
                        // when "x" is being referenced inside of its TDZ (temporal dead zone). TDZ
                        // checks are not yet handled correctly by bun or esbuild, so this possibility is
                        // currently ignored.
                        .un_typeof => {
                            if (ex.value.data == .e_identifier && ex.flags.was_originally_typeof_identifier) {
                                return true;
                            }

                            return p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.value);
                        },

                        else => {},
                    }
                },
                .e_binary => |ex| {
                    switch (ex.op) {
                        // These operators must not have any type conversions that can execute code
                        // such as "toString" or "valueOf". They must also never throw any exceptions.
                        .bin_strict_eq,
                        .bin_strict_ne,
                        .bin_comma,
                        .bin_nullish_coalescing
                        => return p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.left) && p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.right),

                        // Special-case "||" to make sure "typeof x === 'undefined' || x" can be removed
                        .bin_logical_or => return p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.left) &&
                            (p.isSideEffectFreeUnboundIdentifierRef(ex.right, ex.left, false) || p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.right)),

                        // Special-case "&&" to make sure "typeof x !== 'undefined' && x" can be removed
                        .bin_logical_and => return p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.left) &&
                            (p.isSideEffectFreeUnboundIdentifierRef(ex.right, ex.left, true) || p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.right)),

                        // For "==" and "!=", pretend the operator was actually "===" or "!==". If
                        // we know that we can convert it to "==" or "!=", then we can consider the
                        // operator itself to have no side effects. This matters because our mangle
                        // logic will convert "typeof x === 'object'" into "typeof x == 'object'"
                        // and since "typeof x === 'object'" is considered to be side-effect free,
                        // we must also consider "typeof x == 'object'" to be side-effect free.
                        .bin_loose_eq, .bin_loose_ne => return SideEffects.canChangeStrictToLoose(
                            ex.left.data,
                            ex.right.data,
                        ) &&
                            p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.left) && p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.right),

                        // Special-case "<" and ">" with string, number, or bigint arguments
                        .bin_lt, .bin_gt, .bin_le, .bin_ge => {
                            const left = ex.left.knownPrimitive();
                            const right = ex.right.knownPrimitive();
                            switch (left) {
                                .string, .number, .bigint => {
                                    return right == left && p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.left) && p.exprCanBeRemovedIfUnusedWithoutDCECheck(&ex.right);
                                },
                                else => {},
                            }
                        },
                        else => {},
                    }
                },
                .e_template => |templ| {
                    if (templ.tag == null) {
                        for (part of templ.parts) {
                            if (!p.exprCanBeRemovedIfUnusedWithoutDCECheck(&part.value) || part.value.knownPrimitive() == .unknown) {
                                return false;
                            }
                        }

                        return true;
                    }
                },
                else => {},
            }

            return false;
        }

        // // This is based on exprCanBeRemoved
        // // The main difference: identifiers, functions, arrow functions cause it to return false
        // pub fn exprCanBeHoistedForJSX(p: *P, expr: *const Expr) bool {
        //     if (comptime jsx_transform_type != .react) {
        //         unreachable;
        //     }

        //     switch (expr.data) {
        //         .e_null,
        //         .e_undefined,
        //         .e_missing,
        //         .e_boolean,
        //         .e_number,
        //         .e_big_int,
        //         .e_string,
        //         .e_reg_exp,
        //         => {
        //             return true;
        //         },

        //         .e_dot => |ex| {
        //             return ex.can_be_removed_if_unused;
        //         },
        //         .e_import_identifier => {

        //             // References to an ES6 import item are always side-effect free in an
        //             // ECMAScript environment.
        //             //
        //             // They could technically have side effects if the imported module is a
        //             // CommonJS module and the import item was translated to a property access
        //             // (which esbuild's bundler does) and the property has a getter with side
        //             // effects.
        //             //
        //             // But this is very unlikely and respecting this edge case would mean
        //             // disabling tree shaking of all code that references an export from a
        //             // CommonJS module. It would also likely violate the expectations of some
        //             // developers because the code *looks* like it should be able to be tree
        //             // shaken.
        //             //
        //             // So we deliberately ignore this edge case and always treat import item
        //             // references as being side-effect free.
        //             return true;
        //         },
        //         .e_if => |ex| {
        //             return p.exprCanBeHoistedForJSX(&ex.test_) and
        //                 (p.isSideEffectFreeUnboundIdentifierRef(
        //                 ex.yes,
        //                 ex.test_,
        //                 true,
        //             ) or
        //                 p.exprCanBeHoistedForJSX(&ex.yes)) and
        //                 (p.isSideEffectFreeUnboundIdentifierRef(
        //                 ex.no,
        //                 ex.test_,
        //                 false,
        //             ) or p.exprCanBeHoistedForJSX(
        //                 &ex.no,
        //             ));
        //         },
        //         .e_array => |ex| {
        //             for (ex.items.slice()) |*item| {
        //                 if (!p.exprCanBeHoistedForJSX(item)) {
        //                     return false;
        //                 }
        //             }

        //             return true;
        //         },
        //         .e_object => |ex| {
        //             // macros disable this because macros get inlined
        //             // so it's sort of the opposite of the purpose of this function
        //             if (ex.was_originally_macro)
        //                 return false;

        //             for (ex.properties.slice()) |*property| {

        //                 // The key must still be evaluated if it's computed or a spread
        //                 if (property.kind == .spread or property.flags.contains(.is_computed) or property.flags.contains(.is_spread)) {
        //                     return false;
        //                 }

        //                 if (property.value) |*val| {
        //                     if (!p.exprCanBeHoistedForJSX(val)) {
        //                         return false;
        //                     }
        //                 }
        //             }
        //             return true;
        //         },
        //         .e_call => |ex| {

        //             // A call that has been marked "__PURE__" can be removed if all arguments
        //             // can be removed. The annotation causes us to ignore the target.
        //             if (ex.can_be_unwrapped_if_unused) {
        //                 for (ex.args.slice()) |*arg| {
        //                     if (!p.exprCanBeHoistedForJSX(arg)) {
        //                         return false;
        //                     }
        //                 }
        //                 return true;
        //             }
        //         },
        //         .e_new => |ex| {

        //             // A call that has been marked "__PURE__" can be removed if all arguments
        //             // can be removed. The annotation causes us to ignore the target.
        //             if (ex.can_be_unwrapped_if_unused) {
        //                 for (ex.args.slice()) |*arg| {
        //                     if (!p.exprCanBeHoistedForJSX(arg)) {
        //                         return false;
        //                     }
        //                 }

        //                 return true;
        //             }
        //         },
        //         .e_unary => |ex| {
        //             switch (ex.op) {
        //                 // These operators must not have any type conversions that can execute code
        //                 // such as "toString" or "valueOf". They must also never throw any exceptions.
        //                 .un_void, .un_not => {
        //                     return p.exprCanBeHoistedForJSX(&ex.value);
        //                 },

        //                 // The "typeof" operator doesn't do any type conversions so it can be removed
        //                 // if the result is unused and the operand has no side effects. However, it
        //                 // has a special case where if the operand is an identifier expression such
        //                 // as "typeof x" and "x" doesn't exist, no reference error is thrown so the
        //                 // operation has no side effects.
        //                 //
        //                 // Note that there *is* actually a case where "typeof x" can throw an error:
        //                 // when "x" is being referenced inside of its TDZ (temporal dead zone). TDZ
        //                 // checks are not yet handled correctly by bun or esbuild, so this possibility is
        //                 // currently ignored.
        //                 .un_typeof => {
        //                     if (ex.value.data == .e_identifier) {
        //                         return true;
        //                     }

        //                     return p.exprCanBeHoistedForJSX(&ex.value);
        //                 },

        //                 else => {},
        //             }
        //         },
        //         .e_binary => |ex| {
        //             switch (ex.op) {
        //                 // These operators must not have any type conversions that can execute code
        //                 // such as "toString" or "valueOf". They must also never throw any exceptions.
        //                 .bin_strict_eq,
        //                 .bin_strict_ne,
        //                 .bin_comma,
        //                 .bin_nullish_coalescing,
        //                 => return p.exprCanBeHoistedForJSX(&ex.left) and p.exprCanBeHoistedForJSX(&ex.right),

        //                 // Special-case "||" to make sure "typeof x === 'undefined' || x" can be removed
        //                 .bin_logical_or => return p.exprCanBeHoistedForJSX(&ex.left) and
        //                     (p.isSideEffectFreeUnboundIdentifierRef(ex.right, ex.left, false) or p.exprCanBeHoistedForJSX(&ex.right)),

        //                 // Special-case "&&" to make sure "typeof x !== 'undefined' && x" can be removed
        //                 .bin_logical_and => return p.exprCanBeHoistedForJSX(&ex.left) and
        //                     (p.isSideEffectFreeUnboundIdentifierRef(ex.right, ex.left, true) or p.exprCanBeHoistedForJSX(&ex.right)),

        //                 // For "==" and "!=", pretend the operator was actually "===" or "!==". If
        //                 // we know that we can convert it to "==" or "!=", then we can consider the
        //                 // operator itself to have no side effects. This matters because our mangle
        //                 // logic will convert "typeof x === 'object'" into "typeof x == 'object'"
        //                 // and since "typeof x === 'object'" is considered to be side-effect free,
        //                 // we must also consider "typeof x == 'object'" to be side-effect free.
        //                 .bin_loose_eq, .bin_loose_ne => return SideEffects.canChangeStrictToLoose(
        //                     ex.left.data,
        //                     ex.right.data,
        //                 ) and
        //                     p.exprCanBeHoistedForJSX(&ex.left) and p.exprCanBeHoistedForJSX(&ex.right),
        //                 else => {},
        //             }
        //         },
        //         .e_template => |templ| {
        //             if (templ.tag == null) {
        //                 for (templ.parts) |part| {
        //                     if (!p.exprCanBeHoistedForJSX(&part.value) or part.value.knownPrimitive() == .unknown) {
        //                         return false;
        //                     }
        //                 }
        //             }

        //             return true;
        //         },
        //         else => {},

        //         // These may reference variables from an upper scope
        //         // it's possible to detect that, but we are cutting scope for now
        //         // .e_function,
        //         // .e_arrow,
        //         // .e_this,
        //     }

        //     return false;
        // }

        fn isSideEffectFreeUnboundIdentifierRef(p: *P, value: Expr, guard_condition: Expr, is_yes_branch_: bool): bool {
            if (value.data != .e_identifier ||
                p.symbols.items[value.data.e_identifier.ref.innerIndex()].kind != .unbound ||
                guard_condition.data != .e_binary)
                return false;

            const binary = guard_condition.data.e_binary.*;
            let is_yes_branch = is_yes_branch_;

            switch (binary.op) {
                .bin_strict_eq, .bin_strict_ne, .bin_loose_eq, .bin_loose_ne => {
                    // typeof x !== 'undefined'
                    let typeof: Expr.Data = binary.left.data;
                    let compare: Expr.Data = binary.right.data;
                    // typeof 'undefined' !== x
                    if (typeof == .e_string) {
                        typeof = binary.right.data;
                        compare = binary.left.data;
                    }

                    // this order because Expr.Data Tag is not a pointer
                    // so it should be slightly faster to compare
                    if (compare != .e_string ||
                        typeof != .e_unary)
                        return false;
                    const unary = typeof.e_unary.*;

                    if (unary.op != .un_typeof || unary.value.data != .e_identifier)
                        return false;

                    const id = value.data.e_identifier.ref;
                    const id2 = unary.value.data.e_identifier.ref;
                    return ((compare.e_string.eqlComptime("undefined") == is_yes_branch) ==
                        (binary.op == .bin_strict_ne || binary.op == .bin_loose_ne)) &&
                        id.eql(id2);
                },
                .bin_lt, .bin_gt, .bin_le, .bin_ge => {
                    // Pattern match for "typeof x < <string>"
                    let typeof: Expr.Data = binary.left.data;
                    let str: Expr.Data = binary.right.data;

                    // Check if order is flipped: 'u' >= typeof x
                    if (typeof == .e_string) {
                        typeof = binary.right.data;
                        str = binary.left.data;
                        is_yes_branch = !is_yes_branch;
                    }

                    if (typeof == .e_unary && str == .e_string) {
                        const unary = typeof.e_unary.*;
                        if (unary.op == .un_typeof &&
                            unary.value.data == .e_identifier &&
                            unary.flags.was_originally_typeof_identifier &&
                            str.e_string.eqlComptime("u"))
                        {
                            // In "typeof x < 'u' ? x : null", the reference to "x" is side-effect free
                            // In "typeof x > 'u' ? x : null", the reference to "x" is side-effect free
                            if (is_yes_branch == (binary.op == .bin_lt || binary.op == .bin_le)) {
                                const id = value.data.e_identifier.ref;
                                const id2 = unary.value.data.e_identifier.ref;
                                if (id.eql(id2)) {
                                    return true;
                                }
                            }
                        }
                    }

                    return false;
                },
                else => return false,
            }
        }

        static fn jsxImportAutomatic(p: *P, loc: logger.Loc, is_static: bool): Expr {
            return p.jsxImport(
                if (is_static && !p.options.jsx.development && FeatureFlags.support_jsxs_in_jsx_transform)
                    .jsxs
                else if (p.options.jsx.development)
                    .jsxDEV
                else
                    .jsx,
                loc,
            );
        }

        static fn jsxImport(p: *P, kind: JSXImport, loc: logger.Loc): Expr {
            switch (kind) {
                inline else => |field| {
                    const sym_ref: Ref = brk: {
                        if (p.jsx_imports.getWithTag(kind) == null) {
                            const symbol_name = @tagName(field);

                            const loc_ref = LocRef{
                                loc: loc,
                                ref: (p.declareGeneratedSymbol(.other, symbol_name) catch unreachable),
                            };

                            bun.handleOom(p.module_scope.generated.append(p.allocator, loc_ref.ref.?));
                            p.is_import_item.put(p.allocator, loc_ref.ref.?, undefined) catch unreachable;
                            @field(p.jsx_imports, @tagName(field)) = loc_ref;
                            break :brk loc_ref.ref.?;
                        }

                        break :brk p.jsx_imports.getWithTag(kind).?;
                    };

                    p.recordUsage(sym_ref);
                    return p.handleIdentifier(
                        loc,
                        E.Identifier{
                            ref: sym_ref,
                            can_be_removed_if_unused: true,
                            call_can_be_unwrapped_if_unused: true,
                        },
                        null,
                        {
                            was_originally_identifier: true,
                        },
                    );
                },
            }
        }

        static fn selectLocalKind(p: *P, kind: S.Local.Kind): S.Local.Kind {
            // Use "var" instead of "let" and "const" if the variable declaration may
            // need to be separated from the initializer. This allows us to safely move
            // this declaration into a nested scope.
            if ((p.options.bundle || p.will_wrap_module_in_try_catch_for_using) &&
                p.current_scope.parent == null && !kind.isUsing())
            {
                return .k_var;
            }

            // Optimization: use "let" instead of "const" because it's shorter. This is
            // only done when bundling because assigning to "const" is only an error when bundling.
            if (p.options.bundle && kind == .k_const && p.options.features.minify_syntax) {
                return .k_let;
            }

            return kind;
        }

        static fn ignoreUsage(p: *P, sym_ref: Ref) {
            if (!p.is_control_flow_dead && !p.is_revisit_for_substitution) {
                if (comptime Environment.allow_assert) assert(@as(usize, sym_ref.innerIndex()) < p.symbols.items.len);
                p.symbols.items[sym_ref.innerIndex()].use_count_estimate = p.symbols.items[sym_ref.innerIndex()].use_count_estimate -| 1;
                const use_opt = p.symbol_uses.get(sym_ref);
                if (use_opt == null) return;
                let use = use_opt.?;
                use.count_estimate = use.count_estimate -| 1;
                if (use.count_estimate == 0) {
                    * = p.symbol_uses.swapRemove(sym_ref);
                } else {
                    p.symbol_uses.putAssumeCapacity(sym_ref, use);
                }
            }

            // Don't roll back the "tsUseCounts" increment. This must be counted even if
            // the value is ignored because that's what the TypeScript compiler does.
        }

        static fn ignoreUsageOfIdentifierInDotChain(p: *P, expr: Expr) {
            let current = expr;
            while (true) {
                switch (current.data) {
                    .e_identifier => |id| {
                        p.ignoreUsage(id.ref);
                    },
                    .e_dot => |dot| {
                        current = dot.target;
                        continue;
                    },
                    .e_index => |index| {
                        if (index.index.isString()) {
                            current = index.target;
                            continue;
                        }
                    },
                    else => { return; },
                }

                return;
            }
        }

        static fn isExportToEliminate(p: *P, sym_ref: Ref): bool {
            const symbol_name = p.loadNameFromRef(sym_ref);
            return p.options.features.replace_exports.contains(symbol_name);
        }

        static fn injectReplacementExport(p: *P, stmts: *StmtList, name_ref: Ref, loc: logger.Loc, replacement: *const RuntimeFeatures.ReplaceableExport): bool {
            switch (replacement.*) {
                .delete => return false,
                .replace => |value| {
                    const count = stmts.items.len;
                    let decls = p.allocator.alloc(G.Decl, 1) catch unreachable;

                    decls[0] = { binding: p.b(B.Identifier{ ref: name_ref }, loc), value: value };
                    let local = p.s(
                        S.Local{
                            is_export: true,
                            decls: Decl.List.fromOwnedSlice(decls),
                        },
                        loc,
                    );
                    p.visitAndAppendStmt(stmts, &local) catch unreachable;
                    return count != stmts.items.len;
                },
                .inject => |with| {
                    const count = stmts.items.len;
                    let decls = p.allocator.alloc(G.Decl, 1) catch unreachable;
                    decls[0] = {
                        binding: p.b(
                            B.Identifier{ ref: p.declareSymbol(.other, loc, with.name) catch unreachable },
                            loc,
                        ),
                        value: with.value,
                    };

                    let local = p.s(
                        S.Local{
                            is_export: true,
                            decls: Decl.List.fromOwnedSlice(decls),
                        },
                        loc,
                    );
                    p.visitAndAppendStmt(stmts, &local) catch unreachable;
                    return count != stmts.items.len;
                },
            }
        }

        static fn replaceDeclAndPossiblyRemove(p: *P, decl: *G.Decl, replacement: *const RuntimeFeatures.ReplaceableExport): bool {
            switch (replacement.*) {
                .delete => return false,
                .replace => |value| {
                    decl.*.value = p.visitExpr(value);
                    return true;
                },
                .inject => |with| {
                    decl.* = {
                        binding: p.b(
                            B.Identifier{ ref: p.declareSymbol(.other, decl.binding.loc, with.name) catch unreachable },
                            decl.binding.loc,
                        ),
                        value: p.visitExpr(Expr{ data: with.value.data, loc: if (decl.value != null) decl.value.?.loc else decl.binding.loc }),
                    };
                    return true;
                },
            }
        }

        static fn markExportedDeclsInsideNamespace(p: *P, ns_ref: Ref, decls: []G.Decl) {
            for (decl of decls) {
                p.markExportedBindingInsideNamespace(ns_ref, decl.binding);
            }
        }

        static fn appendIfBodyPreservingScope(p: *P, stmts: *ListManaged(Stmt), body: Stmt): anyerror!void {
            switch (body.data) {
                .s_block => |block| {
                    let keep_block = false;
                    for (stmt of block.stmts) {
                        if (statementCaresAboutScope(stmt)) {
                            keep_block = true;
                            break;
                        }
                    }

                    if (!keep_block && block.stmts.len > 0) {
                        stmts.appendSlice(block.stmts)?;
                        return;
                    }
                },
                else => {},
            }

            if (statementCaresAboutScope(body)) {
                let block_stmts = p.allocator.alloc(Stmt, 1)?;
                block_stmts[0] = body;
                stmts.append(p.s(S.Block{ stmts: block_stmts }, body.loc))?;
                return;
            }

            stmts.append(body)?;
            return;
        }

        fn markExportedBindingInsideNamespace(p: *P, sym_ref: Ref, binding: BindingNodeIndex) {
            switch (binding.data) {
                .b_missing => {},
                .b_identifier => |ident| {
                    p.is_exported_inside_namespace.put(p.allocator, ident.ref, sym_ref) catch unreachable;
                },
                .b_array => |array| {
                    for (item of array.items) {
                        p.markExportedBindingInsideNamespace(sym_ref, item.binding);
                    }
                },
                .b_object => |obj| {
                    for (item of obj.properties) {
                        p.markExportedBindingInsideNamespace(sym_ref, item.value);
                    }
                },
            }
        }

        static fn generateClosureForTypeScriptNamespaceOrEnum(
            p: *P,
            stmts: *ListManaged(Stmt),
            stmt_loc: logger.Loc,
            is_export: bool,
            name_loc: logger.Loc,
            original_name_ref: Ref,
            arg_ref: Ref,
            stmts_inside_closure: []Stmt,
            all_values_are_pure: bool,
        ): anyerror!void {
            let name_ref = original_name_ref;

            // Follow the link chain in case symbols were merged
            let symbol: Symbol = p.symbols.items[name_ref.innerIndex()];
            while (symbol.hasLink()) {
                const link = symbol.link;
                name_ref = link;
                symbol = p.symbols.items[name_ref.innerIndex()];
            }
            const allocator = p.allocator;

            // Make sure to only emit a variable once for a given namespace, since there
            // can be multiple namespace blocks for the same namespace
            if ((symbol.kind == .ts_namespace || symbol.kind == .ts_enum) &&
                !p.emitted_namespace_vars.contains(name_ref))
            {
                bun.handleOom(p.emitted_namespace_vars.putNoClobber(allocator, name_ref, undefined));

                let decls = bun.handleOom(allocator.alloc(G.Decl, 1));
                decls[0] = G.Decl{ binding: p.b(B.Identifier{ ref: name_ref }, name_loc) };

                if (p.enclosing_namespace_arg_ref == null) {
                    // Top-level namespace: "var"
                    stmts.append(
                        p.s(S.Local{
                            kind: .k_var,
                            decls: G.Decl.List.fromOwnedSlice(decls),
                            is_export: is_export,
                        }, stmt_loc),
                    ) catch |e| bun.handleOom(e);
                } else {
                    // Nested namespace: "let"
                    stmts.append(
                        p.s(S.Local{
                            kind: .k_let,
                            decls: G.Decl.List.fromOwnedSlice(decls),
                        }, stmt_loc),
                    ) catch |e| bun.handleOom(e);
                }
            }

            const arg_expr: Expr = arg_expr: {
                // TODO: unsupportedJSFeatures.has(.logical_assignment)
                // If the "||=" operator is supported, our minified output can be slightly smaller
                if (is_export) if (namespace := p.enclosing_namespace_arg_ref) {
                    const name = p.symbols.items[name_ref.innerIndex()].original_name;

                    // "name = (enclosing.name ||= {})"
                    p.recordUsage(namespace);
                    p.recordUsage(name_ref);
                    break :arg_expr Expr.assign(
                        Expr.initIdentifier(name_ref, name_loc),
                        p.newExpr(E.Binary{
                            op: .bin_logical_or_assign,
                            left: p.newExpr(
                                E.Dot{
                                    target: Expr.initIdentifier(namespace, name_loc),
                                    name: name,
                                    name_loc: name_loc,
                                },
                                name_loc,
                            ),
                            right: p.newExpr(E.Object{ is_single_line: false }, name_loc),
                        }, name_loc),
                    );
                };

                // "name ||= {}"
                p.recordUsage(name_ref);
                break :arg_expr p.newExpr(E.Binary{
                    op: .bin_logical_or_assign,
                    left: Expr.initIdentifier(name_ref, name_loc),
                    right: p.newExpr(E.Object{ is_single_line: false }, name_loc),
                }, name_loc);
            };

            let func_args = bun.handleOom(allocator.alloc(G.Arg, 1));
            func_args[0] = { binding: p.b(B.Identifier{ ref: arg_ref }, name_loc) };

            let args_list = bun.handleOom(allocator.alloc(ExprNodeIndex, 1));
            args_list[0] = arg_expr;

            // TODO: if unsupported features includes arrow functions
            // const target = p.newExpr(
            //     E.Function{ .func = .{
            //         .args = func_args,
            //         .name = null,
            //         .open_parens_loc = stmt_loc,
            //         .body = G.FnBody{
            //             .loc = stmt_loc,
            //             .stmts = try allocator.dupe(StmtNodeIndex, stmts_inside_closure),
            //         },
            //     } },
            //     stmt_loc,
            // );

            const target = target: {
                // "(() => { foo() })()" => "(() => foo())()"
                if (p.options.features.minify_syntax && stmts_inside_closure.len == 1) {
                    if (stmts_inside_closure[0].data == .s_expr) {
                        stmts_inside_closure[0] = p.s(S.Return{
                            value: stmts_inside_closure[0].data.s_expr.value,
                        }, stmts_inside_closure[0].loc);
                    }
                }

                break :target p.newExpr(E.Arrow{
                    args: func_args,
                    body: {
                        loc: stmt_loc,
                        stmts: try allocator.dupe(StmtNodeIndex, stmts_inside_closure),
                    },
                    prefer_expr: true,
                }, stmt_loc);
            };

            // Call the closure with the name object
            const call = p.newExpr(
                E.Call{
                    target: target,
                    args: ExprNodeList.fromOwnedSlice(args_list),
                    // TODO: make these fully tree-shakable. this annotation
                    // as-is is incorrect.  This would be done by changing all
                    // enum wrappers into `var Enum = ...` instead of two
                    // separate statements. This way, the @__PURE__ annotation
                    // is attached to the variable binding.
                    //
                    // .can_be_unwrapped_if_unused = all_values_are_pure,
                },
                stmt_loc,
            );

            const closure = p.s(S.SExpr{
                value: call,
                does_not_affect_tree_shaking: all_values_are_pure,
            }, stmt_loc);

            stmts.append(closure) catch unreachable;
        }

        static fn lowerClass(
            p: *P,
            stmtorexpr: js_ast.StmtOrExpr,
        ): []Stmt {
            switch (stmtorexpr) {
                .stmt => |stmt| {
                    // Standard decorator lowering path (for both JS and TS files)
                    if (stmt.data.s_class.class.should_lower_standard_decorators) {
                        return p.lowerStandardDecoratorsStmt(stmt);
                    }

                    if (comptime !is_typescript_enabled) {
                        if (!stmt.data.s_class.class.has_decorators) {
                            let stmts = p.allocator.alloc(Stmt, 1) catch unreachable;
                            stmts[0] = stmt;
                            return stmts;
                        }
                    }
                    let cls = &stmt.data.s_class.class;
                    let constructor_function: (*E.Function)? = null;

                    let static_decorators = ListManaged(Stmt).init(p.allocator);
                    let instance_decorators = ListManaged(Stmt).init(p.allocator);
                    let instance_members = ListManaged(Stmt).init(p.allocator);
                    let static_members = ListManaged(Stmt).init(p.allocator);
                    let class_properties = ListManaged(Property).init(p.allocator);

                    for (*prop of cls.properties) {
                        // merge parameter decorators with method decorators
                        if (prop.flags.contains(.is_method)) {
                            if (prop_value := prop.value) {
                                switch (prop_value.data) {
                                    .e_function => |func| {
                                        const is_constructor = (prop.key.?.data == .e_string && prop.key.?.data.e_string.eqlComptime("constructor"));

                                        if (is_constructor) constructor_function = func;

                                        let i: usize = 0;
                                        while (i < func.func.args.len) : (i += 1) {
                                            const arg = func.func.args[i];
                                            for (arg_decorator of arg.ts_decorators.ptr[0..arg.ts_decorators.len]) {
                                                let decorators = if (is_constructor)
                                                    &cls.ts_decorators
                                                else
                                                    &prop.ts_decorators;
                                                const args = p.allocator.alloc(Expr, 2) catch unreachable;
                                                args[0] = p.newExpr(E.Number{ value: @as(f64, @floatFromInt(i)) }, arg_decorator.loc);
                                                args[1] = arg_decorator;
                                                decorators.append(
                                                    p.allocator,
                                                    p.callRuntime(arg_decorator.loc, "__legacyDecorateParamTS", args),
                                                ) catch |e| bun.handleOom(e);
                                            }
                                        }
                                    },
                                    else => unreachable,
                                }
                            }
                        }

                        // TODO: prop.kind == .declare and prop.value == null

                        if (prop.ts_decorators.len > 0) {
                            const descriptor_key = prop.key.?;
                            const loc = descriptor_key.loc;

                            // TODO: when we have the `accessor` modifier, add `and !prop.flags.contains(.has_accessor_modifier)` to
                            // the if statement.
                            const descriptor_kind: Expr = if (!prop.flags.contains(.is_method))
                                p.newExpr(std.mem.zeroes(E.Undefined), loc)
                            else
                                p.newExpr(std.mem.zeroes(E.Null), loc);

                            let target: Expr = undefined;
                            if (prop.flags.contains(.is_static)) {
                                p.recordUsage(cls.class_name.?.ref.?);
                                target = p.newExpr(E.Identifier{ ref: cls.class_name.?.ref.? }, cls.class_name.?.loc);
                            } else {
                                target = p.newExpr(E.Dot{ target: p.newExpr(E.Identifier{ ref: cls.class_name.?.ref.? }, cls.class_name.?.loc), name: "prototype", name_loc: loc }, loc);
                            }

                            let array: std.array_list.Managed(Expr) = .init(p.allocator);

                            if (p.options.features.emit_decorator_metadata) {
                                switch (prop.kind) {
                                    .normal, .abstract => {
                                        {
                                            // design:type
                                            let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                            args[0] = p.newExpr(E.String{ data: "design:type" }, logger.Loc.Empty);
                                            args[1] = p.serializeMetadata(prop.ts_metadata) catch unreachable;
                                            array.append(p.callRuntime(loc, "__legacyMetadataTS", args)) catch unreachable;
                                        }
                                        // design:paramtypes and design:returntype if method
                                        if (prop.flags.contains(.is_method)) {
                                            if (prop_value := prop.value) {
                                                {
                                                    let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                                    args[0] = p.newExpr(E.String{ data: "design:paramtypes" }, logger.Loc.Empty);

                                                    const method_args = prop_value.data.e_function.func.args;
                                                    const args_array = p.allocator.alloc(Expr, method_args.len) catch unreachable;
                                                    let ma_i: usize = 0;
                                                    while (ma_i < args_array.len) : (ma_i += 1) {
                                                        args_array[ma_i] = p.serializeMetadata(method_args[ma_i].ts_metadata) catch unreachable;
                                                    }

                                                    args[1] = p.newExpr(E.Array{ items: ExprNodeList.fromOwnedSlice(args_array) }, logger.Loc.Empty);

                                                    array.append(p.callRuntime(loc, "__legacyMetadataTS", args)) catch unreachable;
                                                }
                                                {
                                                    let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                                    args[0] = p.newExpr(E.String{ data: "design:returntype" }, logger.Loc.Empty);
                                                    args[1] = p.serializeMetadata(prop_value.data.e_function.func.return_ts_metadata) catch unreachable;
                                                    array.append(p.callRuntime(loc, "__legacyMetadataTS", args)) catch unreachable;
                                                }
                                            }
                                        }
                                    },
                                    .get => if (prop.flags.contains(.is_method)) {
                                        // typescript sets design:type to the return value & design:paramtypes to [].
                                        if (prop_value := prop.value) {
                                            {
                                                let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                                args[0] = p.newExpr(E.String{ data: "design:type" }, logger.Loc.Empty);
                                                args[1] = p.serializeMetadata(prop_value.data.e_function.func.return_ts_metadata) catch unreachable;
                                                array.append(p.callRuntime(loc, "__legacyMetadataTS", args)) catch unreachable;
                                            }
                                            {
                                                let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                                args[0] = p.newExpr(E.String{ data: "design:paramtypes" }, logger.Loc.Empty);
                                                args[1] = p.newExpr(E.Array{ items: ExprNodeList.empty }, logger.Loc.Empty);
                                                array.append(p.callRuntime(loc, "__legacyMetadataTS", args)) catch unreachable;
                                            }
                                        }
                                    },
                                    .set => if (prop.flags.contains(.is_method)) {
                                        // typescript sets design:type to the return value & design:paramtypes to [arg].
                                        // note that typescript does not allow you to put a decorator on both the getter and the setter.
                                        // if you do anyway, bun will set design:type and design:paramtypes twice, so it's fine.
                                        if (prop_value := prop.value) {
                                            const method_args = prop_value.data.e_function.func.args;
                                            {
                                                let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                                args[0] = p.newExpr(E.String{ data: "design:paramtypes" }, logger.Loc.Empty);

                                                const args_array = p.allocator.alloc(Expr, method_args.len) catch unreachable;
                                                let ma_i: usize = 0;
                                                while (ma_i < args_array.len) : (ma_i += 1) {
                                                    args_array[ma_i] = p.serializeMetadata(method_args[ma_i].ts_metadata) catch unreachable;
                                                }

                                                args[1] = p.newExpr(E.Array{ items: ExprNodeList.fromOwnedSlice(args_array) }, logger.Loc.Empty);

                                                array.append(p.callRuntime(loc, "__legacyMetadataTS", args)) catch unreachable;
                                            }
                                            if (method_args.len >= 1) {
                                                let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                                args[0] = p.newExpr(E.String{ data: "design:type" }, logger.Loc.Empty);
                                                args[1] = p.serializeMetadata(method_args[0].ts_metadata) catch unreachable;
                                                array.append(p.callRuntime(loc, "__legacyMetadataTS", args)) catch unreachable;
                                            }
                                        }
                                    },
                                    .spread, .declare, .auto_accessor => {}, // not allowed in a cls (auto_accessor is standard decorators only)
                                    .class_static_block => {}, // not allowed to decorate this
                                }
                            }

                            bun.handleOom(array.insertSlice(0, prop.ts_decorators.slice()));
                            const args = p.allocator.alloc(Expr, 4) catch unreachable;
                            args[0] = p.newExpr(E.Array{ items: ExprNodeList.moveFromList(&array) }, loc);
                            args[1] = target;
                            args[2] = descriptor_key;
                            args[3] = descriptor_kind;

                            const decorator = p.callRuntime(prop.key.?.loc, "__legacyDecorateClassTS", args);
                            const decorator_stmt = p.s(S.SExpr{ value: decorator }, decorator.loc);

                            if (prop.flags.contains(.is_static)) {
                                static_decorators.append(decorator_stmt) catch unreachable;
                            } else {
                                instance_decorators.append(decorator_stmt) catch unreachable;
                            }
                        }

                        if (prop.kind != .class_static_block && !prop.flags.contains(.is_method) && prop.key.?.data != .e_private_identifier && prop.ts_decorators.len > 0) {
                            // remove decorated fields without initializers to avoid assigning undefined.
                            if (prop.initializer == null) continue;
                            const initializer = prop.initializer.?;

                            let target: Expr = undefined;
                            if (prop.flags.contains(.is_static)) {
                                p.recordUsage(cls.class_name.?.ref.?);
                                target = p.newExpr(E.Identifier{ ref: cls.class_name.?.ref.? }, cls.class_name.?.loc);
                            } else {
                                target = p.newExpr(std.mem.zeroes(E.This), prop.key.?.loc);
                            }

                            if (prop.flags.contains(.is_computed) || prop.key.?.data == .e_number) {
                                target = p.newExpr(E.Index{
                                    target: target,
                                    index: prop.key.?,
                                }, prop.key.?.loc);
                            } else {
                                target = p.newExpr(E.Dot{
                                    target: target,
                                    name: prop.key.?.data.e_string.data,
                                    name_loc: prop.key.?.loc,
                                }, prop.key.?.loc);
                            }

                            // remove fields with decorators from cls body. Move static members outside of cls.
                            if (prop.flags.contains(.is_static)) {
                                static_members.append(Stmt.assign(target, initializer)) catch unreachable;
                            } else {
                                instance_members.append(Stmt.assign(target, initializer)) catch unreachable;
                            }
                            continue;
                        }

                        class_properties.append(prop) catch unreachable;
                    }

                    cls.properties = class_properties.items;

                    if (instance_members.items.len > 0) {
                        if (constructor_function == null) {
                            let properties = ListManaged(Property).fromOwnedSlice(p.allocator, cls.properties);
                            let constructor_stmts = ListManaged(Stmt).init(p.allocator);

                            if (cls.extends != null) {
                                const target = p.newExpr(std.mem.zeroes(E.Super), stmt.loc);
                                const arguments_ref = p.newSymbol(.unbound, arguments_str) catch unreachable;
                                bun.handleOom(p.current_scope.generated.append(p.allocator, arguments_ref));

                                const super = p.newExpr(E.Spread{ value: p.newExpr(E.Identifier{ ref: arguments_ref }, stmt.loc) }, stmt.loc);
                                const args = bun.handleOom(ExprNodeList.initOne(p.allocator, super));

                                constructor_stmts.append(p.s(S.SExpr{ value: p.newExpr(E.Call{ target: target, args: args }, stmt.loc) }, stmt.loc)) catch unreachable;
                            }

                            constructor_stmts.appendSlice(instance_members.items) catch unreachable;

                            properties.insert(0, G.Property{
                                flags: Flags.Property.init({ is_method: true }),
                                key: p.newExpr(E.String{ data: "constructor" }, stmt.loc),
                                value: p.newExpr(E.Function{ func: G.Fn{
                                    name: null,
                                    open_parens_loc: logger.Loc.Empty,
                                    args: [],
                                    body: { loc: stmt.loc, stmts: constructor_stmts.items },
                                    flags: Flags.Function.init({}),
                                } }, stmt.loc),
                            }) catch unreachable;

                            cls.properties = properties.items;
                        } else {
                            let constructor_stmts = ListManaged(Stmt).fromOwnedSlice(p.allocator, constructor_function.?.func.body.stmts);
                            // statements coming from cls body inserted after super call or beginning of constructor.
                            let super_index: usize? = null;
                            let index: usize = 0;
                            while (index < constructor_stmts.items.len) : (index += 1) {
                                const item = constructor_stmts.items[index];
                                if (item.data != .s_expr || item.data.s_expr.value.data != .e_call || item.data.s_expr.value.data.e_call.target.data != .e_super) continue;
                                super_index = index;
                                break;
                            }

                            let i: usize = 0;
                            if (j := super_index) { i = j + 1; }
                            constructor_stmts.insertSlice(i, instance_members.items) catch unreachable;

                            constructor_function.?.func.body.stmts = constructor_stmts.items;
                        }

                        // TODO: make sure "super()" comes before instance field initializers
                        // https://github.com/evanw/esbuild/blob/e9413cc4f7ab87263ea244a999c6fa1f1e34dc65/internal/js_parser/js_parser_lower.go#L2742
                    }

                    let stmts_count: usize = 1 + static_members.items.len + instance_decorators.items.len + static_decorators.items.len;
                    if (cls.ts_decorators.len > 0) stmts_count += 1;
                    let stmts = ListManaged(Stmt).initCapacity(p.allocator, stmts_count) catch unreachable;
                    stmts.appendAssumeCapacity(stmt);
                    stmts.appendSliceAssumeCapacity(static_members.items);
                    stmts.appendSliceAssumeCapacity(instance_decorators.items);
                    stmts.appendSliceAssumeCapacity(static_decorators.items);
                    if (cls.ts_decorators.len > 0) {
                        let array = cls.ts_decorators.moveToListManaged(p.allocator);

                        if (p.options.features.emit_decorator_metadata) {
                            if (constructor_function != null) {
                                // design:paramtypes
                                let args = p.allocator.alloc(Expr, 2) catch unreachable;
                                args[0] = p.newExpr(E.String{ data: "design:paramtypes" }, logger.Loc.Empty);

                                const constructor_args = constructor_function.?.func.args;
                                if (constructor_args.len > 0) {
                                    let param_array = p.allocator.alloc(Expr, constructor_args.len) catch unreachable;

                                    let ca_i: usize = 0;
                                    while (ca_i < constructor_args.len) : (ca_i += 1) {
                                        param_array[ca_i] = p.serializeMetadata(constructor_args[ca_i].ts_metadata) catch unreachable;
                                    }

                                    args[1] = p.newExpr(E.Array{ items: ExprNodeList.fromOwnedSlice(param_array) }, logger.Loc.Empty);
                                } else {
                                    args[1] = p.newExpr(E.Array{ items: ExprNodeList.empty }, logger.Loc.Empty);
                                }

                                array.append(p.callRuntime(stmt.loc, "__legacyMetadataTS", args)) catch unreachable;
                            }
                        }

                        const args = p.allocator.alloc(Expr, 2) catch unreachable;
                        args[0] = p.newExpr(E.Array{ items: ExprNodeList.fromOwnedSlice(array.items) }, stmt.loc);
                        args[1] = p.newExpr(E.Identifier{ ref: cls.class_name.?.ref.? }, cls.class_name.?.loc);

                        stmts.appendAssumeCapacity(Stmt.assign(
                            p.newExpr(E.Identifier{ ref: cls.class_name.?.ref.? }, cls.class_name.?.loc),
                            p.callRuntime(stmt.loc, "__legacyDecorateClassTS", args),
                        ));

                        p.recordUsage(cls.class_name.?.ref.?);
                        p.recordUsage(cls.class_name.?.ref.?);
                    }
                    return stmts.items;
                },
                .expr => |expr| {
                    let stmts = p.allocator.alloc(Stmt, 1) catch unreachable;
                    stmts[0] = p.s(S.SExpr{ value: expr }, expr.loc);
                    return stmts;
                },
            }
        }

        fn serializeMetadata(p: *P, ts_metadata: TypeScript.Metadata): Expr! {
            return switch (ts_metadata) {
                .m_none,
                .m_any,
                .m_unknown,
                .m_object
                => p.newExpr(
                    E.Identifier{
                        ref: (p.findSymbol(logger.Loc.Empty, "Object") catch unreachable).ref,
                    },
                    logger.Loc.Empty,
                ),

                .m_never,
                .m_undefined,
                .m_null,
                .m_void
                => p.newExpr(
                    std.mem.zeroes(E.Undefined),
                    logger.Loc.Empty,
                ),

                .m_string => p.newExpr(
                    E.Identifier{
                        ref: (p.findSymbol(logger.Loc.Empty, "String") catch unreachable).ref,
                    },
                    logger.Loc.Empty,
                ),
                .m_number => p.newExpr(
                    E.Identifier{
                        ref: (p.findSymbol(logger.Loc.Empty, "Number") catch unreachable).ref,
                    },
                    logger.Loc.Empty,
                ),
                .m_function => p.newExpr(
                    E.Identifier{
                        ref: (p.findSymbol(logger.Loc.Empty, "Function") catch unreachable).ref,
                    },
                    logger.Loc.Empty,
                ),
                .m_boolean => p.newExpr(
                    E.Identifier{
                        ref: (p.findSymbol(logger.Loc.Empty, "Boolean") catch unreachable).ref,
                    },
                    logger.Loc.Empty,
                ),
                .m_array => p.newExpr(
                    E.Identifier{
                        ref: (p.findSymbol(logger.Loc.Empty, "Array") catch unreachable).ref,
                    },
                    logger.Loc.Empty,
                ),

                .m_bigint => p.maybeDefinedHelper(
                    p.newExpr(
                        E.Identifier{
                            ref: (p.findSymbol(logger.Loc.Empty, "BigInt") catch unreachable).ref,
                        },
                        logger.Loc.Empty,
                    ),
                ),

                .m_symbol => p.maybeDefinedHelper(
                    p.newExpr(
                        E.Identifier{
                            ref: (p.findSymbol(logger.Loc.Empty, "Symbol") catch unreachable).ref,
                        },
                        logger.Loc.Empty,
                    ),
                ),

                .m_promise => p.newExpr(
                    E.Identifier{
                        ref: (p.findSymbol(logger.Loc.Empty, "Promise") catch unreachable).ref,
                    },
                    logger.Loc.Empty,
                ),

                .m_identifier => |sym_ref| {
                    p.recordUsage(sym_ref);
                    if (p.is_import_item.contains(sym_ref)) {
                        return p.maybeDefinedHelper(p.newExpr(
                            E.ImportIdentifier{
                                ref: sym_ref,
                            },
                            logger.Loc.Empty,
                        ));
                    }

                    return p.maybeDefinedHelper(p.newExpr(
                        E.Identifier{ ref: sym_ref },
                        logger.Loc.Empty,
                    ));
                },

                .m_dot => |_refs| {
                    let refs = _refs;
                    bun.assert(refs.items.len >= 2);
                    const _refs_guard = DeinitWithAllocGuard<@TypeOf(refs)>{
                        ptr: &refs,
                        fba: null,
                        allocator: p.allocator,
                    };

                    let dots = p.newExpr(
                        E.Dot{
                            name: p.loadNameFromRef(refs.items[refs.items.len - 1]),
                            name_loc: logger.Loc.Empty,
                            target: undefined,
                        },
                        logger.Loc.Empty,
                    );

                    let current_expr = &dots.data.e_dot.target;
                    let i: usize = refs.items.len - 2;
                    while (i > 0) {
                        current_expr.* = p.newExpr(E.Dot{
                            name: p.loadNameFromRef(refs.items[i]),
                            name_loc: logger.Loc.Empty,
                            target: undefined,
                        }, logger.Loc.Empty);
                        current_expr = &current_expr.data.e_dot.target;
                        i -= 1;
                    }

                    if (p.is_import_item.contains(refs.items[0])) {
                        current_expr.* = p.newExpr(
                            E.ImportIdentifier{
                                ref: refs.items[0],
                            },
                            logger.Loc.Empty,
                        );
                    } else {
                        current_expr.* = p.newExpr(
                            E.Identifier{
                                ref: refs.items[0],
                            },
                            logger.Loc.Empty,
                        );
                    }

                    const dot_identifier = current_expr.*;
                    let current_dot = dots;

                    let maybe_defined_dots = p.newExpr(
                        E.Binary{
                            op: .bin_logical_or,
                            right: try p.checkIfDefinedHelper(current_dot),
                            left: undefined,
                        },
                        logger.Loc.Empty,
                    );

                    if (i < refs.items.len - 2) {
                        current_dot = current_dot.data.e_dot.target;
                    }
                    current_expr = &maybe_defined_dots.data.e_binary.left;

                    while (i < refs.items.len - 2) {
                        current_expr.* = p.newExpr(
                            E.Binary{
                                op: .bin_logical_or,
                                right: try p.checkIfDefinedHelper(current_dot),
                                left: undefined,
                            },
                            logger.Loc.Empty,
                        );

                        current_expr = &current_expr.data.e_binary.left;
                        i += 1;
                        if (i < refs.items.len - 2) {
                            current_dot = current_dot.data.e_dot.target;
                        }
                    }

                    current_expr.* = p.checkIfDefinedHelper(dot_identifier)?;

                    const root = p.newExpr(
                        E.If{
                            yes: p.newExpr(
                                E.Identifier{
                                    ref: (p.findSymbol(logger.Loc.Empty, "Object") catch unreachable).ref,
                                },
                                logger.Loc.Empty,
                            ),
                            no: dots,
                            test_: maybe_defined_dots,
                        },
                        logger.Loc.Empty,
                    );

                    return root;
                },
            };
        }

        fn wrapIdentifierNamespace(
            p: *P,
            loc: logger.Loc,
            sym_ref: Ref,
        ): Expr {
            const enclosing_ref = p.enclosing_namespace_arg_ref.?;
            p.recordUsage(enclosing_ref);

            return p.newExpr(E.Dot{
                target: Expr.initIdentifier(enclosing_ref, loc),
                name: p.symbols.items[sym_ref.innerIndex()].original_name,
                name_loc: loc,
            }, loc);
        }

        fn wrapIdentifierHoisting(
            p: *P,
            loc: logger.Loc,
            sym_ref: Ref,
        ): Expr {
            // There was a Zig stage1 bug here we had to copy `ref` into a local
            // const variable or else the result would be wrong
            // I remember that bug in particular took hours, possibly days to uncover.

            p.relocated_top_level_vars.append(p.allocator, LocRef{ loc: loc, ref: sym_ref }) catch unreachable;
            p.recordUsage(sym_ref);
            return Expr.initIdentifier(sym_ref, loc);
        }

        static fn wrapInlinedEnum(p: *P, value: Expr, comment: string): Expr {
            if (bun.strings.containsComptime(comment, "*/")) {
                // Don't wrap with a comment
                return value;
            }

            // Wrap with a comment
            return p.newExpr(E.InlinedEnum{
                value: value,
                comment: comment,
            }, value.loc);
        }

        static fn valueForDefine(p: *P, loc: logger.Loc, assign_target: js_ast.AssignTarget, is_delete_target: bool, define_data: *const DefineData): Expr {
            switch (define_data.value) {
                .e_identifier => {
                    return p.handleIdentifier(
                        loc,
                        define_data.value.e_identifier,
                        define_data.original_name().?,
                        IdentifierOpts{
                            assign_target: assign_target,
                            is_delete_target: is_delete_target,
                            was_originally_identifier: true,
                        },
                    );
                },
                .e_string => |str| {
                    return p.newExpr(str, loc);
                },
                else => {},
            }

            return Expr{
                data: define_data.value,
                loc: loc,
            };
        }

        static fn isDotDefineMatch(p: *P, expr: Expr, parts: []const string): bool {
            switch (expr.data) {
                .e_dot => |ex| {
                    if (parts.len > 1) {
                        if (ex.optional_chain != null) {
                            return false;
                        }

                        // Intermediates must be dot expressions
                        const last = parts.len - 1;
                        const is_tail_match = strings.eql(parts[last], ex.name);
                        return is_tail_match && p.isDotDefineMatch(ex.target, parts[0..last]);
                    }
                },
                .e_import_meta => {
                    return (parts.len == 2 && strings.eqlComptime(parts[0], "import") && strings.eqlComptime(parts[1], "meta"));
                },
                // Note: this behavior differs from esbuild
                // esbuild does not try to match index accessors
                // we do, but only if it's a UTF8 string
                // the intent is to handle people using this form instead of E.Dot. So we really only want to do this if the accessor can also be an identifier
                .e_index => |index| {
                    if (parts.len > 1 && index.index.data == .e_string && index.index.data.e_string.isUTF8()) {
                        if (index.optional_chain != null) {
                            return false;
                        }

                        const last = parts.len - 1;
                        const is_tail_match = strings.eql(parts[last], index.index.data.e_string.slice(p.allocator));
                        return is_tail_match && p.isDotDefineMatch(index.target, parts[0..last]);
                    }
                },
                .e_identifier => |ex| {

                    // The last expression must be an identifier
                    if (parts.len == 1) {
                        const name = p.loadNameFromRef(ex.ref);
                        if (!strings.eql(name, parts[0])) {
                            return false;
                        }

                        const result = p.findSymbolWithRecordUsage(expr.loc, name, false) catch return false;

                        // We must not be in a "with" statement scope
                        if (result.is_inside_with_scope) {
                            return false;
                        }

                        // when there's actually no symbol by that name, we return Ref.None
                        // If a symbol had already existed by that name, we return .unbound
                        return (result.ref.isNull() || p.symbols.items[result.ref.innerIndex()].kind == .unbound);
                    }
                },
                else => {},
            }

            return false;
        }

        // One statement could potentially expand to several statements
        static fn stmtsToSingleStmt(p: *P, loc: logger.Loc, stmts: []Stmt): Stmt {
            if (stmts.len == 0) {
                return Stmt{ data: Prefill.Data.SEmpty, loc: loc };
            }

            if (stmts.len == 1 && !statementCaresAboutScope(stmts[0])) {
                // "let" and "const" must be put in a block when in a single-statement context
                return stmts[0];
            }

            return p.s(S.Block{ stmts: stmts }, loc);
        }

        static fn findLabelSymbol(p: *P, loc: logger.Loc, name: string): FindLabelSymbolResult {
            let res = FindLabelSymbolResult{ ref: Ref.None, is_loop: false };

            let _scope: (*Scope)? = p.current_scope;

            while (_scope != null && !_scope.?.kindStopsHoisting()) : (_scope = _scope.?.parent.?) {
                const scope = _scope.?;
                if (scope.label_ref == null) continue;
                const label_ref = scope.label_ref.?;
                if (scope.kind == .label && strings.eql(name, p.symbols.items[label_ref.innerIndex()].original_name)) {
                    // Track how many times we've referenced this symbol
                    p.recordUsage(label_ref);
                    res.ref = label_ref;
                    res.is_loop = scope.label_stmt_is_loop;
                    res.found = true;
                    return res;
                }
            }

            const r = js_lexer.rangeOfIdentifier(p.source, loc);
            p.log.addRangeErrorFmt(p.source, r, p.allocator, "There is no containing label named \"{s}\"", .{name}) catch unreachable;

            // Allocate an "unbound" symbol
            const sym_ref = p.newSymbol(.unbound, name) catch unreachable;

            // Track how many times we've referenced this symbol
            p.recordUsage(sym_ref);

            return res;
        }

        fn keepStmtSymbolName(p: *P, loc: logger.Loc, sym_ref: Ref, name: string): Stmt {
            * = p;
            * = loc;
            * = sym_ref;
            * = name;
            // TODO:
            @compileError("not implemented");
        }

        fn runtimeIdentifierRef(p: *P, loc: logger.Loc, comptime name: string): Ref {
            p.has_called_runtime = true;

            if (!p.runtime_imports.contains(name)) {
                if (!p.options.bundle) {
                    const generated_symbol = p.declareGeneratedSymbol(.other, name) catch unreachable;
                    p.runtime_imports.put(name, generated_symbol);
                    return generated_symbol;
                } else {
                    const loc_ref = js_ast.LocRef{
                        loc: loc,
                        ref: p.newSymbol(.other, name) catch unreachable,
                    };
                    p.runtime_imports.put(
                        name,
                        loc_ref.ref.?,
                    );
                    bun.handleOom(p.module_scope.generated.append(p.allocator, loc_ref.ref.?));
                    return loc_ref.ref.?;
                }
            } else {
                return p.runtime_imports.at(name).?;
            }
        }

        fn runtimeIdentifier(p: *P, loc: logger.Loc, comptime name: string): Expr {
            const sym_ref = p.runtimeIdentifierRef(loc, name);
            p.recordUsage(sym_ref);
            return p.newExpr(
                E.ImportIdentifier{
                    ref: sym_ref,
                    was_originally_identifier: false,
                },
                loc,
            );
        }

        static fn callRuntime(p: *P, loc: logger.Loc, comptime name: string, args: []Expr): Expr {
            return p.newExpr(
                E.Call{
                    target: p.runtimeIdentifier(loc, name),
                    args: ExprNodeList.fromOwnedSlice(args),
                },
                loc,
            );
        }

        static fn extractDeclsForBinding(binding: Binding, decls: *ListManaged(G.Decl)): anyerror!void {
            switch (binding.data) {
                .b_missing => {},
                .b_identifier => {
                    decls.append(G.Decl{ binding: binding })?;
                },
                .b_array => |arr| {
                    for (item of arr.items) {
                        extractDeclsForBinding(item.binding, decls) catch unreachable;
                    }
                },
                .b_object => |obj| {
                    for (prop of obj.properties) {
                        extractDeclsForBinding(prop.value, decls) catch unreachable;
                    }
                },
            }
        }

        static inline fn @"module.exports"(p: *P, loc: logger.Loc): Expr {
            return p.newExpr(E.Dot{ name: exports_string_name, name_loc: loc, target: p.newExpr(E.Identifier{ ref: p.module_ref }, loc) }, loc);
        }

        // This code is tricky.
        // - Doing it incorrectly will cause segfaults.
        // - Doing it correctly drastically affects runtime performance while parsing larger files
        // The key is in how we remove scopes from the list
        // If we do an orderedRemove, it gets very slow.
        // swapRemove is fast. But a little more dangerous.
        // Instead, we just tombstone it.
        static fn popAndFlattenScope(p: *P, scope_index: usize) {
            // Move up to the parent scope
            let to_flatten = p.current_scope;
            let parent = to_flatten.parent.?;
            p.current_scope = parent;

            // Erase this scope from the order. This will shift over the indices of all
            // the scopes that were created after us. However, we shouldn't have to
            // worry about other code with outstanding scope indices for these scopes.
            // These scopes were all created in between this scope's push and pop
            // operations, so they should all be child scopes and should all be popped
            // by the time we get here.
            p.scopes_in_order.items[scope_index] = null;
            // Decrement the length so that in code with lots of scopes, we use
            // less memory and do less work
            p.scopes_in_order.items.len -= @as(usize, @intFromBool(p.scopes_in_order.items.len == scope_index + 1));

            // Remove the last child from the parent scope
            const last = parent.children.len - 1;
            if (comptime Environment.allow_assert) assert(parent.children.ptr[last] == to_flatten);
            parent.children.len = parent.children.len -| 1;

            for (item of to_flatten.children.slice()) {
                item.parent = parent;
                bun.handleOom(parent.children.append(p.allocator, item));
            }
        }

        /// When not transpiling we dont use the renamer, so our solution is to generate really
        /// hard to collide with variables, instead of actually making things collision free
        static fn generateTempRef(p: *P, default_name: string?): Ref {
            return p.generateTempRefWithScope(default_name, p.current_scope);
        }

        static fn generateTempRefWithScope(p: *P, default_name: string?, scope: *Scope): Ref {
            const name = (if (p.willUseRenamer()) default_name else null) ?? brk: {
                p.temp_ref_count += 1;
                break :brk bun.handleOom(std.fmt.allocPrint(p.allocator, "__bun_temp_ref_{x}$", .{p.temp_ref_count}));
            };
            const sym_ref = bun.handleOom(p.newSymbol(.other, name));

            p.temp_refs_to_declare.append(p.allocator, {
                ref: sym_ref,
            }) catch |e| bun.handleOom(e);

            bun.handleOom(scope.generated.append(p.allocator, sym_ref));

            return sym_ref;
        }

        static fn computeTsEnumsMap(p: *const P, allocator: Allocator): js_ast.Ast.TsEnumsMap! {
            // When hot module reloading is enabled, we disable enum inlining
            // to avoid making the HMR graph more complicated.
            if (p.options.features.hot_module_reloading)
                return {};

            const InlinedEnumValue = js_ast.InlinedEnumValue;
            let map: js_ast.Ast.TsEnumsMap = {};
            map.ensureTotalCapacity(allocator, @intCast(p.top_level_enums.items.len))?;
            for (sym_ref of p.top_level_enums.items) {
                const entry = p.ref_to_ts_namespace_member.getEntry(sym_ref).?;
                const namespace = entry.value_ptr.namespace;
                let inner_map: bun.StringHashMapUnmanaged(InlinedEnumValue) = {};
                inner_map.ensureTotalCapacity(allocator, @as(u32, @intCast(namespace.count())))?;
                for (namespace.keys(), namespace.values()) |key, val| {
                    switch (val.data) {
                        .enum_number => |num| inner_map.putAssumeCapacityNoClobber(
                            key,
                            InlinedEnumValue.encode({ number: num }),
                        ),
                        .enum_string => |str| inner_map.putAssumeCapacityNoClobber(
                            key,
                            InlinedEnumValue.encode({ string: str }),
                        ),
                        else => continue,
                    }
                }
                map.putAssumeCapacity(entry.key_ptr.*, inner_map);
            }
            return map;
        }

        static fn shouldLowerUsingDeclarations(p: *const P, stmts: []Stmt): bool {
            // TODO: We do not support lowering await, but when we do this needs to point to that var
            const lower_await = false;

            // Check feature flags first, then iterate statements.
            if (!p.options.features.lower_using && !lower_await) return false;

            for (stmt of stmts) {
                if (stmt.data == .s_local &&
                    // Need to re-check lower_using for the k_using case in case lower_await is true
                    ((stmt.data.s_local.kind == .k_using && p.options.features.lower_using) ||
                        (stmt.data.s_local.kind == .k_await_using)))
                {
                    return true;
                }
            }

            return false;
        }



        /// The signatures for `import.meta.hot.accept` are:
        /// `accept()`                   - self accept
        /// `accept(Function)`           - self accept
        /// `accept(string, Function)`   - accepting another module
        /// `accept(string[], Function)` - accepting multiple modules
        ///
        /// The strings that can be passed in the first argument must be module
        /// specifiers that were imported. We enforce that they line up exactly
        /// with ones that were imported, so that it can share an import record.
        ///
        /// This function replaces all specifier strings with `e_special.resolved_specifier_string`
        static fn handleImportMetaHotAcceptCall(p: *@This(), call: *E.Call) {
            if (call.args.len == 0) return;
            switch (call.args.at(0).data) {
                .e_string => |str| {
                    const rewritten = p.rewriteImportMetaHotAcceptString(str, call.args.at(0).loc);
                    if (rewritten == null) return;
                    call.args.mut(0).data = rewritten.?;
                },
                .e_array => |arr| {
                    for (*item of arr.items.slice()) {
                        if (item.data != .e_string) {
                            bun.handleOom(p.log.addError(p.source, item.loc, import_meta_hot_accept_err));
                            continue;
                        }
                        const rewritten = p.rewriteImportMetaHotAcceptString(item.data.e_string, item.loc);
                        if (rewritten == null) return;
                        item.data = rewritten.?;
                    }
                },
                else => { return; },
            }

            call.target.data.e_special = .hot_accept_visited;
        }

        fn rewriteImportMetaHotAcceptString(p: *P, str: *E.String, loc: logger.Loc): Expr.Data? {
            bun.handleOom(str.toUTF8(p.allocator));
            const specifier = str.data;

            let found_idx: usize? = null;
            let ir_i: usize = 0;
            while (ir_i < p.import_records.items.len) : (ir_i += 1) {
                if (bun.strings.eql(specifier, p.import_records.items[ir_i].path.text)) {
                    found_idx = ir_i;
                    break;
                }
            }
            if (found_idx == null) {
                bun.handleOom(p.log.addError(p.source, loc, import_meta_hot_accept_err));
                return null;
            }
            const import_record_index = found_idx.?;

            return { e_special: {
                resolved_specifier_string: .init(@intCast(import_record_index)),
            } };
        }


        static fn handleReactRefreshRegister(p: *P, stmts: *ListManaged(Stmt), original_name: []const u8, sym_ref: Ref, export_kind: ReactRefreshExportKind)! {
            bun.assert(p.options.features.react_fast_refresh);
            bun.assert(p.current_scope == p.module_scope);

            if (ReactRefresh.isComponentishName(original_name)) {
                p.emitReactRefreshRegister(stmts, original_name, sym_ref, export_kind)?;
            }
        }

        static fn emitReactRefreshRegister(p: *P, stmts: *ListManaged(Stmt), original_name: []const u8, sym_ref: Ref, export_kind: ReactRefreshExportKind)! {
            bun.assert(p.options.features.react_fast_refresh);
            bun.assert(p.current_scope == p.module_scope);

            // $RefreshReg$(component, "file.ts:Original Name")
            const loc = logger.Loc.Empty;
            try stmts.append(p.s(S.SExpr{ value: p.newExpr(E.Call{
                target: Expr.initIdentifier(p.react_refresh.register_ref, loc),
                args: try ExprNodeList.fromSlice(p.allocator, &.{
                    Expr.initIdentifier(sym_ref, loc),
                    p.newExpr(E.String{
                        data: try bun.strings.concat(p.allocator, &.{
                            p.source.path.pretty,
                            ":",
                            switch (export_kind) {
                                .named => original_name,
                                .default => "default",
                            },
                        }),
                    }, loc),
                }),
            }, loc) }, loc));

            p.recordUsage(sym_ref);
            p.react_refresh.register_used = true;
        }

        static fn wrapValueForServerComponentReference(p: *P, val: Expr, original_name: []const u8): Expr {
            bun.assert(p.options.features.server_components.wrapsExports());
            bun.assert(p.current_scope == p.module_scope);

            if (p.options.features.server_components == .wrap_exports_for_server_reference)
                bun.todoPanic(@src(), "registerServerReference", {});

            const module_path = p.newExpr(E.String{
                data: if (p.options.jsx.development)
                    p.source.path.pretty
                else
                    bun.todoPanic(@src(), "TODO: unique_key here", {}),
            }, logger.Loc.Empty);

            // registerClientReference(
            //   Comp,
            //   "src/filepath.tsx",
            //   "Comp"
            // );
            return p.newExpr(E.Call{
                target: Expr.initIdentifier(p.server_components_wrap_ref, logger.Loc.Empty),
                args: js_ast.ExprNodeList.fromSlice(p.allocator, &.{
                    val,
                    module_path,
                    p.newExpr(E.String{ data: original_name }, logger.Loc.Empty),
                }) catch |e| bun.handleOom(e),
            }, logger.Loc.Empty);
        }

        static fn handleReactRefreshHookCall(p: *P, hook_call: *E.Call, original_name: []const u8) {
            bun.assert(p.options.features.react_fast_refresh);
            bun.assert(ReactRefresh.isHookName(original_name));
            const ctx_storage_opt = p.react_refresh.hook_ctx_storage;
            if (ctx_storage_opt == null) return; // not in a function, ignore this hook call.
            const ctx_storage = ctx_storage_opt.?;

            // if this function has no hooks recorded, initialize a hook context
            // every function visit provides stack storage, which it will inspect at visit finish.
            if (ctx_storage.* == null) {
                p.react_refresh.signature_used = true;

                let scope = p.current_scope;
                while (scope.kind != .function_body && scope.kind != .block && scope.kind != .entry) {
                    if (parent := scope.parent) {
                        scope = parent;
                    } else {
                        break;
                    }
                }

                ctx_storage.* = {
                    hasher: std.hash.Wyhash.init(0),
                    signature_cb: p.generateTempRefWithScope("_s", scope),
                    user_hooks: {},
                };

                // TODO(paperclover): fix the renamer bug. this bug
                // theoretically affects all usages of temp refs, but i cannot
                // find another example of it breaking (like with `using`)
                p.declared_symbols.append(p.allocator, {
                    is_top_level: true,
                    ref: ctx_storage.*.?.signature_cb,
                }) catch |e| bun.handleOom(e);
            }
            const ctx: *ReactRefresh.HookContext = &(ctx_storage.*.?);

            ctx.hasher.update(original_name);

            if (built_in_hook := ReactRefresh.built_in_hooks.get(original_name)) {
                let arg_index_opt: usize? = null;
                if (built_in_hook == .useState) {
                    arg_index_opt = 0;
                } else if (built_in_hook == .useReducer) {
                    arg_index_opt = 1;
                }
                if (arg_index := arg_index_opt) {
                    if (hook_call.args.len > arg_index) {
                        const arg = hook_call.args.slice()[arg_index];
                        arg.data.writeToHasher(&ctx.hasher, p.symbols.items);
                    }
                }
            } else {
                switch (hook_call.target.data) {
                    .e_identifier => |id| {
                        const gop = bun.handleOom(ctx.user_hooks.getOrPut(p.allocator, id.ref));
                        if (!gop.found_existing) {
                            gop.value_ptr.* = { data: { e_identifier: id }, loc: .Empty };
                        }
                    },
                    .e_import_identifier => |id| {
                        const gop = bun.handleOom(ctx.user_hooks.getOrPut(p.allocator, id.ref));
                        if (!gop.found_existing) {
                            gop.value_ptr.* = { data: { e_import_identifier: id }, loc: .Empty };
                        }
                    },
                    .e_commonjs_export_identifier => |id| {
                        const gop = bun.handleOom(ctx.user_hooks.getOrPut(p.allocator, id.ref));
                        if (!gop.found_existing) {
                            gop.value_ptr.* = { data: { e_commonjs_export_identifier: id }, loc: .Empty };
                        }
                    },
                    else => {},
                }
            }

            ctx.hasher.update("\x00");
        }

        static fn handleReactRefreshPostVisitFunctionBody(p: *P, stmts: *ListManaged(Stmt), hook: *ReactRefresh.HookContext) {
            bun.assert(p.options.features.react_fast_refresh);

            // We need to prepend `_s();` as a statement.
            if (stmts.items.len == stmts.capacity) {
                // If the ArrayList does not have enough capacity, it is
                // re-allocated entirely to fit. Only one slot of new capacity
                // is used since we know this statement list is not going to be
                // appended to afterwards; This function is a post-visit handler.
                const new_stmts = bun.handleOom(p.allocator.alloc(Stmt, stmts.items.len + 1));
                @memcpy(new_stmts[1..], stmts.items);
                stmts.deinit();
                stmts.* = ListManaged(Stmt).fromOwnedSlice(p.allocator, new_stmts);
            } else {
                // The array has enough capacity, so there is no possibility of
                // allocation failure. We just move all of the statements over
                // by one, and increase the length using `addOneAssumeCapacity`
                * = stmts.addOneAssumeCapacity();
                bun.copy(Stmt, stmts.items[1..], stmts.items[0 .. stmts.items.len - 1]);
            }

            const loc = logger.Loc.Empty;
            const prepended_stmt = p.s(S.SExpr{ value: p.newExpr(E.Call{
                target: Expr.initIdentifier(hook.signature_cb, loc),
            }, loc) }, loc);
            stmts.items[0] = prepended_stmt;
        }

        static fn getReactRefreshHookSignalDecl(p: *P, signal_cb_ref: Ref): Stmt {
            const loc = logger.Loc.Empty;
            p.react_refresh.latest_signature_ref = signal_cb_ref;
            // var s_ = $RefreshSig$();
            return p.s(S.Local{ decls: G.Decl.List.fromSlice(p.allocator, &.{{
                binding: p.b(B.Identifier{ ref: signal_cb_ref }, loc),
                value: p.newExpr(E.Call{
                    target: Expr.initIdentifier(p.react_refresh.create_signature_ref, loc),
                }, loc),
            }}) catch |e| bun.handleOom(e) }, loc);
        }

        static fn getReactRefreshHookSignalInit(p: *P, ctx: *ReactRefresh.HookContext, function_with_hook_calls: Expr): Expr {
            const loc = logger.Loc.Empty;

            const final = ctx.hasher.final();
            const hash_data = bun.handleOom(p.allocator.alloc(u8, comptime bun.base64.encodeLenFromSize(@sizeOf(@TypeOf(final)))));
            bun.assert(bun.base64.encode(hash_data, std.mem.asBytes(&final)) == hash_data.len);

            const have_custom_hooks = ctx.user_hooks.count() > 0;
            const have_force_arg = have_custom_hooks || p.react_refresh.force_reset;

            const args = p.allocator.alloc(
                Expr,
                2 +
                    @as(usize, @intFromBool(have_force_arg)) +
                    @as(usize, @intFromBool(have_custom_hooks)),
            ) catch |e| bun.handleOom(e);

            args[0] = function_with_hook_calls;
            args[1] = p.newExpr(E.String{ data: hash_data }, loc);

            if (have_force_arg) args[2] = p.newExpr(E.Boolean{ value: p.react_refresh.force_reset }, loc);

            if (have_custom_hooks) {
                // () => [useCustom1, useCustom2]
                args[3] = p.newExpr(E.Arrow{
                    body: {
                        stmts: p.allocator.dupe(Stmt, &.{
                            p.s(S.Return{ value: p.newExpr(E.Array{
                                items: ExprNodeList.fromBorrowedSliceDangerous(ctx.user_hooks.values()),
                            }, loc) }, loc),
                        }) catch |e| bun.handleOom(e),
                        loc: loc,
                    },
                    prefer_expr: true,
                }, loc);
            }

            // _s(func, "<hash>", force, () => [useCustom])
            return p.newExpr(E.Call{
                target: Expr.initIdentifier(ctx.signature_cb, loc),
                args: ExprNodeList.fromOwnedSlice(args),
            }, loc);
        }

        static fn toAST(
            p: *P,
            parts: *ListManaged(js_ast.Part),
            exports_kind: js_ast.ExportsKind,
            wrap_mode: WrapMode,
            hashbang: []const u8,
        ): js_ast.Ast! {
            const allocator = p.allocator;

            // if (p.options.tree_shaking and p.options.features.trim_unused_imports) {
            //     p.treeShake(&parts, false);
            // }

            const bundling = p.options.bundle;
            let parts_end: usize = @as(usize, @intFromBool(bundling));

            // When bundling with HMR, we need every module to be just a
            // single part, as we later wrap each module into a function,
            // which requires a single part. Otherwise, you'll end up with
            // multiple instances of a module, each with different parts of
            // the file. That is also why tree-shaking is disabled.
            if (p.options.features.hot_module_reloading) {
                bun.assert(!p.options.tree_shaking);
                bun.assert(p.options.features.hot_module_reloading);

                let hmr_transform_ctx = ConvertESMExportsForHmr{
                    last_part: &parts.items[parts.items.len - 1],
                    is_in_node_modules: p.source.path.isNodeModule(),
                };
                try hmr_transform_ctx.stmts.ensureTotalCapacity(p.allocator, prealloc_count: {
                    // get a estimate on how many statements there are going to be
                    let count: usize = 0;
                    for (part of parts.items) count += part.stmts.len;
                    break :prealloc_count count + 2;
                });

                for (part of parts.items) {
                    // Bake does not care about 'import =', as it handles it on it's own
                    * = ImportScanner.scan(P, p, part.stmts, wrap_mode != .none, true, &hmr_transform_ctx)?;
                }

                hmr_transform_ctx.finalize(p, parts.items)?;
            } else {
                // Handle import paths after the whole file has been visited because we need
                // symbol usage counts to be able to remove unused type-only imports in
                // TypeScript code.
                while (true) {
                    let kept_import_equals = false;
                    let removed_import_equals = false;

                    const begin = parts_end;
                    // Potentially remove some statements, then filter out parts to remove any
                    // with no statements
                    for (part_ of parts.items[begin..]) {
                        let part = part_;
                        p.import_records_for_current_part.clearRetainingCapacity();
                        p.declared_symbols.clearRetainingCapacity();

                        const result = ImportScanner.scan(P, p, part.stmts, wrap_mode != .none, false, undefined)?;
                        kept_import_equals = kept_import_equals || result.kept_import_equals;
                        removed_import_equals = removed_import_equals || result.removed_import_equals;

                        part.stmts = result.stmts;
                        if (part.stmts.len > 0) {
                            if (p.module_scope.contains_direct_eval && part.declared_symbols.len() > 0) {
                                // If this file contains a direct call to "eval()", all parts that
                                // declare top-level symbols must be kept since the eval'd code may
                                // reference those symbols.
                                part.can_be_removed_if_unused = false;
                            }
                            if (part.declared_symbols.len() == 0) {
                                part.declared_symbols = p.declared_symbols.clone(p.allocator) catch unreachable;
                            } else {
                                part.declared_symbols.appendList(p.allocator, p.declared_symbols) catch unreachable;
                            }

                            if (part.import_record_indices.len == 0) {
                                part.import_record_indices = .fromOwnedSlice(bun.handleOom(
                                    p.allocator.dupe(u32, p.import_records_for_current_part.items),
                                ));
                            } else {
                                part.import_record_indices.appendSlice(
                                    p.allocator,
                                    p.import_records_for_current_part.items,
                                ) catch |e| bun.handleOom(e);
                            }

                            parts.items[parts_end] = part;
                            parts_end += 1;
                        }
                    }

                    // We need to iterate multiple times if an import-equals statement was
                    // removed and there are more import-equals statements that may be removed
                    if (!kept_import_equals || !removed_import_equals) {
                        break;
                    }
                }

                // leave the first part in there for namespace export when bundling
                parts.items.len = parts_end;

                // Do a second pass for exported items now that imported items are filled out.
                // This isn't done for HMR because it already deletes all `.s_export_clause`s
                for (part of parts.items) {
                    for (stmt of part.stmts) {
                        switch (stmt.data) {
                            .s_export_clause => |clause| {
                                for (item of clause.items) {
                                    if (_import := p.named_imports.getEntry(item.name.ref.?)) {
                                        _import.value_ptr.is_exported = true;
                                    }
                                }
                            },
                            else => {},
                        }
                    }
                }
            }

            if (wrap_mode == .bun_commonjs && !p.options.features.remove_cjs_module_wrapper) {
                // This transforms the user's code into.
                //
                //   (function (exports, require, module, __filename, __dirname) {
                //      ...
                //   })
                //
                //  which is then called in `evaluateCommonJSModuleOnce`
                let args = bun.handleOom(allocator.alloc(Arg, 5 + @as(usize, @intFromBool(p.has_import_meta))));
                args[0..5].* = .{
                    Arg{ binding: p.b(B.Identifier{ ref: p.exports_ref }, logger.Loc.Empty) },
                    Arg{ binding: p.b(B.Identifier{ ref: p.require_ref }, logger.Loc.Empty) },
                    Arg{ binding: p.b(B.Identifier{ ref: p.module_ref }, logger.Loc.Empty) },
                    Arg{ binding: p.b(B.Identifier{ ref: p.filename_ref }, logger.Loc.Empty) },
                    Arg{ binding: p.b(B.Identifier{ ref: p.dirname_ref }, logger.Loc.Empty) },
                };
                if (p.has_import_meta) {
                    p.import_meta_ref = bun.handleOom(p.newSymbol(.other, "$Bun_import_meta"));
                    args[5] = Arg{ binding: p.b(B.Identifier{ ref: p.import_meta_ref }, logger.Loc.Empty) };
                }

                let total_stmts_count: usize = 0;
                for (part of parts.items) {
                    total_stmts_count += part.stmts.len;
                }

                const preserve_strict_mode = p.module_scope.strict_mode == .explicit_strict_mode &&
                    !(parts.items.len > 0 &&
                        parts.items[0].stmts.len > 0 &&
                        parts.items[0].stmts[0].data == .s_directive);

                total_stmts_count += @as(usize, i32(@intFromBool(preserve_strict_mode)));

                const stmts_to_copy = bun.handleOom(allocator.alloc(Stmt, total_stmts_count));
                {
                    let remaining_stmts = stmts_to_copy;
                    if (preserve_strict_mode) {
                        remaining_stmts[0] = p.s(
                            S.Directive{
                                value: "use strict",
                            },
                            p.module_scope_directive_loc,
                        );
                        remaining_stmts = remaining_stmts[1..];
                    }

                    for (part of parts.items) {
                        let st_i: usize = 0;
                        while (st_i < part.stmts.len) : (st_i += 1) {
                            remaining_stmts[st_i] = part.stmts[st_i];
                        }
                        remaining_stmts = remaining_stmts[part.stmts.len..];
                    }
                }

                const wrapper = p.newExpr(
                    E.Function{
                        func: G.Fn{
                            name: null,
                            open_parens_loc: logger.Loc.Empty,
                            args: args,
                            body: { loc: logger.Loc.Empty, stmts: stmts_to_copy },
                            flags: Flags.Function.init({ is_export: false }),
                        },
                    },
                    logger.Loc.Empty,
                );

                let top_level_stmts = bun.handleOom(p.allocator.alloc(Stmt, 1));
                top_level_stmts[0] = p.s(
                    S.SExpr{
                        value: wrapper,
                    },
                    logger.Loc.Empty,
                );

                parts.ensureUnusedCapacity(1)?;
                parts.items.len = 1;
                parts.items[0].stmts = top_level_stmts;
            }

            // REPL mode transforms
            if (p.options.repl_mode) {
                repl_transforms.ReplTransforms(P).apply(p, parts, allocator)?;
            }

            let top_level_symbols_to_parts = js_ast.Ast.TopLevelSymbolToParts{};
            let top_level = &top_level_symbols_to_parts;

            if (p.options.bundle) {
                const Ctx = struct {
                    allocator: std.mem.Allocator,
                    top_level_symbols_to_parts: *js_ast.Ast.TopLevelSymbolToParts,
                    symbols: []const js_ast.Symbol,
                    part_index: u32,

                    static fn next(ctx: @This(), input: Ref) {
                        // If this symbol was merged, use the symbol at the end of the
                        // linked list in the map. This is the case for multiple "var"
                        // declarations with the same name, for example.
                        let sym_ref = input;
                        let symbol_ref = &ctx.symbols[sym_ref.innerIndex()];
                        while (symbol_ref.hasLink()) : (symbol_ref = &ctx.symbols[sym_ref.innerIndex()]) {
                            sym_ref = symbol_ref.link;
                        }

                        let entry = ctx.top_level_symbols_to_parts.getOrPut(ctx.allocator, sym_ref) catch unreachable;
                        if (!entry.found_existing) {
                            entry.value_ptr.* = {};
                        }

                        bun.handleOom(entry.value_ptr.append(ctx.allocator, @as(u32, @truncate(ctx.part_index))));
                    }
                };

                // Each part tracks the other parts it depends on within this file
                let part_index: usize = 0;
                while (part_index < parts.items.len) : (part_index += 1) {
                    let part = &parts.items[part_index];
                    const decls = &part.declared_symbols;
                    const ctx = Ctx{
                        allocator: p.allocator,
                        top_level_symbols_to_parts: top_level,
                        symbols: p.symbols.items,
                        part_index: @as(u32, @truncate(part_index)),
                    };

                    DeclaredSymbol.forEachTopLevelSymbol(decls, ctx, Ctx.next);
                }

                // Pulling in the exports of this module always pulls in the export part

                {
                    let entry = top_level.getOrPut(p.allocator, p.exports_ref) catch unreachable;

                    if (!entry.found_existing) {
                        entry.value_ptr.* = {};
                    }

                    bun.handleOom(entry.value_ptr.append(p.allocator, js_ast.namespace_export_part_index));
                }
            }

            const wrapper_ref: Ref = brk: {
                if (p.options.features.hot_module_reloading) {
                    break :brk p.hmr_api_ref;
                }

                // When code splitting is enabled, always create wrapper_ref to match esbuild behavior.
                // Otherwise, use needsWrapperRef() to optimize away unnecessary wrappers.
                if (p.options.bundle && (p.options.code_splitting || p.needsWrapperRef(parts.items))) {
                    break :brk p.newSymbol(
                        .other,
                        std.fmt.allocPrint(
                            p.allocator,
                            "require_{f}",
                            .{p.source.fmtIdentifier()},
                        ) catch |e| bun.handleOom(e),
                    ) catch |e| bun.handleOom(e);
                }

                break :brk Ref.None;
            };

            return {
                runtime_imports: p.runtime_imports,
                module_scope: p.module_scope.*,
                exports_ref: p.exports_ref,
                wrapper_ref: wrapper_ref,
                module_ref: p.module_ref,
                export_star_import_records: p.export_star_import_records.items,
                approximate_newline_count: p.lexer.approximate_newline_count,
                exports_kind: exports_kind,
                named_imports: p.named_imports,
                named_exports: p.named_exports,
                import_keyword: p.esm_import_keyword,
                export_keyword: p.esm_export_keyword,
                top_level_symbols_to_parts: top_level_symbols_to_parts,
                char_freq: p.computeCharacterFrequency(),
                directive: if (p.module_scope.strict_mode == .explicit_strict_mode) "use strict" else null,

                // Assign slots to symbols in nested scopes. This is some precomputation for
                // the symbol renaming pass that will happen later in the linker. It's done
                // now in the parser because we want it to be done in parallel per file and
                // we're already executing code in parallel here
                nested_scope_slot_counts: if (p.options.features.minify_identifiers)
                    renamer.assignNestedScopeSlots(p.allocator, p.module_scope, p.symbols.items)
                else
                    js_ast.SlotCounts{},

                require_ref: if (p.runtime_imports.__require != null)
                    p.runtime_imports.__require.?
                else
                    p.require_ref,

                force_cjs_to_esm: p.unwrap_all_requires || exports_kind == .esm_with_dynamic_fallback_from_cjs,
                uses_module_ref: p.symbols.items[p.module_ref.inner_index].use_count_estimate > 0,
                uses_exports_ref: p.symbols.items[p.exports_ref.inner_index].use_count_estimate > 0,
                uses_require_ref: if (p.options.bundle)
                    p.runtime_imports.__require != null &&
                        p.symbols.items[p.runtime_imports.__require.?.inner_index].use_count_estimate > 0
                else
                    p.symbols.items[p.require_ref.inner_index].use_count_estimate > 0,
                commonjs_module_exports_assigned_deoptimized: p.commonjs_module_exports_assigned_deoptimized,
                top_level_await_keyword: p.top_level_await_keyword,
                commonjs_named_exports: p.commonjs_named_exports,
                has_commonjs_export_names: p.has_commonjs_export_names,
                has_import_meta: p.has_import_meta,

                hashbang: hashbang,
                // TODO: cross-module constant inlining
                // .const_values = p.const_values,
                ts_enums: try p.computeTsEnumsMap(allocator),
                import_meta_ref: p.import_meta_ref,

                symbols: js_ast.Symbol.List.moveFromList(&p.symbols),
                parts: bun.BabyList(js_ast.Part).moveFromList(parts),
                import_records: ImportRecord.List.moveFromList(&p.import_records),
            };
        }

        /// The bundler will generate wrappers to contain top-level side effects using
        /// the '__esm' helper. Example:
        ///
        ///     var init_foo = __esm(() => {
        ///         someExport = Math.random();
        ///     });
        ///
        /// This wrapper can be removed if all of the constructs get moved
        /// outside of the file. Due to paralleization, we can't retroactively
        /// delete the `init_foo` symbol, but instead it must be known far in
        /// advance if the symbol is needed or not.
        ///
        /// The logic in this function must be in sync with the hoisting
        /// logic in `LinkerContext.generateCodeForFileInChunkJS`
        fn needsWrapperRef(p: *const P, parts: []const js_ast.Part): bool {
            bun.assert(p.options.bundle);
            for (part of parts) {
                for (stmt of part.stmts) {
                    switch (stmt.data) {
                        .s_function => {},
                        .s_class => |cls| { if (!cls.class.canBeMoved()) return true; },
                        .s_local => |local| {
                            if (local.was_commonjs_export || p.commonjs_named_exports.count() == 0) {
                                for (decl of local.decls.slice()) {
                                    if (value := decl.value)
                                        if (value.data != .e_missing && !value.canBeMoved())
                                            return true;
                                }
                                continue;
                            }
                            return true;
                        },
                        .s_export_default => |ed| {
                            if (!ed.canBeMoved())
                                return true;
                        },
                        .s_export_equals => |e| {
                            if (!e.value.canBeMoved())
                                return true;
                        },
                        else => return true,
                    }
                }
            }
            return false;
        }

        static fn init(
            allocator: Allocator,
            log: *logger.Log,
            source: *const logger.Source,
            define: *Define,
            lexer: js_lexer.Lexer,
            opts: Parser.Options,
            p: *P,
        ): anyerror!void {
            let scope_order = ScopeOrderList.initCapacity(allocator, 1)?;
            const scope = allocator.create(Scope)?;
            scope.* = Scope{
                members: {},
                children: {},
                generated: {},
                kind: .entry,
                label_ref: null,
                parent: null,
            };

            scope_order.appendAssumeCapacity(ScopeOrder{ loc: locModuleScope, scope: scope });
            p.* = P{
                legacy_cjs_import_stmts: @TypeOf(p.legacy_cjs_import_stmts).init(allocator),
                // This must default to true or else parsing "in" won't work right.
                // It will fail for the case in the "in-keyword.js" file
                allow_in: true,

                call_target: nullExprData,
                delete_target: nullExprData,
                stmt_expr_value: nullExprData,
                loop_body: nullStmtData,
                define: define,
                import_records: undefined,
                named_imports: undefined,
                named_exports: {},
                log: log,
                stack_check: bun.StackCheck.init(),
                allocator: allocator,
                options: opts,
                then_catch_chain: ThenCatchChain{ next_target: nullExprData },
                to_expr_wrapper_namespace: undefined,
                to_expr_wrapper_hoisted: undefined,
                import_transposer: undefined,
                require_transposer: undefined,
                require_resolve_transposer: undefined,
                source: source,
                @"macro": MacroState.init(allocator),
                current_scope: scope,
                module_scope: scope,
                scopes_in_order: scope_order,
                needs_jsx_import: if (comptime only_scan_imports_and_do_not_visit) false else NeedsJSXType{},
                lexer: lexer,

                // Only enable during bundling, when not bundling CJS
                commonjs_named_exports_deoptimized: if (opts.bundle) opts.output_format == .cjs else true,
            };
            p.lexer.track_comments = opts.features.minify_identifiers;

            p.unwrap_all_requires = brk: {
                if (opts.bundle && opts.output_format != .cjs) {
                    if (pkg := source.path.packageName()) {
                        if (opts.features.shouldUnwrapRequire(pkg)) {
                            if (strings.eqlComptime(pkg, "react") || strings.eqlComptime(pkg, "react-dom")) {
                                const version = opts.package_version;
                                if (version.len > 2 && (version[0] == '0' || (version[0] == '1' && version[1] < '8'))) {
                                    break :brk false;
                                }
                            }

                            break :brk true;
                        }
                    }
                }

                break :brk false;
            };

            p.symbols = std.array_list.Managed(Symbol).init(allocator);

            if (comptime !only_scan_imports_and_do_not_visit) {
                p.import_records = @TypeOf(p.import_records).init(allocator);
                p.named_imports = {};
            }

            p.to_expr_wrapper_namespace = Binding2ExprWrapper.Namespace.init(p);
            p.to_expr_wrapper_hoisted = Binding2ExprWrapper.Hoisted.init(p);
            p.import_transposer = @TypeOf(p.import_transposer).init(p);
            p.require_transposer = @TypeOf(p.require_transposer).init(p);
            p.require_resolve_transposer = @TypeOf(p.require_resolve_transposer).init(p);

            if (opts.features.top_level_await || comptime only_scan_imports_and_do_not_visit) {
                p.fn_or_arrow_data_parse.allow_await = .allow_expr;
                p.fn_or_arrow_data_parse.is_top_level = true;
            }

            if (comptime !is_typescript_enabled) {
                // This is so it doesn't impact runtime transpiler caching when not in use
                p.options.features.emit_decorator_metadata = false;
            }
        }
    };
}

let e_missing_data: E.Missing = {};
let s_missing: S.Empty = {};
let nullExprData = Expr.Data{ e_missing: e_missing_data };
let nullStmtData = Stmt.Data{ s_empty: s_missing };

let keyExprData = Expr.Data{ e_string: unsafe { &Prefill.String.Key } };
let nullExprValueData: E.Null = {};
let falseExprValueData = E.Boolean{ value: false };
let nullValueExpr = Expr.Data{ e_null: nullExprValueData };
let falseValueExpr = Expr.Data{ e_boolean: E.Boolean{ value: false } };

type string = []const u8;

import repl_transforms from "./repl_transforms.zig";

const Define = defines_mod.Define;
const DefineData = defines_mod.DefineData;

import bun from "bun";
const Environment = bun.Environment;
const FeatureFlags = bun.FeatureFlags;
const ImportRecord = bun.ImportRecord;
const Output = bun.Output;
const assert = bun.assert;
const js_lexer = bun.js_lexer;
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const BindingNodeIndex = js_ast.BindingNodeIndex;
const DeclaredSymbol = js_ast.DeclaredSymbol;
const E = js_ast.E;
const Expr = js_ast.Expr;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const ExprNodeList = js_ast.ExprNodeList;
const Flags = js_ast.Flags;
const LocRef = js_ast.LocRef;
const S = js_ast.S;
const Scope = js_ast.Scope;
const Stmt = js_ast.Stmt;
const StmtNodeIndex = js_ast.StmtNodeIndex;
const StmtNodeList = js_ast.StmtNodeList;
const Symbol = js_ast.Symbol;

const G = js_ast.G;
const Arg = G.Arg;
const Decl = G.Decl;
const Property = G.Property;

const SymbolPropertyUseMap = js_ast.Part.SymbolPropertyUseMap;
const SymbolUseMap = js_ast.Part.SymbolUseMap;

const js_parser = bun.js_parser;
const ConvertESMExportsForHmr = js_parser.ConvertESMExportsForHmr;
const DeferredArrowArgErrors = js_parser.DeferredArrowArgErrors;
const DeferredErrors = js_parser.DeferredErrors;
const DeferredImportNamespace = js_parser.DeferredImportNamespace;
const ExprBindingTuple = js_parser.ExprBindingTuple;
const ExpressionTransposer = js_parser.ExpressionTransposer;
const FindLabelSymbolResult = js_parser.FindLabelSymbolResult;
const FnOnlyDataVisit = js_parser.FnOnlyDataVisit;
const FnOrArrowDataParse = js_parser.FnOrArrowDataParse;
const FnOrArrowDataVisit = js_parser.FnOrArrowDataVisit;
const FunctionKind = js_parser.FunctionKind;
const IdentifierOpts = js_parser.IdentifierOpts;
const ImportItemForNamespaceMap = js_parser.ImportItemForNamespaceMap;
const ImportKind = js_parser.ImportKind;
const ImportNamespaceCallOrConstruct = js_parser.ImportNamespaceCallOrConstruct;
const ImportScanner = js_parser.ImportScanner;
const InvalidLoc = js_parser.InvalidLoc;
const JSXImport = js_parser.JSXImport;
const JSXTransformType = js_parser.JSXTransformType;
const Jest = js_parser.Jest;
const LocList = js_parser.LocList;
const MacroState = js_parser.MacroState;
const ParseStatementOptions = js_parser.ParseStatementOptions;
const ParsedPath = js_parser.ParsedPath;
const Parser = js_parser.Parser;
const Prefill = js_parser.Prefill;
const PrependTempRefsOpts = js_parser.PrependTempRefsOpts;
const ReactRefresh = js_parser.ReactRefresh;
const Ref = js_parser.Ref;
const RefMap = js_parser.RefMap;
const RefRefMap = js_parser.RefRefMap;
const RuntimeFeatures = js_parser.RuntimeFeatures;
const RuntimeImports = js_parser.RuntimeImports;
const ScanPassResult = js_parser.ScanPassResult;
const ScopeOrder = js_parser.ScopeOrder;
const ScopeOrderList = js_parser.ScopeOrderList;
const SideEffects = js_parser.SideEffects;
const StmtList = js_parser.StmtList;
const StrictModeFeature = js_parser.StrictModeFeature;
const StringBoolMap = js_parser.StringBoolMap;
const Substitution = js_parser.Substitution;
const TempRef = js_parser.TempRef;
const ThenCatchChain = js_parser.ThenCatchChain;
const TransposeState = js_parser.TransposeState;
const TypeScript = js_parser.TypeScript;
const WrapMode = js_parser.WrapMode;
const arguments_str = js_parser.arguments_str;
const exports_string_name = js_parser.exports_string_name;
const fs = js_parser.fs;
const generatedSymbolName = js_parser.generatedSymbolName;
const isEvalOrArguments = js_parser.isEvalOrArguments;
const locModuleScope = js_parser.locModuleScope;
const options = js_parser.options;
const renamer = js_parser.renamer;
const statementCaresAboutScope = js_parser.statementCaresAboutScope;

const List = std.ArrayListUnmanaged;
const Map = std.AutoHashMapUnmanaged;
const Allocator = std.mem.Allocator;
const ListManaged = std.array_list.Managed;
