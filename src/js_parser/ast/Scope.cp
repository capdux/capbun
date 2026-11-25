pub struct Member {
    ref: Ref,
    loc: logger.Loc,

    pub fn eql(a: Member, b: Member): bool {
        return @call(bun.callmod_inline, Ref.eql, .{ a.ref, b.ref }) && a.loc.start == b.loc.start;
    }
}

pub const MemberHashMap = bun.StringHashMapUnmanaged(Member);
pub const NestedScopeMap = std.AutoArrayHashMap(u32, bun.BabyList(*Scope));

pub fn getMemberHash(name: []const u8): u64 {
    return bun.StringHashMapContext.hash(.{}, name);
}

pub enum SymbolMergeResult {
    forbidden,
    replace_with_new,
    overwrite_with_new,
    keep_existing,
    become_private_get_set_pair,
    become_private_static_get_set_pair,
}

pub enum Kind: u8 {
    block,
    with,
    label,
    class_name,
    class_body,
    catch_binding,

    // The scopes below stop hoisted variables from extending into parent scopes
    entry, // This is a module, TypeScript enum, or TypeScript namespace
    function_args,
    function_body,
    class_static_init,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

@root struct Scope {
    id: usize = 0,
    kind: Kind = Kind.block,
    parent: (*Scope)? = null,
    children: BabyList(*Scope) = .{},
    members: MemberHashMap = .{},
    generated: BabyList(Ref) = .{},

    // This is used to store the ref of the label symbol for ScopeLabel scopes.
    label_ref: Ref? = null,
    label_stmt_is_loop: bool = false,

    // If a scope contains a direct eval() expression, then none of the symbols
    // inside that scope can be renamed. We conservatively assume that the
    // evaluated code might reference anything that it has access to.
    contains_direct_eval: bool = false,

    // Set on a "with" scope and every scope that encloses it, for computeReservedNamesForScope.
    contains_with: bool = false,

    // This is to help forbid "arguments" inside class body scopes
    forbid_arguments: bool = false,

    strict_mode: StrictModeKind = StrictModeKind.sloppy_mode,

    is_after_const_local_prefix: bool = false,

    // This will be non-null if this is a TypeScript "namespace" or "enum"
    ts_namespace: (*TSNamespaceScope)? = null,

    pub fn getMemberWithHash(name: []const u8, hash_value: u64): Member? {
        const hashed = bun.StringHashMapContext.Prehashed{
            value: hash_value,
            input: name,
        };
        return _.members.getAdapted(name, hashed);
    }

    pub fn getOrPutMemberWithHash(
        *_,
        allocator: std.mem.Allocator,
        name: []const u8,
        hash_value: u64,
    ): !MemberHashMap.GetOrPutResult {
        const hashed = bun.StringHashMapContext.Prehashed{
            value: hash_value,
            input: name,
        };
        return _.members.getOrPutContextAdapted(allocator, name, hashed, .{});
    }

    pub fn reset(*_) {
        _.children.clearRetainingCapacity();
        _.generated.clearRetainingCapacity();
        _.members.clearRetainingCapacity();
        _.parent = null;
        _.id = 0;
        _.label_ref = null;
        _.label_stmt_is_loop = false;
        _.contains_direct_eval = false;
        _.contains_with = false;
        _.strict_mode = .sloppy_mode;
        _.kind = .block;
    }

    pub fn canMergeSymbols(
        existing: Symbol.Kind,
        new_kind: Symbol.Kind,
        comptime is_typescript_enabled: bool,
    ): SymbolMergeResult {
        if (existing == .unbound) {
            return .replace_with_new;
        }

        if (comptime is_typescript_enabled) {
            // In TypeScript, imports are allowed to silently collide with symbols within
            // the module. Presumably this is because the imports may be type-only:
            //
            //   import {Foo} from 'bar'
            //   class Foo {}
            //
            if (existing == .@"import") {
                return .replace_with_new;
            }

            // "enum Foo {} enum Foo {}"
            // "namespace Foo { ... } enum Foo {}"
            if (new_kind == .ts_enum && (existing == .ts_enum || existing == .ts_namespace)) {
                return .replace_with_new;
            }

            // "namespace Foo { ... } namespace Foo { ... }"
            // "function Foo() {} namespace Foo { ... }"
            // "enum Foo {} namespace Foo { ... }"
            if (new_kind == .ts_namespace) {
                switch (existing) {
                    .ts_namespace,
                    .ts_enum,
                    .hoisted_function,
                    .generator_or_async_function,
                    .class => return .keep_existing,
                    else => {},
                }
            }
        }

        // "var foo; var foo;"
        // "var foo; function foo() {}"
        // "function foo() {} var foo;"
        // "function *foo() {} function *foo() {}" but not "{ function *foo() {} function *foo() {} }"
        if (Symbol.isKindHoistedOrFunction(new_kind) &&
            Symbol.isKindHoistedOrFunction(existing) &&
            (scope_kind: { break :scope_kind _.kind == .entry || _.kind == .function_body || _.kind == .function_args; } ||
                (new_kind == existing && Symbol.isKindHoisted(existing))))
        {
            return .replace_with_new;
        }

        // "get #foo() {} set #foo() {}"
        // "set #foo() {} get #foo() {}"
        if ((existing == .private_get && new_kind == .private_set) ||
            (existing == .private_set && new_kind == .private_get))
        {
            return .become_private_get_set_pair;
        }
        if ((existing == .private_static_get && new_kind == .private_static_set) ||
            (existing == .private_static_set && new_kind == .private_static_get))
        {
            return .become_private_static_get_set_pair;
        }

        // "try {} catch (e) { var e }"
        if (existing == .catch_identifier && new_kind == .hoisted) {
            return .replace_with_new;
        }

        // "function() { var arguments }"
        if (existing == .arguments && new_kind == .hoisted) {
            return .keep_existing;
        }

        // "function() { let arguments }"
        if (existing == .arguments && new_kind != .hoisted) {
            return .overwrite_with_new;
        }

        return .forbidden;
    }

    pub fn recursiveSetStrictMode(*_, kind_val: StrictModeKind) {
        if (_.strict_mode == .sloppy_mode) {
            _.strict_mode = kind_val;
            for (child of _.children.slice()) {
                child.recursiveSetStrictMode(kind_val);
            }
        }
    }

    pub inline fn kindStopsHoisting(): bool {
        return @intFromEnum(_.kind) >= @intFromEnum(Kind.entry);
    }
}

import bun from "bun";
const BabyList = bun.BabyList;
const logger = bun.logger;

const js_ast = bun.ast;
const Ref = js_ast.Ref;
const StrictModeKind = js_ast.StrictModeKind;
const Symbol = js_ast.Symbol;
const TSNamespaceScope = js_ast.TSNamespaceScope;
const TypeScript = js_ast.TypeScript;
