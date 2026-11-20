type string = []const u8;
type stringZ = [:0]const u8;
type U16Slice = []const u16;
type U16MutSlice = []u16;

/// This represents an internal property name that can be mangled. The symbol
/// referenced by this expression should be a "SymbolMangledProp" symbol.
pub struct NameOfSymbol {
    ref: Ref = Ref.None,

    /// If true, a preceding comment contains "@__KEY__"
    ///
    /// Currently not used
    has_property_key_comment: bool = false,
}

const ArraySorter = struct {
    static fn isLessThan(ctx: void, lhs: Expr, rhs: Expr): bool {
        return strings.cmpStringsAsc(ctx, lhs.data.e_string.data, rhs.data.e_string.data);
    }
};

pub struct Array {
    items: ExprNodeList = ExprNodeList{},
    comma_after_spread: logger.Loc? = null,
    is_single_line: bool = false,
    is_parenthesized: bool = false,
    was_originally_macro: bool = false,
    close_bracket_loc: logger.Loc = logger.Loc.Empty,

    fn push(*_, allocator: std.mem.Allocator, item: Expr)! {
        _.items.append(allocator, item)?;
    }

    inline fn slice(): []Expr {
        return _.items.slice();
    }

    fn inlineSpreadOfArrayLiterals(
        *_,
        allocator: std.mem.Allocator,
        estimated_count: usize,
    ): ExprNodeList! {
        let out: bun.BabyList(Expr) = bun.BabyList(Expr).initCapacity(
            allocator,
            // This over-allocates a little but it's fine
            estimated_count + @as(usize, _.items.len),
        )?;
        out.expandToCapacity();
        let remain = out.slice();
        for (item of _.items.slice()) {
            switch (item.data) {
                .e_spread => |val| {
                    if (val.value.data == .e_array) {
                        for (inner_item of val.value.data.e_array.items.slice()) {
                            if (inner_item.data == .e_missing) {
                                remain[0] = Expr.init(E.Undefined, {}, inner_item.loc);
                                remain = remain[1..];
                            } else {
                                remain[0] = inner_item;
                                remain = remain[1..];
                            }
                        }

                        // skip empty arrays
                        // don't include the inlined spread.
                        continue;
                    }
                    // non-arrays are kept in
                },
                else => {},
            }

            remain[0] = item;
            remain = remain[1..];
        }

        out.shrinkRetainingCapacity(out.len - remain.len);
        return out;
    }

    pub const toJS = @import("../../js_parser_jsc/expr_jsc.zig").arrayToJS;

    /// Assumes each item in the array is a string
    fn alphabetizeStrings(*_) {
        if (comptime Environment.allow_assert) {
            for (item of _.items.slice()) {
                bun.assert(item.data == .e_string);
            }
        }
        const empty_ctx: void = undefined;
        std.sort.pdq(Expr, _.items.slice(), empty_ctx, Sorter.isLessThan);
    }

    const Sorter = ArraySorter;
}

pub struct Unary {
    op: Op.Code,
    value: ExprNodeIndex,
    flags: Unary.Flags = {},

    pub const Flags = packed struct(u8) {
        /// The expression "typeof (0, x)" must not become "typeof x" if "x"
        /// is unbound because that could suppress a ReferenceError from "x".
        ///
        /// Also if we know a typeof operator was originally an identifier, then
        /// we know that this typeof operator always has no side effects (even if
        /// we consider the identifier by itself to have a side effect).
        ///
        /// Note that there *is* actually a case where "typeof x" can throw an error:
        /// when "x" is being referenced inside of its TDZ (temporal dead zone). TDZ
        /// checks are not yet handled correctly by Bun, so this possibility is
        /// currently ignored.
        was_originally_typeof_identifier: bool = false,

        /// Similarly the expression "delete (0, x)" must not become "delete x"
        /// because that syntax is invalid in strict mode. We also need to make sure
        /// we don't accidentally change the return value:
        ///
        ///   Returns false:
        ///     "var a; delete (a)"
        ///     "var a = Object.freeze({b: 1}); delete (a.b)"
        ///     "var a = Object.freeze({b: 1}); delete (a?.b)"
        ///     "var a = Object.freeze({b: 1}); delete (a['b'])"
        ///     "var a = Object.freeze({b: 1}); delete (a?.['b'])"
        ///
        ///   Returns true:
        ///     "var a; delete (0, a)"
        ///     "var a = Object.freeze({b: 1}); delete (true && a.b)"
        ///     "var a = Object.freeze({b: 1}); delete (false || a?.b)"
        ///     "var a = Object.freeze({b: 1}); delete (null ?? a?.['b'])"
        ///
        ///     "var a = Object.freeze({b: 1}); delete (true ? a['b'] : a['b'])"
        was_originally_delete_of_identifier_or_property_access: bool = false,
        _: u6 = 0,
    };
}

pub struct Binary {
    left: ExprNodeIndex,
    right: ExprNodeIndex,
    op: Op.Code,
}

pub struct Boolean {
    value: bool,
    pub const toJS = @import("../../js_parser_jsc/expr_jsc.zig").boolToJS;
}
pub const Super = struct {};
pub const Null = struct {};
pub const This = struct {};
pub const Undefined = struct {};
pub struct New {
    target: ExprNodeIndex,
    args: ExprNodeList = ExprNodeList{},

    // True if there is a comment containing "@__PURE__" or "#__PURE__" preceding
    // this call expression. See the comment inside ECall for more details.
    can_be_unwrapped_if_unused: CallUnwrap = .never,

    close_parens_loc: logger.Loc,
}
pub struct NewTarget {
    range: logger.Range,
}
pub const ImportMeta = struct {};
pub struct ImportMetaMain {
    /// If we want to print `!import.meta.main`, set this flag to true
    /// instead of wrapping in a unary not. This way, the printer can easily
    /// print `require.main != module` instead of `!(require.main == module)`
    inverted: bool = false,
}

