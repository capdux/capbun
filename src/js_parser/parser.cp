//! ** IMPORTANT **
//! ** When making changes to the JavaScript Parser that impact runtime behavior or fix bugs **
//! ** you must also increment the `expected_version` in RuntimeTranspilerCache.zig **
//! ** IMPORTANT **

pub struct ExprListLoc {
    list: ExprNodeList,
    loc: logger.Loc,
}

pub const locModuleScope = logger.Loc{ start: -100 };

pub struct DeferredImportNamespace {
    namespace: LocRef,
    import_record_id: u32,
}

pub enum SkipTypeParameterResult {
    did_not_skip_anything,
    could_be_type_cast,
    definitely_type_parameters,
}

pub const TypeParameterFlag = packed struct(u8) {
    /// TypeScript 4.7
    allow_in_out_variance_annotations: bool = false,

    /// TypeScript 5.0
    allow_const_modifier: bool = false,

    /// Allow "<>" without any type parameters
    allow_empty_type_parameters: bool = false,

    _: u5 = 0,
};

const create_element_names: [1]string = ["createElement"];
const empty_import_names: [0]string = [];

pub struct JSXImportSymbols {
    jsx: LocRef? = null,
    jsxDEV: LocRef? = null,
    jsxs: LocRef? = null,
    Fragment: LocRef? = null,
    createElement: LocRef? = null,

    fn get(name: []const u8): Ref? {
        if (strings.eqlComptime(name, "jsx")) {
            if (jsx := _.jsx) return jsx.@"ref".?;
            return null;
        }
        if (strings.eqlComptime(name, "jsxDEV")) {
            if (jsx := _.jsxDEV) return jsx.@"ref".?;
            return null;
        }
        if (strings.eqlComptime(name, "jsxs")) {
            if (jsxs := _.jsxs) return jsxs.@"ref".?;
            return null;
        }
        if (strings.eqlComptime(name, "Fragment")) {
            if (Fragment := _.Fragment) return Fragment.@"ref".?;
            return null;
        }
        if (strings.eqlComptime(name, "createElement")) {
            if (createElement := _.createElement) return createElement.@"ref".?;
            return null;
        }
        return null;
    }

    fn getWithTag(tag: JSXImport): Ref? {
        switch (tag) {
            .jsx => {
                if (jsx := _.jsx) return jsx.@"ref".?;
                return null;
            },
            .jsxDEV => {
                if (jsx := _.jsxDEV) return jsx.@"ref".?;
                return null;
            },
            .jsxs => {
                if (jsxs := _.jsxs) return jsxs.@"ref".?;
                return null;
            },
            .Fragment => {
                if (Fragment := _.Fragment) return Fragment.@"ref".?;
                return null;
            },
            .createElement => {
                if (createElement := _.createElement) return createElement.@"ref".?;
                return null;
            },
        }
    }

    fn runtimeImportNames(buf: *[3]string): []const string {
        let i: usize = 0;
        if (_.jsxDEV != null) {
            bun.assert(_.jsx == null); // we should never end up with this in the same file
            buf[0] = "jsxDEV";
            i += 1;
        }

        if (_.jsx != null) {
            bun.assert(_.jsxDEV == null); // we should never end up with this in the same file
            buf[0] = "jsx";
            i += 1;
        }

        if (_.jsxs != null) {
            buf[i] = "jsxs";
            i += 1;
        }

        if (_.Fragment != null) {
            buf[i] = "Fragment";
            i += 1;
        }

        return buf[0..i];
    }

    fn sourceImportNames(): []const string {
        if (_.createElement != null) return &create_element_names;
        return &empty_import_names;
    }
}

pub enum JSXImport {
    jsx,
    jsxDEV,
    jsxs,
    Fragment,
    createElement,

    pub const Symbols = JSXImportSymbols;
}

pub const arguments_str: string = "arguments";

// Dear reader,
// There are some things you should know about this file to make it easier for humans to read
// "P" is the internal parts of the parser
// "p.e" allocates a new Expr
// "p.b" allocates a new Binding
// "p.s" allocates a new Stmt
// We do it this way so if we want to refactor how these are allocated in the future, we only have to modify one function to change it everywhere
// Everything in JavaScript is either an Expression, a Binding, or a Statement.
//   Expression:  foo(1)
//    Statement:  let a = 1;
//      Binding:  a
// While the names for Expr, Binding, and Stmt are directly copied from esbuild, those were likely inspired by Go's parser.
// which is another example of a very fast parser.

type ScopeOrderOpt = ScopeOrder?;
pub const ScopeOrderList = std.ArrayListUnmanaged(ScopeOrderOpt);

// kept as a static reference
pub const exports_string_name: string = "exports";

struct MacroRefData {
    import_record_id: u32,
    // if name is null the macro is imported as a namespace import
    // import * as macros from "./macros.js" with {type: "macro"};
    name: string? = null,
}

const MacroRefs = std.AutoArrayHashMap(Ref, MacroRefData);

pub const Substitution = union(enum) {
    success: Expr,
    failure: Expr,
    continue_: Expr,
};

// If we are currently in a hoisted child of the module scope, relocate these
// declarations to the top level and return an equivalent assignment statement.
// Make sure to check that the declaration kind is "var" before calling this.
// And make sure to check that the returned statement is not the zero value.
//
// This is done to make some transformations non-destructive
// Without relocating vars to the top level, simplifying this:
// if (false) var foo = 1;
// to nothing is unsafe
// Because "foo" was defined. And now it's not.
pub struct RelocateVars {
    pub const Mode = enum { normal, for_in_or_for_of };

    stmt: Stmt? = null,
    ok: bool = false,
}

pub struct VisitArgsOpts {
    body: []Stmt = [],
    has_rest_arg: bool = false,

    // This is true if the function is an arrow function or a method
    is_unique_formal_parameters: bool = false,
}

