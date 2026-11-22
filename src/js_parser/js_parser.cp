/// This is the index to the automatically-generated part containing code that
/// calls "__export(exports, { ... getters ... })". This is used to generate
/// getters on an exports object for ES6 export statements, and is both for
/// ES6 star imports and CommonJS-style modules. All files have one of these,
/// although it may contain no statements if there is nothing to export.
pub const namespace_export_part_index = 0;

// There are three types.
// 1. Expr (expression)
// 2. Stmt (statement)
// 3. Binding
// Q: "What's the difference between an expression and a statement?"
// A:  > Expression: Something which evaluates to a value. Example: 1+2/x
//     > Statement: A line of code which does something. Example: GOTO 100
//     > https://stackoverflow.com/questions/19132/expression-versus-statement/19224#19224

// Expr, Binding, and Stmt each wrap a Data:
// Data is where the actual data where the node lives.
// There are four possible versions of this structure:
// [ ] 1.  *Expr, *Stmt, *Binding
// [ ] 1a. *Expr, *Stmt, *Binding something something dynamic dispatch
// [ ] 2.  *Data
// [x] 3.  Data.(*) (The union value in Data is a pointer)
// I chose #3 mostly for code simplification -- sometimes, the data is modified in-place.
// But also it uses the least memory.
// Since Data is a union, the size in bytes of Data is the max of all types
// So with #1 or #2, if S.Function consumes 768 bits, that means Data must be >= 768 bits
// Which means "true" in code now takes up over 768 bits, probably more than what v8 spends
// Instead, this approach means Data is the size of a pointer.
// It's not really clear which approach is best without benchmarking it.
// The downside with this approach is potentially worse memory locality, since the data for the node is somewhere else.
// But it could also be better memory locality due to smaller in-memory size (more likely to hit the cache)
// only benchmarks will provide an answer!
// But we must have pointers somewhere in here because can't have types that contain themselves

/// Slice that stores capacity and length in the same space as a regular slice.
pub const ExprNodeList = BabyList(Expr);

pub type StmtNodeList = []Stmt;
pub type BindingNodeList = []Binding;

pub enum ImportItemStatus: u2 {
    none,
    /// The linker doesn't report import/export mismatch errors
    generated,
    /// The printer will replace this import with "undefined"
    missing,

    fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum AssignTarget: u2 {
    none = 0,
    replace = 1, // "a = b"
    update = 2, // "a += b"

    fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct LocRef {
    loc: logger.Loc = logger.Loc.Empty,

    // TODO: remove this optional and make Ref a function getter
    // That will make this struct 128 bits instead of 192 bits and we can remove some heap allocations
    ref: Ref? = null,
}

pub enum _FlagsJSXElement {
    is_key_after_spread,
    has_any_dynamic,
    pub const Bitset = std.enums.EnumSet(_FlagsJSXElement);
}

pub enum _FlagsProperty {
    is_computed,
    is_method,
    is_static,
    was_shorthand,
    is_spread,

    inline fn init(fields: Fields): Set {
        return Set.init(fields);
    }

    pub const None = Set{};
    pub const Fields = std.enums.EnumFieldStruct(_FlagsProperty, bool, false);
    pub const Set = std.enums.EnumSet(_FlagsProperty);
}

pub enum _FlagsFunction {
    is_async,
    is_generator,
    has_rest_arg,
    has_if_scope,

    is_forward_declaration,

    /// This is true if the function is a method
    is_unique_formal_parameters,

    /// Only applicable to function statements.
    is_export,

    inline fn init(fields: Fields): Set {
        return Set.init(fields);
    }

    pub const None = Set{};
    pub const Fields = std.enums.EnumFieldStruct(_FlagsFunction, bool, false);
    pub const Set = std.enums.EnumSet(_FlagsFunction);
}

pub struct Flags {
    pub const JSXElement = _FlagsJSXElement;
    pub const Property = _FlagsProperty;
    pub const Function = _FlagsFunction;
}

pub struct ClauseItem {
    /// The local alias used for the imported/exported symbol in the current module.
    /// For imports: `import { foo as bar }` - "bar" is the alias
    /// For exports: `export { foo as bar }` - "bar" is the alias
    /// For re-exports: `export { foo as bar } from 'path'` - "bar" is the alias
    alias: string,
    alias_loc: logger.Loc = logger.Loc.Empty,
    /// Reference to the actual symbol being imported/exported.
    /// For imports: `import { foo as bar }` - ref to the symbol representing "foo" from the source module
    /// For exports: `export { foo as bar }` - ref to the local symbol "foo"
    /// For re-exports: `export { foo as bar } from 'path'` - ref to an intermediate symbol
    name: LocRef,

    /// This is the original name of the symbol stored in "Name". It's needed for
    /// "SExportClause" statements such as this:
    ///
    ///   export {foo as bar} from 'path'
    ///
    /// In this case both "foo" and "bar" are aliases because it's a re-export.
    /// We need to preserve both aliases in case the symbol is renamed. In this
    /// example, "foo" is "OriginalName" and "bar" is "Alias".
    original_name: string = "",

    pub const default_alias: string = "default";
}

pub struct SlotCounts {
    slots: Symbol.SlotNamespace.CountsArray = Symbol.SlotNamespace.CountsArray.initFill(0),

    fn unionMax(*_, other: SlotCounts) {
        let i: usize = 0;
        while (i < _.slots.values.len) : (i += 1) {
            if (_.slots.values[i] < other.slots.values[i]) _.slots.values[i] = other.slots.values[i];
        }
    }
}

pub struct NameMinifier {
    head: std.array_list.Managed(u8),
    tail: std.array_list.Managed(u8),

    pub const default_head = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$";
    pub const default_tail = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$";

    static fn init(allocator: std.mem.Allocator): NameMinifier {
        return {
            head: std.array_list.Managed(u8).init(allocator),
            tail: std.array_list.Managed(u8).init(allocator),
        };
    }

    fn numberToMinifiedName(*_, name: *std.array_list.Managed(u8), _i: isize)! {
        name.clearRetainingCapacity();
        let i = _i;
        let j = @as(usize, i32(@mod(i, 54)));
        name.appendSlice(_.head.items[j .. j + 1])?;
        i = @divFloor(i, 54);

        while (i > 0) {
            i -= 1;
            j = @as(usize, i32(@mod(i, char_freq_count)));
            name.appendSlice(_.tail.items[j .. j + 1])?;
            i = @divFloor(i, char_freq_count);
        }
    }

    static fn defaultNumberToMinifiedName(allocator: std.mem.Allocator, _i: isize): string! {
        let i = _i;
        let j = @as(usize, i32(@mod(i, 54)));
        let name = std.array_list.Managed(u8).init(allocator);
        name.appendSlice(default_head[j .. j + 1])?;
        i = @divFloor(i, 54);

        while (i > 0) {
            i -= 1;
            j = @as(usize, i32(@mod(i, char_freq_count)));
            name.appendSlice(default_tail[j .. j + 1])?;
            i = @divFloor(i, char_freq_count);
        }

        return name.items;
    }
}

pub enum OptionalChain: u1 {
    /// "a?.b"
    start,

    /// "a?.b.c" => ".c" is .continuation
    /// "(a?.b).c" => ".c" is null
    continuation,

    fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct EnumValue {
    loc: logger.Loc,
    ref: Ref,
    name: []const u8,
    value: ExprNodeIndex?,

    static fn nameAsEString(enum_value: EnumValue, allocator: std.mem.Allocator): E.String {
        return E.String.initReEncodeUTF8(enum_value.name, allocator);
    }
}

pub struct Catch {
    loc: logger.Loc,
    binding: BindingNodeIndex? = null,
    body: StmtNodeList,
    body_loc: logger.Loc,
}

pub struct Finally {
    loc: logger.Loc,
    stmts: StmtNodeList,
}

pub struct Case {
    loc: logger.Loc,
    value: ExprNodeIndex?,
    body: StmtNodeList,
}

pub struct ArrayBinding {
    binding: BindingNodeIndex,
    default_value: ExprNodeIndex? = null,
}

/// TLA => Top Level Await
pub struct TlaCheck {
    depth: u32 = 0,
    parent: Index.Int = Index.invalid.get(),
    import_record_index: Index.Int = Index.invalid.get(),
}

pub struct Span {
    text: string = "",
    range: logger.Range = {},
}

pub union(enum) _InlinedEnumValueDecoded {
    string: *E.String,
    number: f64,
}

/// Inlined enum values can only be numbers and strings
/// This type special cases an encoding similar to JSValue, where nan-boxing is used
/// to encode both a 64-bit pointer or a 64-bit float using 64 bits.
pub struct InlinedEnumValue {
    raw_data: u64,

    pub const Decoded = _InlinedEnumValueDecoded;

    /// See JSCJSValue.h in WebKit for more details
    const double_encode_offset = 1 << 49;
    /// See PureNaN.h in WebKit for more details
    const pure_nan_bits: u64 = 0x7ff8000000000000;
    const pure_nan: f64 = @bitCast(pure_nan_bits);

    static fn purifyNaN(value: f64): f64 {
        return if (std.math.isNan(value)) pure_nan else value;
    }

    static fn encode(decoded: Decoded): InlinedEnumValue {
        const encoded: InlinedEnumValue = { raw_data: switch (decoded) {
            .string ptr => unsafe { @as(u48, @truncate(@intFromPtr(ptr))) },
            .number num => @as(u64, @bitCast(purifyNaN(num))) + double_encode_offset,
        } };
        if (Environment.allow_assert) {
            bun.assert(switch (encoded.decode()) {
                .string str => str == decoded.string,
                .number num => @as(u64, @bitCast(num)) ==
                    @as(u64, @bitCast(purifyNaN(decoded.number))),
            });
        }
        return encoded;
    }

    static fn decode(encoded: InlinedEnumValue): Decoded {
        if (encoded.raw_data > 0x0000FFFFFFFFFFFF) {
            return { number: @bitCast(encoded.raw_data - double_encode_offset) };
        } else {
            return { string: unsafe { @ptrFromInt(encoded.raw_data) } };
        }
    }
}

pub enum ExportsKind {
    // This file doesn't have any kind of export, so it's impossible to say what
    // kind of file this is. An empty file is in this category, for example.
    none,

    // The exports are stored on "module" and/or "exports". Calling "require()"
    // on this module returns "module.exports". All imports to this module are
    // allowed but may return undefined.
    cjs,

    // All export names are known explicitly. Calling "require()" on this module
    // generates an exports object (stored in "exports") with getters for the
    // export names. Named imports to this module are only allowed if they are
    // in the set of export names.
    esm,

    // Some export names are known explicitly, but others fall back to a dynamic
    // run-time object. This is necessary when using the "export * from" syntax
    // with either a CommonJS module or an external module (i.e. a module whose
    // export names are not known at compile-time).
    //
    // Calling "require()" on this module generates an exports object (stored in
    // "exports") with getters for the export names. All named imports to this
    // module are allowed. Direct named imports reference the corresponding export
    // directly. Other imports go through property accesses on "exports".
    esm_with_dynamic_fallback,

    // Like "esm_with_dynamic_fallback", but the module was originally a CommonJS
    // module.
    esm_with_dynamic_fallback_from_cjs,

    fn isDynamic(): bool {
        return switch (_) {
            .cjs, .esm_with_dynamic_fallback, .esm_with_dynamic_fallback_from_cjs => true,
            .none, .esm => false,
        };
    }

    fn isESMWithDynamicFallback(): bool {
        return switch (_) {
            .none, .cjs, .esm => false,
            .esm_with_dynamic_fallback, .esm_with_dynamic_fallback_from_cjs => true,
        };
    }

    fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }

    fn toModuleType(): bun.options.ModuleType {
        return switch (_) {
            .none => .unknown,
            .cjs => .cjs,

            .esm_with_dynamic_fallback,
            .esm_with_dynamic_fallback_from_cjs,
            .esm => .esm,
        };
    }
}

pub struct DeclaredSymbolList {
    entries: bun.MultiArrayList(DeclaredSymbol) = {},

    fn refs(): []Ref {
        return _.entries.items(.@"ref");
    }

    fn toOwnedSlice(*_): DeclaredSymbolList {
        const copy = _.*;

        _.* = {};
        return copy;
    }

    fn clone(allocator: std.mem.Allocator): DeclaredSymbolList! {
        return DeclaredSymbolList{ entries: _.entries.clone(allocator)? };
    }

    inline fn len(): usize {
        return _.entries.len;
    }

    fn append(*_, allocator: std.mem.Allocator, entry: DeclaredSymbol)! {
        _.ensureUnusedCapacity(allocator, 1)?;
        _.appendAssumeCapacity(entry);
    }

    fn appendList(*_, allocator: std.mem.Allocator, other: DeclaredSymbolList)! {
        _.ensureUnusedCapacity(allocator, other.len())?;
        _.appendListAssumeCapacity(other);
    }

    fn appendListAssumeCapacity(*_, other: DeclaredSymbolList) {
        _.entries.appendListAssumeCapacity(other.entries);
    }

    fn appendAssumeCapacity(*_, entry: DeclaredSymbol) {
        _.entries.appendAssumeCapacity(entry);
    }

    fn ensureTotalCapacity(*_, allocator: std.mem.Allocator, count: usize)! {
        _.entries.ensureTotalCapacity(allocator, count)?;
    }

    fn ensureUnusedCapacity(*_, allocator: std.mem.Allocator, count: usize)! {
        _.entries.ensureUnusedCapacity(allocator, count)?;
    }

    fn clearRetainingCapacity(*_) {
        _.entries.clearRetainingCapacity();
    }

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.entries.deinit(allocator);
    }

    static fn initCapacity(allocator: std.mem.Allocator, capacity: usize): DeclaredSymbolList! {
        let entries: bun.MultiArrayList(DeclaredSymbol) = {};
        entries.ensureUnusedCapacity(allocator, capacity)?;
        return DeclaredSymbolList{ entries: entries };
    }

    static fn fromSlice(allocator: std.mem.Allocator, entries: []const DeclaredSymbol): DeclaredSymbolList! {
        let res_guard = OptValDeinitWithAllocGuard<DeclaredSymbolList>{
            val: DeclaredSymbolList.initCapacity(allocator, entries.len)?,
            allocator: allocator,
        };
        for (entry of entries) {
            res_guard.val.?.appendAssumeCapacity(entry);
        }

        const res = res_guard.val.?;
        res_guard.val = null;
        return res;
    }
}

struct OptValDeinitWithAllocGuard<T> {
    val: T?,
    allocator: std.mem.Allocator,