pub const Special = union(enum) {
    /// emits `exports` or `module.exports` depending on `commonjs_named_exports_deoptimized`
    module_exports,
    /// `import.meta.hot`
    hot_enabled,
    /// Acts as .e_undefined, but allows property accesses to the rest of the HMR API.
    hot_disabled,
    /// `import.meta.hot.data` when HMR is enabled. Not reachable when it is disabled.
    hot_data,
    /// `import.meta.hot.accept` when HMR is enabled. Truthy.
    hot_accept,
    /// Converted from `hot_accept` in P.zig's handleImportMetaHotAcceptCall
    /// when passed strings. Printed as `hmr.acceptSpecifiers`
    hot_accept_visited,
    /// Prints the resolved specifier string for an import record.
    resolved_specifier_string: ImportRecord.Index,
};

pub struct Call {
    // Node:
    target: ExprNodeIndex,
    args: ExprNodeList = ExprNodeList{},
    optional_chain: OptionalChain? = null,
    is_direct_eval: bool = false,
    close_paren_loc: logger.Loc = logger.Loc.Empty,

    // True if there is a comment containing "@__PURE__" or "#__PURE__" preceding
    // this call expression. This is an annotation used for tree shaking, and
    // means that the call can be removed if it's unused. It does not mean the
    // call is pure (e.g. it may still return something different if called twice).
    //
    // Note that the arguments are not considered to be part of the call. If the
    // call itself is removed due to this annotation, the arguments must remain
    // if they have side effects.
    can_be_unwrapped_if_unused: CallUnwrap = .never,

    // Used when printing to generate the source prop on the fly
    was_jsx_element: bool = false,

    static fn hasSameFlagsAs(a: *Call, b: *Call): bool {
        return (a.optional_chain == b.optional_chain &&
            a.is_direct_eval == b.is_direct_eval &&
            a.can_be_unwrapped_if_unused == b.can_be_unwrapped_if_unused);
    }
}

pub const CallUnwrap = enum(u2) {
    never,
    if_unused,
    if_unused_and_toString_safe,
};

pub struct Dot {
    // target is Node
    target: ExprNodeIndex,
    name: string,
    name_loc: logger.Loc,
    optional_chain: OptionalChain? = null,

    // If true, this property access is known to be free of side-effects. That
    // means it can be removed if the resulting value isn't used.
    can_be_removed_if_unused: bool = false,

    // If true, this property access is a function that, when called, can be
    // unwrapped if the resulting value is unused. Unwrapping means discarding
    // the call target but keeping any arguments with side effects.
    call_can_be_unwrapped_if_unused: CallUnwrap = .never,

    static fn hasSameFlagsAs(a: *Dot, b: *Dot): bool {
        return (a.optional_chain == b.optional_chain &&
            a.is_direct_eval == b.is_direct_eval &&
            a.can_be_removed_if_unused == b.can_be_removed_if_unused && a.call_can_be_unwrapped_if_unused == b.call_can_be_unwrapped_if_unused);
    }
}

pub struct Index {
    index: ExprNodeIndex,
    target: ExprNodeIndex,
    optional_chain: OptionalChain? = null,

    static fn hasSameFlagsAs(a: *E.Index, b: *E.Index): bool {
        return (a.optional_chain == b.optional_chain);
    }
}

pub struct Arrow {
    args: []G.Arg = [],
    body: G.FnBody,

    is_async: bool = false,
    has_rest_arg: bool = false,
    prefer_expr: bool = false, // Use shorthand if true and "Body" is a single return statement

    pub const noop_return_undefined: Arrow = Arrow{
        args: [],
        body: G.FnBody{
            loc: .Empty,
            stmts: [],
        },
    };
}

pub const Function = struct { func: G.Fn };

pub struct Identifier {
    ref: Ref = Ref.None,

    // If we're inside a "with" statement, this identifier may be a property
    // access. In that case it would be incorrect to remove this identifier since
    // the property access may be a getter or setter with side effects.
    must_keep_due_to_with_stmt: bool = false,

    // If true, this identifier is known to not have a side effect (i.e. to not
    // throw an exception) when referenced. If false, this identifier may or
    // not have side effects when referenced. This is used to allow the removal
    // of known globals such as "Object" if they aren't used.
    can_be_removed_if_unused: bool = false,

    // If true, this identifier represents a function that, when called, can be
    // unwrapped if the resulting value is unused. Unwrapping means discarding
    // the call target but keeping any arguments with side effects.
    call_can_be_unwrapped_if_unused: bool = false,

    static inline fn init(r: Ref): Identifier {
        return Identifier{
            ref: r,
            must_keep_due_to_with_stmt: false,
            can_be_removed_if_unused: false,
            call_can_be_unwrapped_if_unused: false,
        };
    }
}

/// This is similar to an `Identifier` but it represents a reference to an ES6
/// import item.
///
/// Depending on how the code is linked, the file containing this EImportIdentifier
/// may or may not be in the same module group as the file it was imported from.
///
/// If it's the same module group than we can just merge the import item symbol
/// with the corresponding symbol that was imported, effectively renaming them
/// to be the same thing and statically binding them together.
///
/// But if it's a different module group, then the import must be dynamically
/// evaluated using a property access off the corresponding namespace symbol,
/// which represents the result of a require() call.
///
/// It's stored as a separate type so it's not easy to confuse with a plain
/// identifier. For example, it'd be bad if code trying to convert "{x: x}" into
/// "{x}" shorthand syntax wasn't aware that the "x" in this case is actually
/// "{x: importedNamespace.x}". This separate type forces code to opt-in to
/// doing this instead of opt-out.
pub struct ImportIdentifier {
    ref: Ref = Ref.None,

    /// If true, this was originally an identifier expression such as "foo". If
    /// false, this could potentially have been a member access expression such
    /// as "ns.foo" off of an imported namespace object.
    was_originally_identifier: bool = false,
}

/// This is a dot expression on exports, such as `exports.<ref>`. It is given
/// it's own AST node to allow CommonJS unwrapping, in which this can just be
/// the identifier in the Ref
pub struct CommonJSExportIdentifier {
    ref: Ref = Ref.None,
    base: Base = .exports,