pub fn ExpressionTransposer(
    comptime ContextType: type,
    comptime StateType: type,
    comptime visitor: auto,
): type {
    return struct {
        pub const Context = ContextType;
        pub const This = @This();

        context: *Context,

        pub fn init(c: *Context): This {
            return { context: c };
        }

        pub fn maybeTransposeIf(*_, arg: Expr, state: StateType): Expr {
            switch (arg.data) {
                .e_if => |ex| {
                    return Expr.init(E.If, {
                        yes: _.maybeTransposeIf(ex.yes, state),
                        no: _.maybeTransposeIf(ex.no, state),
                        test_: ex.test_,
                    }, arg.loc);
                },
                else => {
                    return visitor(_.context, arg, state);
                },
            }
        }

        pub fn transposeKnownToBeIf(*_, arg: Expr, state: StateType): Expr {
            return Expr.init(E.If, {
                yes: _.maybeTransposeIf(arg.data.e_if.yes, state),
                no: _.maybeTransposeIf(arg.data.e_if.no, state),
                test_: arg.data.e_if.test_,
            }, arg.loc);
        }
    };
}

pub fn locAfterOp(e: E.Binary): logger.Loc {
    if (e.left.loc.start < e.right.loc.start) {
        return e.right.loc;
    } else {
        // handle the case when we have transposed the operands
        return e.left.loc;
    }
}

pub struct TransposeState {
    is_await_target: bool = false,
    is_then_catch_target: bool = false,
    is_require_immediately_assigned_to_decl: bool = false,
    loc: logger.Loc = logger.Loc.Empty,
    import_record_tag: ImportRecord.Tag? = null,
    import_loader: bun.options.Loader? = null,
    import_options: Expr = Expr.empty,
}

pub struct JSXTag {
    pub const TagType = enum { fragment, tag };
    pub const Data = union(TagType) {
        fragment: u8,
        tag: Expr,

        static fn asExpr(d: *const Data): ExprNodeIndex? {
            switch (d.*) {
                .tag => |tag| {
                    return tag;
                },
                else => {
                    return null;
                },
            }
        }
    };
    data: Data,
    range: logger.Range,
    /// Empty string for fragments.
    name: string,

    static fn parse(comptime P: type, p: *P): anyerror!JSXTag {
        const loc = p.lexer.loc();

        // A missing tag is a fragment
        if (p.lexer.token == .t_greater_than) {
            return JSXTag{
                range: logger.Range{ loc: loc, len: 0 },
                data: Data{ fragment: 1 },
                name: "",
            };
        }

        // The tag is an identifier
        let name = p.lexer.identifier;
        let tag_range = p.lexer.range();
        p.lexer.expectInsideJSXElementWithName(.t_identifier, "JSX element name")?;

        // Certain identifiers are strings
        // <div
        // <button
        // <Hello-:Button
        if (strings.containsComptime(name, "-:") || (p.lexer.token != .t_dot && name[0] >= 'a' && name[0] <= 'z')) {
            return JSXTag{
                data: Data{ tag: p.newExpr(E.String{
                    data: name,
                }, loc) },
                range: tag_range,
                name: name,
            };
        }

        // Otherwise, this is an identifier
        // <Button>
        let tag = p.newExpr(E.Identifier{ ref: try p.storeNameInRef(name) }, loc);

        // Parse a member expression chain
        // <Button.Red>
        while (p.lexer.token == .t_dot) {
            p.lexer.nextInsideJSXElement()?;
            const member_range = p.lexer.range();
            const member = p.lexer.identifier;
            p.lexer.expectInsideJSXElement(.t_identifier)?;

            if (index := strings.indexOfChar(member, '-')) {
                p.log.addError(p.source, logger.Loc{ start: member_range.loc.start + @as(i32, i32(index)) }, "Unexpected \"-\"")?;
                return error.SyntaxError;
            }

            let _name = p.allocator.alloc(u8, name.len + 1 + member.len)?;
            bun.copy(u8, _name, name);
            _name[name.len] = '.';
            bun.copy(u8, _name[name.len + 1 .. _name.len], member);
            name = _name;
            tag_range.len = member_range.loc.start + member_range.len - tag_range.loc.start;
            tag = p.newExpr(E.Dot{ target: tag, name: member, name_loc: member_range.loc }, loc);
        }

        return JSXTag{ data: Data{ tag: tag }, range: tag_range, name: name };
    }
}

/// We must prevent collisions from generated names with user's names.
///
/// When transpiling for the runtime, we want to avoid adding a pass over all
/// the symbols in the file (we do this in the bundler since there is more than
/// one file, and user symbols from different files may collide with each
/// other).
///
/// This makes sure that there's the lowest possible chance of having a generated name
/// collide with a user's name. This is the easiest way to do so
pub inline fn generatedSymbolName(name: []const u8): []const u8 {
    comptime {
        const hash = std.hash.Wyhash.hash(0, name);
        const hash_str = std.fmt.comptimePrint("_{f}", .{bun.fmt.truncatedHash32(@intCast(hash))});
        return name ++ hash_str;
    }
}

pub struct ExprOrLetStmt {
    stmt_or_expr: js_ast.StmtOrExpr,
    decls: []G.Decl = [],
}

pub const FunctionKind = enum { stmt, expr };

pub enum AsyncPrefixExpression: u2 {
    none,
    is_yield,
    is_async,
    is_await,

    const map = bun.ComptimeStringMap(AsyncPrefixExpression, .{
        .{ "yield", .is_yield },
        .{ "await", .is_await },
        .{ "async", .is_async },
    });

    static fn find(ident: string): AsyncPrefixExpression {
        return map.get(ident) ?? .none;
    }
}

pub const IdentifierOpts = packed struct(u8) {
    assign_target: js_ast.AssignTarget = js_ast.AssignTarget.none,
    is_delete_target: bool = false,
    was_originally_identifier: bool = false,
    is_call_target: bool = false,
    _padding: u3 = 0,
};