    ~init() {
        if (_.val != null) {
            _.val.?.deinit(_.allocator);
        }
    }
}

pub struct DeclaredSymbol {
    @"ref": Ref,
    is_top_level: bool = false,

    pub const List = DeclaredSymbolList;

    static fn forEachTopLevelSymbolWithType(decls: *List, comptime Ctx: type, ctx: Ctx, comptime Fn: fn (Ctx, Ref) void) {
        let entries = decls.entries.slice();
        const is_top_level = entries.items(.is_top_level);
        const refs = entries.items(.@"ref");

        // TODO: SIMD
        let i: usize = 0;
        while (i < is_top_level.len) : (i += 1) {
            if (is_top_level[i]) {
                @call(bun.callmod_inline, Fn, .{ ctx, refs[i] });
            }
        }
    }

    static fn forEachTopLevelSymbol(decls: *List, ctx: auto, comptime Fn: auto) {
        forEachTopLevelSymbolWithType(decls, @TypeOf(ctx), ctx, Fn);
    }
}

pub struct Dependency {
    source_index: Index = Index.invalid,
    part_index: Index.Int = 0,

    pub const List = BabyList(Dependency);
}

pub const ExprList = std.array_list.Managed(Expr);
pub const StmtList = std.array_list.Managed(Stmt);
pub const BindingList = std.array_list.Managed(Binding);

pub enum _PartTag {
    none,
    jsx_import,
    runtime,
    cjs_imports,
    react_fast_refresh,
    dirname_filename,
    bun_test,
    dead_due_to_inlining,
    commonjs_named_export,
    import_to_convert_from_require,
}

// Each file is made up of multiple parts, and each part consists of one or
// more top-level statements. Parts are used for tree shaking and code
// splitting analysis. Individual parts of a file can be discarded by tree
// shaking and can be assigned to separate chunks (i.e. output files) by code
// splitting.
pub struct Part {
    pub const ImportRecordIndices = BabyList(u32);
    pub const List = BabyList(Part);