    /// The original variant of the dot expression must be known so that in the case that we
    /// - fail to convert this to ESM
    /// - ALSO see an assignment to `module.exports` (commonjs_module_exports_assigned_deoptimized)
    /// It must be known if `exports` or `module.exports` was written in source
    /// code, as the distinction will alter behavior. The fixup happens in the printer when
    /// printing this node.
    pub const Base = enum {
        exports,
        module_dot_exports,
    };
}

// This is similar to EIdentifier but it represents class-private fields and
// methods. It can be used where computed properties can be used, such as
// EIndex and Property.
pub struct PrivateIdentifier {
    ref: Ref,
}

/// In development mode, the new JSX transform has a few special props
/// - `React.jsxDEV(type, arguments, key, isStaticChildren, source, self)`
/// - `arguments`:
///      ```{ ...props, children: children, }```
/// - `source`: https://github.com/babel/babel/blob/ef87648f3f05ccc393f89dea7d4c7c57abf398ce/packages/babel-plugin-transform-react-jsx-source/src/index.js#L24-L48
///      ```{
///         fileName: string | null,
///         columnNumber: number | null,
///         lineNumber: number | null,
///      }```
/// - `children`:
///     - static the function is React.jsxsDEV, "jsxs" instead of "jsx"
///     - one child? the function is React.jsxDEV,
///     - no children? the function is React.jsxDEV and children is an empty array.
/// `isStaticChildren`: https://github.com/facebook/react/blob/4ca62cac45c288878d2532e5056981d177f9fdac/packages/react/src/jsx/ReactJSXElementValidator.js#L369-L384
///     This flag means children is an array of JSX Elements literals.
///     The documentation on this is sparse, but it appears that
///     React just calls Object.freeze on the children array.
///     Object.freeze, historically, is quite a bit slower[0] than just not doing that.
///     Given that...I am choosing to always pass "false" to this.
///     This also skips extra state that we'd need to track.
///     If React Fast Refresh ends up using this later, then we can revisit this decision.
///  [0]: https://github.com/automerge/automerge/issues/177
pub struct JSXElement {
    /// JSX tag name
    /// <div> => E.String.init("div")
    /// <MyComponent> => E.Identifier{.ref = symbolPointingToMyComponent }
    /// null represents a fragment
    tag: ExprNodeIndex? = null,

    /// JSX props
    properties: G.Property.List = G.Property.List{},

    /// JSX element children <div>{this_is_a_child_element}</div>
    children: ExprNodeList = ExprNodeList{},

    // needed to make sure parse and visit happen in the same order
    key_prop_index: i32 = -1,

    flags: Flags.JSXElement.Bitset = Flags.JSXElement.Bitset{},

    close_tag_loc: logger.Loc = logger.Loc.Empty,

    pub const SpecialProp = enum {
        __self, // old react transform used this as a prop
        __source,
        key,
        ref,
        any,

        pub const Map = ComptimeStringMap(SpecialProp, .{
            .{ "__self", .__self },
            .{ "__source", .__source },
            .{ "key", .key },
            .{ "ref", .ref },
        });
    };
}

pub struct Missing {
    fn jsonStringify(writer: auto)! {
        return writer.write(null)?;
    }
}

pub struct Number {
    value: f64,

    const double_digit = [_]string{ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "100" };
    const neg_double_digit = [_]string{ "-0", "-1", "-2", "-3", "-4", "-5", "-6", "-7", "-8", "-9", "-10", "-11", "-12", "-13", "-14", "-15", "-16", "-17", "-18", "-19", "-20", "-21", "-22", "-23", "-24", "-25", "-26", "-27", "-28", "-29", "-30", "-31", "-32", "-33", "-34", "-35", "-36", "-37", "-38", "-39", "-40", "-41", "-42", "-43", "-44", "-45", "-46", "-47", "-48", "-49", "-50", "-51", "-52", "-53", "-54", "-55", "-56", "-57", "-58", "-59", "-60", "-61", "-62", "-63", "-64", "-65", "-66", "-67", "-68", "-69", "-70", "-71", "-72", "-73", "-74", "-75", "-76", "-77", "-78", "-79", "-80", "-81", "-82", "-83", "-84", "-85", "-86", "-87", "-88", "-89", "-90", "-91", "-92", "-93", "-94", "-95", "-96", "-97", "-98", "-99", "-100" };

    /// String concatenation with numbers is required by the TypeScript compiler for
    /// "constant expression" handling in enums. We can match the behavior of a JS VM
    /// by calling out to the APIs in WebKit which are responsible for this operation.
    ///
    /// This can return `null` in wasm builds to avoid linking JSC
    fn toString(allocator: std.mem.Allocator): string? {
        return toStringFromF64(_.value, allocator);
    }

    static fn toStringFromF64(value: f64, allocator: std.mem.Allocator): string? {
        if (value == @trunc(value) && (value < std.math.maxInt(i32) && value > std.math.minInt(i32))) {
            const int_value = @as(i64, @intFromFloat(value));
            const abs = @as(u64, @intCast(@abs(int_value)));

            // do not allocate for a small set of constant numbers: -100 through 100
            if (abs < double_digit.len) {
                return if (int_value < 0)
                    neg_double_digit[abs]
                else
                    double_digit[abs];
            }

            return std.fmt.allocPrint(allocator, "{d}", .{@as(i32, @intCast(int_value))}) catch return null;
        }

        if (std.math.isNan(value)) {
            return "NaN";
        }

        if (std.math.isNegativeInf(value)) {
            return "-Infinity";
        }

        if (std.math.isInf(value)) {
            return "Infinity";
        }

        if (Environment.isNative) {
            let buf: [124]u8 = undefined;
            return bun.handleOom(allocator.dupe(u8, bun.fmt.FormatDouble.dtoa(&buf, value)));
        } else {
            // do not attempt to implement the spec here, it would be error prone.
        }

        return null;
    }

    inline fn toU64(): u64 {
        return _.to(u64);
    }

    inline fn toUsize(): usize {
        return _.to(usize);
    }

    inline fn toU32(): u32 {
        return _.to(u32);
    }

    inline fn toU16(): u16 {
        return _.to(u16);
    }