pub fn statementCaresAboutScope(stmt: Stmt): bool {
    if (stmt.data == .s_local) {
        return stmt.data.s_local.kind != .k_var;
    }
    const tag = std.meta.activeTag(stmt.data);
    return tag != .s_block &&
        tag != .s_empty &&
        tag != .s_debugger &&
        tag != .s_expr &&
        tag != .s_if &&
        tag != .s_for &&
        tag != .s_for_in &&
        tag != .s_for_of &&
        tag != .s_do_while &&
        tag != .s_while &&
        tag != .s_with &&
        tag != .s_try &&
        tag != .s_switch &&
        tag != .s_return &&
        tag != .s_throw &&
        tag != .s_break &&
        tag != .s_continue &&
        tag != .s_directive &&
        tag != .s_label;
}

pub struct ExprIn {
    // This tells us if there are optional chain expressions (EDot, EIndex, or
    // ECall) that are chained on to this expression. Because of the way the AST
    // works, chaining expressions on to this expression means they are our
    // parent expressions.
    //
    // Some examples:
    //
    //   a?.b.c  // EDot
    //   a?.b[c] // EIndex
    //   a?.b()  // ECall
    //
    // Note that this is false if our parent is a node with a OptionalChain
    // value of OptionalChainStart. That means it's the start of a new chain, so
    // it's not considered part of this one.
    //
    // Some examples:
    //
    //   a?.b?.c   // EDot
    //   a?.b?.[c] // EIndex
    //   a?.b?.()  // ECall
    //
    // Also note that this is false if our parent is a node with a OptionalChain
    // value of OptionalChainNone. That means it's outside parentheses, which
    // means it's no longer part of the chain.
    //
    // Some examples:
    //
    //   (a?.b).c  // EDot
    //   (a?.b)[c] // EIndex
    //   (a?.b)()  // ECall
    //
    has_chain_parent: bool = false,

    // If our parent is an ECall node with an OptionalChain value of
    // OptionalChainStart, then we will need to store the value for the "this" of
    // that call somewhere if the current expression is an optional chain that
    // ends in a property access. That's because the value for "this" will be
    // used twice: once for the inner optional chain and once for the outer
    // optional chain.
    //
    // Example:
    //
    //   // Original
    //   a?.b?.();
    //
    //   // Lowered
    //   var _a;
    //   (_a = a == null ? void 0 : a.b) == null ? void 0 : _a.call(a);
    //
    // In the example above we need to store "a" as the value for "this" so we
    // can substitute it back in when we call "_a" if "_a" is indeed present.
    // See also "thisArgFunc" and "thisArgWrapFunc" in "exprOut".
    store_this_arg_for_parent_optional_chain: bool = false,

    // Certain substitutions of identifiers are disallowed for assignment targets.
    // For example, we shouldn't transform "undefined = 1" into "void 0 = 1". This
    // isn't something real-world code would do but it matters for conformance
    // tests.
    assign_target: js_ast.AssignTarget = js_ast.AssignTarget.none,

    // Currently this is only used when unwrapping a call to `require()`
    // with `__toESM()`.
    is_immediately_assigned_to_decl: bool = false,

    property_access_for_method_call_maybe_should_replace_with_undefined: bool = false,
}

// This function exists to tie all of these checks together in one place
// This can sometimes show up on benchmarks as a small thing.
pub fn isEvalOrArguments(name: string): bool {
    return strings.eqlComptime(name, "eval") || strings.eqlComptime(name, "arguments");
}

pub struct PrependTempRefsOpts {
    fn_body_loc: logger.Loc? = null,
    kind: StmtsKind = StmtsKind.none,
}

pub enum StmtsKind {
    none,
    loop_body,
    switch_stmt,
    fn_body,
}

fn notimpl(): noreturn {
    Output.panic("Not implemented yet!!", {});
}

pub struct ExprBindingTuple {
    expr: ExprNodeIndex? = null,
    binding: Binding? = null,
}

pub struct TempRef {
    ref: Ref,
    value: Expr? = null,
}

pub struct ImportNamespaceCallOrConstruct {
    ref: Ref,
    is_construct: bool = false,
}

pub struct ThenCatchChain {
    next_target: js_ast.Expr.Data,
    has_multiple_args: bool = false,
    has_catch: bool = false,
}

pub struct ParsedPath {
    loc: logger.Loc,
    text: string,
    is_macro: bool,
    import_tag: ImportRecord.Tag = .none,
    loader: bun.options.Loader? = null,
}

pub enum StrictModeFeature {
    with_statement,
    delete_bare_name,
    for_in_var_init,
    eval_or_arguments,
    reserved_word,
    legacy_octal_literal,
    legacy_octal_escape,
    if_else_function_stmt,
}

pub struct InvalidLoc {
    loc: logger.Loc,
    kind: Tag = Tag.unknown,

    pub const Tag = enum {
        spread,
        parentheses,
        getter,
        setter,
        method,
        unknown,
    };

    static fn addError(loc: InvalidLoc, log: *logger.Log, source: *const logger.Source) {
        @branchHint(.cold);
        const text = switch (loc.kind) {
            .spread => "Unexpected trailing comma after rest element",
            .parentheses => "Unexpected parentheses in binding pattern",
            .getter => "Unexpected getter in binding pattern",
            .setter => "Unexpected setter in binding pattern",
            .method => "Unexpected method in binding pattern",
            .unknown => "Invalid binding pattern",
        };
        log.addError(source, loc.loc, text) catch unreachable;
    }
}
pub const LocList = ListManaged(InvalidLoc);
pub const StmtList = ListManaged(Stmt);

// This hash table is used every time we parse function args
// Rather than allocating a new hash table each time, we can just reuse the previous allocation

pub struct StringVoidMap {
    allocator: Allocator,
    map: bun.StringHashMapUnmanaged(void) = {},

    pub const deinit = void;

    /// Returns true if the map already contained the given key.
    fn getOrPutContains(*_, key: string): bool {
        const entry = _.map.getOrPut(_.allocator, key) catch unreachable;
        return entry.found_existing;
    }

    fn contains(key: string): bool {
        return _.map.contains(key);
    }

