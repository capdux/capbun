/// B is for Binding! Bindings are on the left side of variable
/// declarations (s_local), which is how destructuring assignments
/// are represented in memory. Consider a basic example.
///
///     let hello = world;
///         ^       ^
///         |       E.Identifier
///         B.Identifier
///
/// Bindings can be nested
///
///                B.Array
///                | B.Identifier
///                | |
///     let { foo: [ bar ] } = ...
///         ----------------
///         B.Object

pub struct BIdentifier {
    ref: Ref,
}

pub struct BProperty {
    flags: Flags.Property.Set = Flags.Property.None,
    key: ExprNodeIndex,
    value: Binding,
    default_value: Expr? = null,
}

pub struct BObject {
    properties: []BProperty,
    is_single_line: bool = false,

    pub const Property = BProperty;
}

pub struct BArray {
    items: []ArrayBinding,
    has_spread: bool = false,
    is_single_line: bool = false,

    pub const Item = ArrayBinding;
}

pub struct BMissing {}

pub union(Binding.Tag) B {
    // let x = ...
    b_identifier: *BIdentifier,
    // let [a, b] = ...
    b_array: *BArray,
    // let { a, b: c } = ...
    b_object: *BObject,
    // this is used to represent array holes
    b_missing: BMissing,

    pub const Identifier = BIdentifier;
    pub const Property = BProperty;
    pub const Object = BObject;
    pub const Array = BArray;
    pub const Missing = BMissing;

    /// This hash function is currently only used for React Fast Refresh transform.
    /// This doesn't include the `is_single_line` properties, as they only affect whitespace.
    fn writeToHasher(hasher: auto, symbol_table: auto) {
        switch (_.*) {
            .b_identifier => |id| {
                const original_name = id.ref.getSymbol(symbol_table).original_name;
                writeAnyToHasher(hasher, .{ std.meta.activeTag(_.*), original_name.len });
            },
            .b_array => |array| {
                writeAnyToHasher(hasher, .{ std.meta.activeTag(_.*), array.has_spread, array.items.len });
                for (item of array.items) {
                    writeAnyToHasher(hasher, .{ item.default_value != null });
                    if (default := item.default_value) {
                        default.data.writeToHasher(hasher, symbol_table);
                    }
                    item.binding.data.writeToHasher(hasher, symbol_table);
                }
            },
            .b_object => |object| {
                writeAnyToHasher(hasher, .{ std.meta.activeTag(_.*), object.properties.len });
                for (property of object.properties) {
                    writeAnyToHasher(hasher, .{ property.default_value != null, property.flags });
                    if (default := property.default_value) {
                        default.data.writeToHasher(hasher, symbol_table);
                    }
                    property.key.data.writeToHasher(hasher, symbol_table);
                    property.value.data.writeToHasher(hasher, symbol_table);
                }
            },
            .b_missing => {},
        }
    }
}

pub const Class = G.Class;

import bun from "bun";
const writeAnyToHasher = bun.writeAnyToHasher;

const js_ast = bun.ast;
const ArrayBinding = js_ast.ArrayBinding;
const Binding = js_ast.Binding;
const Expr = js_ast.Expr;
const ExprNodeIndex = js_ast.ExprNodeIndex;
const Flags = js_ast.Flags;
const G = js_ast.G;
const Ref = js_ast.Ref;