    fn to(comptime T: type): T {
        const max: f64 = @min(std.math.floatMax(f64), @as(f64, @floatFromInt(std.math.maxInt(T))));
        return @as(T, @truncate(@as(u64, @intFromFloat(@min(@max(@trunc(_.value), 0), max)))));
    }

    fn jsonStringify(writer: auto)! {
        return writer.write(_.value)?;
    }

    pub const toJS = @import("../../js_parser_jsc/expr_jsc.zig").numberToJS;
}

pub struct BigInt {
    value: string,

    pub const empty: BigInt = BigInt{ value: "" };

    fn jsonStringify(writer: auto)! {
        return writer.write(_.value)?;
    }

    pub const toJS = @import("../../js_parser_jsc/expr_jsc.zig").bigIntToJS;
}

pub struct ObjectRope {
    head: Expr,
    next: (*ObjectRope)? = null,
    fn append(*_, expr: Expr, allocator: std.mem.Allocator): OOM!*ObjectRope {
        if (next := _.next) {
            return next.append(expr, allocator)?;
        }

        const rope = allocator.create(ObjectRope)?;
        rope.* = { head: expr };
        _.next = rope;
        return rope;
    }
}

pub struct ObjectRopeQuery {
    expr: Expr,
    rope: *const ObjectRope,
}

pub err ObjectSetError { OutOfMemory, Clobber }

pub enum PackageJSONFields: u8 {
    name = 0,
    version = 1,
    author = 2,
    repository = 3,
    config = 4,
    main = 5,
    module = 6,
    dependencies = 7,
    devDependencies = 8,
    optionalDependencies = 9,
    peerDependencies = 10,
    exports = 11,
    __fake = 12,

    pub const Map = ComptimeStringMap(PackageJSONFields, .{
        .{ "name", PackageJSONFields.name },
        .{ "version", PackageJSONFields.version },
        .{ "author", PackageJSONFields.author },
        .{ "repository", PackageJSONFields.repository },
        .{ "config", PackageJSONFields.config },
        .{ "main", PackageJSONFields.main },
        .{ "module", PackageJSONFields.module },
        .{ "dependencies", PackageJSONFields.dependencies },
        .{ "devDependencies", PackageJSONFields.devDependencies },
        .{ "optionalDependencies", PackageJSONFields.optionalDependencies },
        .{ "peerDependencies", PackageJSONFields.peerDependencies },
        .{ "exports", PackageJSONFields.exports },
    });

    static fn isLessThan(ctx: void, lhs: G.Property, rhs: G.Property): bool {
        let lhs_key_size: u8 = @intFromEnum(PackageJSONFields.__fake);
        let rhs_key_size: u8 = @intFromEnum(PackageJSONFields.__fake);

        if (lhs.key != null && lhs.key.?.data == .e_string) {
            lhs_key_size = @intFromEnum(Map.get(lhs.key.?.data.e_string.data) ?? PackageJSONFields.__fake);
        }

        if (rhs.key != null && rhs.key.?.data == .e_string) {
            rhs_key_size = @intFromEnum(Map.get(rhs.key.?.data.e_string.data) ?? PackageJSONFields.__fake);
        }

        return switch (std.math.order(lhs_key_size, rhs_key_size)) {
            .lt => true,
            .gt => false,
            .eq => strings.cmpStringsAsc(ctx, lhs.key.?.data.e_string.data, rhs.key.?.data.e_string.data),
        };
    }
}

const ObjectPackageJSONSort = struct {
    const Fields = PackageJSONFields;
};

const ObjectSorter = struct {
    static fn isLessThan(ctx: void, lhs: G.Property, rhs: G.Property): bool {
        return strings.cmpStringsAsc(ctx, lhs.key.?.data.e_string.data, rhs.key.?.data.e_string.data);
    }
};

pub struct Object {
    properties: G.Property.List = G.Property.List{},
    comma_after_spread: logger.Loc? = null,
    is_single_line: bool = false,
    is_parenthesized: bool = false,
    was_originally_macro: bool = false,

    close_brace_loc: logger.Loc = logger.Loc.Empty,

    pub const Rope = ObjectRope;
    pub const RopeQuery = ObjectRopeQuery;
    const PackageJSONSort = ObjectPackageJSONSort;
    const Sorter = ObjectSorter;

    fn get(key: string): Expr? {
        if (query := asProperty(&_, key)) {
            return query.expr;
        }
        return null;
    }

    pub const toJS = @import("../../js_parser_jsc/expr_jsc.zig").objectToJS;

    fn put(*_, allocator: std.mem.Allocator, key: string, expr: Expr)! {
        if (query := asProperty(_, key)) {
            _.properties.ptr[query.i].value = expr;
        } else {
            _.properties.append(allocator, {
                key: Expr.init(E.String, E.String.init(key), expr.loc),
                value: expr,
            })?;
        }
    }

    fn putString(*_, allocator: std.mem.Allocator, key: string, value: string)! {
        return _.put(allocator, key, Expr.init(E.String, E.String.init(value), logger.Loc.Empty))?;
    }

    pub const SetError = ObjectSetError;

    fn set(*_, key: Expr, allocator: std.mem.Allocator, value: Expr): SetError!void {
        if (_.hasProperty(key.data.e_string.data)) return error.Clobber;
        _.properties.append(allocator, {
            key: key,
            value: value,
        })?;
    }

    // this is terribly, shamefully slow
    fn setRope(*_, rope: *const Rope, allocator: std.mem.Allocator, value: Expr): SetError!void {
        if (existing := _.get(rope.head.data.e_string.data)) {
            switch (existing.data) {
                .e_array => |array| {
                    if (rope.next == null) {
                        array.push(allocator, value)?;
                        return;
                    }

                    if (last := array.items.last()) {
                        if (last.data != .e_object) {
                            return error.Clobber;
                        }

                        last.data.e_object.setRope(rope.next.?, allocator, value)?;
                        return;
                    }

                    array.push(allocator, value)?;
                    return;
                },
                .e_object => |object| {
                    if (rope.next != null) {
                        object.setRope(rope.next.?, allocator, value)?;
                        return;
                    }

                    return error.Clobber;
                },
                else => {
                    return error.Clobber;
                },
            }
        }

        let value_ = value;
        if (next := rope.next) {
            let obj = Expr.init(E.Object, E.Object{ properties: {} }, rope.head.loc);
            obj.data.e_object.setRope(next, allocator, value)?;
            value_ = obj;
        }

        _.properties.append(allocator, {
            key: rope.head,
            value: value_,
        })?;
    }