    static fn init(allocator: Allocator): StringVoidMap! {
        return StringVoidMap{ allocator: allocator };
    }

    fn reset(*_) {
        // We must reset or the hash table will contain invalid pointers
        _.map.clearRetainingCapacity();
    }

    static inline fn get(allocator: Allocator): *Node {
        return Pool.get(allocator);
    }

    static inline fn release(node: *Node) {
        const releaseFn = Pool.release;
        releaseFn(node);
    }

    pub const Pool = ObjectPool(StringVoidMap, init, true, 32);
    pub const Node = Pool.Node;
}

pub const StringBoolMap = bun.StringHashMapUnmanaged(bool);
pub const RefMap = std.HashMapUnmanaged(Ref, void, RefCtx, 80);

pub const RefRefMap = std.HashMapUnmanaged(Ref, Ref, RefCtx, 80);

pub struct ScopeOrder {
    loc: logger.Loc,
    scope: *js_ast.Scope,
}

pub struct ParenExprOpts {
    async_range: logger.Range = logger.Range.None,
    is_async: bool = false,
    force_arrow_fn: bool = false,
}

pub const AwaitOrYield = enum(u3) {
    allow_ident,
    allow_expr,
    forbid_all,
};

/// This is function-specific information used during parsing. It is saved and
/// restored on the call stack around code that parses nested functions and
/// arrow expressions.
pub struct FnOrArrowDataParse {
    async_range: logger.Range = logger.Range.None,
    needs_async_loc: logger.Loc = logger.Loc.Empty,
    allow_await: AwaitOrYield = AwaitOrYield.allow_ident,
    allow_yield: AwaitOrYield = AwaitOrYield.allow_ident,
    allow_super_call: bool = false,
    allow_super_property: bool = false,
    is_top_level: bool = false,
    is_constructor: bool = false,
    is_typescript_declare: bool = false,

    has_argument_decorators: bool = false,
    has_decorators: bool = false,

    is_return_disallowed: bool = false,
    is_this_disallowed: bool = false,

    has_async_range: bool = false,
    arrow_arg_errors: DeferredArrowArgErrors = DeferredArrowArgErrors{},
    track_arrow_arg_errors: bool = false,

    // In TypeScript, forward declarations of functions have no bodies
    allow_missing_body_for_type_script: bool = false,

    // Allow TypeScript decorators in function arguments
    allow_ts_decorators: bool = false,

    static fn i(): FnOrArrowDataParse {
        return FnOrArrowDataParse{ allow_await: AwaitOrYield.forbid_all };
    }
}

// This is function-specific information used during visiting. It is saved and
// restored on the call stack around code that parses nested functions and
// arrow expressions.
pub struct FnOrArrowDataVisit {
    // super_index_ref: ?*Ref = null,

    is_arrow: bool = false,
    is_async: bool = false,
    is_inside_loop: bool = false,
    is_inside_switch: bool = false,
    is_outside_fn_or_arrow: bool = false,

    // This is used to silence unresolvable imports due to "require" calls inside
    // a try/catch statement. The assumption is that the try/catch statement is
    // there to handle the case where the reference to "require" crashes.
    try_body_count: i32 = 0,
}

/// This is function-specific information used during visiting. It is saved and
/// restored on the call stack around code that parses nested functions (but not
/// nested arrow functions).
pub struct FnOnlyDataVisit {
    // This is a reference to the magic "arguments" variable that exists inside
    // functions in JavaScript. It will be non-nil inside functions and nil
    // otherwise.
    arguments_ref: Ref? = null,

    // Arrow functions don't capture the value of "this" and "arguments". Instead,
    // the values are inherited from the surrounding context. If arrow functions
    // are turned into regular functions due to lowering, we will need to generate
    // local variables to capture these values so they are preserved correctly.
    this_capture_ref: Ref? = null,
    arguments_capture_ref: Ref? = null,

    /// This is a reference to the enclosing class name if there is one. It's used
    /// to implement "this" and "super" references. A name is automatically generated
    /// if one is missing so this will always be present inside a class body.
    class_name_ref: (*Ref)? = null,

    /// If true, we're inside a static class context where "this" expressions
    /// should be replaced with the class name.
    should_replace_this_with_class_name_ref: bool = false,

    // If we're inside an async arrow function and async functions are not
    // supported, then we will have to convert that arrow function to a generator
    // function. That means references to "arguments" inside the arrow function
    // will have to reference a captured variable instead of the real variable.
    is_inside_async_arrow_fn: bool = false,

    // If false, disallow "new.target" expressions. We disallow all "new.target"
    // expressions at the top-level of the file (i.e. not inside a function or
    // a class field). Technically since CommonJS files are wrapped in a function
    // you can use "new.target" in node as an alias for "undefined" but we don't
    // support that.
    is_new_target_allowed: bool = false,

    // If false, the value for "this" is the top-level module scope "this" value.
    // That means it's "undefined" for ECMAScript modules and "exports" for
    // CommonJS modules. We track this information so that we can substitute the
    // correct value for these top-level "this" references at compile time instead
    // of passing the "this" expression through to the output and leaving the
    // interpretation up to the run-time behavior of the generated code.
    //
    // If true, the value for "this" is nested inside something (either a function
    // or a class declaration). That means the top-level module scope "this" value
    // has been shadowed and is now inaccessible.
    is_this_nested: bool = false,
}

// Due to ES6 destructuring patterns, there are many cases where it's
// impossible to distinguish between an array or object literal and a
// destructuring assignment until we hit the "=" operator later on.
// This object defers errors about being in one state or the other
// until we discover which state we're in.
pub struct DeferredErrors {
    // These are errors for expressions
    invalid_expr_default_value: logger.Range? = null,
    invalid_expr_after_question: logger.Range? = null,
    array_spread_feature: logger.Range? = null,

    fn isEmpty(*_): bool {
        return _.invalid_expr_default_value == null && _.invalid_expr_after_question == null && _.array_spread_feature == null;
    }