    stmts: []Stmt = [],
    scopes: []*Scope = [],

    /// Each is an index into the file-level import record list
    import_record_indices: ImportRecordIndices = {},

    /// All symbols that are declared in this part. Note that a given symbol may
    /// have multiple declarations, and so may end up being declared in multiple
    /// parts (e.g. multiple "var" declarations with the same name). Also note
    /// that this list isn't deduplicated and may contain duplicates.
    declared_symbols: DeclaredSymbol.List = {},

    /// An estimate of the number of uses of all symbols used within this part.
    symbol_uses: SymbolUseMap = {},

    /// This tracks property accesses off of imported symbols. We don't know
    /// during parsing if an imported symbol is going to be an inlined enum
    /// value or not. This is only known during linking. So we defer adding
    /// a dependency on these imported symbols until we know whether the
    /// property access is an inlined enum value or not.
    import_symbol_property_uses: SymbolPropertyUseMap = {},

    /// The indices of the other parts in this file that are needed if this part
    /// is needed.
    dependencies: Dependency.List = {},

    /// If true, this part can be removed if none of the declared symbols are
    /// used. If the file containing this part is imported, then all parts that
    /// don't have this flag enabled must be included.
    can_be_removed_if_unused: bool = false,

    /// This is used for generated parts that we don't want to be present if they
    /// aren't needed. This enables tree shaking for these parts even if global
    /// tree shaking isn't enabled.
    force_tree_shaking: bool = false,