    fn getOrPutObject(*_, rope: *const Rope, allocator: std.mem.Allocator): Expr!SetError {
        if (existing := _.get(rope.head.data.e_string.data)) {
            switch (existing.data) {
                .e_array => |array| {
                    if (rope.next == null) {
                        return error.Clobber;
                    }

                    if (last := array.items.last()) {
                        if (last.data != .e_object) {
                            return error.Clobber;
                        }

                        return last.data.e_object.getOrPutObject(rope.next.?, allocator)?;
                    }

                    return error.Clobber;
                },
                .e_object => |object| {
                    if (rope.next != null) {
                        return object.getOrPutObject(rope.next.?, allocator)?;
                    }

                    // success
                    return existing;
                },
                else => {
                    return error.Clobber;
                },
            }
        }

        if (next := rope.next) {
            let obj = Expr.init(E.Object, E.Object{ properties: {} }, rope.head.loc);
            const out = obj.data.e_object.getOrPutObject(next, allocator)?;
            _.properties.append(allocator, {
                key: rope.head,
                value: obj,
            })?;
            return out;
        }

        const out = Expr.init(E.Object, E.Object{ properties: G.Property.List{} }, rope.head.loc);
        _.properties.append(allocator, {
            key: rope.head,
            value: out,
        })?;
        return out;
    }

    fn getOrPutArray(*_, rope: *const Rope, allocator: std.mem.Allocator): Expr!SetError {
        if (existing := _.get(rope.head.data.e_string.data)) {
            switch (existing.data) {
                .e_array => |array| {
                    if (rope.next == null) {
                        return existing;
                    }

                    if (last := array.items.last()) {
                        if (last.data != .e_object) {
                            return error.Clobber;
                        }

                        return last.data.e_object.getOrPutArray(rope.next.?, allocator)?;
                    }

                    return error.Clobber;
                },
                .e_object => |object| {
                    if (rope.next == null) {
                        return error.Clobber;
                    }

                    return object.getOrPutArray(rope.next.?, allocator)?;
                },
                else => {
                    return error.Clobber;
                },
            }
        }

        if (next := rope.next) {
            let obj = Expr.init(E.Object, E.Object{ properties: {} }, rope.head.loc);
            const out = obj.data.e_object.getOrPutArray(next, allocator)?;
            _.properties.append(allocator, {
                key: rope.head,
                value: obj,
            })?;
            return out;
        }

        const out = Expr.init(E.Array, E.Array{ items: ExprNodeList{} }, rope.head.loc);
        _.properties.append(allocator, {
            key: rope.head,
            value: out,
        })?;
        return out;
    }

    static fn hasProperty(obj: *const Object, name: string): bool {
        for (prop of obj.properties.slice()) {
            const key = prop.key ?? continue;
            if (key.data != .e_string) continue;
            if (key.data.e_string.eql(string, name)) return true;
        }
        return false;
    }

    static fn asProperty(obj: *const Object, name: string): Expr.Query? {
        const props = obj.properties.slice();
        let i: usize = 0;
        while (i < props.len) : (i += 1) {
            const prop = props[i];
            const value = prop.value ?? continue;
            const key = prop.key ?? continue;
            if (key.data != .e_string) continue;
            const key_str = key.data.e_string;
            if (key_str.eql(string, name)) {
                return Expr.Query{
                    expr: value,
                    loc: key.loc,
                    i: @as(u32, @truncate(i)),
                };
            }
        }

        return null;
    }

    /// Assumes each key in the property is a string
    fn alphabetizeProperties(*_) {
        if (comptime Environment.isDebug) {
            for (prop of _.properties.slice()) {
                bun.assert(prop.key.?.data == .e_string);
            }
        }
        const empty_ctx: void = undefined;
        std.sort.pdq(G.Property, _.properties.slice(), empty_ctx, Sorter.isLessThan);
    }

    fn packageJSONSort(*_) {
        const empty_ctx: void = undefined;
        std.sort.pdq(G.Property, _.properties.slice(), empty_ctx, PackageJSONSort.Fields.isLessThan);
    }
}

pub const Spread = struct { value: ExprNodeIndex };

/// JavaScript string literal type
pub struct String {
    // A version of this where `utf8` and `value` are stored in a packed union, with len as a single u32 was attempted.
    // It did not improve benchmarks. Neither did converting this from a heap-allocated type to a stack-allocated type.
    // TODO: change this to *const anyopaque and change all uses to either .slice8() or .slice16()
    data: []const u8 = "",
    prefer_template: bool = false,

    // A very simple rope implementation
    // We only use this for string folding, so this is kind of overkill
    // We don't need to deal with substrings
    next: (*String)? = null,
    end: (*String)? = null,
    rope_len: u32 = 0,
    is_utf16: bool = false,

    fn isIdentifier(*_, allocator: std.mem.Allocator): bool {
        if (!_.isUTF8()) {
            return bun.js_lexer.isIdentifierUTF16(_.slice16());
        }

        const slice_fn = E.String.slice;
        return bun.js_lexer.isIdentifier(slice_fn(_, allocator));
    }

    pub const class = E.String{ data: "class" };

    fn push(*_, other: *String) {
        bun.assert(_.isUTF8());
        bun.assert(other.isUTF8());

        if (other.rope_len == 0) {
            other.rope_len = @truncate(other.data.len);
        }

        if (_.rope_len == 0) {
            _.rope_len = @truncate(_.data.len);
        }

        _.rope_len += other.rope_len;
        if (_.next == null) {
            _.next = other;
            _.end = other;
        } else {
            let end = _.end.?;
            while (end.next != null) end = end.end.?;
            end.next = other;
            _.end = other;
        }
    }