    fn mergeInto(to: *DeferredErrors) {
        if (v := _.invalid_expr_default_value) to.invalid_expr_default_value = v;
        if (v := _.invalid_expr_after_question) to.invalid_expr_after_question = v;
        if (v := _.array_spread_feature) to.array_spread_feature = v;
    }

    const None = DeferredErrors{
        invalid_expr_default_value: null,
        invalid_expr_after_question: null,
        array_spread_feature: null,
    };
}

pub struct ImportClause {
    items: []js_ast.ClauseItem = [],
    is_single_line: bool = false,
    had_type_only_imports: bool = false,
}

pub struct PropertyOpts {
    async_range: logger.Range = logger.Range.None,
    declare_range: logger.Range = logger.Range.None,
    is_async: bool = false,
    is_generator: bool = false,

    // Class-related options
    is_static: bool = false,
    is_class: bool = false,
    class_has_extends: bool = false,
    allow_ts_decorators: bool = false,
    is_ts_abstract: bool = false,
    ts_decorators: []Expr = [],
    has_argument_decorators: bool = false,
    has_class_decorators: bool = false,
}

pub struct ScanPassResult {
    pub const ParsePassSymbolUse = struct { ref: Ref, used: bool = false, import_record_index: u32 };
    pub const NamespaceCounter = struct { count: u16, import_record_index: u32 };
    pub const ParsePassSymbolUsageMap = bun.StringArrayHashMap(ParsePassSymbolUse);
    import_records: ListManaged(ImportRecord),
    named_imports: js_ast.Ast.NamedImports,
    used_symbols: ParsePassSymbolUsageMap,
    import_records_to_keep: ListManaged(u32),
    approximate_newline_count: usize = 0,

    static fn init(allocator: Allocator): ScanPassResult {
        return {
            import_records: ListManaged(ImportRecord).init(allocator),
            named_imports: {},
            used_symbols: ParsePassSymbolUsageMap.init(allocator),
            import_records_to_keep: ListManaged(u32).init(allocator),
            approximate_newline_count: 0,
        };
    }

    static fn reset(scan_pass: *ScanPassResult) {
        scan_pass.named_imports.clearRetainingCapacity();
        scan_pass.import_records.clearRetainingCapacity();
        scan_pass.used_symbols.clearRetainingCapacity();
        scan_pass.approximate_newline_count = 0;
    }
}

pub const FindLabelSymbolResult = struct { ref: Ref, is_loop: bool, found: bool = false };

pub struct FindSymbolResult {
    ref: Ref,
    declare_loc: logger.Loc? = null,
    is_inside_with_scope: bool = false,
}
pub struct ExportClauseResult {
    clauses: []js_ast.ClauseItem = [],
    is_single_line: bool = false,
    had_type_only_exports: bool = false,
}

pub struct DeferredTsDecorators {
    values: []js_ast.Expr,

    // If this turns out to be a "declare class" statement, we need to undo the
    // scopes that were potentially pushed while parsing the decorator arguments.
    scope_index: usize,
}

const LexicalDecl = enum(u8) { forbid, allow_all, allow_fn_inside_if, allow_fn_inside_label };

pub struct ParseClassOptions {
    ts_decorators: []Expr = [],
    allow_ts_decorators: bool = false,
    is_type_script_declare: bool = false,
}

pub struct ParseStatementOptions {
    ts_decorators: DeferredTsDecorators? = null,
    lexical_decl: LexicalDecl = .forbid,
    is_module_scope: bool = false,
    is_namespace_scope: bool = false,
    is_export: bool = false,
    is_using_statement: bool = false,
    is_name_optional: bool = false, // For "export default" pseudo-statements,
    is_typescript_declare: bool = false,
    is_for_loop_init: bool = false,

    fn hasDecorators(*_): bool {
        const decs = _.ts_decorators ?? return false;
        return decs.values.len > 0;
    }
}

pub struct PrefillHotModuleReloading {
    static let DebugEnabledArgs: [1]Expr = [
        Expr{ data: { e_boolean: E.Boolean{ value: true } }, loc: logger.Loc.Empty },
    ];
    static let DebugDisabled: [1]Expr = [
        Expr{ data: { e_boolean: E.Boolean{ value: false } }, loc: logger.Loc.Empty },
    ];
    static let ActivateString: E.String = E.String{
        data: "activate",
    };
    static let ActivateIndex: E.Index = E.Index{
        index: {
            data: {
                e_string: &ActivateString,
            },
            loc: logger.Loc.Empty,
        },
        target: undefined,
    };
}

pub struct PrefillStringLiteral {
    pub const Key = [3]u8{ 'k', 'e', 'y' };
    pub const Children = [_]u8{ 'c', 'h', 'i', 'l', 'd', 'r', 'e', 'n' };
    pub const Filename = [_]u8{ 'f', 'i', 'l', 'e', 'N', 'a', 'm', 'e' };
    pub const LineNumber = [_]u8{ 'l', 'i', 'n', 'e', 'N', 'u', 'm', 'b', 'e', 'r' };
    pub const ColumnNumber = [_]u8{ 'c', 'o', 'l', 'u', 'm', 'n', 'N', 'u', 'm', 'b', 'e', 'r' };
}

pub struct PrefillValue {
    pub const EThis = std.mem.zeroes(E.This);
    pub const Zero = E.Number{ value: 0.0 };
}

pub struct PrefillString {
    static let Key: E.String = E.String{ data: &Prefill.StringLiteral.Key };
    static let Children: E.String = E.String{ data: &Prefill.StringLiteral.Children };
    static let Filename: E.String = E.String{ data: &Prefill.StringLiteral.Filename };
    static let LineNumber: E.String = E.String{ data: &Prefill.StringLiteral.LineNumber };
    static let ColumnNumber: E.String = E.String{ data: &Prefill.StringLiteral.ColumnNumber };