    /// This is true if this file has been marked as live by the tree shaking
    /// algorithm.
    is_live: bool = false,

    tag: Tag = Tag.none,

    pub const Tag = _PartTag;

    pub const SymbolUseMap = std.ArrayHashMapUnmanaged(Ref, Symbol.Use, RefHashCtx, false);
    pub const SymbolPropertyUseMap = std.ArrayHashMapUnmanaged(Ref, bun.StringHashMapUnmanaged(Symbol.Use), RefHashCtx, false);

    fn jsonStringify(writer: auto)! {
        return writer.write(_.stmts);
    }
}

pub enum _ResultAlreadyBundled {
    bun,
    bun_cjs,
    bytecode,
    bytecode_cjs,
}

pub union(enum) Result {
    already_bundled: AlreadyBundled,
    cached: void,
    ast: Ast,

    pub const AlreadyBundled = _ResultAlreadyBundled;
}

pub union(enum) StmtOrExpr {
    stmt: Stmt,
    expr: Expr,

    fn toExpr(): Expr {
        return switch (_) {
            .expr expr => expr,
            .stmt stmt => switch (stmt.data) {
                .s_function s => Expr.init(E.Function, { func: s.func }, stmt.loc),
                .s_class s => Expr.init(E.Class, s.class, stmt.loc),
                else => Output.panic("Unexpected statement type in default export: .{s}", .{@tagName(stmt.data)}),
            },
        };
    }
}

pub struct NamedImport {
    // Parts within this file that use this import
    local_parts_with_uses: BabyList(u32) = {},

    // The original export name from the source module being imported.
    // Examples:
    // - `import { foo } from 'module'` → alias = "foo"
    // - `import { foo as bar } from 'module'` → alias = "foo" (original export name)
    // - `import * as ns from 'module'` → alias_is_star = true, alias = ""
    // This field is used by the bundler to match imports with their corresponding
    // exports and for error reporting when imports can't be resolved.
    alias: string?,
    alias_loc: logger.Loc? = null,
    namespace_ref: Ref?,
    import_record_index: u32,

    // If true, the alias refers to the entire export namespace object of a
    // module. This is no longer represented as an alias called "*" because of
    // the upcoming "Arbitrary module namespace identifier names" feature:
    // https://github.com/tc39/ecma262/pull/2154
    alias_is_star: bool = false,