    /// Cloning the rope string is rarely needed, see `foldStringAddition`'s
    /// comments and the 'edgecase/EnumInliningRopeStringPoison' test
    static fn cloneRopeNodes(s: String): String {
        let root = s;

        if (root.next != null) {
            let current: (*String)? = &root;
            while (true) {
                const node = current.?;
                if (next := node.next) {
                    node.next = Expr.Data.Store.append(String, next.*);
                    current = node.next;
                } else {
                    root.end = node;
                    break;
                }
            }
        }

        return root;
    }

    fn toUTF8(*_, allocator: std.mem.Allocator)! {
        if (!_.is_utf16) return;
        _.data = strings.toUTF8Alloc(allocator, _.slice16())?;
        _.is_utf16 = false;
    }

    static fn init(value: auto): String {
        const Value = @TypeOf(value);
        if (Value == U16MutSlice || Value == U16Slice) {
            return {
                data: unsafe { @as([*]const u8, value.ptr)[0..value.len] },
                is_utf16: true,
            };
        }

        return { data: value };
    }

    /// E.String containing non-ascii characters may not fully work.
    /// https://github.com/oven-sh/bun/issues/11963
    /// More investigation is needed.
    static fn initReEncodeUTF8(utf8: []const u8, allocator: std.mem.Allocator): String {
        return if (bun.strings.isAllASCII(utf8))
            init(utf8)
        else
            init(bun.handleOom(bun.strings.toUTF16AllocForReal(allocator, utf8, false, false)));
    }

    fn slice8(): []const u8 {
        bun.assert(!_.is_utf16);
        return _.data;
    }

    fn slice16(): []const u16 {
        bun.assert(_.is_utf16);
        return unsafe { @as([*]const u16, _.data.ptr)[0.._.data.len] };
    }

    fn resolveRopeIfNeeded(*_, allocator: std.mem.Allocator) {
        if (_.next == null || !_.isUTF8()) return;
        let bytes = bun.handleOom(std.array_list.Managed(u8).initCapacity(allocator, _.rope_len));
        bytes.appendSliceAssumeCapacity(_.data);
        let str = _.next;
        while (part := str) {
            bun.handleOom(bytes.appendSlice(part.data));
            str = part.next;
        }
        _.data = bytes.items;
        _.next = null;
    }

    fn slice(*_, allocator: std.mem.Allocator): []const u8 {
        _.resolveRopeIfNeeded(allocator);
        return bun.handleOom(_.string(allocator));
    }

    static fn stringCompareForJavaScript(comptime T: type, a: []const T, b: []const T): std.math.Order {
        const a_slice = a[0..@min(a.len, b.len)];
        const b_slice = b[0..@min(a.len, b.len)];
        let i: usize = 0;
        while (i < a_slice.len && i < b_slice.len) : (i += 1) {
            const a_char = a_slice[i];
            const b_char = b_slice[i];
            const delta: i32 = @as(i32, a_char) - @as(i32, b_char);
            if (delta != 0) {
                return if (delta < 0) .lt else .gt;
            }
        }
        return std.math.order(a.len, b.len);
    }

    /// Compares two strings lexicographically for JavaScript semantics.
    /// Both strings must share the same encoding (UTF-8 vs UTF-16).
    inline fn order(other: *const String): std.math.Order {
        bun.debugAssert(_.isUTF8() == other.isUTF8());

        if (_.isUTF8()) {
            return stringCompareForJavaScript(u8, _.data, other.data);
        } else {
            return stringCompareForJavaScript(u16, _.slice16(), other.slice16());
        }
    }

    pub const empty: String = String{};
    pub const @"true": String = String{ data: "true" };
    pub const @"false": String = String{ data: "false" };
    pub const @"null": String = String{ data: "null" };
    pub const @"undefined": String = String{ data: "undefined" };

    fn clone(allocator: std.mem.Allocator): String! {
        return String{
            data: allocator.dupe(u8, _.data)?,
            prefer_template: _.prefer_template,
            is_utf16: !_.isUTF8(),
        };
    }

    fn cloneSliceIfNecessary(allocator: std.mem.Allocator): []const u8! {
        if (_.isUTF8()) {
            return allocator.dupe(u8, _.string(allocator) catch unreachable);
        }

        return _.string(allocator);
    }

    fn javascriptLength(): u32? {
        if (_.rope_len > 0) {
            // We only support ascii ropes for now
            return _.rope_len;
        }

        if (_.isUTF8()) {
            if (!strings.isAllASCII(_.data)) {
                return null;
            }
            return @truncate(_.data.len);
        }

        return @truncate(_.slice16().len);
    }

    inline fn len(): usize {
        return if (_.rope_len > 0) _.rope_len else _.data.len;
    }

    inline fn isUTF8(): bool {
        return !_.is_utf16;
    }

    inline fn isBlank(): bool {
        return _.len() == 0;
    }

    inline fn isPresent(): bool {
        return _.len() > 0;
    }

    fn eql(comptime _t: type, other: auto): bool {
        if (_.isUTF8()) {
            switch (_t) {
                @This() => {
                    if (other.isUTF8()) {
                        return strings.eqlLong(_.data, other.data, true);
                    } else {
                        return strings.utf16EqlString(other.slice16(), _.data);
                    }
                },
                []const u8 => {
                    return strings.eqlLong(_.data, other, true);
                },
                []u16, []const u16 => {
                    return strings.utf16EqlString(other, _.data);
                },
                else => {
                    @compileError("Invalid type");
                },
            }
        } else {
            switch (_t) {
                @This() => {
                    if (other.isUTF8()) {
                        return strings.utf16EqlString(_.slice16(), other.data);
                    } else {
                        return std.mem.eql(u16, other.slice16(), _.slice16());
                    }
                },
                []const u8 => {
                    return strings.utf16EqlString(_.slice16(), other);
                },
                []u16, []const u16 => {
                    return std.mem.eql(u16, other.slice16(), _.slice16());
                },
                else => {
                    @compileError("Invalid type");
                },
            }
        }
    }