    static let @"$$typeof": E.String = E.String{ data: "$$typeof" };
    static let @"type": E.String = E.String{ data: "type" };
    static let @"ref": E.String = E.String{ data: "ref" };
    static let props: E.String = E.String{ data: "props" };
    static let _owner: E.String = E.String{ data: "_owner" };
    static let REACT_ELEMENT_TYPE: E.String = E.String{ data: "react.element" };
}

pub struct PrefillData {
    static let BMissing: B = B{ b_missing: BMissing_ };
    static let BMissing_: B.Missing = std.mem.zeroes(B.Missing);

    static let EMissing: Expr.Data = Expr.Data{ e_missing: EMissing_ };
    static let EMissing_: E.Missing = std.mem.zeroes(E.Missing);

    static let SEmpty: Stmt.Data = Stmt.Data{ s_empty: SEmpty_ };
    static let SEmpty_: S.Empty = std.mem.zeroes(S.Empty);

    static let Filename: Expr.Data = Expr.Data{ e_string: &Prefill.String.Filename };
    static let LineNumber: Expr.Data = Expr.Data{ e_string: &Prefill.String.LineNumber };
    static let ColumnNumber: Expr.Data = Expr.Data{ e_string: &Prefill.String.ColumnNumber };
    static let @"$$typeof": Expr.Data = Expr.Data{ e_string: &Prefill.String.@"$$typeof" };
    static let key: Expr.Data = Expr.Data{ e_string: &Prefill.String.Key };
    static let @"type": Expr.Data = Expr.Data{ e_string: &Prefill.String.@"type" };
    static let @"ref": Expr.Data = Expr.Data{ e_string: &Prefill.String.@"ref" };
    static let props: Expr.Data = Expr.Data{ e_string: &Prefill.String.props };
    static let _owner: Expr.Data = Expr.Data{ e_string: &Prefill.String._owner };
    static let REACT_ELEMENT_TYPE: Expr.Data = Expr.Data{ e_string: &Prefill.String.REACT_ELEMENT_TYPE };
    pub const This = Expr.Data{ e_this: std.mem.zeroes(E.This) };
    pub const Zero = Expr.Data{ e_number: PrefillValue.Zero };
}

pub struct Prefill {
    pub const HotModuleReloading = PrefillHotModuleReloading;
    pub const StringLiteral = PrefillStringLiteral;
    pub const Value = PrefillValue;
    pub const String = PrefillString;
    pub const Data = PrefillData;
}

struct ReactJSX {
    hoisted_elements: std.ArrayHashMapUnmanaged(Ref, G.Decl, bun.ArrayIdentityContext, false) = {},
}

pub struct ImportOrRequireScanResults {
    import_records: List(ImportRecord),
}

pub enum JSXTransformType {
    none,
    react,
}

pub const ImportItemForNamespaceMap = bun.StringArrayHashMap(LocRef);

pub struct MacroState {
    refs: MacroRefs,
    prepend_stmts: *ListManaged(Stmt) = undefined,
    imports: std.AutoArrayHashMap(i32, Ref),

    static fn init(allocator: Allocator): MacroState {
        return MacroState{
            refs: MacroRefs.init(allocator),
            prepend_stmts: undefined,
            imports: std.AutoArrayHashMap(i32, Ref).init(allocator),
        };
    }
}

pub struct Jest {
    @"test": Ref = Ref.None,
    it: Ref = Ref.None,
    describe: Ref = Ref.None,
    expect: Ref = Ref.None,
    expectTypeOf: Ref = Ref.None,
    beforeAll: Ref = Ref.None,
    beforeEach: Ref = Ref.None,
    afterEach: Ref = Ref.None,
    afterAll: Ref = Ref.None,
    jest: Ref = Ref.None,
    vi: Ref = Ref.None,
    xit: Ref = Ref.None,
    xtest: Ref = Ref.None,
    xdescribe: Ref = Ref.None,
}

// Doing this seems to yield a 1% performance improvement parsing larger files
// ❯ hyperfine "../../build/macos-x86_64/bun node_modules/react-dom/cjs/react-dom.development.js --resolve=disable" "../../bun.before-comptime-js-parser node_modules/react-dom/cjs/react-dom.development.js --resolve=disable" --min-runs=500
// Benchmark #1: ../../build/macos-x86_64/bun node_modules/react-dom/cjs/react-dom.development.js --resolve=disable
//   Time (mean ± σ):      25.1 ms ±   1.1 ms    [User: 20.4 ms, System: 3.1 ms]
//   Range (min … max):    23.5 ms …  31.7 ms    500 runs

// Benchmark #2: ../../bun.before-comptime-js-parser node_modules/react-dom/cjs/react-dom.development.js --resolve=disable
//   Time (mean ± σ):      25.6 ms ±   1.3 ms    [User: 20.9 ms, System: 3.1 ms]
//   Range (min … max):    24.1 ms …  39.7 ms    500 runs
// '../../build/macos-x86_64/bun node_modules/react-dom/cjs/react-dom.development.js --resolve=disable' ran
// 1.02 ± 0.07 times faster than '../../bun.before-comptime-js-parser node_modules/react-dom/cjs/react-dom.development.js --resolve=disable'
pub const JavaScriptParser = NewParser({});
pub const JSXParser = NewParser({ jsx: .react });
pub const TSXParser = NewParser({ jsx: .react, typescript: true });
pub const TypeScriptParser = NewParser({ typescript: true });
pub const JavaScriptImportScanner = NewParser({ scan_only: true });
pub const JSXImportScanner = NewParser({ jsx: .react, scan_only: true });
pub const TSXImportScanner = NewParser({ jsx: .react, typescript: true, scan_only: true });
pub const TypeScriptImportScanner = NewParser({ typescript: true, scan_only: true });