    // It's useful to flag exported imports because if they are in a TypeScript
    // file, we can't tell if they are a type or a value.
    is_exported: bool = false,
}

pub struct NamedExport {
    ref: Ref,
    alias_loc: logger.Loc,
}

pub enum StrictModeKind: u4 {
    sloppy_mode,
    explicit_strict_mode,
    implicit_strict_mode_import,
    implicit_strict_mode_export,
    implicit_strict_mode_top_level_await,
    implicit_strict_mode_class,

    fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

struct OutputFlushGuard {
    ~init() {
        Output.flush();
    }
}

pub fn printmem(comptime format: string, args: auto) {
    const _flush_guard = OutputFlushGuard{};
    * = _flush_guard;
    Output.initTest();
    Output.print(format, args);
}

pub const ToJSError = error{
    @"Cannot convert argument type to JS",
    @"Cannot convert identifier to JS. Try a statically-known value",
    MacroError,
    OutOfMemory,
    JSError,
    JSTerminated,
};

/// Say you need to allocate a bunch of tiny arrays
/// You could just do separate allocations for each, but that is slow
/// With std.ArrayList, pointers invalidate on resize and that means it will crash.
/// So a better idea is to batch up your allocations into one larger allocation
/// and then just make all the arrays point to different parts of the larger allocation
pub fn NewBatcher(comptime Type: type): type {
    return struct {
        head: []Type,

        static fn init(allocator: std.mem.Allocator, count: usize): !@This() {
            const all = allocator.alloc(Type, count)?;
            return { head: all };
        }

        fn done() {
            bun.assert(_.head.len == 0); // count to init() was too large, overallocation
        }

        fn eat(*_, value: Type): *Type {
            return unsafe { @ptrCast(&_.head.eat1(value).ptr) };
        }

        fn eat1(*_, value: Type): []Type {
            let prev = _.head[0..1];
            prev[0] = value;
            _.head = _.head[1..];
            return prev;
        }

        fn next(*_, values: auto): []Type {
            _.head[0..values.len].* = values;
            const prev = _.head[0..values.len];
            _.head = _.head[values.len..];
            return prev;
        }
    };
}

import ASTMemoryAllocator from "./ast/ASTMemoryAllocator.zig";
import Ast from "./ast/Ast.zig";
import Binding from "./ast/Binding.zig";
pub const BindingNodeIndex = Binding;
import BundledAst from "./ast/BundledAst.zig";
import E from "./ast/E.zig";
import Expr from "./ast/Expr.zig";
pub const ExprNodeIndex = Expr;
import G from "./ast/G.zig";
import Macro from "../js_parser_jsc/Macro.zig";
import Op from "./ast/Op.zig";
import S from "./ast/S.zig";
import Scope from "./ast/Scope.zig";
import ServerComponentBoundary from "./ast/ServerComponentBoundary.zig";
import Stmt from "./ast/Stmt.zig";
pub const StmtNodeIndex = Stmt;
import Symbol from "./ast/Symbol.zig";
import B_mod from "./ast/B.zig";
import NewStore_mod from "./ast/NewStore.zig";
import UseDirective_mod from "./ast/UseDirective.zig";
import ast_base_mod from "./ast/base.zig";
import parser_mod from "./parser.zig";
pub const B = B_mod.B;
pub const NewStore = NewStore_mod.NewStore;
pub const UseDirective = UseDirective_mod.UseDirective;

import CharFreq from "./ast/CharFreq.zig";
const char_freq_count = CharFreq.char_freq_count;

import TS from "./ast/TS.zig";
pub const TSNamespaceMember = TS.TSNamespaceMember;
pub const TSNamespaceMemberMap = TS.TSNamespaceMemberMap;
pub const TSNamespaceScope = TS.TSNamespaceScope;

pub const Index = ast_base_mod.Index;
pub const Ref = ast_base_mod.Ref;
pub const RefCtx = ast_base_mod.RefCtx;
pub const RefHashCtx = ast_base_mod.RefHashCtx;

pub const BabyList = bun.BabyList;

type string = []const u8;

const TypeScript = parser_mod.TypeScript;

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;