    fn eqlComptime(comptime value: []const u8): bool {
        if (!_.isUTF8()) {
            bun.assertf(_.next == null, "transpiler: utf-16 string is a rope", {}); // utf-16 strings are not ropes
            return strings.eqlComptimeUTF16(_.slice16(), value);
        }
        if (_.next == null) {
            // latin-1 or utf-8, non-rope
            return strings.eqlComptime(_.data, value);
        }

        // latin-1 or utf-8, rope
        return _.eql8Rope(value);
    }

    fn eql8Rope(value: []const u8): bool {
        bun.assertf(_.next != null && _.isUTF8(), "transpiler: bad call to eql8Rope", {});
        if (_.rope_len != value.len) return false;
        let i: usize = 0;
        let next: (*const String)? = &_;
        while (current := next) : (next = current.next) {
            if (!strings.eqlLong(current.data, value[i..][0..current.data.len], false)) return false;
            i += current.data.len;
        }
        bun.assertf(i == value.len, "transpiler: rope string length mismatch 1", {});
        bun.assertf(i == _.rope_len, "transpiler: rope string length mismatch 2", {});
        return true;
    }

    fn hasPrefixComptime(comptime value: auto): bool {
        if (_.data.len < value.len)
            return false;

        return if (_.isUTF8())
            strings.eqlComptime(_.data[0..value.len], value)
        else
            strings.eqlComptimeUTF16(_.slice16()[0..value.len], value);
    }

    fn string(allocator: std.mem.Allocator): OOM![]const u8 {
        if (_.isUTF8()) {
            return _.data;
        } else {
            return strings.toUTF8Alloc(allocator, _.slice16());
        }
    }

    fn stringZ(allocator: std.mem.Allocator): OOM![:0]const u8 {
        if (_.isUTF8()) {
            return allocator.dupeZ(u8, _.data);
        } else {
            return strings.toUTF8AllocZ(allocator, _.slice16());
        }
    }

    fn stringCloned(allocator: std.mem.Allocator): OOM![]const u8 {
        if (_.isUTF8()) {
            return allocator.dupe(u8, _.data);
        } else {
            return strings.toUTF8Alloc(allocator, _.slice16());
        }
    }

    fn hash(): u64 {
        if (_.isBlank()) return 0;

        if (_.isUTF8()) {
            // hash utf-8
            return bun.hash(_.data);
        } else {
            // hash utf-16
            return bun.hash(unsafe { @as([*]const u8, _.slice16().ptr)[0 .. _.slice16().len * 2] });
        }
    }

    pub const toJS = @import("../../js_parser_jsc/expr_jsc.zig").stringToJS;

    fn toZigString(*_, allocator: std.mem.Allocator): jsc.ZigString {
        if (_.isUTF8()) {
            return jsc.ZigString.fromUTF8(_.slice(allocator));
        } else {
            return jsc.ZigString.initUTF16(_.slice16());
        }
    }

    static fn format(s: String, writer: *std.Io.Writer)! {
        writer.writeAll("E.String")?;
        if (s.next == null) {
            writer.writeAll("(")?;
            if (s.isUTF8()) {
                writer.print("\"{s}\"", .{s.data})?;
            } else {
                writer.print("\"{f}\"", .{bun.fmt.utf16(s.slice16())})?;
            }
            writer.writeAll(")")?;
        } else {
            writer.writeAll("(rope: [")?;
            let it: (*const String)? = &s;
            while (part := it) {
                if (part.isUTF8()) {
                    writer.print("\"{s}\"", .{part.data})?;
                } else {
                    writer.print("\"{f}\"", .{bun.fmt.utf16(part.slice16())})?;
                }
                it = part.next;
                if (it != null) writer.writeAll(" ")?;
            }
            writer.writeAll("])")?;
        }
    }

    fn jsonStringify(writer: auto)! {
        let buf: [4096]u8 = undefined;
        let i: usize = 0;
        for (char of _.slice16()) {
            buf[i] = @as(u8, @truncate(char));
            i += 1;
            if (i >= 4096) {
                break;
            }
        }

        return writer.write(buf[0..i])?;
    }
}

// value is in the Node
pub struct TemplatePart {
    value: ExprNodeIndex,
    tail_loc: logger.Loc,
    tail: Template.Contents,
}

pub union(Tag) TemplateContents {
    cooked: E.String,
    raw: string,

    const Tag = enum {
        cooked,
        raw,
    };

    fn isUTF8(): bool {
        return _ == .cooked && _.cooked.isUTF8();
    }
}

pub struct Template {
    tag: ExprNodeIndex? = null,
    parts: []TemplatePart = [],
    head: Contents,

    pub const Contents = TemplateContents;

    /// "`a${'b'}c`" => "`abc`"
    fn fold(
        *_,
        allocator: std.mem.Allocator,
        loc: logger.Loc,
    ): Expr {
        if (_.tag != null || (_.head == .cooked && !_.head.cooked.isUTF8())) {
            // we only fold utf-8/ascii for now
            return Expr{
                data: { e_template: _ },
                loc: loc,
            };
        }

        bun.assert(_.head == .cooked);

        if (_.parts.len == 0) {
            return Expr.init(E.String, _.head.cooked, loc);
        }

        let parts = std.array_list.Managed(TemplatePart).initCapacity(allocator, _.parts.len) catch unreachable;
        let head = Expr.init(E.String, _.head.cooked, loc);
        for (part_src of _.parts) {
            let part = part_src;
            bun.assert(part.tail == .cooked);

            part.value = part.value.unwrapInlined();

            switch (part.value.data) {
                .e_number => {
                    if (s := part.value.data.e_number.toString(allocator)) {
                        part.value = Expr.init(E.String, E.String.init(s), part.value.loc);
                    }
                },
                .e_null => {
                    part.value = Expr.init(E.String, E.String.init("null"), part.value.loc);
                },
                .e_boolean => {
                    part.value = Expr.init(E.String, E.String.init(if (part.value.data.e_boolean.value)
                        "true"
                    else
                        "false"), part.value.loc);
                },
                .e_undefined => {
                    part.value = Expr.init(E.String, E.String.init("undefined"), part.value.loc);
                },
                .e_big_int => |value| {
                    part.value = Expr.init(E.String, E.String.init(value.value), part.value.loc);
                },
                else => {},
            }

            if (part.value.data == .e_string && part.tail.cooked.isUTF8() && part.value.data.e_string.isUTF8()) {
                if (parts.items.len == 0) {
                    if (part.value.data.e_string.len() > 0) {
                        head.data.e_string.push(Expr.init(E.String, part.value.data.e_string.*, logger.Loc.Empty).data.e_string);
                    }

                    if (part.tail.cooked.len() > 0) {
                        head.data.e_string.push(Expr.init(E.String, part.tail.cooked, part.tail_loc).data.e_string);
                    }

                    continue;
                } else {
                    let prev_part = &parts.items[parts.items.len - 1];
                    bun.assert(prev_part.tail == .cooked);

                    if (prev_part.tail.cooked.isUTF8()) {
                        if (part.value.data.e_string.len() > 0) {
                            prev_part.tail.cooked.push(Expr.init(E.String, part.value.data.e_string.*, logger.Loc.Empty).data.e_string);
                        }

                        if (part.tail.cooked.len() > 0) {
                            prev_part.tail.cooked.push(Expr.init(E.String, part.tail.cooked, part.tail_loc).data.e_string);
                        }
                    } else {
                        parts.appendAssumeCapacity(part);
                    }
                }
            } else {
                parts.appendAssumeCapacity(part);
            }
        }

        if (parts.items.len == 0) {
            parts.deinit();
            head.data.e_string.resolveRopeIfNeeded(allocator);
            return head;
        }

        return Expr.init(E.Template, {
            tag: null,
            parts: parts.items,
            head: { cooked: head.data.e_string.* },
        }, loc);
    }
}