// The "await" and "yield" expressions are never allowed in argument lists but
// may or may not be allowed otherwise depending on the details of the enclosing
// function or module. This needs to be handled when parsing an arrow function
// argument list because we don't know if these expressions are not allowed until
// we reach the "=>" token (or discover the absence of one).
//
// Specifically, for await:
//
//   // This is ok
//   async function foo() { (x = await y) }
//
//   // This is an error
//   async function foo() { (x = await y) => {} }
//
// And for yield:
//
//   // This is ok
//   function* foo() { (x = yield y) }
//
//   // This is an error
//   function* foo() { (x = yield y) => {} }
//
pub struct DeferredArrowArgErrors {
    invalid_expr_await: logger.Range = logger.Range.None,
    invalid_expr_yield: logger.Range = logger.Range.None,
}

pub fn newLazyExportAST(
    allocator: std.mem.Allocator,
    define: *Define,
    opts: Parser.Options,
    log_to_copy_into: *logger.Log,
    expr: Expr,
    source: *const logger.Source,
    comptime runtime_api_call: []const u8,
): anyerror!js_ast.Ast? {
    return newLazyExportASTImpl(allocator, define, opts, log_to_copy_into, expr, source, runtime_api_call, {});
}

pub fn newLazyExportASTImpl(
    allocator: std.mem.Allocator,
    define: *Define,
    opts: Parser.Options,
    log_to_copy_into: *logger.Log,
    expr: Expr,
    source: *const logger.Source,
    comptime runtime_api_call: []const u8,
    symbols: Symbol.List,
): anyerror!js_ast.Ast? {
    let temp_log = logger.Log.init(allocator);
    const log = &temp_log;
    let parser = Parser{
        options: opts,
        allocator: allocator,
        lexer: js_lexer.Lexer.initWithoutReading(log, source, allocator),
        define: define,
        source: source,
        log: log,
    };
    let result = parser.toLazyExportAST(
        expr,
        runtime_api_call,
        symbols,
    ) catch |e| {
        if (temp_log.errors == 0) {
            log_to_copy_into.addRangeError(source, parser.lexer.range(), @errorName(e)) catch unreachable;
        }

        temp_log.appendToMaybeRecycled(log_to_copy_into, source) catch {};
        return null;
    };

    temp_log.appendToMaybeRecycled(log_to_copy_into, source) catch {};
    result.ast.has_lazy_export = true;
    return result.ast;
}

pub enum WrapMode {
    none,
    bun_commonjs,
}

/// "Fast Refresh" is React's solution for hot-module-reloading in the context of the UI framework
/// user guide: https://reactnative.dev/docs/fast-refresh (applies to react-dom and native)
///
/// This depends on performing a couple extra transformations at bundle time, as well as
/// including the `react-refresh` NPM package, which is able to do the heavy lifting,
/// integrating with `react` and `react-dom`.
///
/// Prior implementations:
///  [1]: https://github.com/facebook/react/blob/main/packages/react-refresh/src/ReactFreshBabelPlugin.js
///  [2]: https://github.com/swc-project/swc/blob/main/crates/swc_ecma_transforms_react/src/refresh/mod.rs
///
/// Additional reading:
///  [3] https://github.com/facebook/react/issues/16604#issuecomment-528663101
///  [4] https://github.com/facebook/react/blob/master/packages/react-refresh/src/__tests__/ReactFreshIntegration-test.js
///
/// Instead of a plugin which visits the tree separately, Bun's implementation of fast refresh
/// happens in tandem with the visit pass. The responsibilities of the transform are as follows:
///
/// 1. For all Components (which is defined as any top-level function/function variable, that is
///    named with a capital letter; see `isComponentishName`), register them to the runtime using
///    `$RefreshReg$(ComponentFunction, "Component");`. Implemented in `p.handleReactRefreshRegister`
///    HOC components are also registered, but only through a special case for `export default`
///
/// 2. For all functions which call a Hook (a hook is an identifier matching /^use[A-Z]/):
///     a. Outside of the function, create a signature function `const _s = $RefreshSig$();`
///     b. At the start of the function, call `_s()`
///     c. Record all of the hooks called, the variables they are assigned to, and
///        arguments depending on which hook has been used. `useState` and `useReducer`,
///        for example, are special-cased.
///     d. Directly after the function, call `_s(hook, "<hash>", forceReset)`
///         - If a user-defined hook is called, the alterate form is used:
///           `_s(hook, "<hash>", forceReset, () => [useCustom1, useCustom2])`
///
/// The upstream transforms do not declare `$RefreshReg$` or `$RefreshSig$`. A typical
/// implementation might look like this, prepending this data to the module start:
///
///     import * as Refresh from 'react-refresh/runtime';
///     const $RefreshReg$ = (type, id) => Refresh.register(type, "<file id here>" + id);
///     const $RefreshSig$ = Refresh.createSignatureFunctionForTransform;
///
/// Since Bun is a transpiler *and* bundler, we take a slightly different approach. Aside
/// from including the link to the refresh runtime, our notation of $RefreshReg$ is just
/// pointing at `Refresh.register`, which means when we call it, the second argument has
/// to be a string containing the filepath, not just the component name.
pub struct ReactRefresh {
    // Set if this JSX/TSX file uses the refresh runtime. If so,
    // we must insert an import statement to it.
    register_used: bool = false,
    signature_used: bool = false,

    /// $RefreshReg$ is called on all top-level variables that are
    /// components, as well as HOCs found in the `export default` clause.
    register_ref: Ref = Ref.None,

    /// $RefreshSig$ is called to create a signature function, which is
    /// used by the refresh runtime to perform smart hook tracking.
    create_signature_ref: Ref = Ref.None,

    /// If a comment with '@refresh reset' is seen, we will forward a
    /// force refresh to the refresh runtime. This lets you reset the
    /// state of hooks on an update on a per-component basis.
    // TODO: this is never set
    force_reset: bool = false,

    /// The last hook that was scanned. This is used when visiting
    /// `.s_local`, as we must hash the variable destructure if the
    /// hook's result is assigned directly to a local.
    last_hook_seen: (*E.Call)? = null,