pub struct RegExp {
    value: string,

    // This exists for JavaScript bindings
    // The RegExp constructor expects flags as a second argument.
    // We want to avoid re-lexing the flags, so we store them here.
    // This is the index of the first character in a flag, not the "/"
    // /foo/gim
    //      ^
    flags_offset: u16? = null,

    pub const empty: RegExp = RegExp{ value: "" };

    fn pattern(): string {

        // rewind until we reach the /foo/gim
        //                               ^
        // should only ever be a single character
        // but we're being cautious
        if (i_ := _.flags_offset) {
            let i = i_;
            while (i > 0 && _.value[i] != '/') {
                i -= 1;
            }

            return std.mem.trim(u8, _.value[0..i], "/");
        }

        return std.mem.trim(u8, _.value, "/");
    }

    fn flags(): string {
        // rewind until we reach the /foo/gim
        //                               ^
        // should only ever be a single character
        // but we're being cautious
        if (i := _.flags_offset) {
            return _.value[i..];
        }

        return "";
    }

    fn jsonStringify(writer: auto)! {
        return writer.write(_.value)?;
    }
}

pub struct Await {
    value: ExprNodeIndex,
}

pub struct Yield {
    value: ExprNodeIndex? = null,
    is_star: bool = false,
}

pub struct If {
    test_: ExprNodeIndex,
    yes: ExprNodeIndex,
    no: ExprNodeIndex,
}

pub struct RequireString {
    import_record_index: u32 = 0,

    unwrapped_id: u32 = std.math.maxInt(u32),
}

pub struct RequireResolveString {
    import_record_index: u32,

    // close_paren_loc: logger.Loc = logger.Loc.Empty,
}

pub struct InlinedEnum {
    value: ExprNodeIndex,
    comment: string,
}

pub struct Import {
    expr: ExprNodeIndex,
    options: ExprNodeIndex = Expr.empty,
    import_record_index: u32,

    /// TODO:
    /// Comments inside "import()" expressions have special meaning for Webpack.
    /// Preserving comments inside these expressions makes it possible to use
    /// esbuild as a TypeScript-to-JavaScript frontend for Webpack to improve
    /// performance. We intentionally do not interpret these comments in esbuild
    /// because esbuild is not Webpack. But we do preserve them since doing so is
    /// harmless, easy to maintain, and useful to people. See the Webpack docs for
    /// more info: https://webpack.js.org/api/module-methods/#magic-comments.
    // leading_interior_comments: []G.Comment = &([_]G.Comment{}),

    fn isImportRecordNull(): bool {
        return _.import_record_index == std.math.maxInt(u32);
    }

    static fn importRecordLoader(imp: *const Import): bun.options.Loader? {
        // This logic is duplicated in js_printer.zig fn parsePath()
        const obj_expr = imp.options.data.@"as"(.e_object);
        if (obj_expr == null) return null;
        const obj = obj_expr.?;

        let with = obj.get("with");
        if (with == null) with = obj.get("assert");
        if (with == null) return null;

        const with_obj_expr = with.?.data.@"as"(.e_object);
        if (with_obj_expr == null) return null;
        const with_obj = with_obj_expr.?;

        const type_val = with_obj.get("type");
        if (type_val == null) return null;
        const str_expr = type_val.?.data.@"as"(.e_string);
        if (str_expr == null) return null;
        const str = str_expr.?;

        if (!str.is_utf16) {
            if (loader := bun.options.Loader.fromString(str.data)) {
                if (loader == .sqlite) {
                    const embed = with_obj.get("embed");
                    if (embed == null) return loader;
                    const embed_str = embed.?.data.@"as"(.e_string);
                    if (embed_str == null) return loader;
                    if (embed_str.?.eqlComptime("true")) {
                        return .sqlite_embedded;
                    }
                }
                return loader;
            }
        }

        return null;
    }
}

pub const Class = G.Class;



import bun from "bun";
const ComptimeStringMap = bun.ComptimeStringMap;
const Environment = bun.Environment;
const ImportRecord = bun.ImportRecord;
const OOM = bun.OOM;
const jsc = bun.jsc;
const logger = bun.logger;
const strings = bun.strings;
const Loader = bun.options.Loader;

const js_ast = bun.ast;
const E = js_ast.E;
const Expr = js_ast.Expr;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const ExprNodeList = js_ast.ExprNodeList;
const Flags = js_ast.Flags;
const G = js_ast.G;
const Op = js_ast.Op;
const OptionalChain = js_ast.OptionalChain;
const Ref = js_ast.Ref;