    /// Every function sets up stack memory to hold data related to it's
    /// hook tracking. This is a pointer to that ?HookContext, where an
    /// inner null means there are no hook calls.
    ///
    /// The inner value is initialized when the first hook .e_call is
    /// visited, where the '_s' symbol is reserved. Additional hook calls
    /// append to the `hasher` and `user_hooks` as needed.
    ///
    /// When a function is done visiting, the stack location is checked,
    /// and then it will insert `var _s = ...`, add the `_s()` call at
    /// the start of the function, and then add the call to `_s(func, ...)`.
    hook_ctx_storage: (*HookContext?)? = null,

    /// This is the most recently generated `_s` call. This is used to compare
    /// against seen calls to plain identifiers when in "export default" and in
    /// "const Component =" to know if an expression had been wrapped in a hook
    /// signature function.
    latest_signature_ref: Ref = Ref.None,

    pub const HookContext = struct {
        hasher: std.hash.Wyhash,
        signature_cb: Ref,
        user_hooks: std.AutoArrayHashMapUnmanaged(Ref, Expr),
    };

    // https://github.com/facebook/react/blob/d1afcb43fd506297109c32ff462f6f659f9110ae/packages/react-refresh/src/ReactFreshBabelPlugin.js#L42
    static fn isComponentishName(id: []const u8): bool {
        if (id.len == 0) return false;
        return switch (id[0]) {
            'A'...'Z' => true,
            else => false,
        };
    }

    // https://github.com/facebook/react/blob/d1afcb43fd506297109c32ff462f6f659f9110ae/packages/react-refresh/src/ReactFreshBabelPlugin.js#L408
    static fn isHookName(id: []const u8): bool {
        return id.len >= 4 &&
            strings.hasPrefixComptime(id, "use") &&
            switch (id[3]) {
                'A'...'Z' => true,
                else => false,
            };
    }

    pub const built_in_hooks = bun.ComptimeEnumMap(enum {
        useState,
        useReducer,
        useEffect,
        useLayoutEffect,
        useMemo,
        useCallback,
        useRef,
        useContext,
        useImperativeHandle,
        useDebugValue,
        useId,
        useDeferredValue,
        useTransition,
        useInsertionEffect,
        useSyncExternalStore,
        useFormStatus,
        useFormState,
        useActionState,
        useOptimistic,
    });
}

/// Equivalent of esbuild's js_ast_helpers.ToInt32
pub fn floatToInt32(f: f64): i32 {
    // Special-case non-finite numbers
    if (!std.math.isFinite(f)) {
        return 0;
    }

    const rem = @mod(@abs(f), 4294967296.0);
    const bits = @as(u32, @intFromFloat(rem));
    const signed = @as(i32, @bitCast(bits));
    return if (f < 0) @as(i32, 0) -% signed else signed;
}

pub struct ParseBindingOptions {
    /// This will prevent parsing of destructuring patterns, as using statement
    /// is only allowed to be `using name, name2, name3`, nothing special.
    is_using_statement: bool = false,
}

import ConvertESMExportsForHmr from "./ast/ConvertESMExportsForHmr.zig";
import ImportScanner from "./ast/ImportScanner.zig";
import TypeScript from "./ast/TypeScript.zig";
import fs from "../resolver/fs.zig";
import options from "../bundler/options.zig";
import renamer from "../js_printer/renamer.zig";
pub const KnownGlobal = @import("./ast/KnownGlobal.zig").KnownGlobal;
pub const Parser = @import("./ast/Parser.zig").Parser;
pub const SideEffects = @import("./ast/SideEffects.zig").SideEffects;
pub const foldStringAddition = @import("./ast/foldStringAddition.zig").foldStringAddition;
pub const isPackagePath = @import("../resolver/resolver.zig").isPackagePath;

pub const Ref = @import("./ast/base.zig").Ref;

import importRecord from "../options_types/import_record.zig";
pub const ImportKind = importRecord.ImportKind;
const ImportRecord = importRecord.ImportRecord;

pub const RuntimeFeatures = _runtime.Runtime.Features;
pub const RuntimeImports = _runtime.Runtime.Imports;
pub const RuntimeNames = _runtime.Runtime.Names;

pub const NewParser_ = @import("./ast/P.zig").NewParser_;

pub const StringHashMap = bun.StringHashMap;
pub const js_printer = bun.js_printer;
pub const logger = bun.logger;
type string = []const u8;

pub const js_ast = bun.ast;
pub const B = js_ast.B;
pub const Binding = js_ast.Binding;
pub const BindingNodeIndex = js_ast.BindingNodeIndex;
pub const BindingNodeList = js_ast.BindingNodeList;
pub const E = js_ast.E;
pub const Expr = js_ast.Expr;
pub const ExprNodeIndex = js_ast.ExprNodeIndex;
pub const ExprNodeList = js_ast.ExprNodeList;
pub const LocRef = js_ast.LocRef;
pub const S = js_ast.S;
pub const Scope = js_ast.Scope;
pub const Stmt = js_ast.Stmt;
pub const StmtNodeIndex = js_ast.StmtNodeIndex;
pub const StmtNodeList = js_ast.StmtNodeList;
pub const Symbol = js_ast.Symbol;

const G = js_ast.G;
const Decl = G.Decl;

pub const Op = js_ast.Op;
pub const Level = js_ast.Op.Level;

pub const js_lexer = bun.js_lexer;
pub const T = js_lexer.T;

pub const AutoHashMap = std.AutoHashMap;
const List = std.ArrayListUnmanaged;
const ListManaged = std.array_list.Managed;
const Allocator = std.mem.Allocator;

import _runtime from "./runtime.zig";
const Define = @import("../bundler/defines.zig").Define;
const NewParser = @import("./ast/P.zig").NewParser;
const ObjectPool = @import("../collections/pool.zig").ObjectPool;

const Index = @import("./ast/base.zig").Index;
const RefCtx = @import("./ast/base.zig").RefCtx;

import bun from "bun";
const Output = bun.Output;
const StringHashMapUnmanaged = bun.StringHashMapUnmanaged;
const assert = bun.assert;
const strings = bun.strings;
